/*
	seq_out_device.c -- output backend for a real MIDI device.

	Linux (Phase 3): ALSA raw MIDI, written straight to
	/dev/snd/midiC<card>D<device>, with no alsa-lib. The port name is
	"hw:C,D,S", "hw:C,D" (any free subdevice) or "hw:C" (device 0).
	Windows (Phase 6, brought forward Oct 4, 2026): WinMM, the port
	being an output device number as text ("0").
	Either (Phase 8, Oct 15, 2026): any other name is a device name,
	looked up in seq_dev_list() at each open (find_port() below; the
	rule is in seq.h at seq_open()).
	Elsewhere: a placeholder that fails to open (SEQ_ERR_PORT).

	Every call is made with the sequencer's lock held (seq_panic()
	excepted), so the device needs no lock of its own; the diagnostics,
	read from other threads, have one.

	── Linux ──────────────────────────────────────────────────────────
	Behavior relied on, by reading the Linux 5.15 ALSA source
	(sound/core/rawmidi.c, control.c, init.c) and alsa-lib's
	rawmidi_hw.c; the open, write and unplug behavior was then seen on
	the Jetson (5.15.148-tegra, CME U2MIDI Pro), Oct 4, 2026:

	- Choosing a subdevice: SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE on
	  /dev/snd/controlC<card>, then open the MIDI device. The kernel
	  matches the preference by the *thread* that opened the control
	  device, so both opens happen here, back to back; the control
	  device is closed afterwards. As alsa-lib does, the subdevice
	  actually opened is checked with SNDRV_RAWMIDI_IOCTL_INFO.
	- A plain open of a port another program has waits until it is
	  free; seq_open() holds the sequencer's lock, so the open is
	  O_NONBLOCK and a busy port fails at once (EBUSY).
	- O_CLOEXEC on both: a program started by ⎕EXEC must not inherit
	  the port and keep it busy after the interpreter lets go.
	- Writes stay O_NONBLOCK (agreed: option (b)). A full kernel buffer
	  (one page, emptied at the device's speed) gives EAGAIN or a short
	  write; the rest of the message is written as room appears, for up
	  to SEQ_DEV_WRITE_LIMIT_MS, after which the port counts as lost. A
	  message is never left half written: there's no running status, so
	  a half message would garble the next one.
	- After a USB disconnect every write fails at once with ENODEV, and
	  a waiting poll() returns POLLERR. Any error other than EAGAIN and
	  EINTR counts as port lost. (Seen on the Jetson.)
	- close() waits for the output to drain (up to 10 s in the kernel).
	  On a live port that's what delivers the last note-offs; after an
	  unplug with bytes left in the buffer it could take the full 10 s.
	  Measured on the Jetson: 0.1 ms after an unplug, so it stays here.

	Test builds (-DSEQ_DEVICE_TEST, make check_device) route the five
	system calls to a fake device (seq_fake_dev.c), so the code path
	here is the same one that runs on the hardware.

	── Windows ────────────────────────────────────────────────────────
	By reading the WinMM documentation; not yet run on the hardware:

	- midiOutOpen() with no callback. MMSYSERR_ALLOCATED is "in use"
	  (the built-in USB MIDI driver lets one program at a time have a
	  device); a bad number, MMSYSERR_NODRIVER or MIDIERR_NODEVICE is
	  "no such port".
	- midiOutShortMsg() per message (always a complete 3-byte message).
	  MIDIERR_NOTREADY (driver busy) is retried every 1 ms for up to
	  SEQ_DEV_WRITE_LIMIT_MS, as a full buffer is on Linux; any other
	  error counts as port lost. Whether an unplugged device gives an
	  error at all, or swallows messages, is to be seen.
	- midiOutClose() without midiOutReset(): the sequencer has already
	  sent its own note-offs, and a reset can add a burst of its own.
	- timeBeginPeriod(1) while the port is open (timeEndPeriod(1) at
	  close), so the playback thread's timed waits aren't rounded to
	  the default ~15 ms. Open is close enough to "while playing", and
	  keeps it in one place.
*/

#include "seq.h"
#include "seq_internal.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t diag_lock = PTHREAD_MUTEX_INITIALIZER;
static struct seq_dev_diag diag = { NULL, 0, "", 0, 0, 0, 0, -1 };

