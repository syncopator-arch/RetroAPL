// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	seq_fake_dev.c -- fake ALSA raw MIDI device for check_device; see
	seq_fake_dev.h. Linux only, test builds only.

	Script commands (seqtest "fakedev ..."):

	  fakedev port C D NSUB    card C gets raw MIDI device D with NSUB
	                           output subdevices
	  fakedev busy C D S       subdevice S is taken by another program
	  fakedev free C D S       and released again
	  fakedev inonly C D       device D on card C has input only
	  fakedev deny C           opening card C's files gives EACCES
	  fakedev name C TEXT...   card C's name (default "Fake card C");
	                           \xHH is the byte HH (Phase 8)
	  fakedev room N|all       the output buffer takes N more bytes
	                           (short write, then EAGAIN), or all
	  fakedev drain            room again for everything; wakes poll()
	  fakedev eintr            the next write fails once with EINTR
	  fakedev unplug C         card C disappears: writes ENODEV, poll
	                           POLLERR, opens ENOENT
	  fakedev replug C         card C is back (open it again)
	  fakedev log              the calls made since the last log, and
	                           the bytes written (hex)
*/

#include "seq_fake_dev.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

#define MAXCARD 32
#define MAXDEV  4
#define MAXSUB  16
#define MAXFD   16
#define FD_BASE 1000

static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;

static struct {
	int nsub[MAXDEV];		/* 0: no such device */
	int busy[MAXDEV][MAXSUB];	/* taken by another program */
	int deny;
	int gone;			/* unplugged */
	int inonly[MAXDEV];		/* input only: no output stream */
	char name[80];			/* "" : "Fake card C" (Phase 8) */
} card[MAXCARD];

static struct {
	int used, ctl, card, dev, sub, flags, dead, prefer;
	pthread_t owner;
} fds[MAXFD];

static long room = -1;			/* -1: unlimited */
static int  eintr_next;
static char logbuf[16384];
static size_t loglen;

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void logf_(const char *fmt, ...)
{
	va_list ap;
	int n;

	if (loglen >= sizeof logbuf - 2)
		return;
	va_start(ap, fmt);
	n = vsnprintf(logbuf + loglen, sizeof logbuf - loglen - 1, fmt, ap);
	va_end(ap);
	if (n > 0)
		loglen += ((size_t)n < sizeof logbuf - loglen - 1)
		          ? (size_t)n : sizeof logbuf - loglen - 2;
	logbuf[loglen++] = '\n';
	logbuf[loglen] = '\0';
}

static int newfd(void)
{
	int i;

	for (i = 0; i < MAXFD; i++)
		if (!fds[i].used) {
			memset(&fds[i], 0, sizeof fds[i]);
			fds[i].used = 1;
			fds[i].prefer = -1;
			return i;
		}
	return -1;
}

static int slot(int fd)
{
	int i = fd - FD_BASE;

	return (i >= 0 && i < MAXFD && fds[i].used) ? i : -1;
}

