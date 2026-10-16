/*
	smf.c -- Standard MIDI File reading (Phase 9, step 1; Oct 16, 2026).
	See smf.h for what it does and MIDI_SEQUENCER_NOTES.md (Phase 9)
	for why.

	The file format, as relied on here (Standard MIDI File 1.0):
	  - "MThd", a 32-bit length (6 or more), then format, track count
	    and division, 16 bits each, big-endian. A division with the top
	    bit set is SMPTE timing, not supported.
	  - Chunks follow: "MTrk" is a track; any other chunk type is
	    skipped, as the spec asks. A chunk whose length runs past the
	    end of the file is read to the end of the file (some writers
	    get the length wrong); an event cut off by the end of its chunk
	    makes the file damaged.
	  - A track is events, each a delta time (a variable-length number,
	    at most 4 bytes) then the event. Channel messages may use
	    running status (the status byte left out after the first);
	    SysEx (F0, F7) and meta (FF) events cancel it. Meta FF 2F (end
	    of track) ends the track; anything after it in the chunk is
	    ignored. System common and real-time bytes (F1-F6, F8-FE) don't
	    belong in a file and make it damaged.
*/

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "smf.h"

#define NKEYS	(16 * 128)	/* channel x pitch, for note pairing */

/* The loop-length marker (Phase 9, agreed Oct 16, 2026, option (c)):
   a sequencer-specific meta event (FF 7F) that smf_write() puts at the
   start of each track it writes, holding the track's loop length in
   the file's ticks, so a loop comes back from the reader exactly as
   long as it went in, even where grid rounding would move its end.
   Other programs skip sequencer-specific events they don't know.
     FF 7F 0E  7D 'R' 'A' 'P' 'L' 01  then 8 bytes, big-endian ticks
   7D is the manufacturer ID MIDI keeps for non-commercial use, so the
   5 bytes after it ("RAPL" and a format number, 1) tell ours from
   anyone else's. */
#define MARK_ID		"\x7DRAPL\x01"
#define MARK_IDLEN	6
#define MARK_LEN	(MARK_IDLEN + 8)

struct note {
	int64_t start;
	int64_t off;		/* -1 while sounding */
	int     pitch, vel, chan;	/* chan 1..16 */
	int     voice;		/* 1.. within the track */
	int32_t next;		/* pairing queue for its key, -1 = end */
	int32_t order;		/* position in the file, for a stable sort */
};

struct prog {
	int64_t tick;
	int     chan, prog;
};

struct track {
	struct note *notes;
	int32_t      nnotes, capnotes;
	struct prog *progs;
	int32_t      nprogs, capprogs;
	int64_t      end, length, grid;
	int64_t      loop;	/* the loop length the writer recorded
				   (the RetroAPL marker), -1 if none */
	int          exact;	/* length is the marker's */
	int          nvoices;
	int         *vchan;	/* channel of each voice, [0] unused */
};

struct smf_file {
	struct smf_info info;
	struct track   *tracks;	/* [0] is track 1 */
	int64_t         tempo_tick, ts_tick;	/* where the reported ones are */
};

const char *
smf_strerror(int err)
{
	switch (err)
	{
		case SMF_OK:		return "ok";
		case SMF_ERR_FORMAT:	return "not a MIDI file, or damaged";
		case SMF_ERR_NOMEM:	return "out of memory";
		case SMF_ERR_RANGE:	return "no such track or voice";
		case SMF_ERR_VALUE:	return "a value the file can't hold";
	}
	return "unknown error";
}

/* ── Reading bytes ─────────────────────────────────────────────────── */

static uint32_t
be16(const unsigned char *p)
{
	return ((uint32_t)p[0] << 8) | p[1];
}

static uint32_t
be32(const unsigned char *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
	     | ((uint32_t)p[2] << 8) | p[3];
}

/* A variable-length number at *pos, at most 4 bytes; advances *pos.
   0 on success, -1 if it runs past end or is longer than 4 bytes. */
static int
vlq(const unsigned char *buf, size_t end, size_t *pos, uint32_t *out)
{
	uint32_t v = 0;
	int i;

	for (i = 0; i < 4; i++)
	{
		if (*pos >= end) return -1;
		v = (v << 7) | (buf[*pos] & 0x7f);
		if (!(buf[(*pos)++] & 0x80))
		{
			*out = v;
			return 0;
		}
	}
	return -1;
}

