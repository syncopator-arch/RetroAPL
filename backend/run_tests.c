/*
	run_tests.c -- regression tests for the APL backend, run via apl_cli.

	A compiled replacement for run_tests.sh, so `make check` needs no
	shell beyond what make itself uses: on Windows the only shell to
	hand may be one that can't run the script (UWIN's sh stalls on it).
	Same interface, same output, same files; run_tests.sh is kept for
	now as a fallback.

	Usage: run_tests [-r] [name ...]

	  Each test is a pair of files in the test directory:
	    NAME.apl       input lines, exactly as typed (UTF-8)
	    NAME.expected  the transcript apl_cli -t produced when the test was
	                   recorded: each input line after the five-space
	                   prompt, followed by its output, as in a GUI session

	  With no names, runs every *.apl in the test directory, in name
	  order. A name may be given bare (basic) or as a path (tests/basic.apl).

	  -r  record: write NAME.expected from the current output instead of
	      comparing -- for new tests, or after a deliberate change in
	      behavior. Read the result before keeping it: a recorded mistake
	      becomes the "expected" answer from then on.

	  On failure, the actual output is left in NAME.out for inspection,
	  and a line diff is printed: '-' lines are expected, '+' actual,
	  with two lines of context around each change.

	Each test runs with APLHOME set to a fresh scratch directory,
	TEST_DIR/.scratch/NAME (a relative path, so a native Windows program
	understands it too), removed afterwards -- tests never touch real
	workspaces or .fn files and can't affect each other. Any files in
	TEST_DIR/fns/ are copied into the scratch APLHOME/fns/ first, where
	)ENTER finds them, and the whole TEST_DIR/images/ tree (subfolders
	and dot files included) into APLHOME/images/, where )IMAG looks.

	The runner hands apl_cli the .apl file as its standard input and a
	file as its standard output and error, directly -- no pipes, so no
	chance of either side blocking on the other. Carriage returns are
	stripped from the output before comparing: on Windows the C runtime
	writes each newline as CR LF, and the recorded transcripts have LF.

	Environment:
	  APL_CLI       the front end to run   (default ./apl_cli, or
	                                        ./apl_cli.exe if that's what exists)
	  TEST_DIR      where the tests live   (default tests)
	  TEST_TIMEOUT  seconds before a test is killed (default 30; 0 = none).
	                Works on Windows too, which the script couldn't do.

	Exit status: 0 if every test passed (or was recorded), 1 otherwise.

	Platform-specific code is confined to make_dir(), set_env() and
	run_child(); everything else is plain C plus dirent.h/sys/stat.h,
	which MinGW provides as well.
*/

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#endif

#define PATHSZ 1024

static const char *apl_cli;
static const char *test_dir;
static long test_timeout;

/* build a path into out (PATHSZ bytes); a path that doesn't fit is a
   hard error rather than a silently truncated, different file */
static void
mkpath(char *out, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(out, PATHSZ, fmt, ap);
	va_end(ap);
	if (n < 0 || n >= PATHSZ) {
		fprintf(stderr, "run_tests: path too long: %s...\n", out);
		exit(1);
	}
}

/* ---------------------------------------------------------------- */
/* small file-system helpers                                        */
/* ---------------------------------------------------------------- */

static int
is_file(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int
is_dir(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
make_dir(const char *p)
{
#ifdef _WIN32
	return _mkdir(p);
#else
	return mkdir(p, 0755);
#endif
}

/* rm -rf: fine if p doesn't exist */
static void
remove_tree(const char *p)
{
	DIR *d;
	struct dirent *e;
	char sub[PATHSZ];

	if (!is_dir(p)) {
		remove(p);
		return;
	}
	d = opendir(p);
	if (d) {
		while ((e = readdir(d)) != NULL) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;
			mkpath(sub, "%s/%s", p, e->d_name);
			remove_tree(sub);
		}
		closedir(d);
	}
	rmdir(p);
}

static int
copy_file(const char *src, const char *dst)
{
	FILE *in, *out;
	char buf[8192];
	size_t n;
	int ok = 1;

	in = fopen(src, "rb");
	if (!in) return 0;
	out = fopen(dst, "wb");
	if (!out) { fclose(in); return 0; }
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
	fclose(in);
	if (fclose(out) != 0) ok = 0;
	return ok;
}

/* Copy the directory tree src into dst (created if missing), files and
   subdirectories alike, dot-names included -- tests/images/ has a
   hidden file and nested folders on purpose, so )IMAG's handling of
   both can be tested. "." and ".." are the only names skipped. */
static void
copy_tree(const char *src, const char *dst)
{
	DIR *d;
	struct dirent *e;
	char s[PATHSZ], t[PATHSZ];

	make_dir(dst);		/* fine if it already exists */
	d = opendir(src);
	if (!d) return;
	while ((e = readdir(d)) != NULL) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		mkpath(s, "%s/%s", src, e->d_name);
		mkpath(t, "%s/%s", dst, e->d_name);
		if (is_dir(s)) copy_tree(s, t);
		else if (is_file(s)) copy_file(s, t);
	}
	closedir(d);
}

