/* SPDX-License-Identifier: GPL-3.0-or-later
 * stress-flows.c – two real threads over the WHOLE firmware, built natively with the SDK's own runtime.
 *
 * The other tests run the built .wasm in one flow, a few events at a time. This one is sfx-forge/sfx-forge.c itself,
 * compiled for this computer together with the SDK's vibe_sdk.c and vibe_font.c (nothing of either is changed), and
 * driven as a host with two flows drives it - hard, for as long as it is told:
 *   the audio flow     a thread that calls vibe_wasm_audio (the SDK's export: it calls vibe_on_audio) block after
 *                      block, 1..256 frames, as fast as it can. Before a block it fills the host's transport table as
 *                      a DAW does, now and then; after it, it looks at every sample - a number, within -1..1, and
 *                      written at all - and empties the firmware's MIDI out queue as a host does, looking at every
 *                      message
 *   the control flow   a thread that calls what a host calls there, from a seeded row of random numbers: pads, keys,
 *                      notes, every button (with SHIFT, held, let go twice), encoders, the host's parameters with the
 *                      mixer's 65, MIDI, the mouse on the stage, clicks into the display, SAVE, the transport - and
 *                      SysEx: patches and whole songs packed by the firmware's own core (so they are valid), in
 *                      order, shuffled, twice, with a piece missing or a byte wrong, the firmware's own messages
 *                      echoed back, and garbage of every length. Between them it draws: the display and the stage.
 *   a watch            the main thread: a flow that does not move for ten seconds is a hang
 * It is built with the undefined-behaviour sanitizer (an index out of its array, a shift too far, a division by
 * zero, a number that does not fit its integer, ...): a hit says where and ends the run. So does a crash.
 *
 * WHAT IT CANNOT SEE
 *   - A write through a pointer past its buffer, or a read of memory that was never set: that is the address and the
 *     memory sanitizer's work, and Zig 0.13 brings neither. The bounds check sees an index into an array whose size
 *     the compiler knows at that place, no more.
 *   - Two flows writing the same value. That is the thread sanitizer's work (Zig has it for Linux only:
 *     stress-flows.sh says how). Here a race is seen only when it does harm that shows: a crash, a bad index, a
 *     sample that is no number, a flow that stands still.
 *   - Whether it sounds RIGHT. A wrong note is a number within -1..1 like any other.
 *   - The .wasm. This is the same source built for another machine: 64-bit pointers, another compiler back end, a
 *     real libc. What depends on those (the size of a table as the host reads it, the .wasm's stack of 256 KB) is
 *     not tested here.
 *   - A value that is no number as the .wasm treats it. With the build's fast maths this machine's code makes the
 *     SDK's clamp of a parameter turn a NaN into an end of its range; the .wasm's lets it through. So `hostile`
 *     shows less here than a NaN does there.
 *   - The plugin's own timing. Both threads run flat out: the audio makes about a thousand seconds of sound a
 *     second, so "three seconds later" is a moment here.
 * The seed makes the control flow's row of events the same again, not how the two threads fall together: a failure
 * that needs its moment may need several runs. The word `one` runs the same rows in ONE thread, block and event in
 * turn - there the seed is the whole run, and a failure comes again at the same event.
 *
 *   sh tests/stress-flows.sh                    (builds, then five seeds of 20 seconds)
 *   out/stress/stress-flows [seconds, 20] [seed, 1] [one] [hostile] [wild] [try-...]
 *
 * Two words add what no host should send, a value that is no number (NaN, an infinity): `hostile` as a parameter's
 * value - the one place where the firmware itself says a host can (fw_pages.h, mix_from_host) - and `wild` as the
 * value of an event (a velocity, an encoder's turn), which nothing promises to survive.
 * And four make a run fail on purpose, in this file's own code, to show that such a failure is seen: `try-index`,
 * `try-crash`, `try-sample`, `try-hang` (stress-flows.sh runs the first three before the seeds).
 *
 * WHAT THIS FILE HOLDS, in its order:
 *   threads, a clock and a nap for Windows and POSIX
 *   what the host has from the SDK                   the exports of vibe_sdk.c, the tables behind three of them
 *   the run                                          its seed and mode, the counters, the last things the control
 *                                                    flow did, fail (the one way a run ends badly)
 *   what the SDK asks its host for                   vibe_host_log, vibe_host_request, vibe_host_store (a store in
 *                                                    memory)
 *   the sanitizer's messages                         __ubsan_handle_...: Zig brings no runtime for them
 *   a crash                                          the exception or signal, said before the process ends
 *   making valid things                              a random patch, a random song, their messages
 *   the control flow                                 one function for each kind of thing, control_op chooses
 *   the audio flow                                   audio_block
 *   main                                             the store before the start, the threads, the watch, the verdict
 * To read it: main first, then control_op and audio_block.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vibe.h"
#include "../core/sfx_song.h"

/* ==== threads, a clock and a nap ================================================================================== */
#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
#define THREAD_FN(name) static DWORD WINAPI name(LPVOID arg)
#define THREAD_END      return 0

static thread_t thread_go(LPTHREAD_START_ROUTINE f) {
  return CreateThread(0, 0, f, 0, 0, 0);
}

static double seconds(void) {
  return (double)GetTickCount64() / 1000.0;
}

/* ms: 0 = let another thread run */
static void nap(int ms) {
  Sleep((DWORD)ms);
}

static void end_now(int code) {
  fflush(stdout);
  TerminateProcess(GetCurrentProcess(), (UINT)code);
  for (;;) {
  }
}
#else
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
typedef pthread_t thread_t;
#define THREAD_FN(name) static void *name(void *arg)
#define THREAD_END      return 0

static thread_t thread_go(void *(*f)(void *)) {
  pthread_t t;
  pthread_create(&t, 0, f, 0);
  return t;
}

static double seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void nap(int ms) {
  if (ms <= 0) {
    sched_yield();
  } else {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, 0);
  }
}

static void end_now(int code) {
  fflush(stdout);
  _exit(code);
}
#endif

/* ==== what the host has from the SDK ============================================================================== */
/* The exports of vibe_sdk.c, as the plugin calls them (their names in the .wasm are these). */
void vibe_wasm_init(float sr);
void vibe_wasm_event(int type, int id, int down, float value, int midi);
void vibe_wasm_audio(int frames, int has_input);
float *vibe_wasm_io(void);
uint16_t *vibe_wasm_draw(void);
uint16_t *vibe_wasm_stage_draw(void);
int vibe_wasm_flows(void);
int vibe_wasm_param_count(void);
void vibe_wasm_param_set(int i, float v);
float vibe_wasm_param_value(int i);
uint32_t vibe_wasm_param_changed(int word);
uint8_t *vibe_wasm_sysex_buf(void);
void vibe_wasm_sysex_in(int n);
void *vibe_wasm_midi_out(void);
void *vibe_wasm_transport(void);
uint8_t *vibe_wasm_editor_inbox(void);
void vibe_wasm_editor_data_in(int n);

/* the MIDI out queue behind vibe_wasm_midi_out (vibe_sdk.c, "SDK 9: MIDI out"): the firmware writes, the host reads */
typedef struct {
  uint16_t frame;
  uint8_t st;
  uint8_t d1;
  uint8_t d2;
  uint8_t len;
} midi_msg;

typedef struct {
  volatile uint32_t w;
  volatile uint32_t r;
  volatile uint32_t dropped;
  volatile uint32_t sysex_len;
  midi_msg q[256];
  uint8_t sysex[256];
} midi_queue;

/* ==== the run ===================================================================================================== */

static uint32_t seed = 1;
static double run_seconds = 20.0;
static float rate = 48000.0f;
static int one_flow; /* 1: both rows in one thread, in turn */
static int hostile;  /* 1: also parameters that are no number */
static int wild;     /* 1: also events whose value is no number */
/* a run made to fail, to show that this test sees it (try-index, try-crash, try-sample, try-hang): after TRY_AT blocks
   the audio flow itself writes past an array of this file, writes to an address that is not there, puts a sample that
   is no number into the block, or stands still */
enum {
  TRY_NONE,
  TRY_INDEX,
  TRY_CRASH,
  TRY_SAMPLE,
  TRY_HANG
};
#define TRY_AT 2000
static int try_what;

/* which flow this thread is */
enum {
  FLOW_MAIN,
  FLOW_AUDIO,
  FLOW_CONTROL
};
static const char *const FLOW_NAME[3] = {"the main thread", "the audio flow", "the control flow"};
static _Thread_local int flow;

static volatile uint32_t stop_now;
static volatile uint32_t audio_done;
static volatile uint32_t control_done;
static uint32_t failed;