/* ── Building a track ──────────────────────────────────────────────── */

static int
add_note(struct track *t, int64_t tick, int chan, int pitch, int vel)
{
	struct note *n;

	if (t->nnotes == t->capnotes)
	{
		int32_t cap = t->capnotes ? t->capnotes * 2 : 64;
		struct note *nn;

		if (cap < t->capnotes) return SMF_ERR_NOMEM;	/* overflow */
		nn = realloc(t->notes, (size_t)cap * sizeof *nn);
		if (!nn) return SMF_ERR_NOMEM;
		t->notes = nn;
		t->capnotes = cap;
	}
	n = &t->notes[t->nnotes];
	n->start = tick;
	n->off = -1;
	n->pitch = pitch;
	n->vel = vel;
	n->chan = chan;
	n->voice = 0;
	n->next = -1;
	n->order = t->nnotes;
	t->nnotes++;
	return SMF_OK;
}

static int
add_prog(struct track *t, int64_t tick, int chan, int prog)
{
	if (t->nprogs == t->capprogs)
	{
		int32_t cap = t->capprogs ? t->capprogs * 2 : 16;
		struct prog *np;

		if (cap < t->capprogs) return SMF_ERR_NOMEM;
		np = realloc(t->progs, (size_t)cap * sizeof *np);
		if (!np) return SMF_ERR_NOMEM;
		t->progs = np;
		t->capprogs = cap;
	}
	t->progs[t->nprogs].tick = tick;
	t->progs[t->nprogs].chan = chan;
	t->progs[t->nprogs].prog = prog;
	t->nprogs++;
	return SMF_OK;
}

static int64_t
gcd64(int64_t a, int64_t b)
{
	while (b)
	{
		int64_t r = a % b;
		a = b;
		b = r;
	}
	return a;
}

/* Sort order for voice allocation: channel, then start, then pitch,
   then position in the file (so the sort is stable). */
static int
cmp_note(const void *pa, const void *pb)
{
	const struct note *a = pa, *b = pb;

	if (a->chan != b->chan) return a->chan < b->chan ? -1 : 1;
	if (a->start != b->start) return a->start < b->start ? -1 : 1;
	if (a->pitch != b->pitch) return a->pitch < b->pitch ? -1 : 1;
	return a->order < b->order ? -1 : (a->order > b->order);
}

/* After a track is read: its end, grid and length, and its voices
   (rule (b), see smf.h). Notes end up sorted by channel and start. */
static int
finish_track(struct track *t, int64_t end, int division)
{
	int64_t *vlast_start = NULL, *vlast_off = NULL;
	int *nv;
	int32_t i;
	int cap = 0, base, v, chan;

	/* notes still sounding end at the end of the track; a note of
	   length 0 (a note-off on its own tick) is made 1 tick */
	for (i = 0; i < t->nnotes; i++)
	{
		struct note *n = &t->notes[i];

		if (n->off < 0) n->off = end;
		if (n->off <= n->start) n->off = n->start + 1;
		if (n->off > end) end = n->off;
	}
	t->end = end;

	t->grid = division;
	for (i = 0; i < t->nnotes; i++)
		t->grid = gcd64(t->grid, t->notes[i].start);
	t->length = ((end + t->grid - 1) / t->grid) * t->grid;
	/* the writer's marker wins, unless notes run past it (the file
	   was edited elsewhere since): then the grid rule as for any file */
	t->exact = 0;
	if (t->loop >= 0) {
		int64_t lastoff = 0;

		for (i = 0; i < t->nnotes; i++)
			if (t->notes[i].off > lastoff) lastoff = t->notes[i].off;
		if (t->loop >= lastoff && (t->loop > 0 || t->nnotes == 0)) {
			t->length = t->loop;
			t->exact = 1;
		}
	}

	if (t->nnotes == 0) return SMF_OK;
	qsort(t->notes, (size_t)t->nnotes, sizeof *t->notes, cmp_note);

	/* voices per channel, numbered on from the channels before */
	base = 0;
	i = 0;
	while (i < t->nnotes)
	{
		int used = 0;

		chan = t->notes[i].chan;
		for (; i < t->nnotes && t->notes[i].chan == chan; i++)
		{
			struct note *n = &t->notes[i];

			for (v = 0; v < used; v++)
				if (vlast_off[v] <= n->start
				    && vlast_start[v] < n->start)
					break;
			if (v == used)
			{
				if (used == cap)
				{
					int nc = cap ? cap * 2 : 8;
					int64_t *a, *b;

					a = realloc(vlast_start, (size_t)nc * sizeof *a);
					if (!a) goto nomem;
					vlast_start = a;
					b = realloc(vlast_off, (size_t)nc * sizeof *b);
					if (!b) goto nomem;
					vlast_off = b;
					cap = nc;
				}
				used++;
			}
			vlast_start[v] = n->start;
			vlast_off[v] = n->off;
			n->voice = base + v + 1;
		}
		nv = realloc(t->vchan, (size_t)(base + used + 1) * sizeof *nv);
		if (!nv) goto nomem;
		t->vchan = nv;
		for (v = 0; v < used; v++) t->vchan[base + v + 1] = chan;
		base += used;
	}
	t->nvoices = base;
	free(vlast_start);
	free(vlast_off);
	return SMF_OK;
nomem:
	free(vlast_start);
	free(vlast_off);
	return SMF_ERR_NOMEM;
}

