/*
	smf.h -- Standard MIDI File reading (Phase 9, step 1; Oct 16, 2026).

	Part of the sequencer library (libseq.a) but independent of the
	sequencer itself: nothing here touches the sequencer's state, opens
	a port or starts a thread. Plain C, no interpreter headers. The
	caller reads the file into memory; this parses the bytes, so the
	library knows nothing about file names or $APLHOME (the ⎕MIDIREAD
	glue in seqfuncs.c finds the file).

	Design: MIDI_SEQUENCER_NOTES.md, Phase 9. In short:

	  - Tracks are numbered in file order from 1. There is no special
	    "conductor" track: a track with no notes (tempo map only, say)
	    is a track with no voices.
	  - Notes are paired note-on with note-off per channel and pitch,
	    first on, first off. A note-on with velocity 0 is a note-off.
	    A note with no note-off ends at the end of its track; a note-off
	    with no note-on is ignored. A note of length 0 is made 1 tick.
	  - Each track is split into voices that sound one note at a time,
	    so each voice can be sent to the sequencer as a track: first by
	    channel (ascending), then, within a channel, each note (taken in
	    order of start, lowest pitch first among notes starting on the
	    same tick) goes to the lowest-numbered voice whose last note has
	    ended by the new note's start (rule (b), the person, Oct 16,
	    2026). A chord's notes go to voices 1, 2, ... lowest first; a
	    note held under moving notes keeps its voice. Voices are
	    numbered from 1 within the track, all of the lowest channel's
	    voices first.
	  - Where a track ends (its loop length): in a file smf_write()
	    wrote, exactly the length it recorded (a sequencer-specific
	    meta event, see smf.c; option (c), agreed Oct 16, 2026), unless
	    notes now run past it. Otherwise the later of its end-of-track
	    event and its last note-off, rounded up to the track's grid
	    (agreed Oct 16, 2026). The grid is the greatest
	    common divisor of the file's ticks per beat and every note start
	    in the track: the largest step, at most a beat, that every note
	    starts on. MuseScore ends a track a little after its last
	    note-off, which this puts back on the music's grid; a track
	    played in by hand, not quantized, has a grid of 1 tick and is
	    left alone. Every voice of a track has the track's length.

	Everything is in the file's own ticks (division per beat) until the
	voice matrix is built, where steps and lengths become beats.

	Writing (Phase 9, step 2; Oct 16, 2026): smf_write() makes a format
	1 file from tracks of notes given in ticks (the ⎕MIDIWRITE glue
	gets them from the sequencer). Track 1 starts with the tempo and
	(V100) the key signature, C major; there is no separate conductor
	track, as MuseScore writes it, so the file's track k is the
	caller's track k. V100: each track's bank select and program
	change, if it has a program, at its start. V101: the time
	signature in track 1, when the caller gives one (none: players
	assume 4/4), and each track's name, when it has one, as its
	first event. Each track's
	end-of-track falls at its length, and the length is also recorded
	exactly in a marker the reader looks for, so a loop comes back as
	long as it went in.

	What's skipped: controllers, pitch bend, aftertouch, SysEx and meta
	events other than tempo, time signature and end of track. Program
	changes, and bank select (controllers 0 and 32; V100), are read
	only to report the program and bank a voice starts with.
	Not supported (SMF_ERR_FORMAT): SMPTE timing (a negative division).
*/

#ifndef SMF_H
#define SMF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum smf_err {
	SMF_OK = 0,
	SMF_ERR_FORMAT,		/* not a Standard MIDI File, damaged, or
				   SMPTE timing */
	SMF_ERR_NOMEM,
	SMF_ERR_RANGE,		/* no such track or voice */
	SMF_ERR_VALUE		/* smf_write(): something it can't write
				   (Phase 9, step 2) */
};

struct smf_file;		/* a parsed file; smf_parse(), smf_free() */

/* Parses len bytes. On SMF_OK *out is a new parsed file (free it with
   smf_free()); otherwise *out is NULL. The bytes aren't kept. */
int smf_parse(const unsigned char *buf, size_t len, struct smf_file **out);
void smf_free(struct smf_file *f);

/* The file as a whole. */
struct smf_info {
	int    format;		/* 0, 1 or 2, as the header says */
	int    ntracks;		/* track chunks read */
	int    division;	/* ticks per beat */
	double tempo;		/* beats per minute of the earliest tempo
				   event in the file (lowest track first on
				   a tie), rounded to 3 decimals; -1 if
				   there is none (a player then assumes
				   120) */
	int    ts_num;		/* earliest time signature, e.g. 3 and 4 */
	int    ts_den;		/*   for 3/4; both -1 if there is none */
};
void smf_get_info(const struct smf_file *f, struct smf_info *info);

/* How many voices track t (1..ntracks) has: 0 if it has no notes,
   -1 if there is no such track. */
int smf_voices(const struct smf_file *f, int t);