void seq_dev_get_diag(struct seq_dev_diag *d)
{
	pthread_mutex_lock(&diag_lock);
	*d = diag;
	pthread_mutex_unlock(&diag_lock);
}

#if defined(__linux__) || defined(_WIN32)

static int64_t mono_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* A failure: where, the system's code, and that code as text. */
static void fail(const char *step, int err, const char *text)
{
	pthread_mutex_lock(&diag_lock);
	diag.step = step;
	diag.err = err;
	snprintf(diag.errtext, sizeof diag.errtext, "%s", text ? text : "");
	pthread_mutex_unlock(&diag_lock);
}

static void diag_reset(void)
{
	pthread_mutex_lock(&diag_lock);
	diag.step = NULL;
	diag.err = 0;
	diag.errtext[0] = '\0';
	diag.writes = 0;
	diag.waits = 0;
	diag.wait_max_ns = 0;
	diag.subdevice = -1;
	pthread_mutex_unlock(&diag_lock);
}

/* A message went out; waited != 0 if it had to wait (since start). */
static void diag_sent(int waited, int64_t start)
{
	pthread_mutex_lock(&diag_lock);
	diag.writes++;
	if (waited) {
		int64_t w = mono_ns() - start;

		diag.waits++;
		if (w > diag.wait_max_ns)
			diag.wait_max_ns = w;
	}
	pthread_mutex_unlock(&diag_lock);
}

static void diag_closed(int64_t t0)
{
	pthread_mutex_lock(&diag_lock);
	diag.close_ns = mono_ns() - t0;
	pthread_mutex_unlock(&diag_lock);
}

/* One decimal number 0..max; returns the position after it, or NULL. */
static const char *number(const char *s, int max, int *v)
{
	long x = 0;
	int k = 0;

	while (*s >= '0' && *s <= '9' && k < 6) {
		x = x * 10 + (*s++ - '0');
		k++;
	}
	if (k == 0 || k == 6 || x > max)
		return NULL;
	*v = (int)x;
	return s;
}

/* ── Opening by device name (Phase 8) ────────────────────────────── */

static int lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* 1 if hay contains needle, ignoring the case of A-Z (the same
   folding the interpreter's glue and ⎕LCASE do). An empty needle
   never gets here. */
static int contains(const char *hay, const char *needle)
{
	const char *h, *n;

	for (; *hay; hay++) {
		for (h = hay, n = needle; *n && lower(*h) == lower(*n); h++, n++)
			;
		if (!*n)
			return 1;
	}
	return 0;
}

/* Labels are plain ASCII (seq.h, seq_dev_list()). */
static void ascii_only(char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s < 32 || (unsigned char)*s > 126)
			*s = '?';
}

/* seq_dev_list(), also saying whether any card was left out for lack
   of permission (Linux; never on Windows). Per platform, below. */
static int dev_list(struct seq_port_info *ports, int max, int *denied);

int seq_dev_list(struct seq_port_info *ports, int max)
{
	int denied;

	return dev_list(ports, max, &denied);
}

/* Finds the first output port whose label contains name and puts its
   port into port (size bytes): SEQ_OK. No match is SEQ_ERR_PORT, or
   SEQ_ERR_PORT_ACCESS if some card couldn't be read for lack of
   permission (the device may well be there: on Linux the permission,
   the audio group, covers every card at once, so a name given while
   not in the group says why rather than "no such port"). Called from
   dev_open(), so with the sequencer's lock held: the list can be
   static. */
static int find_port(const char *name, char *port, size_t size)
{
	static struct seq_port_info list[SEQ_MAX_PORTS];
	int i, denied, n = dev_list(list, SEQ_MAX_PORTS, &denied);

	if (n > SEQ_MAX_PORTS)
		n = SEQ_MAX_PORTS;
	for (i = 0; i < n; i++)
		if (contains(list[i].name, name)) {
			snprintf(port, size, "%s", list[i].port);
			return SEQ_OK;
		}
	return denied ? SEQ_ERR_PORT_ACCESS : SEQ_ERR_PORT;
}

#endif

#if defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