/* One MTrk chunk's events, buf[pos..end). */
static int
read_track(struct smf_file *f, int tno, const unsigned char *buf,
           size_t pos, size_t end)
{
	struct track *t = &f->tracks[tno];
	int32_t *qhead, *qtail;
	int64_t tick = 0;
	uint32_t delta, len;
	int status = 0, r = SMF_OK, k;

	qhead = malloc(NKEYS * sizeof *qhead);
	qtail = malloc(NKEYS * sizeof *qtail);
	if (!qhead || !qtail)
	{
		free(qhead);
		free(qtail);
		return SMF_ERR_NOMEM;
	}
	for (k = 0; k < NKEYS; k++) qhead[k] = qtail[k] = -1;
	t->loop = -1;

	while (pos < end)
	{
		int b, hi, chan, d1, d2;

		if (vlq(buf, end, &pos, &delta) < 0) goto bad;
		tick += delta;
		if (pos >= end) goto bad;
		b = buf[pos];
		if (b == 0xFF)				/* meta */
		{
			int type;

			if (pos + 1 >= end) goto bad;
			type = buf[pos + 1];
			pos += 2;
			if (vlq(buf, end, &pos, &len) < 0) goto bad;
			if (len > end - pos) goto bad;
			status = 0;
			if (type == 0x2F) break;	/* end of track */
			if (type == 0x7F && len == MARK_LEN
			    && memcmp(buf + pos, MARK_ID, MARK_IDLEN) == 0)
			{
				uint64_t v = 0;
				int b8;

				for (b8 = 0; b8 < 8; b8++)
					v = (v << 8) | buf[pos + MARK_IDLEN + b8];
				if (v <= (uint64_t)INT64_MAX / 2)
					t->loop = (int64_t)v;
			}
			if (type == 0x51 && len >= 3 && tick < f->tempo_tick)
			{
				uint32_t us = ((uint32_t)buf[pos] << 16)
				            | ((uint32_t)buf[pos + 1] << 8)
				            | buf[pos + 2];
				/* to 3 decimals: a file stores whole
				   microseconds per beat, so 90 bpm comes
				   back as 89.999955 unrounded */
				if (us > 0)
				{
					f->info.tempo = floor(60000000000.0
					        / (double)us + 0.5) / 1000.0;
					f->tempo_tick = tick;
				}
			}
			if (type == 0x58 && len >= 2 && tick < f->ts_tick)
			{
				f->info.ts_num = buf[pos];
				f->info.ts_den = buf[pos + 1] < 31
				               ? 1 << buf[pos + 1] : -1;
				f->ts_tick = tick;
			}
			pos += len;
			continue;
		}
		if (b == 0xF0 || b == 0xF7)		/* SysEx */
		{
			pos++;
			if (vlq(buf, end, &pos, &len) < 0) goto bad;
			if (len > end - pos) goto bad;
			pos += len;
			status = 0;
			continue;
		}
		if (b >= 0xF0) goto bad;		/* not in a file */
		if (b & 0x80)
		{
			status = b;
			pos++;
		}
		else if (!status)
			goto bad;			/* data with no status */

		hi = status & 0xF0;
		chan = (status & 0x0F) + 1;
		if (pos >= end || (buf[pos] & 0x80)) goto bad;
		d1 = buf[pos++];
		d2 = 0;
		if (hi != 0xC0 && hi != 0xD0)
		{
			if (pos >= end || (buf[pos] & 0x80)) goto bad;
			d2 = buf[pos++];
		}

		k = (chan - 1) * 128 + d1;
		if (hi == 0x90 && d2 > 0)		/* note-on */
		{
			if ((r = add_note(t, tick, chan, d1, d2)) != SMF_OK)
				goto done;
			if (qtail[k] >= 0) t->notes[qtail[k]].next = t->nnotes - 1;
			else qhead[k] = t->nnotes - 1;
			qtail[k] = t->nnotes - 1;
		}
		else if (hi == 0x80 || hi == 0x90)	/* note-off */
		{
			int32_t i = qhead[k];

			if (i >= 0)		/* first on, first off */
			{
				t->notes[i].off = tick;
				qhead[k] = t->notes[i].next;
				if (qhead[k] < 0) qtail[k] = -1;
			}
		}
		else if (hi == 0xC0)
		{
			if ((r = add_prog(t, tick, chan, d1)) != SMF_OK)
				goto done;
		}
	}
	r = finish_track(t, tick, f->info.division);
	goto done;
bad:
	r = SMF_ERR_FORMAT;
done:
	free(qhead);
	free(qtail);
	return r;
}