/* the counts of a run. The audio flow's: */
static volatile uint64_t blocks;
static volatile uint64_t frames_run;
static volatile uint64_t loud_blocks; /* blocks with a sample that is not 0 */
static float peak;
static uint64_t midi_short;
static uint64_t midi_sysex;
/* the control flow's: */
static volatile uint64_t ops;
static uint64_t events;
static uint64_t draws;
static uint64_t stage_draws;
static uint64_t params_set;
static uint64_t sysex_valid;
static uint64_t sysex_garbage;
static uint64_t sysex_echoed;
static uint64_t songs_whole;
static uint64_t songs_broken;
static uint64_t saves;
/* the host's side: */
static uint64_t store_calls;
static uint64_t store_refused;
static uint64_t logs;

/* the last things the control flow did, for the report of a failure: a ring of LAST */
#define LAST 24
static struct {
  const char *what;
  int a;
  int b;
  int c;
  float v;
  uint64_t at;
} last[LAST];
static volatile uint32_t last_n;

static void note(const char *what, int a, int b, int c, float v) {
  uint32_t i = last_n % LAST;
  last[i].what = what;
  last[i].a = a;
  last[i].b = b;
  last[i].c = c;
  last[i].v = v;
  last[i].at = ops;
  last_n = last_n + 1;
}

/* The one way a run ends badly: it says what, the seed, where both flows were, and ends the process with 1.
   (A second thread that fails at the same moment waits here for the first to end it.) */
static void fail(const char *fmt, ...) {
  if (__atomic_exchange_n(&failed, 1, __ATOMIC_SEQ_CST)) {
    for (;;) {
      nap(1000);
    }
  }
  va_list ap;
  va_start(ap, fmt);
  printf("  FAIL  ");
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n        in %s; seed %u, %s, %.0f Hz%s%s; after %llu blocks (%llu frames) and %llu control steps\n",
         FLOW_NAME[flow], seed, one_flow ? "one flow" : "two flows", (double)rate, hostile ? ", hostile" : "",
         wild ? ", wild" : "", (unsigned long long)blocks, (unsigned long long)frames_run, (unsigned long long)ops);
  printf("        the control flow's last steps (step: what a b c value), the latest last:\n");
  uint32_t n = last_n;
  for (uint32_t k = n > LAST ? n - LAST : 0; k < n; k++) {
    uint32_t i = k % LAST;
    printf("          %llu: %s %d %d %d %g\n", (unsigned long long)last[i].at, last[i].what ? last[i].what : "?",
           last[i].a, last[i].b, last[i].c, (double)last[i].v);
  }
  printf("flows stress FAILED\n");
  end_now(1);
}

/* the next random number of the row *s (xorshift32) */
static uint32_t rnd(uint32_t *s) {
  *s ^= *s << 13;
  *s ^= *s >> 17;
  *s ^= *s << 5;
  return *s;
}

/* 0..n-1 */
static int pick(uint32_t *s, int n) {
  return (int)(rnd(s) % (uint32_t)n);
}

/* 0..1 */
static float unit(uint32_t *s) {
  return (float)(rnd(s) >> 8) * (1.0f / 16777215.0f);
}

/* x is a number (looked at by its bits: nothing a compiler's fast maths could drop) */
static int is_number(float x) {
  union {
    float f;
    uint32_t u;
  } b = {x};
  return (b.u & 0x7F800000u) != 0x7F800000u;
}

/* a float from its bits */
static float from_bits(uint32_t u) {
  union {
    uint32_t u;
    float f;
  } b = {u};
  return b.f;
}

/* ==== what the SDK asks its host for ============================================================================== */
/* The store: a few named files in memory. Only the control flow may come here (vibe.h: no log, store or request
   from vibe_on_audio) - the audio flow doing so is a failure. */
#define FILES     8
#define FILE_MAX  (SFX_SONG_MAX + 64)
#define FILE_NAME 128
static struct {
  char name[FILE_NAME];
  int used;
  int len;
  uint8_t data[FILE_MAX];
} files[FILES];
/* 1: the store takes nothing more, as a full device */
static int store_full;

static void host_only_control(const char *what) {
  if (flow == FLOW_AUDIO) {
    fail("the audio flow called the host's %s (vibe.h, \"two flows\": never from vibe_on_audio)", what);
  }
}

/* the file of that name: its place, or -1 */
static int file_find(const char *name, int name_len) {
  for (int i = 0; i < FILES; i++) {
    if (files[i].used && (int)strlen(files[i].name) == name_len && !memcmp(files[i].name, name, (size_t)name_len)) {
      return i;
    }
  }
  return -1;
}

/* a file is put into the store (also by main, before the start) */
static int file_put(const char *name, int name_len, const void *data, int len) {
  int i = file_find(name, name_len);
  if (len < 0 || len > FILE_MAX || name_len < 1 || name_len >= FILE_NAME) {
    return -1;
  }
  for (int k = 0; i < 0 && k < FILES; k++) {
    if (!files[k].used) {
      i = k;
    }
  }
  if (i < 0) {
    return -1;
  }
  memcpy(files[i].name, name, (size_t)name_len);
  files[i].name[name_len] = 0;
  files[i].used = 1;
  files[i].len = len;
  if (len) {
    memcpy(files[i].data, data, (size_t)len);
  }
  return 0;
}

void vibe_host_log(const char *s, int len) {
  (void)s;
  (void)len;
  host_only_control("log");
  logs++;
}

void vibe_host_request(int what) {
  (void)what;
  host_only_control("request");
}

/* op 0 read, 1 write, 2 size, 3 delete (vibe_sdk.c); every other op: this host has no such thing */
int vibe_host_store(int op, const char *name, int name_len, void *data, int len) {
  host_only_control("store");
  store_calls++;
  if (op < 0 || op > 3) {
    return -1;
  }
  int i = file_find(name, name_len);
  if (op == 0) {
    if (i < 0) {
      return -1;
    }
    int n = len < files[i].len ? len : files[i].len;
    if (n > 0) {
      memcpy(data, files[i].data, (size_t)n);
    }
    return n;
  }
  if (op == 1) {
    if (store_full) {
      store_refused++;
      return -1;
    }
    return file_put(name, name_len, data, len);
  }
  if (op == 2) {
    return i < 0 ? -1 : files[i].len;
  }
  if (i < 0) {
    return -1;
  }
  files[i].used = 0;
  return 0;
}

/* ==== the sanitizer's messages ==================================================================================== */
/* The objects are built with -fsanitize=undefined and friends, not as traps: at a hit the code calls one of these
   with the place in the source. Zig 0.13 brings no runtime that has them, so they are here: each says what and
   where, and the run fails. (All of them get the place first in what `data` points at; `value` is the index for
   out_of_bounds and something else or nothing for the others.) */
typedef struct {
  const char *file;
  uint32_t line;
  uint32_t column;
} source_place;

static void sanitizer(const char *what, const void *data, uintptr_t value) {
  const source_place *p = data;
  const char *file = p && p->file ? p->file : "?";
  // clang-format off
  const char *whose = strstr(file, "stress-flows") ? "this test's own source"
                    : strstr(file, "vibe_sdk") || strstr(file, "vibe_font") || strstr(file, "vibe-fm-sdk")
                        ? "the SDK's own source, not the firmware's"
                    : "the firmware's source";
  // clang-format on
  fail("sanitizer: %s at %s:%u:%u (%s); value %lld", what, file, p ? p->line : 0, p ? p->column : 0, whose,
       (long long)(intptr_t)value);
}

#define SANITIZER(name, what)                                                                                          \
  void __ubsan_handle_##name(void *data, uintptr_t value);                                                             \
  void __ubsan_handle_##name(void *data, uintptr_t value) { sanitizer(what, data, value); }
SANITIZER(out_of_bounds_abort, "an index out of its array")
SANITIZER(shift_out_of_bounds_abort, "a shift too far, or of a negative number")
SANITIZER(divrem_overflow_abort, "a division by zero (or the lowest integer by -1)")
SANITIZER(add_overflow_abort, "a signed addition that overflows")
SANITIZER(sub_overflow_abort, "a signed subtraction that overflows")
SANITIZER(mul_overflow_abort, "a signed multiplication that overflows")
SANITIZER(negate_overflow_abort, "the lowest integer negated")
SANITIZER(float_cast_overflow_abort, "a float that does not fit the integer it is made into")
SANITIZER(type_mismatch_v1_abort, "a null or misaligned pointer used")
SANITIZER(pointer_overflow_abort, "pointer arithmetic that overflows")
SANITIZER(load_invalid_value_abort, "a value that its type does not have")
SANITIZER(builtin_unreachable, "code that cannot be reached was reached")
SANITIZER(missing_return, "a function ended without its value")
SANITIZER(vla_bound_not_positive_abort, "an array of no or negative length")
SANITIZER(nonnull_arg_abort, "null given where it must not be")
SANITIZER(nonnull_return_v1_abort, "null returned where it must not be")
SANITIZER(invalid_builtin_abort, "a builtin given a value it does not take")
SANITIZER(alignment_assumption_abort, "an alignment that is not there")
SANITIZER(implicit_conversion_abort, "a number changed by its conversion")
SANITIZER(function_type_mismatch_abort, "a function called as another type")