#ifdef SEQ_DEVICE_TEST
#include "seq_fake_dev.h"
#define SYS_OPEN   fake_open
#define SYS_CLOSE  fake_close
#define SYS_IOCTL  fake_ioctl
#define SYS_WRITE  fake_write
#define SYS_POLL   fake_poll
#else
#define SYS_OPEN   open
#define SYS_CLOSE  close
#define SYS_IOCTL  ioctl
#define SYS_WRITE  write
#define SYS_POLL   poll
#endif

static int fd = -1;

static void fail_errno(const char *step, int e)
{
	fail(step, e, e ? strerror(e) : "");
}

/* Starts "hw:", in either case (Phase 8: anything else is a device
   name). */
static int is_hw(const char *port)
{
	return lower(port[0]) == 'h' && lower(port[1]) == 'w' && port[2] == ':';
}

/* "hw:C", "hw:C,D", "hw:C,D,S". 1 if understood. */
static int parse_port(const char *port, int *card, int *dev, int *sub)
{
	const char *p;

	*dev = 0;
	*sub = -1;
	if (!port || !is_hw(port))
		return 0;
	if (!(p = number(port + 3, 255, card)))
		return 0;
	if (*p == ',') {
		if (!(p = number(p + 1, 255, dev)))
			return 0;
		if (*p == ',' && !(p = number(p + 1, 255, sub)))
			return 0;
	}
	return *p == '\0';
}

/* Why an open failed, as a seq error code. */
static int open_error(int e)
{
	switch (e) {
	case EBUSY:
	case EAGAIN:
		return SEQ_ERR_PORT_BUSY;
	case EACCES:
	case EPERM:
		return SEQ_ERR_PORT_ACCESS;
	default:		/* ENOENT, ENODEV, ENXIO: no such port */
		return SEQ_ERR_PORT;
	}
}

static int dev_open(const char *port)
{
	int card, dev, sub, ctl, e;
	char path[64], named[40];
	struct snd_rawmidi_info info;

	diag_reset();
	if (port && *port && !is_hw(port)) {	/* a device name (Phase 8) */
		if ((e = find_port(port, named, sizeof named)) != SEQ_OK) {
			fail("find by name", 0, e == SEQ_ERR_PORT_ACCESS
			     ? "no port with that name, and a card was"
			       " refused: no permission" : "no port with that name");
			return e;
		}
		port = named;
	}
	if (!parse_port(port, &card, &dev, &sub)) {
		fail_errno("port name", 0);
		return SEQ_ERR_PORT_NAME;
	}

	snprintf(path, sizeof path, "/dev/snd/controlC%d", card);
	ctl = SYS_OPEN(path, O_RDWR | O_CLOEXEC);
	if (ctl < 0) {
		e = errno;
		fail_errno("control open", e);
		return open_error(e);
	}
	if (SYS_IOCTL(ctl, SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE, &sub) < 0) {
		e = errno;
		SYS_CLOSE(ctl);
		fail_errno("prefer-subdevice ioctl", e);
		return SEQ_ERR_PORT;
	}

	snprintf(path, sizeof path, "/dev/snd/midiC%dD%d", card, dev);
	fd = SYS_OPEN(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		e = errno;
		SYS_CLOSE(ctl);
		fail_errno("midi open", e);
		return open_error(e);
	}

	memset(&info, 0, sizeof info);
	info.stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
	if (SYS_IOCTL(fd, SNDRV_RAWMIDI_IOCTL_INFO, &info) < 0) {
		e = errno;
		if (sub >= 0) {		/* can't confirm the one asked for */
			SYS_CLOSE(fd);
			SYS_CLOSE(ctl);
			fd = -1;
			fail_errno("info ioctl", e);
			return SEQ_ERR_PORT;
		}
		info.subdevice = (unsigned)-1;
	} else if (sub >= 0 && info.subdevice != (unsigned)sub) {
		/* alsa-lib retries here; 5.15 never gives another one */
		SYS_CLOSE(fd);
		SYS_CLOSE(ctl);
		fd = -1;
		fail_errno("wrong subdevice", 0);
		return SEQ_ERR_PORT_BUSY;
	}
	SYS_CLOSE(ctl);

	pthread_mutex_lock(&diag_lock);
	diag.subdevice = (int)info.subdevice;
	pthread_mutex_unlock(&diag_lock);
	return SEQ_OK;
}

