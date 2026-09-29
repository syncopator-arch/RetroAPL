# Makefile for APL Interpreter backend
#
# Targets:
#   all         -- builds stub_test, libapl.a and apl_cli
#   stub_test   -- standalone interpreter for smoke testing (no AS_DLL)
#   libapl.a    -- static library for Qt frontend (compiled with -DAS_DLL)
#   apl_cli     -- UTF-8 command-line front end, for regression testing
#   run_tests   -- the regression test runner (run_tests.c)
#   check       -- build apl_cli and run all the tests (see below)
#   apl_cli_san -- apl_cli built with AddressSanitizer and UBSan (Linux)
#   check_san   -- run the regression tests against apl_cli_san
#   clean       -- remove all generated files

CC = gcc
AR = ar

# Platform source file selection. $(OS) is set to "Windows_NT" by cmd.exe
# and PowerShell automatically (also by mingw32-make), so this doesn't
# require the caller to pass anything special -- `make` and `mingw32-make`
# both just work.
#
ifeq ($(OS),Windows_NT)
    PLATFORM_SRC = windows_platform.c
	LDFLAGS = -lm -lbcrypt
	EXE = .exe
else
    PLATFORM_SRC = linux_platform.c
	LDFLAGS = -lm
	EXE =
endif

# EXE: the suffix gcc gives executables here -- on Windows `-o apl_cli`
# writes apl_cli.exe -- used only by clean, so it removes the real files.

#				-fsigned-char \

# Flags common to both builds
#
# -fno-strict-aliasing: the backend routinely reads the same memory
# through different pointer types (workspace bytes as MemEnt/ValEnt,
# union MultiP, int32_t* over byte buffers). Without this flag gcc may
# assume that never happens and reorder or drop loads/stores at -O2.
#
# -std=gnu99: the C dialect the code is written in, stated rather than
# left to each compiler's default (older gcc defaults to gnu89 and rejects
# e.g. `for (int i = ...)`; newer defaults to gnu17). gnu99 rather than
# c99 so the POSIX functions stay declared.
#
# Remaining warning suppression:
#   pointer-sign   UCHAR*/char* mixing (~115), cosmetic, deferred
CFLAGS_COMMON = -std=gnu99 -Wall -O2 -g -fno-strict-aliasing \
                -Wno-pointer-sign

# stub_test: standalone interpreter path in main.c (no AS_DLL)
CFLAGS_STUB = $(CFLAGS_COMMON) -DNO_INPUT_PROC

# libapl.a: DLL path in main.c, no standalone apl_main()
CFLAGS_LIB  = $(CFLAGS_COMMON) -DNO_INPUT_PROC -DAS_DLL


# Source files shared by both targets
SRCS = commands.c dyop.c exec.c filfuncs.c fn.c globals.c index.c \
       interfac.c lineproc.c links.c $(PLATFORM_SRC) \
       main.c monop.c mplmem.c msgs.c parse.c rndsrc.c sort.c \
       sys.c sysfuncs.c terminal.c vprint.c platform_translate.c inprod.c

# grfuncs.c calls gw_open()/gw_close()/gw_nums()/gw_size()/gw_title(),
# which are only ever defined in the Qt frontend (graph_bridge.cpp) --
# nothing in this backend build can resolve them. libapl.a is fine
# because it's always linked into the Qt app, which supplies those
# symbols. stub_test is a standalone, non-Qt smoke-test binary and was
# never going to run graphics anyway, so grfuncs.c is deliberately left
# out of its object list rather than making it link-fail on symbols it
# has no way to provide.
LIB_ONLY_SRCS = grfuncs.c

# Object files for stub_test (compiled without AS_DLL, in stub_objs/)
STUB_OBJDIR = stub_objs
STUB_OBJS   = $(addprefix $(STUB_OBJDIR)/,$(SRCS:.c=.o))

# Object files for libapl.a (compiled with AS_DLL, in lib_objs/)
LIB_OBJDIR  = lib_objs
LIB_OBJS    = $(addprefix $(LIB_OBJDIR)/,$(SRCS:.c=.o)) \
              $(addprefix $(LIB_OBJDIR)/,$(LIB_ONLY_SRCS:.c=.o))

# Stub frontend
STUB_SRC = stub_frontend.c
STUB_OBJ = $(STUB_OBJDIR)/stub_frontend.o
STUB_TEST = stub_test

# Static library
STATIC_LIB = libapl.a

# apl_cli: command-line front end. Links the same AS_DLL objects as
# libapl.a (from lib_objs/), so tests exercise exactly the backend code
# the Qt app runs -- minus grfuncs.o, for the reason given above
# LIB_ONLY_SRCS. apl_cli.c supplies the graphics placeholders that the
# platform files' #ifndef AS_DLL block supplies for stub_test.
CLI_OBJDIR = cli_objs
CLI_SRCS   = apl_cli.c kapl_unicode.c
CLI_OBJS   = $(addprefix $(CLI_OBJDIR)/,$(CLI_SRCS:.c=.o))
CLI_LIB_OBJS = $(addprefix $(LIB_OBJDIR)/,$(SRCS:.c=.o))
APL_CLI    = apl_cli

# Translation self-test, and the Qt header whose table kapl_unicode.c
# must match. Override KAPL_HPP on the command line if the front end
# lives elsewhere, e.g.  make check KAPL_HPP=../frontend
KAPL_TEST  = kapl_unicode_test
KAPL_HPP  ?= ../frontend

