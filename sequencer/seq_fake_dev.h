/*
	seq_fake_dev.h -- a fake ALSA raw MIDI device for check_device
	(Linux only, Phase 3). With -DSEQ_DEVICE_TEST, seq_out_device.c
	calls these instead of open/close/ioctl/write/poll, so its real
	code runs against a device whose behavior the test script sets.

	The fake follows the kernel behavior described in
	seq_out_device.c (by reading the 5.15 source): the subdevice
	preference counts only for the thread that set it; a busy port
	fails with EBUSY when opened O_NONBLOCK; a full buffer gives a
	short write, then EAGAIN; after an unplug, writes fail with ENODEV
	and poll() reports POLLERR. It checks our side, not the kernel's.
*/

#ifndef SEQ_FAKE_DEV_H
#define SEQ_FAKE_DEV_H

#include <stddef.h>
#include <sys/types.h>
#include <poll.h>

int     fake_open(const char *path, int flags);
int     fake_close(int fd);
int     fake_ioctl(int fd, unsigned long req, void *arg);
ssize_t fake_write(int fd, const void *buf, size_t n);
int     fake_poll(struct pollfd *p, nfds_t n, int timeout_ms);

/* Script control (seqtest's "fakedev" command). Returns 0 if the
   command was understood. Output (for "log") goes to out. */
int     fake_command(char **w, int nw, void *out /* FILE * */);

/* Back to no ports, empty log (between tests). */
void    fake_reset(void);

#endif /* SEQ_FAKE_DEV_H */
