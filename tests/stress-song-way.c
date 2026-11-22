/* SPDX-License-Identifier: MIT
 * stress-song-way.c – two real threads over sfx-forge/song_way.h, the very code the firmware uses.
 *
 * The test hosts call the firmware in one flow, so they can never show what two flows do to a song. This does:
 *   the editor   a thread that rewrites its song as the firmware's keys do - a grid change (sfx_pattern_regrid), a COPY,
 *                a CLR PAT, a whole new song, single steps - as fast as it can, and after each edit writes a check of
 *                all eight patterns into the song's head and calls song_way_put
 *   the player   a thread that calls song_way_take as an audio block would and then reads its song: the check in the
 *                head must be the check of the patterns it has, every pattern's cells must fit its grid, and the edit's
 *                number must never go back. A song that fails is one the player would have played half made.
 *   a message    every few edits the editor sends one that must find the song as it was then (the firmware's START and
 *                CUE): song_way_need, the number into a queue. The player works it only when song_way_has says so -
 *                and then the edit's number in its song must be that of the message, or a later one. How often a
 *                message had to wait (worked at first sight, it would have started in an older song) is counted too.
 * It runs twice. "in place": the editor rewrites the player's own song, as the firmware did before - half-made songs
 * are seen (how many depends on the machine; it shows that this test can see them). "song_way": none may be seen, the
 * last edit must arrive, and it says how often each side stepped back for the other.
 * Then twice more with nothing but the hand-over (single steps, no check of the patterns: so the two meet all the time,
 * and each steps back for the other in a good part of its tries): first with every message worked at first sight, as
 * the firmware did before messages waited - some start in an older song, which shows this can see it - then as it is.
 *   sh tests/stress-song-way.sh          (zig cc; any C11 compiler with threads will do)
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../sfx-forge/song_way.h"
#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
#define THREAD_FN(name) static DWORD WINAPI name(LPVOID arg)
#define THREAD_END return 0
static thread_t thread_go(LPTHREAD_START_ROUTINE f) { return CreateThread(0, 0, f, 0, 0, 0); }
static void thread_wait(thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
static double seconds(void) { return (double)GetTickCount64() / 1000.0; }
#else
#include <pthread.h>
#include <time.h>
typedef pthread_t thread_t;
#define THREAD_FN(name) static void *name(void *arg)
#define THREAD_END return 0
static thread_t thread_go(void *(*f)(void *)) { pthread_t t; pthread_create(&t, 0, f, 0); return t; }
static void thread_wait(thread_t t) { pthread_join(t, 0); }
static double seconds(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
#endif

static sfx_song editors, players;                /* the control flow's song, the audio flow's */
static song_way way;
static int tight, at_first_sight;                 /* 1: only the hand-over; 1: a message is worked when seen, its song here or not */
static int in_place;                             /* 1: as before - the editor writes the player's song itself */
static uint32_t stop_now, editor_done;
static double run_seconds = 3.0;
static uint64_t edits, puts_back, takes, takes_back, looks, torn, went_back;
/* the messages: one writer, one reader, as the firmware's queue */
#define QN 64
static struct { uint32_t need, edit; } q[QN];
static uint32_t qw, qr;
static uint64_t sent, worked, waited, too_early, owed_puts;