# Regression test runner: a small C program, so check needs no particular
# shell -- on Windows the only sh available may be one that can't run
# run_tests.sh (UWIN's stalls on it). Same interface and output as the
# script, which is kept for now as a fallback: `sh run_tests.sh` (or
# `ksh run_tests.sh` under UWIN) still works.
RUN_TESTS  = run_tests

# apl_cli_san: apl_cli with every object -- backend and front end --
# rebuilt under AddressSanitizer (buffer overruns, use-after-free) and
# UBSan (signed overflow, bad shifts, misaligned access, float-to-int
# conversions out of range, ...), in its own directory so the normal
# builds are untouched. -O1 keeps the reports readable. gcc-on-Linux
# only: mingw's gcc has no AddressSanitizer.
#
# If one kind of report floods the output before the rest can be looked
# at, it can be switched off temporarily with -fno-sanitize=<kind>.
#
# Alignment checking is off: the workspace allocator places MemEnt/
# ValEnt entries on 2-byte boundaries, so nearly every workspace access
# is reported. Harmless on aarch64/x86-64 Linux, where unaligned loads
# work, but formally undefined; changing it touches the allocator, the
# garbage collector and possibly saved-workspace layout, so it's a
# separate decision. Remove -fno-sanitize=alignment to see them again.
SAN_FLAGS  = -fsanitize=address,undefined,float-cast-overflow \
             -fno-sanitize=alignment \
             -fno-omit-frame-pointer -O1
SAN_OBJDIR = san_objs
SAN_OBJS   = $(addprefix $(SAN_OBJDIR)/,$(SRCS:.c=.o) $(CLI_SRCS:.c=.o))
APL_CLI_SAN = apl_cli_san

# Leak checking is off for now: it reports at exit, on every test, and
# leaks are a separate job from the memory errors this is aimed at.
SAN_ENV    = ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1

# ── Default target ──────────────────────────────────────────────────────
all: $(STUB_TEST) $(STATIC_LIB) $(APL_CLI)

# ── stub_test ───────────────────────────────────────────────────────────
$(STUB_TEST): $(STUB_OBJS) $(STUB_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(STUB_OBJDIR)/%.o: %.c | $(STUB_OBJDIR)
	$(CC) $(CFLAGS_STUB) -c $< -o $@

$(STUB_OBJDIR):
	mkdir $@

# ── libapl.a ────────────────────────────────────────────────────────────
$(STATIC_LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(LIB_OBJDIR)/%.o: %.c | $(LIB_OBJDIR)
	$(CC) $(CFLAGS_LIB) -c $< -o $@

$(LIB_OBJDIR):
	mkdir $@

# ── apl_cli ─────────────────────────────────────────────────────────────
$(APL_CLI): $(CLI_LIB_OBJS) $(CLI_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(CLI_OBJDIR)/%.o: %.c | $(CLI_OBJDIR)
	$(CC) $(CFLAGS_LIB) -c $< -o $@

$(CLI_OBJDIR):
	mkdir $@

# ── Tests ───────────────────────────────────────────────────────────────
# check runs, in order: the translation self-test (which also compares
# kapl_unicode.c's table against the Qt header), then the regression
# tests in tests/.
$(KAPL_TEST): kapl_unicode_test.c kapl_unicode.c kapl_unicode.h
	$(CC) -std=gnu99 -Wall -g -o $@ kapl_unicode_test.c

$(RUN_TESTS): run_tests.c
	$(CC) -std=gnu99 -Wall -O2 -o $@ run_tests.c

check: $(KAPL_TEST) $(APL_CLI) $(RUN_TESTS)
	./$(KAPL_TEST) $(KAPL_HPP)
	./$(RUN_TESTS)

# ── apl_cli_san / check_san ─────────────────────────────────────────────
# Sanitizer reports go to stderr, which run_tests merges into each
# test's transcript -- so any report makes that test FAIL, the diff
# shows the report, and the full text stays in tests/NAME.out.
$(APL_CLI_SAN): $(SAN_OBJS)
	$(CC) $(SAN_FLAGS) -o $@ $^ $(LDFLAGS)

$(SAN_OBJDIR)/%.o: %.c | $(SAN_OBJDIR)
	$(CC) $(CFLAGS_LIB) $(SAN_FLAGS) -c $< -o $@

$(SAN_OBJDIR):
	mkdir $@

check_san: $(APL_CLI_SAN) $(RUN_TESTS)
	$(SAN_ENV) APL_CLI=./$(APL_CLI_SAN) ./$(RUN_TESTS)

# ── Clean ────────────────────────────────────────────────────────────────
# On Windows, GNU make runs recipes with sh if it finds one on PATH, and
# with cmd.exe otherwise. The object-directory rules above use plain
# `mkdir $@`, which means the same in both (they only run when the
# directory is missing, and its parent always exists -- `mkdir -p` under
# cmd.exe creates a directory literally named "-p"). `rm -rf` below still
# needs sh: if `make clean` says "'rm' is not recognized", run it from a
# POSIX shell (e.g. the Korn shell) instead.
clean:
	rm -rf $(STUB_OBJDIR) $(LIB_OBJDIR) $(STUB_TEST)$(EXE) $(STATIC_LIB) \
	       $(CLI_OBJDIR) $(APL_CLI)$(EXE) $(KAPL_TEST)$(EXE) $(RUN_TESTS)$(EXE) \
	       tests/*.out tests/.scratch \
	       $(SAN_OBJDIR) $(APL_CLI_SAN)$(EXE)

.PHONY: all clean check check_san