static int dev_send(const unsigned char *m, int len, int64_t tick,
                    int64_t ns)
{
	int off = 0, waited = 0;
	int64_t start = 0, limit = 0, now;
	ssize_t n;

	(void)tick; (void)ns;
	if (fd < 0)
		return -1;
	while (off < len) {
		n = SYS_WRITE(fd, m + off, (size_t)(len - off));
		if (n > 0) {
			off += (int)n;
			if (off < len && !waited) {	/* written in pieces */
				waited = 1;
				start = mono_ns();
				limit = start + (int64_t)SEQ_DEV_WRITE_LIMIT_MS
				                * 1000000;
			}
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
			struct pollfd p;
			int ms;

			if (!waited) {
				waited = 1;
				start = mono_ns();
				limit = start + (int64_t)SEQ_DEV_WRITE_LIMIT_MS
				                * 1000000;
			}
			now = mono_ns();
			if (now >= limit) {
				fail_errno("write timeout", 0);
				return -1;
			}
			ms = (int)((limit - now + 999999) / 1000000);
			p.fd = fd;
			p.events = POLLOUT;
			p.revents = 0;
			if (SYS_POLL(&p, 1, ms) < 0 && errno != EINTR) {
				fail_errno("poll", errno);
				return -1;
			}
			/* POLLERR etc.: the next write gives the real error */
			continue;
		}
		fail_errno("write", errno);
		return -1;
	}
	diag_sent(waited, start);
	return 0;
}

static void dev_close(void)
{
	int64_t t0;

	if (fd < 0)
		return;
	t0 = mono_ns();
	SYS_CLOSE(fd);
	fd = -1;
	diag_closed(t0);
}

/* Appends "a / b" pieces, skipping empty ones and repeats. */
static void add_name(char *buf, size_t size, const unsigned char *s)
{
	const char *t = (const char *)s;
	size_t n = strlen(buf);

	if (!*t || strstr(buf, t))
		return;
	snprintf(buf + n, size - n, "%s%s", n ? " / " : "", t);
}

static int dev_list(struct seq_port_info *ports, int max, int *denied)
{
	int card, count = 0;

	*denied = 0;
	for (card = 0; card < 32; card++) {
		struct snd_ctl_card_info ci;
		char path[64];
		int ctl, dev = -1;

		snprintf(path, sizeof path, "/dev/snd/controlC%d", card);
		ctl = SYS_OPEN(path, O_RDONLY | O_CLOEXEC);
		if (ctl < 0) {
			if (errno == EACCES || errno == EPERM)
				*denied = 1;
			continue;
		}
		memset(&ci, 0, sizeof ci);
		SYS_IOCTL(ctl, SNDRV_CTL_IOCTL_CARD_INFO, &ci);
		while (SYS_IOCTL(ctl, SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE, &dev) == 0
		       && dev >= 0) {
			struct snd_rawmidi_info ri;
			unsigned s, nsub;

			memset(&ri, 0, sizeof ri);
			ri.device = (unsigned)dev;
			ri.stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
			ri.subdevice = 0;
			if (SYS_IOCTL(ctl, SNDRV_CTL_IOCTL_RAWMIDI_INFO, &ri) < 0)
				continue;	/* no output on this device */
			nsub = ri.subdevices_count;
			for (s = 0; s < nsub; s++) {
				ri.device = (unsigned)dev;
				ri.stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
				ri.subdevice = s;
				if (SYS_IOCTL(ctl, SNDRV_CTL_IOCTL_RAWMIDI_INFO, &ri) < 0)
					continue;
				if (count < max) {
					struct seq_port_info *p = &ports[count];

					snprintf(p->port, sizeof p->port, "hw:%d,%d,%u",
					         card, dev, s);
					p->name[0] = '\0';
					add_name(p->name, sizeof p->name, ci.name);
					add_name(p->name, sizeof p->name, ri.name);
					add_name(p->name, sizeof p->name, ri.subname);
					ascii_only(p->name);
				}
				count++;
			}
		}
		SYS_CLOSE(ctl);
	}
	return count;
}

#elif defined(_WIN32)

#include <windows.h>
#include <mmsystem.h>

static HMIDIOUT hout;
static int is_open;