/* whole file into a malloc'd, NUL-terminated buffer; *len gets the size */
static char *
read_file(const char *p, long *len)
{
	FILE *f;
	char *buf;
	long n = 0, cap = 8192;
	size_t got;

	f = fopen(p, "rb");
	if (!f) return NULL;
	buf = malloc(cap + 1);
	if (!buf) { fclose(f); return NULL; }
	while ((got = fread(buf + n, 1, cap - n, f)) > 0) {
		n += (long)got;
		if (n == cap) {
			char *nb = realloc(buf, cap * 2 + 1);
			if (!nb) { free(buf); fclose(f); return NULL; }
			buf = nb;
			cap *= 2;
		}
	}
	fclose(f);
	buf[n] = '\0';
	*len = n;
	return buf;
}

/* copy src to dst without carriage returns; 1 on success */
static int
strip_cr(const char *src, const char *dst)
{
	char *buf;
	long n, i;
	FILE *out;
	int ok;

	buf = read_file(src, &n);
	if (!buf) return 0;
	out = fopen(dst, "wb");
	if (!out) { free(buf); return 0; }
	for (i = 0; i < n; i++)
		if (buf[i] != '\r') putc(buf[i], out);
	ok = (fclose(out) == 0);
	free(buf);
	return ok;
}

/* ---------------------------------------------------------------- */
/* environment and child process -- the platform-specific part      */
/* ---------------------------------------------------------------- */

static void
set_env(const char *name, const char *val)
{
#ifdef _WIN32
	SetEnvironmentVariableA(name, val);	/* what CreateProcess passes on */
#else
	setenv(name, val, 1);
#endif
}

/* Run apl_cli -t with stdin from inpath and stdout+stderr to outpath,
   killing it after test_timeout seconds (0 = no limit). Returns the
   exit status, -1 if it couldn't be started; *timed_out is set to 1 if
   it was killed. */
static int
run_child(const char *inpath, const char *outpath, int *timed_out)
{
	*timed_out = 0;
	fflush(stdout);		/* nothing buffered gets duplicated or lost */
	fflush(stderr);

#ifdef _WIN32
	{
		SECURITY_ATTRIBUTES sa;
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		HANDLE hin, hout;
		char cmd[PATHSZ + 16];
		DWORD wait, code;

		sa.nLength = sizeof(sa);
		sa.lpSecurityDescriptor = NULL;
		sa.bInheritHandle = TRUE;	/* the child gets these as its std handles */

		hin = CreateFileA(inpath, GENERIC_READ, FILE_SHARE_READ, &sa,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hin == INVALID_HANDLE_VALUE) return -1;
		hout = CreateFileA(outpath, GENERIC_WRITE, FILE_SHARE_READ, &sa,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hout == INVALID_HANDLE_VALUE) { CloseHandle(hin); return -1; }

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = hin;
		si.hStdOutput = hout;
		si.hStdError = hout;
		ZeroMemory(&pi, sizeof(pi));

		/* CreateProcess wants a writable command line; backslashes in
		   the program path, since "./apl_cli.exe" isn't reliably
		   understood with forward slashes */
		snprintf(cmd, sizeof(cmd), "\"%s\" -t", apl_cli);
		{
			char *c;
			for (c = cmd; *c && *c != ' '; c++)
				if (*c == '/') *c = '\\';
		}
		if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL,
				&si, &pi)) {
			CloseHandle(hin);
			CloseHandle(hout);
			return -1;
		}
		CloseHandle(pi.hThread);
		CloseHandle(hin);	/* the child has its own copies now */
		CloseHandle(hout);

		wait = WaitForSingleObject(pi.hProcess,
			test_timeout > 0 ? (DWORD)(test_timeout * 1000) : INFINITE);
		if (wait == WAIT_TIMEOUT) {
			TerminateProcess(pi.hProcess, 124);
			WaitForSingleObject(pi.hProcess, INFINITE);
			*timed_out = 1;
		}
		code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hProcess);
		return (int)code;
	}