static uint32_t rnd(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static uint32_t check_of(const sfx_song *s)      /* FNV over the patterns' values (not the bytes between them) */
{
  uint32_t h = 2166136261u;
  for (int p = 0; p < SFX_PATTERNS; p++) {
    const sfx_pattern *pt = &s->pat[p];
    h = (h ^ pt->grid) * 16777619u;
    for (int i = 0; i < SFX_STEPS; i++) {
      h = (h ^ pt->on[i]) * 16777619u;
      for (int t = 0; t < SFX_TRACKS; t++) { h = (h ^ pt->note[t][i]) * 16777619u; h = (h ^ pt->vel[t][i]) * 16777619u; h = (h ^ pt->len[t][i]) * 16777619u; }
      for (int l = 0; l < SFX_LANES; l++) h = (h ^ pt->aut[l][i]) * 16777619u;
    }
  }
  return h;
}
/* the check and the edit's number, kept in the song's head (the arrangement's bytes: any value will do here) */
static void sign(sfx_song *s, uint32_t n) { uint32_t c = check_of(s); memcpy(s->arr, &c, 4); memcpy(s->arr + 4, &n, 4); }
static int whole(const sfx_song *s, uint32_t *n)
{
  uint32_t c; memcpy(&c, s->arr, 4); memcpy(n, s->arr + 4, 4);
  if (c != check_of(s)) return 0;
  for (int p = 0; p < SFX_PATTERNS; p++) {       /* the cells past the grid's are empty: the grid fits the cells */
    if (s->pat[p].grid >= SFX_GRIDS) return 0;
    for (int i = SFX_BAR * sfx_div(s->pat[p].grid); i < SFX_STEPS; i++) if (s->pat[p].on[i]) return 0;
  }
  return 1;
}

static void fill(sfx_pattern *p, uint32_t *r)    /* notes on every step of its grid */
{
  int g = SFX_BAR * sfx_div(p->grid);
  sfx_pattern_clear(p);
  for (int i = 0; i < g; i++) { p->on[i] = (uint16_t)rnd(r); for (int t = 0; t < SFX_TRACKS; t++) { p->note[t][i] = (uint8_t)(rnd(r) & 127); p->vel[t][i] = (uint8_t)(rnd(r) & 7); } for (int l = 0; l < SFX_LANES; l++) p->aut[l][i] = (uint8_t)(rnd(r) & 127); }
}

THREAD_FN(editor)
{
  (void)arg;
  uint32_t r = 0x5F3759DFu, n = 0;
  sfx_song *s = in_place ? &players : &editors;
  double t0 = seconds();
  while (seconds() - t0 < run_seconds) {
    for (int burst = 0; burst < 64; burst++) {
      sfx_pattern *p = &s->pat[rnd(&r) % SFX_PATTERNS];
      if (tight) { p->on[rnd(&r) % SFX_BAR] ^= 1; ++n; memcpy(s->arr + 4, &n, 4); }
      else switch (rnd(&r) % 6) {
      case 0: sfx_pattern_regrid(p, (p->grid + 1) % SFX_GRIDS); break;                       /* SHIFT + F6 (and round to 1/16) */
      case 1: sfx_pattern_regrid(p, (p->grid + SFX_GRIDS - 1) % SFX_GRIDS); break;           /* SHIFT + F5 */
      case 2: s->pat[rnd(&r) % SFX_PATTERNS] = *p; break;                                    /* COPY */
      case 3: fill(p, &r); break;                                                            /* (what a take or FILL leaves) */
      case 4: sfx_pattern_clear(p); break;                                                   /* CLR PAT */
      default: if (rnd(&r) % 16 == 0) { sfx_song_init(s); for (int k = 0; k < SFX_PATTERNS; k++) { s->pat[k].grid = (uint8_t)(rnd(&r) % SFX_GRIDS); fill(&s->pat[k], &r); } }   /* a song comes in */
               else p->on[rnd(&r) % (SFX_BAR * sfx_div(p->grid))] ^= (uint16_t)(1u << (rnd(&r) % SFX_TRACKS)); break;   /* one step */
      }
      if (!tight) sign(s, ++n);
      edits++;
      if (in_place) continue;
      if (burst % 4 == 3) {                                        /* a message that needs this edit in the player's song */
        uint32_t need = song_way_need(&way, s), w = qw;
        if (way.owed) owed_puts++;                                 /* (stepped back every time it tried: the next put is the one) */
        if (w - __atomic_load_n(&qr, __ATOMIC_SEQ_CST) < QN) { q[w % QN].need = need; q[w % QN].edit = n; __atomic_store_n(&qw, w + 1, __ATOMIC_SEQ_CST); sent++; }
      }
      else if (!song_way_put(&way, s)) puts_back++;                /* (stepped back: the next put takes this edit along) */
    }
  }
  if (!in_place) while (!song_way_put(&way, s)) { }                 /* the last edit: until the player is not taking (the firmware comes again with its next event; here that is now) */
  __atomic_store_n(&editor_done, 1, __ATOMIC_SEQ_CST);
  THREAD_END;
}

THREAD_FN(player)
{
  (void)arg;
  uint32_t last = 0, n, seen = 0;
  while (!__atomic_load_n(&stop_now, __ATOMIC_SEQ_CST)) {
    uint32_t w = __atomic_load_n(&qw, __ATOMIC_SEQ_CST), k = qr;   /* (the queue first, then the song: as vibe_on_audio) */
    if (!in_place) {
      uint32_t due = __atomic_load_n(&way.n, __ATOMIC_SEQ_CST) != way.took;
      if (song_way_take(&way, &players)) takes++; else if (due) takes_back++;
    }
    looks++;
    if (tight) { memcpy(&n, players.arr + 4, 4); if (n < last) went_back++; last = n; }
    else if (!whole(&players, &n)) torn++;       /* what a block would have played */
    else { if (n < last) went_back++; last = n; }
    for (; k != w; k++) {                        /* the messages, in their order: each only in its song, or a later one */
      if (!song_way_has(&way, q[k % QN].need) && !at_first_sight) { if (seen != k + 1) { waited++; seen = k + 1; } break; }
      if (n < q[k % QN].edit) too_early++;
      worked++;
    }
    __atomic_store_n(&qr, k, __ATOMIC_SEQ_CST);
  }
  THREAD_END;
}

static int run(int mode)                         /* 0 song_way, 1 in place, 2 only the hand-over and no message waits, 3 only the hand-over */
{
  uint32_t r = 7;
  in_place = mode == 1; tight = mode >= 2; at_first_sight = mode == 2; stop_now = editor_done = 0;
  edits = puts_back = takes = takes_back = looks = torn = went_back = 0;
  qw = qr = 0; sent = worked = waited = too_early = owed_puts = 0;
  memset(&editors, 0, sizeof editors); memset(&players, 0, sizeof players);
  sfx_song_init(&editors);
  for (int k = 0; k < SFX_PATTERNS; k++) fill(&editors.pat[k], &r);
  sign(&editors, 0);
  song_way_start(&way, &editors, &players);
  thread_t a = thread_go(player), b = thread_go(editor);
  thread_wait(b);
  if (!in_place) while (__atomic_load_n(&way.n, __ATOMIC_SEQ_CST) != __atomic_load_n(&way.took, __ATOMIC_SEQ_CST) || __atomic_load_n(&qr, __ATOMIC_SEQ_CST) != qw) { }   /* the player takes the last one, and works the last message */
  __atomic_store_n(&stop_now, 1, __ATOMIC_SEQ_CST);
  thread_wait(a);
  if (in_place) {
    printf("  in place (as before):  %llu edits, the player looked %llu times and saw %llu half-made songs\n", (unsigned long long)edits, (unsigned long long)looks, (unsigned long long)torn);
    return 0;
  }
  int arrived = memcmp(&editors, &players, sizeof editors) == 0;
  if (tight) {
    printf("  %s %llu edits, stepped back: the editor %llu times, the player %llu times; %llu messages, %llu of them worked in an OLDER song%s\n",
           at_first_sight ? "hand-over only, no message waits (as before):" : "hand-over only:       ", (unsigned long long)edits, (unsigned long long)puts_back, (unsigned long long)takes_back, (unsigned long long)sent, (unsigned long long)too_early,
           at_first_sight ? "" : went_back || !arrived || worked != sent ? " - and an edit went back, or the last did not arrive, or a message was left" : "; no edit went back, the last arrived");
    return at_first_sight ? 0 : went_back || !arrived || too_early || worked != sent || !sent;
  }
  printf("  song_way:              %llu edits, %llu taken (the rest went along with a later one), the player looked %llu times and saw %llu half-made songs, %llu that went back\n",
         (unsigned long long)edits, (unsigned long long)takes, (unsigned long long)looks, (unsigned long long)torn, (unsigned long long)went_back);
  printf("                         stepped back for the other: the editor %llu times, the player %llu times; the last edit %s\n",
         (unsigned long long)puts_back, (unsigned long long)takes_back, arrived ? "arrived: the player's song is the editor's, byte for byte" : "DID NOT ARRIVE");
  printf("                         %llu messages, each to find its edit in the player's song: %llu worked, %llu of them in an OLDER song; %llu waited a look or more for their song (%llu sent while the player was taking)\n",
         (unsigned long long)sent, (unsigned long long)worked, (unsigned long long)too_early, (unsigned long long)waited, (unsigned long long)owed_puts);
  return torn || went_back || !arrived || !takes || too_early || worked != sent || !sent;
}

int main(int argc, char **argv)
{
  if (argc > 1) { int s = 0; for (const char *c = argv[1]; *c >= '0' && *c <= '9'; c++) s = s * 10 + (*c - '0'); if (s > 0) run_seconds = (double)s; }
  printf("two threads, %.0f s each way (a song: %u bytes; song_way: %u)\n", run_seconds, (unsigned)sizeof(sfx_song), (unsigned)sizeof(song_way));
  run(1);
  int failed = run(0);
  run(2);
  failed |= run(3);
  printf(failed ? "song way stress FAILED\n" : "song way stress PASSED\n");
  return failed;
}