/* ==== a crash ===================================================================================================== */
#ifdef _WIN32
#include <dbghelp.h>

/* an exception that would end the process: said first, with the place in the source where the debug file gives it */
static LONG WINAPI crashed(EXCEPTION_POINTERS *x) {
  DWORD code = x->ExceptionRecord->ExceptionCode;
  /* (only errors: a debugger's own exceptions and such pass) */
  if ((code & 0xF0000000u) != 0xC0000000u) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  // clang-format off
  const char *what = code == EXCEPTION_ACCESS_VIOLATION      ? "access violation (a bad pointer or index)"
                   : code == EXCEPTION_STACK_OVERFLOW        ? "stack overflow"
                   : code == EXCEPTION_INT_DIVIDE_BY_ZERO    ? "integer division by zero"
                   : code == EXCEPTION_ILLEGAL_INSTRUCTION   ? "illegal instruction (a trap)"
                   : code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ? "array bounds exceeded"
                   :                                           "exception";
  // clang-format on
  void *at = x->ExceptionRecord->ExceptionAddress;
  char where[600] = "no place in the source found";
  HANDLE me = GetCurrentProcess();
  /* (the debug file lies beside the program: that folder is where dbghelp is told to look) */
  char folder[MAX_PATH] = ".";
  DWORD len = GetModuleFileNameA(0, folder, sizeof folder);
  while (len > 0 && len < sizeof folder && folder[len - 1] != '\\' && folder[len - 1] != '/') {
    folder[--len] = 0;
  }
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
  if (code != EXCEPTION_STACK_OVERFLOW && SymInitialize(me, folder, TRUE)) {
    union {
      SYMBOL_INFO s;
      char room[sizeof(SYMBOL_INFO) + 256];
    } sym;
    DWORD64 off = 0;
    DWORD col = 0;
    IMAGEHLP_LINE64 line;
    memset(&sym, 0, sizeof sym);
    memset(&line, 0, sizeof line);
    sym.s.SizeOfStruct = sizeof(SYMBOL_INFO);
    sym.s.MaxNameLen = 255;
    line.SizeOfStruct = sizeof line;
    int has_name = SymFromAddr(me, (DWORD64)(uintptr_t)at, &off, &sym.s) != 0;
    int has_line = SymGetLineFromAddr64(me, (DWORD64)(uintptr_t)at, &col, &line) != 0;
    if (has_name || has_line) {
      snprintf(where, sizeof where, "in %s, %s:%lu", has_name ? sym.s.Name : "?", has_line ? line.FileName : "?",
               has_line ? (unsigned long)line.LineNumber : 0ul);
    }
  }
  if (code == EXCEPTION_ACCESS_VIOLATION && x->ExceptionRecord->NumberParameters >= 2) {
    fail("crash: %s, %s address %p; code at %p, %s", what,
         x->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
         (void *)x->ExceptionRecord->ExceptionInformation[1], at, where);
  }
  fail("crash: %s (0x%08lX); code at %p, %s", what, (unsigned long)code, at, where);
  return EXCEPTION_CONTINUE_SEARCH;
}

static void crash_watch(void) {
  AddVectoredExceptionHandler(1, crashed);
}
#else
static void crashed(int sig, siginfo_t *info, void *context) {
  (void)context;
  // clang-format off
  fail("crash: %s, address %p",
       sig == SIGSEGV ? "segmentation fault (a bad pointer or index)"
       : sig == SIGFPE ? "arithmetic exception (an integer division by zero)"
       : sig == SIGILL ? "illegal instruction (a trap)"
       : sig == SIGBUS ? "bus error"
       :                 "abort",
       info ? info->si_addr : 0);
  // clang-format on
}

static void crash_watch(void) {
  static const int SIGNALS[5] = {SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGABRT};
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = crashed;
  sa.sa_flags = SA_SIGINFO;
  for (int i = 0; i < 5; i++) {
    sigaction(SIGNALS[i], &sa, 0);
  }
}
#endif

/* ==== making valid things ========================================================================================= */

#define SLOTS 16
/* how every SysEx message of SFX FORGE begins, and its messages (sfx-forge/fw_protocol.h) */
static const uint8_t HEAD[4] = {0xF0, 0x7D, 0x56, 0x42};
enum {
  SX_DUMP = 1,
  SX_LOAD,
  SX_TEXT,
  SX_REQUEST,
  SX_SONG,
  SX_REQ_SONG,
  SX_REQ_IDENT,
  SX_IDENT
};
#define SONG_PIECE 240

/* A patch the firmware must take: one of the core's own RANDOM, or any 25 values 0..127 with any engine, or every
   value at one of its ends. */
static void random_patch(sfx_patch *p, uint32_t *s) {
  int kind = pick(s, 4);
  if (kind == 0) {
    uint32_t r = rnd(s) | 1u;
    sfx_patch_random(p, &r);
    return;
  }
  p->version = SFX_PATCH_VERSION;
  p->engine = (uint8_t)pick(s, SFX_ENGINE_COUNT);
  for (int i = 0; i < SFX_P_COUNT; i++) {
    // clang-format off
    p->p[i] = kind == 1 ? (uint8_t)pick(s, 128)
            : kind == 2 ? (uint8_t)(pick(s, 2) ? 127 : 0)
            :             (uint8_t)(pick(s, 4) ? pick(s, 128) : pick(s, 2) ? 127 : 0);
    // clang-format on
  }
}

/* A song the firmware must take: every value within what sfx_song_unpack asks for - any tempo, bar, swing, grid,
   lanes, arrangement - its patterns empty, thin or full. */
static void random_song(sfx_song *g, uint32_t *s) {
  sfx_song_init(g);
  g->bpm = (uint16_t)(40 + pick(s, 201));
  g->len = (uint8_t)(1 + pick(s, SFX_BAR));
  g->swing = (uint8_t)pick(s, 101);
  g->scale = (uint8_t)pick(s, SFX_SCALE_COUNT);
  g->chain = (uint8_t)(1 + pick(s, SFX_PATTERNS));
  for (int t = 0; t < SFX_TRACKS; t++) {
    g->level[t] = (uint8_t)(pick(s, 3) ? 0 : pick(s, 128));
    g->vol[t] = (uint8_t)pick(s, 128);
    g->pan[t] = (uint8_t)pick(s, 128);
  }
  g->mute = (uint16_t)(pick(s, 3) ? 0 : rnd(s));
  g->fx[SFX_M_DELAY_TIME] = (uint8_t)pick(s, SFX_DELAY_TIMES);
  for (int i = 1; i < SFX_M_COUNT; i++) {
    g->fx[i] = (uint8_t)pick(s, 128);
  }
  g->arr_len = (uint8_t)(pick(s, 2) ? 0 : pick(s, SFX_ARR_MAX + 1));
  for (int i = 0; i < SFX_ARR_MAX; i++) {
    g->arr[i] = (uint8_t)pick(s, SFX_PATTERNS);
  }
  for (int l = 0; l < SFX_LANES; l++) {
    if (pick(s, 2)) {
      g->lane[l].track = (uint8_t)pick(s, SFX_TRACKS + 1);
      g->lane[l].to = (uint8_t)pick(s, SFX_AT_COUNT);
    }
  }
  for (int k = 0; k < SFX_PATTERNS; k++) {
    sfx_pattern *p = &g->pat[k];
    /* how full: 0 empty, 1 thin, 2 half, 3 every cell */
    int full = pick(s, 4);
    p->grid = (uint8_t)pick(s, SFX_GRIDS);
    int steps = SFX_BAR * sfx_div(p->grid);
    for (int i = 0; i < steps && full; i++) {
      uint32_t r = rnd(s);
      p->on[i] = (uint16_t)(full == 3 ? 0xFFFF : full == 2 ? r : r & (r >> 8) & (r >> 16));
      for (int t = 0; t < SFX_TRACKS; t++) {
        if (full == 3 || pick(s, 4) == 0) {
          p->note[t][i] = (uint8_t)pick(s, 128);
          p->vel[t][i] = (uint8_t)pick(s, 8);
          p->len[t][i] = (uint8_t)pick(s, steps);
        }
      }
      for (int l = 0; l < SFX_LANES; l++) {
        if (full == 3 || pick(s, 6) == 0) {
          p->aut[l][i] = (uint8_t)pick(s, 128);
        }
      }
    }
  }
}