#else
	{
		pid_t pid, r;
		int st = 0, fd;
		long waited_ms = 0;
		struct timespec tick;

		pid = fork();
		if (pid < 0) return -1;
		if (pid == 0) {
			fd = open(inpath, O_RDONLY);
			if (fd < 0 || dup2(fd, 0) < 0) _exit(127);
			close(fd);
			fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) _exit(127);
			close(fd);
			execl(apl_cli, apl_cli, "-t", (char *)NULL);
			_exit(127);
		}

		/* poll every 50 ms, so no signal handling is needed */
		tick.tv_sec = 0;
		tick.tv_nsec = 50L * 1000000L;
		for (;;) {
			r = waitpid(pid, &st, WNOHANG);
			if (r == pid) break;
			if (r < 0 && errno != EINTR) return -1;
			if (test_timeout > 0 && waited_ms >= test_timeout * 1000L) {
				kill(pid, SIGKILL);
				waitpid(pid, &st, 0);
				*timed_out = 1;
				break;
			}
			nanosleep(&tick, NULL);
			waited_ms += 50;
		}
		if (WIFEXITED(st)) return WEXITSTATUS(st);
		return 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
	}
#endif
}

/* ---------------------------------------------------------------- */
/* line diff for FAIL output                                        */
/* ---------------------------------------------------------------- */

/* split buf (modified in place) into lines; returns the count and a
   malloc'd array of pointers in *lines. A trailing newline doesn't make
   an extra empty line. */
static long
split_lines(char *buf, long len, char ***lines)
{
	long i, n = 0, cap = 64;
	char **v = malloc(cap * sizeof(char *));

	if (!v) { *lines = NULL; return 0; }
	i = 0;
	while (i < len) {
		if (n == cap) {
			char **nv = realloc(v, cap * 2 * sizeof(char *));
			if (!nv) break;
			v = nv;
			cap *= 2;
		}
		v[n++] = buf + i;
		while (i < len && buf[i] != '\n') i++;
		if (i < len) buf[i++] = '\0';
	}
	*lines = v;
	return n;
}

#define CONTEXT 2
#define DIFF_MAX_CELLS 25000000L	/* LCS table limit, ~100 MB of ints */

/* Print expected-vs-actual as '-'/'+' lines with CONTEXT lines of
   context, each indented nine spaces like the other result lines;
   separate hunks are divided by a line of "...". Longest-common-
   subsequence table: tests are a few hundred lines at most, so the
   simple O(n*m) version is plenty. */
static void
print_diff(const char *exp_path, const char *act_path)
{
	char *eb, *ab, **el, **al;
	long elen, alen, n, m, i, j, k, nops;
	int *t;
	char *op;		/* ' ', '-', '+' per edit step */
	long *ei, *ai;		/* line index for each step */
	char *show;
	int gap;

	eb = read_file(exp_path, &elen);
	ab = read_file(act_path, &alen);
	if (!eb || !ab) {
		printf("         (couldn't read files to diff)\n");
		free(eb); free(ab);
		return;
	}
	n = split_lines(eb, elen, &el);
	m = split_lines(ab, alen, &al);

	if ((n + 1) * (m + 1) > DIFF_MAX_CELLS) {
		printf("         (too long to diff here: %ld vs %ld lines)\n", n, m);
		goto done;
	}

	/* t[i][j] = LCS length of el[i..] and al[j..] */
	t = calloc((size_t)((n + 1) * (m + 1)), sizeof(int));
	op = malloc((size_t)(n + m + 1));
	ei = malloc((size_t)(n + m + 1) * sizeof(long));
	ai = malloc((size_t)(n + m + 1) * sizeof(long));
	show = calloc((size_t)(n + m + 1), 1);
	if (!t || !op || !ei || !ai || !show) {
		printf("         (out of memory for diff)\n");
		free(t); free(op); free(ei); free(ai); free(show);
		goto done;
	}
#define T(a,b) t[(a) * (m + 1) + (b)]
	for (i = n - 1; i >= 0; i--)
		for (j = m - 1; j >= 0; j--)
			T(i, j) = strcmp(el[i], al[j]) == 0 ? T(i + 1, j + 1) + 1
				: (T(i + 1, j) >= T(i, j + 1) ? T(i + 1, j) : T(i, j + 1));

	/* walk it into an edit script */
	nops = 0;
	i = j = 0;
	while (i < n || j < m) {
		if (i < n && j < m && strcmp(el[i], al[j]) == 0) {
			op[nops] = ' '; ei[nops] = i++; ai[nops] = j++;
		} else if (i < n && (j == m || T(i + 1, j) >= T(i, j + 1))) {
			op[nops] = '-'; ei[nops] = i++; ai[nops] = j;	/* '-' first, like diff -u */
		} else {
			op[nops] = '+'; ei[nops] = i; ai[nops] = j++;
		}
		nops++;
	}
#undef T

	/* mark changes and their context */
	for (k = 0; k < nops; k++)
		if (op[k] != ' ') {
			long a = k - CONTEXT, b = k + CONTEXT;
			if (a < 0) a = 0;
			if (b >= nops) b = nops - 1;
			for (; a <= b; a++) show[a] = 1;
		}

	gap = 0;
	for (k = 0; k < nops; k++) {
		if (!show[k]) { gap = 1; continue; }
		if (gap && k > 0) printf("         ...\n");
		gap = 0;
		if (op[k] == '+') printf("         +%s\n", al[ai[k]]);
		else if (op[k] == '-') printf("         -%s\n", el[ei[k]]);
		else printf("          %s\n", el[ei[k]]);
	}

	free(t); free(op); free(ei); free(ai); free(show);
done:
	free(el); free(al); free(eb); free(ab);
}

