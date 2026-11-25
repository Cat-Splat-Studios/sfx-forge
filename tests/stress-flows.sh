#!/bin/sh
# Two real threads over the whole firmware (tests/stress-flows.c says what it shows and what it cannot see): builds
# sfx-forge/sfx-forge.c for this computer with the SDK's own vibe_sdk.c and vibe_font.c - nothing of them changed -
# and the undefined-behaviour sanitizer, and runs it once for each seed.
#
#   sh tests/stress-flows.sh                        five seeds (1 2 3 4 5) of 20 seconds
#   sh tests/stress-flows.sh 120 7                  seed 7 for 120 seconds
#   sh tests/stress-flows.sh 20 "3 4" one           seeds 3 and 4, both flows in ONE thread (the seed is the whole run)
#   sh tests/stress-flows.sh 20 1 hostile           also parameters no host should send (NaN, infinity)
#
# Needs Zig 0.13.0, as build.sh: in .tools/, on your PATH, or ZIG=/path/to/zig; and the SDK in .tools/vibe-fm-sdk
# (or VIBE_SDK=/path/to/it).
# First it shows that it can see: three short runs made to fail must fail. Then the seeds. A seed that fails is
# named, with what the program said (a sanitizer's hit with its place in the source, a crash, a hang, a sample that is
# no number) or, should it have said nothing, with its exit code - and with the line that runs it again.
# The last line it prints is "flows stress PASSED" or "flows stress FAILED" (exit code 0 or 1).
#
# The sanitizers, as Zig 0.13 has them:
#   undefined, bounds, float-cast-overflow   built in, not as traps: their messages are in stress-flows.c, since Zig
#                                            brings no runtime for them
#   address                                  not there: Zig brings no runtime for it, for Windows or for Linux (the
#                                            link fails on __asan_init)
#   thread                                   for Linux only (for Windows: "unsupported option"). So, apart:
#
#   STRESS_TSAN=1 sh tests/stress-flows.sh 20 1
#       on Linux: built with the thread sanitizer INSTEAD of the others, and run. It lists every place where the two
#       flows touch the same memory without an atomic or a lock - also where the Protocol allows just that (a single
#       32-bit value with one writer; a queue with one writer and one reader, kept apart by a fence and a counter:
#       the firmware's messages, the SDK's parameters, its frame counter and its MIDI out). So its list is to be
#       read, it is not a verdict: what counts is a place that is none of those. Exit code 66 means "it listed some".
#   STRESS_TSAN=1 STRESS_TARGET=x86_64-linux-gnu sh tests/stress-flows.sh
#       on Windows: only builds that program, for Linux (out/stress/stress-flows-tsan), and says how to run it
#       under WSL.
set -e

SECONDS_EACH="${1:-20}"
SEEDS="${2:-1 2 3 4 5}"
[ $# -ge 2 ] && shift 2 || shift $#

# the repository's root, as a path Windows programs understand too (pwd -W: Git Bash)
ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
SDK="${VIBE_SDK:-$ROOT/.tools/vibe-fm-sdk}"
if [ ! -f "$SDK/sdk/src/vibe_sdk.c" ]; then
  echo "VIBE FM SDK not found at $SDK"
  exit 1
fi

# Zig: ZIG= if given, else the one in .tools/, else the one on the PATH
if [ -z "$ZIG" ]; then
  for z in "$ROOT"/.tools/zig-*/zig.exe "$ROOT"/.tools/zig-*/zig; do
    [ -x "$z" ] && ZIG="$z"
  done
fi
ZIG="${ZIG:-zig}"

# the program: out/stress/stress-flows, with .exe on Windows; its objects beside it
OUT="$ROOT/out/stress"
OBJ="$OUT/flows-obj"
mkdir -p "$OBJ"
EXE="$OUT/stress-flows"
LIBS="-lpthread"
case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*)
    WINDOWS=1
    ;;
esac

# The firmware and the SDK's runtime get the flags of the SDK's own build (its tools/build.sh: -O2 -ffast-math
# -fno-builtin-memset -Wall -Wno-unused-function) without what only the .wasm takes (-target wasm32-freestanding,
# -matomics, -mbulk-memory), and with -g. Natively the SDK's sources leave their three imports to the host
# (vibe_host_log, _request, _store: stress-flows.c has them) and their export names away, by themselves
# (#ifdef __wasm__): nothing has to be defined for them.
# (-Wno-unused-variable: vibe_sdk.c has one that only its .wasm build uses.)
FW="-O2 -ffast-math -fno-builtin-memset -Wall -Wno-unused-function -Wno-unused-variable -g -fno-omit-frame-pointer"
INC_SDK="-I$SDK/sdk/include"
INC_SRC="-I$SDK/sdk/src"