static void fail_mm(const char *step, MMRESULT r)
{
	char text[MAXERRORLENGTH];

	text[0] = '\0';
	if (r != MMSYSERR_NOERROR)
		midiOutGetErrorTextA(r, text, sizeof text);
	fail(step, (int)r, text);
}

/* All digits (a device number). */
static int all_digits(const char *s)
{
	if (!*s)
		return 0;
	for (; *s; s++)
		if (*s < '0' || *s > '9')
			return 0;
	return 1;
}

static int dev_open(const char *port)
{
	int id;
	const char *p;
	char named[40];
	MMRESULT r;

	diag_reset();
	/* Phase 8: anything but a number is a device name, including one
	   that starts with digits (Windows names a second device of the
	   same kind "2- ...") */
	if (port && *port && !all_digits(port)) {
		int e = find_port(port, named, sizeof named);

		if (e != SEQ_OK) {
			fail("find by name", 0, "no port with that name");
			return e;
		}
		port = named;
	}
	if (!port || !(p = number(port, 99999, &id)) || *p != '\0') {
		fail("port name", 0, "");
		return SEQ_ERR_PORT_NAME;
	}
	if ((UINT)id >= midiOutGetNumDevs()) {
		fail("midi open", 0, "no output device with that number");
		return SEQ_ERR_PORT;
	}
	r = midiOutOpen(&hout, (UINT)id, 0, 0, CALLBACK_NULL);
	if (r != MMSYSERR_NOERROR) {
		fail_mm("midi open", r);
		if (r == MMSYSERR_ALLOCATED)
			return SEQ_ERR_PORT_BUSY;
		return SEQ_ERR_PORT;	/* BADDEVICEID, NODRIVER, NODEVICE */
	}
	is_open = 1;
	timeBeginPeriod(1);
	return SEQ_OK;
}

static int dev_send(const unsigned char *m, int len, int64_t tick,
                    int64_t ns)
{
	DWORD msg;
	MMRESULT r;
	int waited = 0;
	int64_t start = 0;

	(void)tick; (void)ns;
	if (!is_open || len != 3)
		return -1;
	msg = (DWORD)m[0] | ((DWORD)m[1] << 8) | ((DWORD)m[2] << 16);
	for (;;) {
		r = midiOutShortMsg(hout, msg);
		if (r == MMSYSERR_NOERROR)
			break;
		if (r != MIDIERR_NOTREADY) {
			fail_mm("write", r);
			return -1;
		}
		if (!waited) {
			waited = 1;
			start = mono_ns();
		} else if (mono_ns() - start
		           >= (int64_t)SEQ_DEV_WRITE_LIMIT_MS * 1000000) {
			fail("write timeout", 0, "");
			return -1;
		}
		Sleep(1);
	}
	diag_sent(waited, start);
	return 0;
}

static void dev_close(void)
{
	int64_t t0;

	if (!is_open)
		return;
	t0 = mono_ns();
	midiOutClose(hout);
	is_open = 0;
	timeEndPeriod(1);
	diag_closed(t0);
}

static int dev_list(struct seq_port_info *ports, int max, int *denied)
{
	UINT i, n = midiOutGetNumDevs();
	int count = 0;

	*denied = 0;

	for (i = 0; i < n; i++) {
		MIDIOUTCAPSA caps;

		if (midiOutGetDevCapsA(i, &caps, sizeof caps) != MMSYSERR_NOERROR)
			continue;
		if (count < max) {
			snprintf(ports[count].port, sizeof ports[count].port,
			         "%u", (unsigned)i);
			snprintf(ports[count].name, sizeof ports[count].name,
			         "%s", caps.szPname);
			ascii_only(ports[count].name);
		}
		count++;
	}
	return count;
}

#else /* neither: a placeholder */

static int dev_open(const char *port)
{
	(void)port;
	return SEQ_ERR_PORT;
}

static int dev_send(const unsigned char *m, int len, int64_t tick,
                    int64_t ns)
{
	(void)m; (void)len; (void)tick; (void)ns;
	return -1;
}

static void dev_close(void)
{
}

int seq_dev_list(struct seq_port_info *ports, int max)
{
	(void)ports; (void)max;
	return 0;
}

#endif

const struct seq_output seq_out_device = { dev_open, dev_send, dev_close };