/* 1 if the two files have identical contents */
static int
same_file(const char *a, const char *b)
{
	char *x, *y;
	long xl, yl;
	int same;

	x = read_file(a, &xl);
	y = read_file(b, &yl);
	same = x && y && xl == yl && memcmp(x, y, (size_t)xl) == 0;
	free(x);
	free(y);
	return same;
}

/* ---------------------------------------------------------------- */
/* test list                                                        */
/* ---------------------------------------------------------------- */

/* "tests/basic.apl" or "basic" -> "basic", into out */
static void
test_name(const char *arg, char *out, size_t outsz)
{
	const char *p = arg, *s;
	size_t len;

	for (s = arg; *s; s++)
		if (*s == '/' || *s == '\\') p = s + 1;
	len = strlen(p);
	if (len > 4 && !strcmp(p + len - 4, ".apl")) len -= 4;
	if (len >= outsz) len = outsz - 1;
	memcpy(out, p, len);
	out[len] = '\0';
}

static int
cmp_str(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* every NAME.apl in test_dir, sorted, as malloc'd names; count in *n */
static char **
list_tests(long *n)
{
	DIR *d;
	struct dirent *e;
	char **v = NULL, **nv;
	long cnt = 0, cap = 0;
	size_t len;

	*n = 0;
	d = opendir(test_dir);
	if (!d) return NULL;
	while ((e = readdir(d)) != NULL) {
		len = strlen(e->d_name);
		if (len <= 4 || strcmp(e->d_name + len - 4, ".apl")) continue;
		if (cnt == cap) {
			cap = cap ? cap * 2 : 32;
			nv = realloc(v, cap * sizeof(char *));
			if (!nv) break;
			v = nv;
		}
		v[cnt] = malloc(len - 3);
		if (!v[cnt]) break;
		memcpy(v[cnt], e->d_name, len - 4);
		v[cnt][len - 4] = '\0';
		cnt++;
	}
	closedir(d);
	if (cnt) qsort(v, (size_t)cnt, sizeof(char *), cmp_str);
	*n = cnt;
	return v;
}

/* ---------------------------------------------------------------- */

int
main(int argc, char **argv)
{
	const char *s;
	int record = 0, argi = 1, timed_out, status;
	long ntests, ti;
	char **names;
	int listed = 0;		/* names came from list_tests(), so free each */
	int pass = 0, fail = 0, recorded = 0;
	char name[256], input[PATHSZ], expected[PATHSZ], actual[PATHSZ],
	     raw[PATHSZ], scratch[PATHSZ], sdir[PATHSZ], fns[PATHSZ],
	     src[PATHSZ], dst[PATHSZ];

	setvbuf(stdout, NULL, _IONBF, 0);	/* results appear as each test ends */

	s = getenv("APL_CLI");
	if (s && *s) apl_cli = s;
	else {
		apl_cli = "./apl_cli";
		if (!is_file(apl_cli) && is_file("./apl_cli.exe"))
			apl_cli = "./apl_cli.exe";
	}
	s = getenv("TEST_DIR");
	test_dir = (s && *s) ? s : "tests";
	s = getenv("TEST_TIMEOUT");
	test_timeout = (s && *s) ? atol(s) : 30L;

	if (argi < argc && !strcmp(argv[argi], "-r")) {
		record = 1;
		argi++;
	}

	if (!is_file(apl_cli)) {
		fprintf(stderr, "run_tests: %s not found (run make apl_cli?)\n", apl_cli);
		return 1;
	}

	if (argi < argc) {
		ntests = argc - argi;
		names = malloc((size_t)ntests * sizeof(char *));
		if (!names) return 1;
		for (ti = 0; ti < ntests; ti++) names[ti] = argv[argi + ti];
	} else {
		names = list_tests(&ntests);
		listed = 1;
		if (!ntests) {
			fprintf(stderr, "run_tests: no tests found in %s\n", test_dir);
			return 1;
		}
	}

	mkpath(sdir, "%s/.scratch", test_dir);

	for (ti = 0; ti < ntests; ti++) {
		test_name(names[ti], name, sizeof(name));
		mkpath(input, "%s/%s.apl", test_dir, name);
		mkpath(expected, "%s/%s.expected", test_dir, name);
		mkpath(actual, "%s/%s.out", test_dir, name);
		mkpath(raw, "%s/%s.out.raw", test_dir, name);

		if (!is_file(input)) {
			printf("MISSING  %s  (%s not found)\n", name, input);
			fail++;
			continue;
		}

		/* fresh scratch APLHOME, with the .fn fixtures copied in */
		mkpath(scratch, "%s/%s", sdir, name);
		remove_tree(scratch);
		make_dir(sdir);		/* fine if it already exists */
		if (make_dir(scratch) != 0) {
			fprintf(stderr, "run_tests: can't create %s\n", scratch);
			return 1;
		}
		mkpath(fns, "%s/fns", test_dir);
		if (is_dir(fns)) {
			DIR *d;
			struct dirent *e;

			mkpath(dst, "%s/fns", scratch);
			make_dir(dst);
			d = opendir(fns);
			if (d) {
				while ((e = readdir(d)) != NULL) {
					mkpath(src, "%s/%s", fns, e->d_name);
					if (!is_file(src)) continue;
					mkpath(dst, "%s/fns/%s", scratch, e->d_name);
					copy_file(src, dst);
				}
				closedir(d);
			}
		}
		/* and the image fixtures, as a whole tree: )IMAG lists
		   subfolders, so unlike fns/ this one isn't flat */
		mkpath(src, "%s/images", test_dir);
		if (is_dir(src)) {
			mkpath(dst, "%s/images", scratch);
			copy_tree(src, dst);
		}
		set_env("APLHOME", scratch);

		status = run_child(input, raw, &timed_out);
		if (status < 0) {
			fprintf(stderr, "run_tests: couldn't run %s\n", apl_cli);
			remove(raw);
			remove_tree(scratch);
			return 1;
		}
		remove(actual);
		strip_cr(raw, actual);
		remove(raw);
		remove_tree(scratch);

		if (timed_out) {
			printf("TIMEOUT  %s  (killed after %lds; partial output in %s)\n",
				name, test_timeout, actual);
			fail++;
			continue;
		}

		if (record) {
			remove(expected);	/* Windows rename() won't replace */
			if (rename(actual, expected) != 0) {
				printf("FAIL     %s  (couldn't write %s)\n", name, expected);
				fail++;
				continue;
			}
			printf("RECORDED %s\n", name);
			recorded++;
		} else if (!is_file(expected)) {
			printf("NEW      %s  (no %s.expected yet -- review %s, then record with -r)\n",
				name, name, actual);
			fail++;
		} else if (same_file(expected, actual)) {
			remove(actual);
			printf("PASS     %s\n", name);
			pass++;
		} else {
			printf("FAIL     %s\n", name);
			print_diff(expected, actual);
			fail++;
		}
	}

	if (listed)
		for (ti = 0; ti < ntests; ti++) free(names[ti]);
	free(names);
	rmdir(sdir);		/* only succeeds if empty, as intended */
	printf("run_tests: %d passed, %d failed, %d recorded\n", pass, fail, recorded);
	return fail ? 1 : 0;
}