/* ── The calls ─────────────────────────────────────────────────────── */

void
smf_free(struct smf_file *f)
{
	int i;

	if (!f) return;
	if (f->tracks)
		for (i = 0; i < f->info.ntracks; i++)
		{
			free(f->tracks[i].notes);
			free(f->tracks[i].progs);
			free(f->tracks[i].vchan);
		}
	free(f->tracks);
	free(f);
}

int
smf_parse(const unsigned char *buf, size_t len, struct smf_file **out)
{
	struct smf_file *f;
	uint32_t hlen, clen;
	size_t pos;
	int want, r;

	*out = NULL;
	if (!buf || len < 14 || memcmp(buf, "MThd", 4) != 0)
		return SMF_ERR_FORMAT;
	hlen = be32(buf + 4);
	if (hlen < 6 || hlen > len - 8) return SMF_ERR_FORMAT;

	f = calloc(1, sizeof *f);
	if (!f) return SMF_ERR_NOMEM;
	f->info.format = (int)be16(buf + 8);
	want = (int)be16(buf + 10);
	f->info.division = (int)be16(buf + 12);
	f->info.tempo = -1.0;
	f->info.ts_num = f->info.ts_den = -1;
	f->tempo_tick = f->ts_tick = INT64_MAX;
	if ((f->info.division & 0x8000) || f->info.division == 0)
	{
		free(f);
		return SMF_ERR_FORMAT;		/* SMPTE timing, or no ticks */
	}
	f->tracks = calloc((size_t)want + 1, sizeof *f->tracks);
	if (!f->tracks)
	{
		free(f);
		return SMF_ERR_NOMEM;
	}

	pos = 8 + (size_t)hlen;
	while (f->info.ntracks < want && len - pos >= 8)
	{
		clen = be32(buf + pos + 4);
		if (clen > len - pos - 8) clen = (uint32_t)(len - pos - 8);
		if (memcmp(buf + pos, "MTrk", 4) == 0)
		{
			f->info.ntracks++;	/* counted first: smf_free()
						   frees what it holds */
			r = read_track(f, f->info.ntracks - 1, buf, pos + 8,
			               pos + 8 + clen);
			if (r != SMF_OK)
			{
				smf_free(f);
				return r;
			}
		}
		pos += 8 + (size_t)clen;
	}
	*out = f;
	return SMF_OK;
}

void
smf_get_info(const struct smf_file *f, struct smf_info *info)
{
	*info = f->info;
}

int
smf_voices(const struct smf_file *f, int t)
{
	if (t < 1 || t > f->info.ntracks) return -1;
	return f->tracks[t - 1].nvoices;
}

int
smf_exact(const struct smf_file *f, int t)
{
	if (t < 1 || t > f->info.ntracks) return -1;
	return f->tracks[t - 1].exact;
}

int
smf_voice_info(const struct smf_file *f, int t, int v,
               struct smf_voice_info *vi)
{
	const struct track *tr;
	int32_t i;
	int64_t first = -1;

	if (t < 1 || t > f->info.ntracks) return SMF_ERR_RANGE;
	tr = &f->tracks[t - 1];
	if (v == 0 ? tr->nvoices != 0 : (v < 1 || v > tr->nvoices))
		return SMF_ERR_RANGE;

