#!/bin/sh
#
# run_tests.sh -- regression tests for the APL backend, run via apl_cli.
#
# Usage: run_tests.sh [-r] [name ...]
#
#   Each test is a pair of files in the test directory:
#     NAME.apl       input lines, exactly as typed (UTF-8)
#     NAME.expected  the transcript apl_cli -t produced when the test was
#                    recorded: each input line after the five-space
#                    prompt, followed by its output, as in a GUI session
#
#   With no names, runs every *.apl in the test directory.
#
#   -r  record: write NAME.expected from the current output instead of
#       comparing -- for new tests, or after a deliberate change in
#       behavior. Read the result before keeping it: a recorded mistake
#       becomes the "expected" answer from then on.
#
#   On failure, the actual output is left in NAME.out for inspection.
#
# Each test runs with APLHOME set to a fresh scratch directory, so tests
# never touch real workspaces or .fn files, and can't affect each other.
# The scratch directories live in TEST_DIR/.scratch/ and are passed as
# relative paths, so this works the same under Windows shells (a native
# program may not understand a Unix-style /tmp path), and nothing beyond
# mkdir/rm is needed to make them. They're removed after each test.
#
# Carriage returns are stripped from the captured output before it's
# compared: on Windows the C runtime writes each newline as CR LF, and
# the recorded transcripts have plain LF.
#
# Function fixtures: any files in TEST_DIR/fns/ (.fn files made with the
# GUI editor) are copied into the scratch APLHOME/fns/ before each test,
# where )ENTER finds them. Each test gets fresh copies.
#
# Environment:
#   APL_CLI       the front end to run          (default ./apl_cli)
#   TEST_DIR      where the tests live          (default tests)
#   TEST_TIMEOUT  seconds before a test is
#                 killed, if `timeout` exists   (default 30)
#
# Exit status: 0 if every test passed (or was recorded), 1 otherwise.

if [ -z "$APL_CLI" ]; then
	APL_CLI=./apl_cli
	[ ! -f "$APL_CLI" ] && [ -f ./apl_cli.exe ] && APL_CLI=./apl_cli.exe
fi
TEST_DIR=${TEST_DIR:-tests}
TEST_TIMEOUT=${TEST_TIMEOUT:-30}

record=0
if [ "$1" = "-r" ]; then
	record=1
	shift
fi

if [ ! -x "$APL_CLI" ]; then
	echo "run_tests: $APL_CLI not found or not executable (run make apl_cli?)" >&2
	exit 1
fi

# Only GNU timeout will do: Windows has an unrelated timeout.exe (a
# "wait N seconds or for a key" command) that may be first on the PATH.
# GNU's identifies itself with --version; Windows' rejects the option.
# Without it, tests simply run with no time limit.
if timeout --version 2>&1 | grep -qi "coreutils"; then
	TIMEOUT="timeout $TEST_TIMEOUT"
else
	TIMEOUT=""
fi

if [ $# -eq 0 ]; then
	set -- "$TEST_DIR"/*.apl
	if [ ! -f "$1" ]; then
		echo "run_tests: no tests found in $TEST_DIR" >&2
		exit 1
	fi
fi

pass=0
fail=0
recorded=0

for t in "$@"; do
	name=$(basename "$t" .apl)
	input="$TEST_DIR/$name.apl"
	expected="$TEST_DIR/$name.expected"
	actual="$TEST_DIR/$name.out"

	if [ ! -f "$input" ]; then
		echo "MISSING  $name  ($input not found)"
		fail=$((fail + 1))
		continue
	fi

	scratch="$TEST_DIR/.scratch/$name"
	rm -rf "$scratch"
	mkdir -p "$scratch" || exit 1
	if [ -d "$TEST_DIR/fns" ]; then
		mkdir "$scratch/fns" && cp "$TEST_DIR"/fns/* "$scratch/fns/" 2> /dev/null
	fi
	APLHOME="$scratch" $TIMEOUT "$APL_CLI" -t < "$input" > "$actual.raw" 2>&1
	status=$?
	tr -d '\r' < "$actual.raw" > "$actual"
	rm -f "$actual.raw"
	rm -rf "$scratch"

	if [ $status -eq 124 ] && [ -n "$TIMEOUT" ]; then
		echo "TIMEOUT  $name  (killed after ${TEST_TIMEOUT}s; partial output in $actual)"
		fail=$((fail + 1))
		continue
	fi

	if [ $record -eq 1 ]; then
		mv "$actual" "$expected"
		echo "RECORDED $name"
		recorded=$((recorded + 1))
	elif [ ! -f "$expected" ]; then
		echo "NEW      $name  (no $name.expected yet -- review $actual, then record with -r)"
		fail=$((fail + 1))
	elif diff "$expected" "$actual" > /dev/null; then
		rm -f "$actual"
		echo "PASS     $name"
		pass=$((pass + 1))
	else
		echo "FAIL     $name"
		diff -u "$expected" "$actual" | sed 's/^/         /'
		fail=$((fail + 1))
	fi
done

rmdir "$TEST_DIR/.scratch" 2> /dev/null
echo "run_tests: $pass passed, $fail failed, $recorded recorded"
[ $fail -eq 0 ]
