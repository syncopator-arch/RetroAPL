# Makefile for the MIDI sequencer library (see MIDI_SEQUENCER_NOTES.md)
#
# Targets:
#   all         -- libseq.a, seqtest and seqdemo
#   libseq.a    -- the library, linked by the interpreter (Phase 4)
#   seqtest     -- standalone test driver (tests/NAME.seq)
#   seqdemo     -- plays through a real port: seqdemo PORT; seqdemo -l
#                  lists the ports (Phase 3; Windows, WinMM: Phase 6)
#   check       -- build seqtest and run every test
#   check_san   -- the tests under AddressSanitizer + UBSan (Linux)
#   check_tsan  -- the tests under ThreadSanitizer (Linux)
#   check_fallback -- the real-clock tests with timed waits forced onto
#                  the time-of-day clock, as winpthreads has it
#   check_device -- the device backend against a fake ALSA device
#                  (Linux; tests/device/NAME.seq, record with
#                  ./seqtest_device -d tests/device -r NAME)
#   clean       -- remove generated files
#
# Recording a test is done with seqtest itself, by name:
#   ./seqtest NAME        (reports NEW; read tests/NAME.out)
#   ./seqtest -r NAME     (copies it to tests/NAME.expected)

CC = gcc
AR = ar

ifeq ($(OS),Windows_NT)
	LDLIBS = -lpthread -lwinmm
	EXE = .exe
else
	LDLIBS = -lpthread
	EXE =
endif

# Same dialect and warning level as the backend.
# -ffp-contract=off: no fused multiply-add, so x86-64 and aarch64 round
# the timing arithmetic the same way and the test logs match exactly.
# -pthread: compile and link for POSIX threads (glibc; winpthreads in
# the Qt kit's MinGW).
CFLAGS = -std=gnu99 -Wall -O2 -g -ffp-contract=off -pthread

SRCS = seq.c seq_out_log.c seq_out_device.c
OBJS = $(SRCS:.c=.o)
HDRS = seq.h seq_internal.h

all: libseq.a seqtest seqdemo

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

libseq.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

seqtest: seqtest.c libseq.a $(HDRS)
	$(CC) $(CFLAGS) seqtest.c libseq.a -o $@ $(LDLIBS)

seqdemo: seqdemo.c libseq.a $(HDRS)
	$(CC) $(CFLAGS) seqdemo.c libseq.a -o $@ $(LDLIBS)

# seqdemo too, so a check always leaves a current demo beside it
check: seqtest seqdemo
	./seqtest

# ── Sanitizer builds (Linux; MinGW has neither) ─────────────────────────
# Built straight from the sources, so they never mix with the normal
# objects. ASan and TSan can't be combined, hence two builds.

SAN_FLAGS  = -fsanitize=address,undefined,float-cast-overflow \
             -fno-sanitize-recover=undefined
TSAN_FLAGS = -fsanitize=thread

seqtest_san: seqtest.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -O1 $(SAN_FLAGS) seqtest.c $(SRCS) -o $@ $(LDLIBS)

seqtest_tsan: seqtest.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -O1 $(TSAN_FLAGS) seqtest.c $(SRCS) -o $@ $(LDLIBS)

check_san: seqtest_san
	./seqtest_san

# halt_on_error: a race report fails the run instead of scrolling past
check_tsan: seqtest_tsan
	TSAN_OPTIONS="halt_on_error=1" ./seqtest_tsan

# The fallback timed wait (seq.c, timed_wait()) that Windows seems to
# need, tried on Linux.
seqtest_fallback: seqtest.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -DSEQ_FORCE_REALTIME_WAIT seqtest.c $(SRCS) -o $@ $(LDLIBS)

check_fallback: seqtest_fallback
	./seqtest_fallback -i
	./seqtest_fallback realtime stress

# The device backend's real code (seq_out_device.c) with its system
# calls routed to a fake device (seq_fake_dev.c), under ASan + UBSan.
# Linux only: the backend is ALSA. Real time, but each test is short.
seqtest_device: seqtest.c $(SRCS) seq_fake_dev.c seq_fake_dev.h $(HDRS)
	$(CC) $(CFLAGS) -O1 $(SAN_FLAGS) -DSEQ_DEVICE_TEST seqtest.c $(SRCS) \
	      seq_fake_dev.c -o $@ $(LDLIBS)

check_device: seqtest_device
	./seqtest_device -d tests/device

clean:
	rm -f $(OBJS) libseq.a seqtest$(EXE) seqdemo$(EXE) seqtest_san \
	      seqtest_tsan seqtest_fallback seqtest_device \
	      tests/*.out tests/device/*.out

.PHONY: all check check_san check_tsan check_fallback check_device clean