int fake_open(const char *path, int flags)
{
	int c, d, i, s, r = -1, e = 0;

	pthread_mutex_lock(&lk);
	if (!(flags & O_CLOEXEC))
		logf_("  (no O_CLOEXEC on %s)", path);
	if (sscanf(path, "/dev/snd/controlC%d", &c) == 1
	    && !strstr(path, "midi")) {
		if (c < 0 || c >= MAXCARD || card[c].gone
		    || !(card[c].nsub[0] || card[c].nsub[1]
		         || card[c].nsub[2] || card[c].nsub[3]))
			e = ENOENT;
		else if (card[c].deny)
			e = EACCES;
		else if ((i = newfd()) < 0)
			e = EMFILE;
		else {
			fds[i].ctl = 1;
			fds[i].card = c;
			fds[i].owner = pthread_self();
			r = FD_BASE + i;
		}
		if (e != ENOENT || (c >= 0 && c < MAXCARD && card[c].gone))
			logf_("open controlC%d: %s", c, e ? strerror(e) : "ok");
		/* (a card that doesn't exist isn't logged: listing ports
		   tries all 32) */
	} else if (sscanf(path, "/dev/snd/midiC%dD%d", &c, &d) == 2) {
		int pref = -1;

		if (c < 0 || c >= MAXCARD || d < 0 || d >= MAXDEV
		    || card[c].gone || !card[c].nsub[d])
			e = ENOENT;
		else if (card[c].deny)
			e = EACCES;
		else if (card[c].inonly[d])
			e = ENXIO;	/* the kernel: no output stream */
		else {
			/* the kernel: a preference set from this thread */
			for (i = 0; i < MAXFD; i++)
				if (fds[i].used && fds[i].ctl && fds[i].card == c
				    && pthread_equal(fds[i].owner, pthread_self())
				    && fds[i].prefer != -1)
					pref = fds[i].prefer;
			s = -1;
			if (pref >= card[c].nsub[d])	/* as the kernel */
				e = ENODEV;
			for (i = 0; !e && i < card[c].nsub[d]; i++) {
				int taken = card[c].busy[d][i], k;

				for (k = 0; k < MAXFD; k++)
					if (fds[k].used && !fds[k].ctl && !fds[k].dead
					    && fds[k].card == c && fds[k].dev == d
					    && fds[k].sub == i)
						taken = 1;
				if ((pref < 0 || pref == i) && !taken) {
					s = i;
					break;
				}
			}
			if (e)
				;
			else if (s < 0)
				e = (flags & O_NONBLOCK) ? EBUSY : EDEADLK;
			else if ((i = newfd()) < 0)
				e = EMFILE;
			else {
				fds[i].card = c;
				fds[i].dev = d;
				fds[i].sub = s;
				fds[i].flags = flags;
				r = FD_BASE + i;
			}
		}
		if (e == EDEADLK)
			logf_("open midiC%dD%d: busy, and a blocking open"
			      " would wait forever", c, d);
		else
			logf_("open midiC%dD%d%s: %s", c, d,
			      (flags & O_NONBLOCK) ? " nonblock" : "",
			      e ? strerror(e) : "ok");
	} else {
		e = ENOENT;
		logf_("open %s: no such file", path);
	}
	pthread_mutex_unlock(&lk);
	if (r < 0)
		errno = e;
	return r;
}

int fake_close(int fd)
{
	int i;

	pthread_mutex_lock(&lk);
	i = slot(fd);
	if (i >= 0) {
		if (fds[i].ctl)
			logf_("close controlC%d", fds[i].card);
		else
			logf_("close midiC%dD%d sub %d", fds[i].card,
			      fds[i].dev, fds[i].sub);
		fds[i].used = 0;
	}
	pthread_mutex_unlock(&lk);
	if (i < 0) {
		errno = EBADF;
		return -1;
	}
	return 0;
}

int fake_ioctl(int fd, unsigned long req, void *arg)
{
	int i, r = 0, e = 0;

	pthread_mutex_lock(&lk);
	i = slot(fd);
	if (i < 0)
		e = EBADF;
	else if (fds[i].dead || card[fds[i].card].gone)
		e = ENODEV;
	else if (req == SNDRV_CTL_IOCTL_CARD_INFO && fds[i].ctl) {
		struct snd_ctl_card_info *ci = arg;

		if (card[fds[i].card].name[0])
			snprintf((char *)ci->name, sizeof ci->name, "%s",
			         card[fds[i].card].name);
		else
			snprintf((char *)ci->name, sizeof ci->name, "Fake card %d",
			         fds[i].card);
	} else if (req == SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE && fds[i].ctl) {
		int d = *(int *)arg + 1, c = fds[i].card;

		while (d < MAXDEV && !card[c].nsub[d])
			d++;
		*(int *)arg = d < MAXDEV ? d : -1;
	} else if (req == SNDRV_CTL_IOCTL_RAWMIDI_INFO && fds[i].ctl) {
		struct snd_rawmidi_info *in = arg;
		int c = fds[i].card;

		if (in->device >= MAXDEV || !card[c].nsub[in->device])
			e = ENXIO;
		else if (card[c].inonly[in->device])
			e = ENOENT;	/* no output stream */
		else if (in->subdevice >= (unsigned)card[c].nsub[in->device])
			e = ENXIO;
		else {
			snprintf((char *)in->name, sizeof in->name, "Fake MIDI %u",
			         in->device);
			snprintf((char *)in->subname, sizeof in->subname,
			         "Fake MIDI %u-%u", in->device, in->subdevice);
			in->subdevices_count = (unsigned)card[c].nsub[in->device];
			in->card = c;
		}
	} else if (req == SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE && fds[i].ctl) {
		fds[i].prefer = *(int *)arg;
		logf_("ioctl prefer-subdevice %d", fds[i].prefer);
	} else if (req == SNDRV_RAWMIDI_IOCTL_INFO && !fds[i].ctl) {
		struct snd_rawmidi_info *in = arg;

		in->device = (unsigned)fds[i].dev;
		in->subdevice = (unsigned)fds[i].sub;
		in->card = fds[i].card;
		logf_("ioctl info: subdevice %d", fds[i].sub);
	} else
		e = ENOTTY;
	pthread_mutex_unlock(&lk);
	if (e) {
		errno = e;
		r = -1;
	}
	return r;
}