/* a sound's message into m (LOAD or DUMP): F0 7D 56 42 <message> <slot> <28 bytes> F7. Returns its length */
static int patch_message(uint8_t *m, int message, int slot, const sfx_patch *p) {
  memcpy(m, HEAD, 4);
  m[4] = (uint8_t)message;
  m[5] = (uint8_t)slot;
  sfx_patch_pack(p, m + 6);
  m[6 + SFX_PATCH_BYTES] = 0xF7;
  return 7 + SFX_PATCH_BYTES;
}

/* piece k of a packed song of n bytes into m:
   F0 7D 56 42 05 <piece lo> <piece hi> <format> <pieces lo> <pieces hi> <up to 240 bytes> F7. Returns its length */
static int piece_message(uint8_t *m, const uint8_t *song, int n, int k) {
  int K = (n + SONG_PIECE - 1) / SONG_PIECE;
  int len = k == K - 1 ? n - k * SONG_PIECE : SONG_PIECE;
  memcpy(m, HEAD, 4);
  m[4] = SX_SONG;
  m[5] = (uint8_t)(k & 0x7F);
  m[6] = (uint8_t)(k >> 7);
  m[7] = song[0];
  m[8] = (uint8_t)(K & 0x7F);
  m[9] = (uint8_t)(K >> 7);
  memcpy(m + 10, song + k * SONG_PIECE, (size_t)len);
  m[10 + len] = 0xF7;
  return 11 + len;
}

/* Bytes in the shape of a song of the formats before (5, or 1-4 by its size): a head within its ranges, then a body
   of 7-bit bytes, thin or thick. Not every one is a song - sfx_song_unpack decides, and must decide without harm. */
static void old_song(uint8_t *b, int n, int format, uint32_t *s) {
  memset(b, 0, (size_t)n);
  b[0] = (uint8_t)format;
  b[1] = (uint8_t)(1 + pick(s, SFX_BAR));
  int bpm = 40 + pick(s, 201);
  b[2] = (uint8_t)(bpm >> 7);
  b[3] = (uint8_t)(bpm & 0x7F);
  b[4] = (uint8_t)pick(s, 101);
  b[5] = (uint8_t)pick(s, SFX_SCALE_COUNT);
  b[6] = (uint8_t)(1 + pick(s, SFX_PATTERNS));
  int thick = 1 + pick(s, 40);
  for (int i = 7; i < n; i++) {
    if (i >= SFX_SONG_HEAD ? pick(s, thick) == 0 : pick(s, 12) == 0) {
      b[i] = (uint8_t)pick(s, i >= SFX_SONG_HEAD || pick(s, 8) == 0 ? 128 : 8);
    }
  }
}

/* ==== the control flow ============================================================================================ */

/* its row of random numbers */
static uint32_t cs;
/* the song it sends and that song packed; a message */
static sfx_song made;
static uint8_t packed[SFX_SONG_MAX + 16];
static uint8_t message[4096];

/* what the audio flow took from the firmware's MIDI out queue, for the control flow to send back (a virtual MIDI
   cable's port is an input as well as an output): a ring, one writer, one reader */
#define ECHOES 32
static struct {
  int n;
  uint8_t b[256];
} echo[ECHOES];
static volatile uint32_t echo_w;
static volatile uint32_t echo_r;

/* the buttons of vibe.h, VB_F1 .. VB_DATA_PUSH: 25 */
#define BUTTONS VB_COUNT

static void ev(int type, int id, int down, float value, int midi) {
  /* DATA turned by more than 16 million detents, or by no number, with SHIFT held: the SDK's own gesture (vibe_sdk.c,
     sk_take) counts such a turn down by ones and never comes back, before the firmware sees the event. Nothing in
     the firmware can change that: such a turn is cut to a million here. */
  if (type == 4 && id == 6 && !(value >= -1.0e6f && value <= 1.0e6f)) {
    value = value > 0.0f ? 1.0e6f : value < 0.0f ? -1.0e6f : 0.0f;
  }
  vibe_wasm_event(type, id, down, value, midi);
  events++;
}

/* a value no host should send (hostile, wild): NaN, the infinities, the largest float */
static float no_number(void) {
  static const uint32_t BITS[6] = {0x7FC00000u, 0xFFC00000u, 0x7F800000u, 0xFF800000u, 0x7F7FFFFFu, 0x7FA00001u};
  return from_bits(BITS[pick(&cs, 6)]);
}

/* a velocity: 0..1, its ends now and then */
static float velocity(void) {
  int k = pick(&cs, 16);
  if (wild && pick(&cs, 50) == 0) {
    return no_number();
  }
  return k == 0 ? 1.0f : k == 1 ? 0.0f : k == 2 ? 0.001f : unit(&cs);
}

static void do_pad(void) {
  /* the 16, now and then one of a bigger grid's */
  int id = pick(&cs, 12) ? pick(&cs, 16) : pick(&cs, 256);
  int down = pick(&cs, 100) < 55;
  float v = down ? velocity() : 0.0f;
  note("pad", id, down, 0, v);
  ev(VIBE_EV_PAD, id, down, v, 0);
}

static void do_key(void) {
  int id = pick(&cs, 10) ? pick(&cs, VIBE_KEYS) : pick(&cs, 256);
  int down = pick(&cs, 100) < 55;
  float v = down ? velocity() : 0.0f;
  note("key", id, down, 0, v);
  ev(VIBE_EV_KEY, id, down, v, 0);
}

/* MIDI: notes on any channel (36-51 play the pads), the game's telemetry (CC 10-17 on channel 2), anything else */
static void do_midi(void) {
  int kind = pick(&cs, 10);
  int st;
  int d1;
  int d2;
  if (kind < 4) {
    st = (pick(&cs, 2) ? 0x90 : 0x80) | pick(&cs, 16);
    d1 = pick(&cs, 3) ? 36 + pick(&cs, 16) : pick(&cs, 128);
    d2 = pick(&cs, 6) ? pick(&cs, 128) : 0;
  } else if (kind < 8) {
    st = 0xB1;
    d1 = 10 + pick(&cs, 8);
    d2 = pick(&cs, 128);
  } else if (kind < 9) {
    st = 0xB0 | pick(&cs, 16);
    d1 = pick(&cs, 128);
    d2 = pick(&cs, 128);
  } else {
    /* (also what MIDI does not have: data bytes above 127) */
    st = 0x80 | pick(&cs, 128);
    d1 = pick(&cs, 256);
    d2 = pick(&cs, 256);
  }
  note("midi", st, d1, d2, 0);
  ev(VIBE_EV_MIDI, 0, 0, 0.0f, st | d1 << 8 | d2 << 16);
}

/* a button of the device: mostly the pages' keys and F1-F6 */
static int a_button(void) {
  int k = pick(&cs, 20);
  // clang-format off
  return k < 6  ? VB_K1 + pick(&cs, 6)
       : k < 12 ? VB_F1 + pick(&cs, 6)
       : k < 19 ? pick(&cs, BUTTONS)
       : pick(&cs, 2) ? VB_X1 + pick(&cs, 16) : pick(&cs, 256);
  // clang-format on
}

static void press(int id) {
  ev(VIBE_EV_BUTTON, id, 1, 0.0f, 0);
  ev(VIBE_EV_BUTTON, id, 0, 0.0f, 0);
}

static void do_button(void) {
  int kind = pick(&cs, 10);
  int id = a_button();
  if (kind < 6) {
    note("button", id, 0, 0, 0);
    press(id);
  } else if (kind < 9) {
    /* SHIFT held, one to three buttons, SHIFT let go (or not: then it stays held) */
    int n = 1 + pick(&cs, 3);
    note("shift+button", id, n, 0, 0);
    ev(VIBE_EV_BUTTON, VB_SHIFT, 1, 0.0f, 0);
    press(id);
    for (int i = 1; i < n; i++) {
      press(a_button());
    }
    if (pick(&cs, 12)) {
      ev(VIBE_EV_BUTTON, VB_SHIFT, 0, 0.0f, 0);
    }
  } else {
    /* only down, or only up */
    int down = pick(&cs, 2);
    note("button half", id, down, 0, 0);
    ev(VIBE_EV_BUTTON, id, down, 0.0f, 0);
  }
}