/* One voice of a track, or the track itself when it has no notes. */
struct smf_voice_info {
	int     notes;		/* 0 for a track with no notes */
	int64_t length;		/* the track's length, ticks: the writer's
				   marker's if there is one (smf_exact()),
				   else rounded to its grid; the same for
				   all its voices */
	int64_t end;		/* the track's end before rounding, ticks
				   (for tests and diagnostics) */
	int64_t grid;		/* the track's grid, ticks */
	int     channel;	/* 1..16; -1 for a track with no notes */
	int     low, high;	/* lowest and highest pitch; -1, no notes */
	int     program;	/* 0..127: the last program change on the
				   voice's channel in this track at or before
				   its first note; -1 if none */
	int     bank;		/* V100: 0..16383, 128 x the last controller
				   0 (bank MSB) plus the last controller 32
				   (LSB) on the voice's channel in this track
				   at or before its first note, a missing
				   half counting as 0; -1 if neither */
};

/* Track t (1..ntracks), voice v (1..smf_voices(); 0 asks about a track
   with no notes). SMF_ERR_RANGE for anything else. */
int smf_voice_info(const struct smf_file *f, int t, int v,
                   struct smf_voice_info *vi);

/* 1 if track t's length is the one smf_write() recorded (its marker),
   0 if it came from the grid rule, -1 if there is no such track. A
   call of its own rather than a field of struct smf_voice_info, so that
   struct keeps the layout seqfuncs.c was built with (objects aren't
   rebuilt when only a header changes; TESTING.md). */
int smf_exact(const struct smf_file *f, int t);

/* One column of a voice, as ⎕SEQTRACK takes it (rows 1-5): pitch, step
   in beats, velocity, length in beats, channel. A leading rest (the
   voice's first note starting after tick 0) is pitch -1 with -1 in
   velocity, length and channel. */
struct smf_col {
	int    pitch;
	double step;
	int    vel;
	double len;
	int    chan;
};

/* The columns of track t, voice v (1..smf_voices()). *ncols receives
   the count; cols is malloc'd (free it) and NULL on error.
   SMF_ERR_RANGE for no such track or voice (a track with no notes has
   no voice 1). Steps and lengths are ticks divided by the file's
   ticks per beat, unrounded: the sequencer rounds start positions to
   its own ticks. A voice's steps add up to the track's length. */
int smf_voice_cols(const struct smf_file *f, int t, int v,
                   struct smf_col **cols, int32_t *ncols);

/* ── Writing (Phase 9, step 2) ───────────────────────────────────── */

struct smf_wnote {
	int64_t start;		/* ticks from the start of the track */
	int64_t off;		/* its note-off, after start */
	int     pitch;		/* 0..127 */
	int     vel;		/* 1..127 */
	int     chan;		/* 1..16 */
};

struct smf_wtrack {
	const struct smf_wnote *notes;	/* in order of start; may be NULL
					   when n is 0 */
	int32_t n;
	int64_t length;		/* where its end-of-track goes: at least
				   the last note-off (0 for an empty
				   track) */
	/* V100: written at the track's start, before any note: bank
	   select (controller 0 = bank / 128, controller 32 = bank % 128)
	   if bank is 0..16383, then the program change, on channel. -1
	   for program writes neither (channel isn't looked at then); -1
	   for bank, only the program. Every field must be set: zeros
	   would mean program 0 on channel 0, which is SMF_ERR_VALUE. */
	int     channel;	/* 1..16 */
	int     program;	/* 0..127, or -1 */
	int     bank;		/* 0..16383, or -1 */
	/* V101: written as the track's name (FF 03), its first event;
	   NULL or "" for none (so zeros mean no name). Written as the
	   bytes given; the caller keeps them to what it wants in a file
	   (the sequencer's names are printable ASCII). */
	const char *name;
};

/* Writes ntracks (1..65535) tracks as a format 1 file with division
   ticks per beat (1..32767) and, at the start of track 1, the tempo
   (beats per minute; none if tempo <= 0), the time signature (V101,
   Oct 19, 2026: ts_num over ts_den, FF 58 04 nn dd 18 08; none if
   ts_num <= 0, else ts_num 1..255 and ts_den a power of 2 from 1 to
   32; MuseScore assumes 4/4 without one and reads only nn and dd)
   and the key signature C major (FF 59 02 00 00; V100, agreed Oct 18,
   2026: MuseScore guessed a key for a file with none), in that order,
   after the track's name if it has one. Within a track, note-offs
   on a tick come before note-ons, so a repeated pitch is struck
   again. No running status. *buf receives a malloc'd copy of the file
   (free it), *len its size. SMF_ERR_VALUE if anything is out of
   range (a note's values, a note-off not after its start, a length
   before a note-off, a gap between events too long for the file's
   28-bit delta times), SMF_ERR_NOMEM. A tempo under about 3.6 bpm
   doesn't fit the file's 24 bits and is written as the slowest it
   can hold. */
int smf_write(const struct smf_wtrack *tracks, int ntracks, int division,
              double tempo, int ts_num, int ts_den,
              unsigned char **buf, size_t *len);

/* Short English text for an error code, for the test driver. */
const char *smf_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* SMF_H */