# ---- apart: the thread sanitizer ----------------------------------------------------------------------------------
if [ -n "$STRESS_TSAN" ]; then
  EXE="$OUT/stress-flows-tsan"
  # (one call for all four sources: with -c Zig puts the sanitizer's runtime into every object, and they collide)
  "$ZIG" cc ${STRESS_TARGET:+-target "$STRESS_TARGET"} $FW -fsanitize=thread "$INC_SDK" "$INC_SRC" -o "$EXE" \
    "$ROOT/tests/stress-flows.c" "$ROOT/sfx-forge/sfx-forge.c" "$SDK/sdk/src/vibe_sdk.c" "$SDK/sdk/src/vibe_font.c" \
    -lpthread
  if [ -n "$STRESS_TARGET" ]; then
    echo "built out/stress/stress-flows-tsan for $STRESS_TARGET. Under WSL, in this folder:"
    echo "  setarch x86_64 -R ./out/stress/stress-flows-tsan 20 1 2> out/stress/flows-tsan.txt"
    exit 0
  fi
  # (the sanitizer wants the program at the address it expects: setarch -R, where there is one)
  RUN=""
  if command -v setarch >/dev/null 2>&1; then
    RUN="setarch $(uname -m) -R"
  fi
  code=0
  for seed in $SEEDS; do
    $RUN "$EXE" "$SECONDS_EACH" "$seed" "$@" || code=$?
  done
  echo "the thread sanitizer's list is above (exit code $code; 66: it listed something). It is not a verdict."
  exit 0
fi

# ---- the build ----------------------------------------------------------------------------------------------------
if [ -n "$WINDOWS" ]; then
  EXE="$EXE.exe"
  # (Windows has its threads without a library; dbghelp names the place of a crash, from debug information as
  # Windows reads it)
  LIBS="-ldbghelp"
  FW="$FW -gcodeview"
fi

# What is checked while it runs. -fno-sanitize-trap: a hit calls a function that says where (stress-flows.c) instead
# of an instruction that only ends the program; -fno-sanitize-recover: and the run ends there.
# (function: clang's check of a call through a pointer, which needs a runtime of its own - left out.)
CHECKS="-fsanitize=undefined -fsanitize=bounds -fsanitize=float-cast-overflow -fno-sanitize=function"
CHECKS="$CHECKS -fno-sanitize-trap=all -fno-sanitize-recover=all"

"$ZIG" cc $FW $CHECKS "$INC_SDK" "$INC_SRC" -c -o "$OBJ/sfx-forge.o" "$ROOT/sfx-forge/sfx-forge.c"
"$ZIG" cc $FW $CHECKS "$INC_SDK" "$INC_SRC" -c -o "$OBJ/vibe_sdk.o" "$SDK/sdk/src/vibe_sdk.c"
"$ZIG" cc $FW $CHECKS "$INC_SDK" "$INC_SRC" -c -o "$OBJ/vibe_font.o" "$SDK/sdk/src/vibe_font.c"
# the test itself: no fast maths, its own warnings
"$ZIG" cc -O1 -Wall -Wno-unused-function -g ${WINDOWS:+-gcodeview} $CHECKS "$INC_SDK" -c -o "$OBJ/stress-flows.o" \
  "$ROOT/tests/stress-flows.c"

# Linked without -fsanitize: Zig would look for a runtime it does not have. On Windows the objects themselves name
# that runtime's library to the linker, so it gets an empty one of that name.
if [ -n "$WINDOWS" ]; then
  echo "void stress_flows_no_runtime(void) {}" > "$OBJ/no-runtime.c"
  "$ZIG" cc -c -o "$OBJ/no-runtime.o" "$OBJ/no-runtime.c"
  rm -f "$OBJ/liblibclang_rt.ubsan_standalone-$(uname -m).a"
  "$ZIG" ar rcs "$OBJ/liblibclang_rt.ubsan_standalone-$(uname -m).a" "$OBJ/no-runtime.o"
  LIBDIR="-L$OBJ"
fi
"$ZIG" cc -g -o "$EXE" "$OBJ/stress-flows.o" "$OBJ/sfx-forge.o" "$OBJ/vibe_sdk.o" "$OBJ/vibe_font.o" \
  ${LIBDIR:+"$LIBDIR"} $LIBS

# ---- the runs -----------------------------------------------------------------------------------------------------
# First, that it can see: three short runs made to fail in the test's own code - an index out of its array (the
# sanitizer), a write to an address that is not there (a crash), a sample that is no number - must each fail, and say
# so.
for try in "index:sanitizer: an index out of its array" "crash:crash: " "sample:is no number"; do
  code=0
  said="$("$EXE" 10 1 "try-${try%%:*}" 2>&1)" || code=$?
  case "$code:$said" in
    1:*"${try#*:}"*"flows stress FAILED") ;;
    *)
      echo "$said"
      echo "  FAIL  a run made to fail (try-${try%%:*}) was not seen to fail as it must (exit code $code)"
      echo "flows stress FAILED"
      exit 1
      ;;
  esac
done
echo "  ok    it can see: three runs made to fail (a bad index, a crash, a sample that is no number) did, and said why"

# a run for each seed; one that fails does not stop the others
FAILED=""
for seed in $SEEDS; do
  code=0
  "$EXE" "$SECONDS_EACH" "$seed" "$@" || code=$?
  if [ "$code" != 0 ]; then
    echo "  FAIL  seed $seed (exit code $code). Again: sh tests/stress-flows.sh $SECONDS_EACH $seed $*"
    FAILED="$FAILED $seed"
  fi
done

if [ -n "$FAILED" ]; then
  echo "seeds that failed:$FAILED"
  echo "flows stress FAILED"
  exit 1
fi
echo "every seed passed: $SEEDS"
echo "flows stress PASSED"