	vi->notes = 0;
	vi->length = tr->length;
	vi->end = tr->end;
	vi->grid = tr->grid;
	vi->channel = v ? tr->vchan[v] : -1;
	vi->low = vi->high = vi->program = -1;
	if (v == 0) return SMF_OK;

	for (i = 0; i < tr->nnotes; i++)
	{
		const struct note *n = &tr->notes[i];

		if (n->voice != v) continue;
		if (first < 0) first = n->start;
		vi->notes++;
		if (vi->low < 0 || n->pitch < vi->low) vi->low = n->pitch;
		if (n->pitch > vi->high) vi->high = n->pitch;
	}
	/* program changes are in file order, so ticks never go down */
	for (i = 0; i < tr->nprogs; i++)
		if (tr->progs[i].chan == vi->channel && tr->progs[i].tick <= first)
			vi->program = tr->progs[i].prog;
	return SMF_OK;
}

int
smf_voice_cols(const struct smf_file *f, int t, int v,
               struct smf_col **cols, int32_t *ncols)
{
	const struct track *tr;
	struct smf_col *c;
	double div;
	int32_t i, n = 0, k = 0, last = -1;

	*cols = NULL;
	*ncols = 0;
	if (t < 1 || t > f->info.ntracks) return SMF_ERR_RANGE;
	tr = &f->tracks[t - 1];
	if (v < 1 || v > tr->nvoices) return SMF_ERR_RANGE;
	div = (double)f->info.division;

	for (i = 0; i < tr->nnotes; i++)
		if (tr->notes[i].voice == v)
		{
			if (n == 0 && tr->notes[i].start > 0) n++;	/* rest */
			n++;
		}
	c = malloc((size_t)n * sizeof *c);
	if (!c) return SMF_ERR_NOMEM;

	/* notes are sorted by channel and start, and a voice is one
	   channel, so its notes come in order of start */
	for (i = 0; i < tr->nnotes; i++)
	{
		const struct note *nt = &tr->notes[i];

		if (nt->voice != v) continue;
		if (last < 0 && nt->start > 0)
		{
			c[k].pitch = -1;
			c[k].step = (double)nt->start / div;
			c[k].vel = -1;
			c[k].len = -1.0;
			c[k].chan = -1;
			k++;
		}
		if (last >= 0)
			c[k - 1].step = (double)(nt->start - tr->notes[last].start) / div;
		c[k].pitch = nt->pitch;
		c[k].vel = nt->vel;
		c[k].len = (double)(nt->off - nt->start) / div;
		c[k].chan = nt->chan;
		c[k].step = 0.0;
		k++;
		last = i;
	}
	c[k - 1].step = (double)(tr->length - tr->notes[last].start) / div;
	*cols = c;
	*ncols = n;
	return SMF_OK;
}

/* ── Writing (Phase 9, step 2) ─────────────────────────────────────── */

struct wbuf {
	unsigned char *p;
	size_t len, cap;
	int err;
};

static void put(struct wbuf *w, const void *d, size_t n)
{
	if (w->err)
		return;
	if (w->len + n > w->cap) {
		size_t cap = w->cap ? w->cap : 1024;
		unsigned char *np;

		while (cap < w->len + n)
			cap *= 2;
		np = realloc(w->p, cap);
		if (!np) {
			w->err = SMF_ERR_NOMEM;
			return;
		}
		w->p = np;
		w->cap = cap;
	}
	memcpy(w->p + w->len, d, n);
	w->len += n;
}

static void put1(struct wbuf *w, int b)
{
	unsigned char c = (unsigned char)b;

	put(w, &c, 1);
}

static void put_be(struct wbuf *w, uint32_t v, int bytes)
{
	while (bytes--)
		put1(w, (int)((v >> (8 * bytes)) & 0xff));
}

/* A delta time; the file's numbers hold 28 bits */
static void put_vlq(struct wbuf *w, int64_t v)
{
	unsigned char b[4];
	int n = 0;

	if (v < 0 || v > 0x0FFFFFFF) {
		w->err = SMF_ERR_VALUE;
		return;
	}
	b[n++] = (unsigned char)(v & 0x7f);
	while (v >>= 7)
		b[n++] = (unsigned char)(0x80 | (v & 0x7f));
	while (n--)
		put1(w, b[n]);
}

struct wev {
	int64_t tick;
	int     on;		/* 0 note-off, 1 note-on: offs first */
	int32_t note;
};

