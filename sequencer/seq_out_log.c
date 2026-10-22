// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	seq_out_log.c -- output backend for the test modes: decodes each
	message into one line of text in an in-memory log instead of
	sending it to a device. Format (seq.h, seq_virtual_log()):

	    8.000 t20160 4000.000ms ON  ch1 60 v100

	Called with the sequencer's lock held (see seq_internal.h).
*/

#include "seq.h"
#include "seq_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char  *log_buf;
static size_t log_len, log_cap;
static int    log_failing;	/* seq_virtual_fail_writes() */

static void log_append(const char *s, size_t n)
{
	if (log_len + n + 1 > log_cap) {
		size_t cap = log_cap ? log_cap * 2 : 4096;
		char *p;

		while (cap < log_len + n + 1)
			cap *= 2;
		p = realloc(log_buf, cap);
		if (!p)
			return;		/* drop the line; tests will show it */
		log_buf = p;
		log_cap = cap;
	}
	memcpy(log_buf + log_len, s, n);
	log_len += n;
	log_buf[log_len] = '\0';
}

static int log_open(const char *port)
{
	(void)port;
	return log_failing ? SEQ_ERR_PORT : SEQ_OK;
}

static int log_send(const unsigned char *m, int len, int64_t tick,
                    int64_t ns)
{
	char line[128];
	int n, kind, ch;
	long long milli, us;

	if (log_failing)
		return -1;
	/* beat to 3 decimals, rounded, in whole numbers so every
	   platform prints the same */
	milli = ((long long)tick * 1000 + SEQ_TICKS_PER_BEAT / 2)
	        / SEQ_TICKS_PER_BEAT;
	us = ((long long)ns + 500) / 1000;
	n = snprintf(line, sizeof line, "%lld.%03lld t%lld %lld.%03lldms ",
	             milli / 1000, milli % 1000, (long long)tick,
	             us / 1000, us % 1000);
	kind = (len > 0) ? (m[0] & 0xF0) : 0;
	ch = (len > 0) ? (m[0] & 0x0F) + 1 : 0;
	if (len == 3 && kind == 0x90)
		n += snprintf(line + n, sizeof line - n, "ON  ch%d %d v%d\n",
		              ch, m[1], m[2]);
	else if (len == 3 && kind == 0x80)
		n += snprintf(line + n, sizeof line - n, "OFF ch%d %d\n",
		              ch, m[1]);
	else if (len == 3 && kind == 0xB0)
		n += snprintf(line + n, sizeof line - n, "CC  ch%d %d %d\n",
		              ch, m[1], m[2]);
	else if (len == 2 && kind == 0xC0)	/* V100 */
		n += snprintf(line + n, sizeof line - n, "PC  ch%d %d\n",
		              ch, m[1]);
	else {
		int i;

		n += snprintf(line + n, sizeof line - n, "RAW");
		for (i = 0; i < len && n < (int)sizeof line - 5; i++)
			n += snprintf(line + n, sizeof line - n, " %02X", m[i]);
		n += snprintf(line + n, sizeof line - n, "\n");
	}
	log_append(line, (size_t)n);
	return 0;
}

static void log_close(void)
{
}

const struct seq_output seq_out_log = { log_open, log_send, log_close };

void seq_log_reset(void)
{
	log_len = 0;
	if (log_buf)
		log_buf[0] = '\0';
	log_failing = 0;
}

void seq_log_fail(int on)
{
	log_failing = on;
}

int seq_log_copy(char *buf, size_t bufsize, size_t *len, int clear)
{
	size_t n = (log_len < bufsize) ? log_len : bufsize;

	if (n && buf)
		memcpy(buf, log_buf, n);
	if (buf && log_len < bufsize)
		buf[log_len] = '\0';
	if (len)
		*len = log_len;
	if (clear && (!buf || log_len <= bufsize)) {
		log_len = 0;
		if (log_buf)
			log_buf[0] = '\0';
	}
	return SEQ_OK;
}