ssize_t fake_write(int fd, const void *buf, size_t n)
{
	const unsigned char *b = buf;
	char hex[64];
	size_t k, take;
	int i, e = 0;
	ssize_t r = -1;

	pthread_mutex_lock(&lk);
	i = slot(fd);
	if (i < 0 || fds[i].ctl)
		e = EBADF;
	else if (fds[i].dead || card[fds[i].card].gone) {
		fds[i].dead = 1;
		e = ENODEV;
	} else if (eintr_next) {
		eintr_next = 0;
		e = EINTR;
	} else if (room == 0) {
		e = (fds[i].flags & O_NONBLOCK) ? EAGAIN : EDEADLK;
	} else {
		take = (room < 0 || (size_t)room >= n) ? n : (size_t)room;
		if (room > 0)
			room -= (long)take;
		hex[0] = '\0';
		for (k = 0; k < take && k < 16; k++)
			snprintf(hex + strlen(hex), sizeof hex - strlen(hex),
			         "%s%02x", k ? " " : "", b[k]);
		if (take < n)
			logf_("write %s (%zu of %zu)", hex, take, n);
		else
			logf_("write %s", hex);
		r = (ssize_t)take;
	}
	if (e && e != EBADF)
		logf_("write: %s", e == EDEADLK
		      ? "buffer full, blocking write would wait" : strerror(e));
	pthread_mutex_unlock(&lk);
	if (r < 0)
		errno = e;
	return r;
}

int fake_poll(struct pollfd *p, nfds_t n, int timeout_ms)
{
	struct timespec until;
	int i, r = 0;

	(void)n;
	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_sec += timeout_ms / 1000;
	until.tv_nsec += (long)(timeout_ms % 1000) * 1000000;
	if (until.tv_nsec >= 1000000000) {
		until.tv_sec++;
		until.tv_nsec -= 1000000000;
	}
	pthread_mutex_lock(&lk);
	logf_("poll (wait for room)");
	for (;;) {
		i = slot(p->fd);
		p->revents = 0;
		if (i < 0)
			p->revents = POLLNVAL;
		else if (fds[i].dead || card[fds[i].card].gone)
			p->revents = POLLERR | POLLNVAL;
		else if (room != 0)
			p->revents = POLLOUT;
		if (p->revents) {
			r = 1;
			break;
		}
		if (pthread_cond_timedwait(&cv, &lk, &until) == ETIMEDOUT) {
			logf_("poll: timed out");
			break;
		}
	}
	pthread_mutex_unlock(&lk);
	return r;
}

void fake_reset(void)
{
	pthread_mutex_lock(&lk);
	memset(card, 0, sizeof card);
	memset(fds, 0, sizeof fds);
	room = -1;
	eintr_next = 0;
	loglen = 0;
	logbuf[0] = '\0';
	pthread_mutex_unlock(&lk);
}