static void do_encoder(void) {
  int id = pick(&cs, 16) ? pick(&cs, VE_COUNT) : pick(&cs, 2) ? VE_X1 + pick(&cs, 8) : pick(&cs, 256);
  int kind = pick(&cs, 10);
  // clang-format off
  float v = kind < 5 ? (pick(&cs, 2) ? 1.0f : -1.0f)
          : kind < 8 ? (unit(&cs) - 0.5f) * 2.0f
          : kind < 9 ? (float)(pick(&cs, 17) - 8)
          :            (unit(&cs) - 0.5f) * 128.0f;
  // clang-format on
  if (wild && pick(&cs, 50) == 0) {
    v = no_number();
  }
  int n = 1 + (pick(&cs, 4) ? 0 : pick(&cs, 24));
  note("encoder", id, n, 0, v);
  for (int i = 0; i < n; i++) {
    ev(VIBE_EV_ENCODER, id, 0, v, 0);
  }
}

/* a host's parameter: mostly the mixer's 65 (6..70), within its range or far outside (the SDK brings it in) */
static void do_param(void) {
  int count = vibe_wasm_param_count();
  int i = pick(&cs, 20) ? pick(&cs, count) : pick(&cs, 400) - 100;
  int kind = pick(&cs, 12);
  // clang-format off
  float v = kind < 6  ? unit(&cs)
          : kind < 8  ? (float)pick(&cs, 2)
          : kind < 10 ? unit(&cs) * 2.0f - 1.0f
          : kind < 11 ? (float)(pick(&cs, 40) - 12)
          :             (unit(&cs) - 0.5f) * 2e30f;
  // clang-format on
  if (hostile && pick(&cs, 25) == 0) {
    v = no_number();
  }
  note("param", i, 0, 0, v);
  vibe_wasm_param_set(i, v);
  params_set++;
}

/* a SysEx message into the firmware, as a host hands it over */
static void sysex(const uint8_t *b, int n) {
  memcpy(vibe_wasm_sysex_buf(), b, (size_t)n);
  vibe_wasm_sysex_in(n);
  events++;
}

static void do_draw(void);

/* a whole song, piece by piece - in order, backwards, shuffled, every piece twice; or broken: a piece missing, a
   byte of one wrong. Now and then something else happens between two pieces. */
static void do_song(void) {
  static uint16_t order[2 * 160];
  random_song(&made, &cs);
  int n = sfx_song_pack(&made, packed);
  int K = (n + SONG_PIECE - 1) / SONG_PIECE;
  int how = pick(&cs, 8);
  int count = K;
  for (int k = 0; k < K; k++) {
    order[k] = (uint16_t)(how == 3 ? K - 1 - k : k);
  }
  if (how == 4) {
    for (int k = K - 1; k > 0; k--) {
      int j = pick(&cs, k + 1);
      uint16_t t = order[k];
      order[k] = order[j];
      order[j] = t;
    }
  }
  if (how == 5) {
    for (int k = K - 1; k >= 0; k--) {
      order[2 * k] = order[2 * k + 1] = order[k];
    }
    count = 2 * K;
  }
  int bad = how >= 6 ? pick(&cs, K) : -1;
  note("song", n, K, how, 0);
  for (int i = 0; i < count; i++) {
    int len = piece_message(message, packed, n, order[i]);
    if (order[i] == bad) {
      if (how == 6) {
        continue;
      }
      message[10 + pick(&cs, len - 11)] = (uint8_t)pick(&cs, pick(&cs, 4) ? 128 : 256);
    }
    sysex(message, len);
    if (pick(&cs, 24) == 0) {
      if (pick(&cs, 2)) {
        do_draw();
      } else {
        ev(VIBE_EV_PAD, pick(&cs, 16), pick(&cs, 2), 0.7f, 0);
      }
    }
  }
  if (how >= 6) {
    songs_broken++;
  } else {
    songs_whole++;
    sysex_valid += (uint64_t)count;
  }
}

/* a song in the shape of the firmwares before: 39 pieces of 200 bytes (the last 99), F0 7D 56 42 05 <piece> 39 ... */
static void do_old_song(void) {
  old_song(packed, SFX_SONG_BYTES_V5, 5, &cs);
  note("old song", 0, 0, 0, 0);
  for (int k = 0; k < 39; k++) {
    int len = k == 38 ? SFX_SONG_BYTES_V5 - 38 * 200 : 200;
    memcpy(message, HEAD, 4);
    message[4] = SX_SONG;
    message[5] = (uint8_t)k;
    message[6] = 39;
    memcpy(message + 7, packed + k * 200, (size_t)len);
    message[7 + len] = 0xF7;
    sysex(message, 8 + len);
  }
  songs_whole++;
}

/* SysEx that is not a valid message: every length, every field at and past its ends */
static void do_garbage(void) {
  static const int LENGTHS[] = {2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 34, 35, 36, 250, 251, 252, 253, 4095, 4096};
  int kind = pick(&cs, 6);
  int n;
  if (kind == 0) {
    /* anything at all after F0 */
    n = 2 + pick(&cs, pick(&cs, 8) ? 300 : 4095);
    for (int i = 0; i < n; i++) {
      message[i] = (uint8_t)pick(&cs, 256);
    }
    message[0] = 0xF0;
  } else if (kind == 1) {
    /* our head, any message number, any bytes, the end where it belongs or not */
    n = 6 + pick(&cs, 300);
    for (int i = 0; i < n; i++) {
      message[i] = (uint8_t)pick(&cs, pick(&cs, 4) ? 128 : 256);
    }
    memcpy(message, HEAD, 4);
    if (pick(&cs, 8)) {
      message[n - 1] = 0xF7;
    }
  } else if (kind == 2) {
    /* a message of ours at a length it does not have */
    n = LENGTHS[pick(&cs, (int)(sizeof LENGTHS / sizeof LENGTHS[0]))];
    for (int i = 0; i < n; i++) {
      message[i] = (uint8_t)pick(&cs, 128);
    }
    memcpy(message, HEAD, n < 4 ? (size_t)n : 4);
    if (n > 4) {
      message[4] = (uint8_t)(1 + pick(&cs, 8));
    }
    message[n - 1] = 0xF7;
  } else if (kind == 3) {
    /* a sound with one byte wrong (the slot, the version, the engine, a value, the checksum) */
    sfx_patch p;
    random_patch(&p, &cs);
    n = patch_message(message, pick(&cs, 2) ? SX_LOAD : SX_DUMP, pick(&cs, 32), &p);
    message[5 + pick(&cs, n - 6)] = (uint8_t)pick(&cs, 256);
  } else if (kind == 4) {
    /* a piece of a song whose numbers are at or past their ends */
    static const int PIECE[] = {0, 1, 38, 39, 127, 128, 158, 159, 160, 161, 255, 16383};
    static const int BYTES[] = {0, 1, 2, 99, 199, 200, 239, 240, 241, 300};
    int k = PIECE[pick(&cs, 12)];
    int K = PIECE[pick(&cs, 12)];
    int len = BYTES[pick(&cs, 10)];
    memcpy(message, HEAD, 4);
    message[4] = SX_SONG;
    message[5] = (uint8_t)(k & 0x7F);
    message[6] = (uint8_t)(pick(&cs, 6) ? (k >> 7) & 0x7F : pick(&cs, 256));
    message[7] = (uint8_t)(pick(&cs, 3) ? SFX_SONG_VERSION : pick(&cs, 128));
    message[8] = (uint8_t)(K & 0x7F);
    message[9] = (uint8_t)(pick(&cs, 6) ? (K >> 7) & 0x7F : pick(&cs, 256));
    for (int i = 0; i < len; i++) {
      message[10 + i] = (uint8_t)pick(&cs, 128);
    }
    if (len && pick(&cs, 2)) {
      message[10] = message[7];
    }
    message[10 + len] = 0xF7;
    n = 11 + len;
  } else {
    /* a piece in the shape of before, its number or its length right or not */
    int k = pick(&cs, 4) ? pick(&cs, 39) : pick(&cs, 128);
    int len = pick(&cs, 3) ? (k == 38 ? 99 : 200) : pick(&cs, 260);
    memcpy(message, HEAD, 4);
    message[4] = SX_SONG;
    message[5] = (uint8_t)k;
    message[6] = 39;
    for (int i = 0; i < len; i++) {
      message[7 + i] = (uint8_t)pick(&cs, 128);
    }
    message[7 + len] = 0xF7;
    n = 8 + len;
  }
  note("garbage sysex", kind, n, 0, 0);
  sysex(message, n);
  sysex_garbage++;
}