static int cmp_wev(const void *pa, const void *pb)
{
	const struct wev *a = pa, *b = pb;

	if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
	if (a->on != b->on) return a->on - b->on;
	return a->note < b->note ? -1 : (a->note > b->note);
}

static int write_track(struct wbuf *w, const struct smf_wtrack *t,
                       int first, double tempo)
{
	struct wev *ev = NULL;
	size_t at;
	int64_t last = 0;
	int32_t i, k = 0;

	if (t->n < 0 || t->n > INT32_MAX / 2 || t->length < 0)
		return SMF_ERR_VALUE;
	for (i = 0; i < t->n; i++) {
		const struct smf_wnote *n = &t->notes[i];

		if (n->pitch < 0 || n->pitch > 127 || n->vel < 1 || n->vel > 127
		    || n->chan < 1 || n->chan > 16 || n->start < 0
		    || n->off <= n->start || n->off > t->length)
			return SMF_ERR_VALUE;
	}
	if (t->n > 0) {
		ev = malloc((size_t)t->n * 2 * sizeof *ev);
		if (!ev)
			return SMF_ERR_NOMEM;
		for (i = 0; i < t->n; i++) {
			ev[k].tick = t->notes[i].start;
			ev[k].on = 1;
			ev[k++].note = i;
			ev[k].tick = t->notes[i].off;
			ev[k].on = 0;
			ev[k++].note = i;
		}
		qsort(ev, (size_t)k, sizeof *ev, cmp_wev);
	}

	put(w, "MTrk", 4);
	at = w->len;
	put_be(w, 0, 4);		/* the length, filled in below */
	if (first && tempo > 0) {
		double us = floor(60000000.0 / tempo + 0.5);

		if (us > 0xFFFFFF) us = 0xFFFFFF;
		if (us < 1) us = 1;
		put_vlq(w, 0);
		put1(w, 0xFF); put1(w, 0x51); put1(w, 3);
		put_be(w, (uint32_t)us, 3);
	}
	if (t->length > 0) {		/* the loop length, exactly */
		int b8;

		put_vlq(w, 0);
		put1(w, 0xFF); put1(w, 0x7F); put1(w, MARK_LEN);
		put(w, MARK_ID, MARK_IDLEN);
		for (b8 = 7; b8 >= 0; b8--)
			put1(w, (int)((t->length >> (8 * b8)) & 0xff));
	}
	for (i = 0; i < k; i++) {
		const struct smf_wnote *n = &t->notes[ev[i].note];

		put_vlq(w, ev[i].tick - last);
		last = ev[i].tick;
		if (ev[i].on) {
			put1(w, 0x90 | (n->chan - 1));
			put1(w, n->pitch);
			put1(w, n->vel);
		} else {
			put1(w, 0x80 | (n->chan - 1));
			put1(w, n->pitch);
			put1(w, 64);	/* the usual note-off velocity */
		}
	}
	free(ev);
	put_vlq(w, t->length - last);	/* end of track at the length */
	put1(w, 0xFF); put1(w, 0x2F); put1(w, 0);
	if (!w->err) {
		uint32_t tl = (uint32_t)(w->len - at - 4);

		w->p[at] = (unsigned char)(tl >> 24);
		w->p[at + 1] = (unsigned char)(tl >> 16);
		w->p[at + 2] = (unsigned char)(tl >> 8);
		w->p[at + 3] = (unsigned char)tl;
	}
	return w->err;
}

int
smf_write(const struct smf_wtrack *tracks, int ntracks, int division,
          double tempo, unsigned char **buf, size_t *len)
{
	struct wbuf w = { NULL, 0, 0, 0 };
	int t, r;

	*buf = NULL;
	*len = 0;
	if (ntracks < 1 || ntracks > 65535 || division < 1 || division > 32767)
		return SMF_ERR_VALUE;
	put(&w, "MThd", 4);
	put_be(&w, 6, 4);
	put_be(&w, 1, 2);			/* format 1 */
	put_be(&w, (uint32_t)ntracks, 2);
	put_be(&w, (uint32_t)division, 2);
	for (t = 0; t < ntracks; t++)
		if ((r = write_track(&w, &tracks[t], t == 0, tempo)) != SMF_OK) {
			free(w.p);
			return r;
		}
	if (w.err) {
		free(w.p);
		return w.err;
	}
	*buf = w.p;
	*len = w.len;
	return SMF_OK;
}