static int num(const char *s, int lo, int hi, int *v)
{
	char *end;
	long x = strtol(s, &end, 10);

	if (end == s || *end || x < lo || x > hi)
		return 0;
	*v = (int)x;
	return 1;
}

/* Phase 8: the words from w[i] on, joined by single spaces, with
   \xHH as the byte HH, into card c's name. 0 if malformed. */
static int set_name(int c, char **w, int i, int nw)
{
	char *d = card[c].name, *end = d + sizeof card[c].name - 1;
	const char *p;
	unsigned v;

	for (; i < nw; i++) {
		if (d > card[c].name && d < end)
			*d++ = ' ';
		for (p = w[i]; *p && d < end; p++) {
			if (p[0] == '\\' && p[1] == 'x' && isxdigit((unsigned char)p[2])
			    && isxdigit((unsigned char)p[3])
			    && sscanf(p + 2, "%2x", &v) == 1) {
				*d++ = (char)v;
				p += 3;
			} else
				*d++ = *p;
		}
	}
	*d = '\0';
	return card[c].name[0] != '\0';
}

int fake_command(char **w, int nw, void *outp)
{
	FILE *out = outp;
	int c, d, s, ok = 1;

	if (nw < 2)
		return 1;
	pthread_mutex_lock(&lk);
	if (!strcmp(w[1], "port") && nw == 5 && num(w[2], 0, MAXCARD - 1, &c)
	    && num(w[3], 0, MAXDEV - 1, &d) && num(w[4], 1, MAXSUB, &s))
		card[c].nsub[d] = s;
	else if ((!strcmp(w[1], "busy") || !strcmp(w[1], "free")) && nw == 5
	         && num(w[2], 0, MAXCARD - 1, &c)
	         && num(w[3], 0, MAXDEV - 1, &d) && num(w[4], 0, MAXSUB - 1, &s))
		card[c].busy[d][s] = (w[1][0] == 'b');
	else if (!strcmp(w[1], "inonly") && nw == 4
	         && num(w[2], 0, MAXCARD - 1, &c) && num(w[3], 0, MAXDEV - 1, &d))
		card[c].inonly[d] = 1;
	else if (!strcmp(w[1], "deny") && nw == 3 && num(w[2], 0, MAXCARD - 1, &c))
		card[c].deny = 1;
	else if (!strcmp(w[1], "name") && nw >= 4 && num(w[2], 0, MAXCARD - 1, &c))
		ok = set_name(c, w, 3, nw);
	else if (!strcmp(w[1], "room") && nw == 3) {
		if (!strcmp(w[2], "all"))
			room = -1;
		else if (num(w[2], 0, 1000000, &s))
			room = s;
		else
			ok = 0;
		pthread_cond_broadcast(&cv);
	} else if (!strcmp(w[1], "drain") && nw == 2) {
		room = -1;
		pthread_cond_broadcast(&cv);
	} else if (!strcmp(w[1], "eintr") && nw == 2)
		eintr_next = 1;
	else if (!strcmp(w[1], "unplug") && nw == 3
	         && num(w[2], 0, MAXCARD - 1, &c)) {
		card[c].gone = 1;
		for (s = 0; s < MAXFD; s++)	/* open files stay dead */
			if (fds[s].used && fds[s].card == c)
				fds[s].dead = 1;
		pthread_cond_broadcast(&cv);
	} else if (!strcmp(w[1], "replug") && nw == 3
	           && num(w[2], 0, MAXCARD - 1, &c))
		card[c].gone = 0;
	else if (!strcmp(w[1], "log") && nw == 2) {
		char *p = logbuf, *e;

		while (*p) {
			e = strchr(p, '\n');
			fprintf(out, "  %.*s\n", (int)(e ? e - p : (long)strlen(p)), p);
			p = e ? e + 1 : p + strlen(p);
		}
		loglen = 0;
		logbuf[0] = '\0';
	} else
		ok = 0;
	pthread_mutex_unlock(&lk);
	return ok ? 0 : 1;
}