static void do_sysex(void) {
  int kind = pick(&cs, 100);
  sfx_patch p;
  int n;
  if (kind < 30) {
    /* LOAD, slot 0-15 (the selected kit) or 16-31 (the MUSIC kit); DUMP */
    random_patch(&p, &cs);
    int msg = pick(&cs, 4) ? SX_LOAD : SX_DUMP;
    int slot = pick(&cs, msg == SX_LOAD ? 32 : 16);
    n = patch_message(message, msg, slot, &p);
    note("load", msg, slot, p.engine, 0);
    sysex(message, n);
    sysex_valid++;
  } else if (kind < 36) {
    /* TEXT: up to 24 characters, also more and none */
    n = pick(&cs, 4) ? pick(&cs, 25) : pick(&cs, 200);
    memcpy(message, HEAD, 4);
    message[4] = SX_TEXT;
    for (int i = 0; i < n; i++) {
      message[5 + i] = (uint8_t)pick(&cs, 128);
    }
    message[5 + n] = 0xF7;
    note("text", n, 0, 0, 0);
    sysex(message, 6 + n);
    sysex_valid++;
  } else if (kind < 46) {
    /* REQUEST a sound, the song, the identity */
    int msg = pick(&cs, 3);
    memcpy(message, HEAD, 4);
    message[4] = (uint8_t)(msg == 0 ? SX_REQUEST : msg == 1 ? SX_REQ_SONG : SX_REQ_IDENT);
    n = 5;
    if (msg == 0) {
      message[n++] = (uint8_t)pick(&cs, 16);
    }
    message[n++] = 0xF7;
    note("request", message[4], 0, 0, 0);
    sysex(message, n);
    sysex_valid++;
  } else if (kind < 58) {
    do_song();
  } else if (kind < 61) {
    do_old_song();
  } else if (kind < 75) {
    /* what the firmware sent, back to it: all that waits */
    uint32_t w = echo_w;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    note("echo", (int)(w - echo_r), 0, 0, 0);
    while (echo_r != w) {
      sysex(echo[echo_r % ECHOES].b, echo[echo_r % ECHOES].n);
      sysex_echoed++;
      __atomic_thread_fence(__ATOMIC_RELEASE);
      echo_r = echo_r + 1;
    }
  } else {
    do_garbage();
  }
}

/* The mouse on the stage (960 x 540): a click with a drag, the wheel, shown and hidden. Mostly inside the picture,
   now and then anywhere a value of y * 4096 + x can be. */
static void do_stage(void) {
  int kind = pick(&cs, 20);
  int wide = pick(&cs, 30) == 0;
  int x = pick(&cs, wide ? 4096 : 960);
  int y = pick(&cs, wide ? 4096 : 540);
  int pointer = pick(&cs, 12) ? 0 : pick(&cs, 256);
  int mods = pick(&cs, 3) ? 0 : pick(&cs, 12) ? pick(&cs, 8) : pick(&cs, 256);
  int midi = pointer | mods << 8;
  note("stage", kind, x, y, (float)mods);
  if (kind < 12) {
    ev(VIBE_EV_STAGE, VIBE_STAGE_DOWN, 1, (float)(y * 4096 + x), midi);
    int moves = pick(&cs, 3) ? 0 : pick(&cs, 12);
    for (int i = 0; i < moves; i++) {
      x += pick(&cs, 41) - 20;
      y += pick(&cs, 41) - 20;
      x = x < 0 ? 0 : x > 4095 ? 4095 : x;
      y = y < 0 ? 0 : y > 4095 ? 4095 : y;
      ev(VIBE_EV_STAGE, VIBE_STAGE_MOVE, 1, (float)(y * 4096 + x), midi);
    }
    if (pick(&cs, 12)) {
      ev(VIBE_EV_STAGE, VIBE_STAGE_UP, 0, (float)(y * 4096 + x), midi);
    }
  } else if (kind < 16) {
    ev(VIBE_EV_STAGE, VIBE_STAGE_WHEEL, 0, pick(&cs, 2) ? 1.0f : -1.0f, midi);
    /* (the wheel's place is the last the mouse had: a move tells it) */
    ev(VIBE_EV_STAGE, VIBE_STAGE_MOVE, 0, (float)(y * 4096 + x), midi);
  } else if (kind < 18) {
    ev(VIBE_EV_STAGE, VIBE_STAGE_MOVE, pick(&cs, 2), (float)(y * 4096 + x), midi);
  } else if (kind < 19) {
    ev(VIBE_EV_STAGE, pick(&cs, 2) ? VIBE_STAGE_SHOWN : VIBE_STAGE_HIDDEN, 0, 0.0f, 0);
  } else {
    float v = wild && pick(&cs, 4) == 0 ? no_number() : (unit(&cs) - 0.25f) * 4.0e7f;
    ev(VIBE_EV_STAGE, pick(&cs, 256), pick(&cs, 2), v, (int)(rnd(&cs) & 0xFFFFFF));
  }
}

/* a click into the display (480 x 320): down, a drag, up */
static void do_touch(void) {
  int x = pick(&cs, 512);
  int y = pick(&cs, 20) ? pick(&cs, 320) : pick(&cs, 4096);
  note("touch", x, y, 0, 0);
  ev(VIBE_EV_TOUCH, VIBE_TOUCH_DOWN, 1, (float)(y * 512 + x), 0);
  int moves = pick(&cs, 2) ? 0 : pick(&cs, 8);
  for (int i = 0; i < moves; i++) {
    x = (x + pick(&cs, 31) + 512 - 15) % 512;
    y = (y + pick(&cs, 31) + 320 - 15) % 320;
    ev(VIBE_EV_TOUCH, VIBE_TOUCH_MOVE, 1, (float)(y * 512 + x), 0);
  }
  if (pick(&cs, 12)) {
    ev(VIBE_EV_TOUCH, VIBE_TOUCH_UP, 0, (float)(y * 512 + x), 0);
  }
}

/* what is left: SAVE (also with the store full), the host's transport, the kinds of event the firmware has no use
   for, a kind that does not exist */
static void do_other(void) {
  int kind = pick(&cs, 20);
  if (kind < 4) {
    store_full = pick(&cs, 4) == 0;
    note("save", store_full, 0, 0, 0);
    ev(VIBE_EV_SAVE, 0, 0, 0.0f, 0);
    saves++;
    store_full = 0;
  } else if (kind < 8) {
    int id = pick(&cs, 2);
    float v = id == VIBE_TRANSPORT_TEMPO ? (pick(&cs, 4) ? 20.0f + unit(&cs) * 380.0f : 0.0f) : 0.0f;
    int down = pick(&cs, 2);
    note("transport", id, down, 0, v);
    ev(VIBE_EV_TRANSPORT, id, down, v, 0);
  } else if (kind < 10) {
    /* bytes from an Editor page (the firmware has none) */
    int n = 1 + pick(&cs, 4096);
    uint8_t *in = vibe_wasm_editor_inbox();
    for (int i = 0; i < n && i < 64; i++) {
      in[i] = (uint8_t)pick(&cs, 256);
    }
    note("editor data", n, 0, 0, 0);
    vibe_wasm_editor_data_in(n);
    events++;
  } else if (kind < 11) {
    /* the store full or not, from here on until a SAVE */
    store_full = pick(&cs, 2);
    note("store full", store_full, 0, 0, 0);
  } else {
    static const uint8_t TYPES[] = {VIBE_EV_PEDAL,  VIBE_EV_CLOCK, VIBE_EV_AREA,  VIBE_EV_MEDIA,
                                    VIBE_EV_EDITOR, VIBE_EV_FADER, VIBE_EV_SYSEX, VIBE_EV_EDITOR_DATA};
    int type = pick(&cs, 4) ? TYPES[pick(&cs, 8)] : pick(&cs, 256);
    int id = pick(&cs, 256);
    int down = pick(&cs, 2);
    float v = pick(&cs, 3) ? unit(&cs) : (unit(&cs) - 0.5f) * 10000.0f;
    if (wild && pick(&cs, 10) == 0) {
      v = no_number();
    }
    note("other event", type, id, down, v);
    ev(type, id, down, v, (int)(rnd(&cs) & 0xFFFFFF));
  }
}

/* a frame of the display, or of the stage; and what a host reads with it */
static void do_draw(void) {
  if (pick(&cs, 5) < 3) {
    if (!vibe_wasm_draw()) {
      fail("vibe_wasm_draw gave no picture");
    }
    draws++;
  } else {
    vibe_wasm_stage_draw();
    stage_draws++;
  }
  /* the parameters the firmware changed itself: each must be a number within its range */
  for (int word = 0; word < VIBE_MAX_PARAMS / 32; word++) {
    uint32_t m = vibe_wasm_param_changed(word);
    for (int b = 0; b < 32 && m; b++) {
      if (m >> b & 1) {
        float v = vibe_wasm_param_value(word * 32 + b);
        if (!hostile && (!is_number(v) || v < -4.0f || v > 24.0f)) {
          fail("parameter %d, changed by the firmware, is %g: not a value of any of its parameters", word * 32 + b,
               (double)v);
        }
      }
    }
  }
}

/* The tour (MENU), left alone for a while: only draws, so that it runs on - any touch would end it. */
static void do_tour(void) {
  int n = 20 + pick(&cs, 400);
  note("tour", n, 0, 0, 0);
  press(VB_MENU);
  for (int i = 0; i < n && !stop_now; i++) {
    do_draw();
    if (!one_flow && pick(&cs, 4) == 0) {
      nap(0);
    }
  }
}

/* one step of the control flow: one of the kinds above, by weight */
static void control_op(void) {
  int k = pick(&cs, 1000);
  if (k < 130) {
    do_pad();
  } else if (k < 210) {
    do_key();
  } else if (k < 300) {
    do_midi();
  } else if (k < 470) {
    do_button();
  } else if (k < 590) {
    do_encoder();
  } else if (k < 670) {
    do_param();
  } else if (k < 760) {
    do_sysex();
  } else if (k < 830) {
    do_stage();
  } else if (k < 860) {
    do_touch();
  } else if (k < 885) {
    do_other();
  } else if (k < 888) {
    do_tour();
  } else {
    note("draw", 0, 0, 0, 0);
    do_draw();
  }
  ops = ops + 1;
}

THREAD_FN(control) {
  (void)arg;
  flow = FLOW_CONTROL;
  while (!stop_now) {
    control_op();
    /* a short pause, now and then: a few spins, the rest of the time slice, a millisecond */
    int p = pick(&cs, 1000);
    if (p < 100) {
      for (volatile int i = pick(&cs, 2000); i > 0; i--) {
      }
    } else if (p < 130) {
      nap(0);
    } else if (p < 134) {
      nap(1);
    }
  }
  control_done = 1;
  THREAD_END;
}

/* ==== the audio flow ============================================================================================== */

/* its row of random numbers; the frames of the block before; the host's song position, in quarters */
static uint32_t as;
static int frames_before = 128;
static double host_beat;
static int host_mode;

/* One block, as a host runs it: the transport table, vibe_wasm_audio, then every sample and the MIDI out queue. */
static void audio_block(void) {
  /* the frames: any of 1..256, the very small, the usual, the same again */
  static const int USUAL[8] = {32, 64, 128, 256, 441, 480, 512, 1};
  int k = pick(&as, 10);
  int frames = k < 4 ? 1 + pick(&as, 256) : k < 6 ? 1 + pick(&as, 8) : k < 8 ? USUAL[pick(&as, 8)] : frames_before;
  if (frames > VIBE_MAX_BLOCK) {
    frames = VIBE_MAX_BLOCK;
  }
  frames_before = frames;

  /* the host's transport, as a DAW fills it before a block. Its mode changes now and then: 0 none, 1 it plays, 2 it
     stands; while it plays the position moves on with the frames, and jumps (a loop, a click into the timeline) */
  vibe_transport_info *tp = vibe_wasm_transport();
  if (pick(&as, 3000) == 0) {
    host_mode = pick(&as, 3);
    tp->has = host_mode != 0;
    tp->playing = host_mode == 1;
    tp->bpm = pick(&as, 8) ? 40.0f + unit(&as) * 200.0f : pick(&as, 2) ? 0.0f : 1.0f + unit(&as) * 998.0f;
    tp->sig_num = 1 + pick(&as, 16);
    tp->sig_den = 1 << pick(&as, 5);
    tp->loop = pick(&as, 2);
    /* (a count-in is a position below 0) */
    host_beat = pick(&as, 4) ? unit(&as) * 64.0 : pick(&as, 2) ? -(double)unit(&as) * 8.0 : unit(&as) * 2.0e6;
  }
  if (host_mode == 1) {
    if (pick(&as, 4000) == 0) {
      host_beat = unit(&as) * 256.0 - 4.0;
    }
    tp->beat = host_beat;
    tp->bar_start = (double)((long long)(host_beat / 4.0)) * 4.0;
    host_beat += (double)frames * (double)tp->bpm / 60.0 / (double)rate;
  }

  /* the block. Its buffers first hold a value no sample has: one that is left is a sample never written */
  float *io = vibe_wasm_io();
  float *l = io + VIBE_MAX_BLOCK;
  float *r = io + 2 * VIBE_MAX_BLOCK;
  for (int i = 0; i < frames; i++) {
    l[i] = 7.0f;
    r[i] = 7.0f;
  }
  vibe_wasm_audio(frames, 0);
  if (try_what && blocks == TRY_AT) {
    static int four[4];
    static volatile int five = 5;
    static volatile uintptr_t nowhere = 8;
    if (try_what == TRY_INDEX) {
      four[five] = 1;
    } else if (try_what == TRY_CRASH) {
      *(volatile int *)nowhere = 1;
    } else if (try_what == TRY_SAMPLE) {
      l[frames - 1] = from_bits(0x7FC00000u);
    } else {
      for (;;) {
        nap(1000);
      }
    }
  }

  /* every sample: a number, within -1..1 */
  int loud = 0;
  for (int c = 0; c < 2; c++) {
    const float *x = c ? r : l;
    for (int i = 0; i < frames; i++) {
      float v = x[i];
      if (!is_number(v)) {
        fail("audio: the %s sample %d of a block of %d is no number (bits 0x%08X)", c ? "right" : "left", i, frames,
             *(const uint32_t *)(const void *)&x[i]);
      }
      if (v == 7.0f) {
        fail("audio: the %s sample %d of a block of %d was never written", c ? "right" : "left", i, frames);
      }
      if (v < -1.0f || v > 1.0f) {
        fail("audio: the %s sample %d of a block of %d is %g, outside -1..1", c ? "right" : "left", i, frames,
             (double)v);
      }
      float a = v < 0.0f ? -v : v;
      if (a > peak) {
        peak = a;
      }
      loud |= a > 0.0f;
    }
  }

  /* the MIDI out queue, emptied as a host does after a block: a short message has a status and 7-bit data, a SysEx
     is F0 .. F7 with 7-bit bytes between (the firmware's own, all of them) */
  midi_queue *q = vibe_wasm_midi_out();
  uint32_t w = q->w;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  for (uint32_t i = q->r; i != w; i++) {
    const midi_msg *m = &q->q[i % 256];
    if (!m->len && m->st == 0xF0) {
      uint32_t n = q->sysex_len;
      if (n < 6 || n > 256 || q->sysex[0] != 0xF0 || q->sysex[n - 1] != 0xF7) {
        fail("MIDI out: a SysEx of %u bytes that is not F0 .. F7", n);
      }
      for (uint32_t b = 1; b + 1 < n; b++) {
        if (q->sysex[b] > 127) {
          fail("MIDI out: a SysEx (message %u) whose byte %u is 0x%02X, above 127", q->sysex[4], b, q->sysex[b]);
        }
      }
      /* (and on to the control flow, which sends it back now and then; a full ring: this one is not sent back) */
      uint32_t ew = echo_w;
      if (ew - echo_r < ECHOES) {
        echo[ew % ECHOES].n = (int)n;
        memcpy(echo[ew % ECHOES].b, q->sysex, n);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        echo_w = ew + 1;
      }
      __atomic_thread_fence(__ATOMIC_RELEASE);
      q->sysex_len = 0;
      midi_sysex++;
    } else {
      if (m->len < 1 || m->len > 3 || m->st < 0x80 || m->d1 > 127 || m->d2 > 127) {
        fail("MIDI out: a message %02X %02X %02X of length %d", m->st, m->d1, m->d2, m->len);
      }
      midi_short++;
    }
  }
  __atomic_thread_fence(__ATOMIC_RELEASE);
  q->r = w;

  loud_blocks = loud_blocks + (uint64_t)loud;
  frames_run = frames_run + (uint64_t)frames;
  blocks = blocks + 1;
}

THREAD_FN(audio) {
  (void)arg;
  flow = FLOW_AUDIO;
  while (!stop_now) {
    audio_block();
  }
  audio_done = 1;
  THREAD_END;
}

/* ==== main ======================================================================================================== */

/* What the store holds before the firmware starts, by the seed: nothing; what a firmware saved (valid files); files
   in the shape of the firmwares before; files of the right names with anything in them. vibe_on_init reads them. */
static void store_before(uint32_t *s) {
  static uint8_t b[FILE_MAX];
  int kind = pick(s, 4);
  if (kind == 0) {
    printf("        the store at the start: empty\n");
    return;
  }
  if (kind == 1 || kind == 2) {
    /* kits.bin: 32 patches, then the 32 bytes that say which named sound each pad holds */
    for (int i = 0; i < 2 * SLOTS; i++) {
      sfx_patch p;
      random_patch(&p, s);
      sfx_patch_pack(&p, b + i * SFX_PATCH_BYTES);
      b[2 * SLOTS * SFX_PATCH_BYTES + i] = (uint8_t)pick(s, 48);
    }
    file_put("kits.bin", 8, b, 2 * SLOTS * (SFX_PATCH_BYTES + 1));
    /* song6.bin: a packed song and the two bytes of its check */
    random_song(&made, s);
    int n = sfx_song_pack(&made, b);
    b[n] = (uint8_t)pick(s, 128);
    b[n + 1] = (uint8_t)pick(s, 128);
    file_put("song6.bin", 9, b, n + 2);
    b[0] = (uint8_t)(pick(s, 2) ? 3 : pick(s, 8));
    file_put("meter.bin", 9, b, 1);
    if (kind == 2) {
      /* and beside it the song.bin of a firmware before, in one of its five formats */
      static const int SIZE[5] = {SFX_SONG_BYTES_V1, SFX_SONG_BYTES_V2, SFX_SONG_BYTES_V3, SFX_SONG_BYTES_V4,
                                  SFX_SONG_BYTES_V5};
      int format = 1 + pick(s, 5);
      old_song(b, SIZE[format - 1], format, s);
      file_put("song.bin", 8, b, SIZE[format - 1]);
      printf("        the store at the start: kits.bin, song6.bin, meter.bin, and a song.bin of format %d\n", format);
    } else {
      printf("        the store at the start: kits.bin, song6.bin, meter.bin as a firmware saves them\n");
    }
    return;
  }
  /* anything, at any length up to what the firmware reads */
  static const char *const NAME[4] = {"kits.bin", "song6.bin", "song.bin", "meter.bin"};
  static const int MOST[4] = {2 * SLOTS * (SFX_PATCH_BYTES + 1) + 8, SFX_SONG_MAX + 8, SFX_SONG_BYTES_V5 + 8, 4};
  for (int f = 0; f < 4; f++) {
    int n = pick(s, 3) ? MOST[f] - pick(s, 16) : pick(s, MOST[f] + 1);
    int top = pick(s, 2) ? 128 : 256;
    for (int i = 0; i < n; i++) {
      b[i] = (uint8_t)pick(s, top);
    }
    if (n > 0 && pick(s, 2)) {
      b[0] = (uint8_t)(1 + pick(s, 7));
    }
    file_put(NAME[f], (int)strlen(NAME[f]), b, n < 0 ? 0 : n);
  }
  printf("        the store at the start: four files of the firmware's names with anything in them\n");
}

int main(int argc, char **argv) {
  static const float RATES[8] = {48000.0f, 44100.0f, 96000.0f, 48000.0f, 88200.0f, 192000.0f, 22050.0f, 44100.0f};
  setvbuf(stdout, 0, _IOLBF, 1024);
  for (int i = 1, n = 0; i < argc; i++) {
    if (!strcmp(argv[i], "one")) {
      one_flow = 1;
    } else if (!strcmp(argv[i], "hostile")) {
      hostile = 1;
    } else if (!strcmp(argv[i], "wild")) {
      wild = 1;
    } else if (!strncmp(argv[i], "try-", 4)) {
      const char *t = argv[i] + 4;
      try_what = !strcmp(t, "index")    ? TRY_INDEX
                 : !strcmp(t, "crash")  ? TRY_CRASH
                 : !strcmp(t, "sample") ? TRY_SAMPLE
                                        : TRY_HANG;
    } else if (n == 0) {
      run_seconds = atof(argv[i]);
      n++;
    } else {
      seed = (uint32_t)strtoul(argv[i], 0, 10);
    }
  }
  if (!seed) {
    seed = 1;
  }
  /* the three rows: what is in the store, the control flow's, the audio flow's */
  uint32_t ss = seed * 2654435761u | 1u;
  cs = (seed ^ 0x9E3779B9u) * 2246822519u | 1u;
  as = (seed + 0x7F4A7C15u) * 3266489917u | 1u;
  rate = RATES[seed % 8];
  crash_watch();

  printf("flows stress: seed %u, %.0f seconds, %s, the host's rate %.0f Hz%s%s\n", seed, run_seconds,
         one_flow ? "ONE flow (block and event in turn)" : "two flows", (double)rate,
         hostile ? ", hostile (parameters that are no number)" : "", wild ? ", wild (events that are no number)" : "");
  store_before(&ss);
  flow = FLOW_CONTROL;
  vibe_wasm_init(rate);
  flow = FLOW_MAIN;
  if (vibe_wasm_flows() != 2) {
    fail("the firmware does not say it runs in two flows (vibe_wasm_flows)");
  }

  double t0 = seconds();
  if (one_flow) {
    /* one thread: the same two rows, a block or a step of the control flow in turn (a third row says which) */
    uint32_t ts = seed * 40503u | 1u;
    while (seconds() - t0 < run_seconds) {
      for (int i = 0; i < 256; i++) {
        if (pick(&ts, 3)) {
          flow = FLOW_AUDIO;
          audio_block();
        } else {
          flow = FLOW_CONTROL;
          control_op();
        }
      }
    }
    flow = FLOW_MAIN;
  } else {
    thread_go(audio);
    thread_go(control);
    /* the watch: each flow must move. (A step of the control flow is short: the longest sends a song of 160 pieces.) */
    uint64_t seen_blocks = 0;
    uint64_t seen_ops = 0;
    double moved_audio = t0;
    double moved_control = t0;
    while (!(audio_done && control_done)) {
      nap(50);
      double now = seconds();
      if (now - t0 >= run_seconds) {
        stop_now = 1;
      }
      if (blocks != seen_blocks || audio_done) {
        seen_blocks = blocks;
        moved_audio = now;
      }
      if (ops != seen_ops || control_done) {
        seen_ops = ops;
        moved_control = now;
      }
      if (now - moved_audio > 10.0) {
        fail("hang: the audio flow has not finished a block for 10 seconds");
      }
      if (now - moved_control > 10.0) {
        fail("hang: the control flow has not finished a step for 10 seconds");
      }
    }
  }
  double took = seconds() - t0;

  printf("  ok    the audio flow: %llu blocks of 1..256 frames, %.0f seconds of sound in %.1f; every sample a number "
         "within -1..1\n",
         (unsigned long long)blocks, (double)frames_run / (double)rate, took);
  printf("        (%llu of the blocks not silent, the highest sample %.4f; MIDI out: %llu short messages and %llu "
         "SysEx, all well formed)\n",
         (unsigned long long)loud_blocks, (double)peak, (unsigned long long)midi_short, (unsigned long long)midi_sysex);
  printf("  ok    the control flow: %llu steps - %llu events, %llu parameters set, %llu draws of the display, %llu "
         "of the stage\n",
         (unsigned long long)ops, (unsigned long long)events, (unsigned long long)params_set, (unsigned long long)draws,
         (unsigned long long)stage_draws);
  printf("        (SysEx in: %llu valid messages with %llu whole songs, %llu broken songs, %llu of garbage, %llu of "
         "the firmware's own sent back; %llu SAVEs)\n",
         (unsigned long long)sysex_valid, (unsigned long long)songs_whole, (unsigned long long)songs_broken,
         (unsigned long long)sysex_garbage, (unsigned long long)sysex_echoed, (unsigned long long)saves);
  printf("  ok    the host: the store was called %llu times (%llu writes refused as full), the log %llu times, "
         "never from the audio flow\n",
         (unsigned long long)store_calls, (unsigned long long)store_refused, (unsigned long long)logs);
  printf("  ok    no sanitizer hit, no crash, no hang\n");
  /* a run that did nothing proves nothing */
  if (!blocks || !ops || !loud_blocks || !draws) {
    fail("the run did too little to count: %llu blocks (%llu not silent), %llu control steps, %llu draws",
         (unsigned long long)blocks, (unsigned long long)loud_blocks, (unsigned long long)ops,
         (unsigned long long)draws);
  }
  printf("flows stress PASSED\n");
  fflush(stdout);
  end_now(0);
  return 0;
}
