/* SPDX-License-Identifier: GPL-3.0-or-later
 * song_way.h – a song from the flow that edits it to the flow that plays it, whole.
 *
 * WHAT IT IS FOR
 *
 * With two flows (VIBE_PARALLEL_AUDIO) the player must never read a pattern while it is rewritten: a grid change moves
 * every cell of it, a COPY or an UNDO replaces it, a song that comes in replaces all eight. So there are three songs:
 *   the editor's   only the control flow touches it: every edit is made there, at once, whole or not
 *   the player's   only the audio flow touches it (sfx_player.song)
 *   the one here   on its way from the first to the second
 * song_way_put (control flow, after anything that may have changed the song) copies what differs into the song here.
 * song_way_take (audio flow, at the start of a block) copies that on into the player's. So the player has the song as
 * it was before an edit or as it is after it, changed between two blocks - never half of one.
 *
 * HOW THE TWO FLOWS KEEP OFF EACH OTHER
 *
 * The song here is the only thing both flows touch that is bigger than one value. They keep off each other with two
 * flags, each written by one flow only: `writes` is 1 while the control flow writes the song here, `reads` while the
 * audio flow reads it. Each sets its own flag FIRST and looks at the other's THEN (both sequentially consistent: of two
 * that do so at the same moment at least one sees the other). Whoever sees the other's flag steps back and comes again:
 * the audio with its next block, the control flow with its next event or draw. Nobody waits, nobody spins, and with the
 * audio not running at all (a host may do that) the control flow goes on putting: the song here is then simply the
 * newest, for whenever a block comes.
 *
 * A MESSAGE THAT NEEDS ITS SONG
 *
 * What must find the song as it was when it was sent (a start, a cue, the mode: the firmware's queue carries them) says
 * which put it needs: song_way_need puts and gives that number, the message carries it, and the audio leaves the
 * message - and all behind it - in the queue until song_way_has says that put, or a later one, is the player's song.
 * That holds however the two flows fall: the audio stepped back in that block, or the control flow did when it sent
 * (then the number is the next put's, and the next put counts even should nothing differ by then).
 * A host that calls everything in one flow never sees a flag set: every put is taken by the next block.
 *
 * WHAT IS HERE, AND WHO CALLS IT
 *
 *   control flow   song_way_start (once, before the audio runs), song_way_put, song_way_hand (put, a few tries),
 *                  song_way_need (put, for a message)
 *   audio flow     song_way_take (at the start of a block), song_way_has (for a message)
 * In the firmware: fw_state.h (song_hand), fw_audio.h (send, vibe_on_audio), fw_events.h (vibe_on_init), and the three
 * calls at the end of sfx-forge.c. This header stands alone - it needs core/sfx_song.h only - so that two real threads
 * can be set on it: tests/stress-song-way.c. A change here is not proved by the firmware's other tests, which run both
 * flows in one thread: run that one.
 */
#ifndef SONG_WAY_H
#define SONG_WAY_H
#include <stddef.h>
#include "../core/sfx_song.h"

/* what is compared and copied by itself: each pattern, and the rest of a song (tempo, mix, lanes, arrangement) */
#define SONG_WAY_PARTS (SFX_PATTERNS + 1)
_Static_assert(offsetof(sfx_song, pat) == 0 && SONG_WAY_PARTS <= 32, "the patterns first, a bit a part");

typedef struct {
  /* on its way. Written by the control flow, read by the audio flow - never at the same time */
  sfx_song song;
  /* control flow: how often it has put one here */
  uint32_t n;
  /* control flow: the parts put since the audio last took, a bit each (read by the audio as it takes) */
  uint32_t parts;
  /* control flow: 1 while it writes `song` */
  uint32_t writes;
  /* control flow: 1 = a message waits for the next put (song_way_need stepped back): that put counts, whatever
     differs */
  uint32_t owed;
  /* audio flow: the n it has taken */
  uint32_t took;
  /* audio flow: 1 while it reads `song` */
  uint32_t reads;
} song_way;

/* where part k begins in a song, in bytes (k = SONG_WAY_PARTS: where the song ends) */
static inline int song_way__at(int k) {
  // clang-format off
  return k < SFX_PATTERNS  ? k * (int)sizeof(sfx_pattern)
       : k == SFX_PATTERNS ? (int)sizeof(((sfx_song *)0)->pat)
       :                     (int)sizeof(sfx_song);
  // clang-format on
}

/* n bytes of a and b are the same (eight bytes at a time: it runs after every event) */
static inline int song_way__same(const uint8_t *a, const uint8_t *b, int n) {
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t x;
    uint64_t y;
    __builtin_memcpy(&x, a + i, 8);
    __builtin_memcpy(&y, b + i, 8);
    if (x != y) {
      return 0;
    }
  }
  for (; i < n; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

/* before the second flow runs (vibe_on_init): the editor's song is the player's and the one here */
static inline void song_way_start(song_way *w, const sfx_song *editors, sfx_song *players) {
  /* (every byte, also those between the values: put compares bytes) */
  __builtin_memcpy(&w->song, editors, sizeof(sfx_song));
  __builtin_memcpy(players, editors, sizeof(sfx_song));
  /* (the count goes on: a message sent before this needs no put that is still to come) */
  w->took = w->n;
  w->reads = 0;
  w->owed = 0;
  w->writes = 0;
  w->parts = 0;
}

/* Control flow: the editor's song as it is now goes on its way. Call it when the song is whole again after anything
   that may have changed it - it costs one compare when nothing has. 1 = on its way (or nothing differs), 0 = the audio
   is taking the one before at this very moment: call again later (the editor's song keeps the edit; nothing is
   lost). */
static inline int song_way_put(song_way *w, const sfx_song *editors) {
  const uint8_t *a = (const uint8_t *)editors;
  uint8_t *b = (uint8_t *)&w->song;

  /* which parts differ from the song here */
  uint32_t parts = 0;
  for (int k = 0; k < SONG_WAY_PARTS; k++) {
    if (!song_way__same(a + song_way__at(k), b + song_way__at(k), song_way__at(k + 1) - song_way__at(k))) {
      parts |= 1u << k;
    }
  }
  if (!parts && !w->owed) {
    return 1;
  }

  /* our flag first, then a look at the audio's: it reads right now - step back */
  __atomic_store_n(&w->writes, 1, __ATOMIC_SEQ_CST);
  if (__atomic_load_n(&w->reads, __ATOMIC_SEQ_CST)) {
    __atomic_store_n(&w->writes, 0, __ATOMIC_SEQ_CST);
    return 0;
  }

  /* those parts into the song here */
  for (int k = 0; k < SONG_WAY_PARTS; k++) {
    if (parts & (1u << k)) {
      __builtin_memcpy(b + song_way__at(k), a + song_way__at(k), (size_t)(song_way__at(k + 1) - song_way__at(k)));
    }
  }
  /* the one before was not taken yet: its parts go along */
  if (__atomic_load_n(&w->took, __ATOMIC_SEQ_CST) != w->n) {
    parts |= w->parts;
  }
  w->parts = parts;

  /* one more put; our flag down */
  __atomic_store_n(&w->n, w->n + 1, __ATOMIC_SEQ_CST);
  __atomic_store_n(&w->writes, 0, __ATOMIC_SEQ_CST);
  w->owed = 0;
  return 1;
}

/* The same, and should the audio be taking right then, again: its take is one copy, over before the next compare is
   (a few tries, never a wait: 0 is still possible, and then the next event or draw brings it). */
#define SONG_WAY_TRIES 4
static inline int song_way_hand(song_way *w, const sfx_song *editors) {
  for (int i = 0; i < SONG_WAY_TRIES; i++) {
    if (song_way_put(w, editors)) {
      return 1;
    }
  }
  return 0;
}

/* Control flow, for a message that must find the song as it is now: puts it, and returns the put the audio must have
   taken before it may work that message (song_way_has). Stepped back: that is the next put, which then counts for
   sure. */
static inline uint32_t song_way_need(song_way *w, const sfx_song *editors) {
  if (song_way_hand(w, editors)) {
    return w->n;
  }
  w->owed = 1;
  return w->n + 1;
}

/* Audio flow, after song_way_take: 1 = the player's song is that put, or a later one */
static inline int song_way_has(const song_way *w, uint32_t need) {
  return (int32_t)(w->took - need) >= 0;
}

/* Audio flow, at the start of a block, before the player renders: 1 = the player's song is another one now (all of an
   edit, or of several). 0 = nothing came - or the control flow is writing the next one this very moment: the player
   keeps the song it has for this block, whole, and takes the new one with the next. */
static inline int song_way_take(song_way *w, sfx_song *players) {
  int got = 0;
  /* nothing was put since the last take */
  if (__atomic_load_n(&w->n, __ATOMIC_ACQUIRE) == w->took) {
    return 0;
  }
  /* our flag first, then a look at the control flow's */
  __atomic_store_n(&w->reads, 1, __ATOMIC_SEQ_CST);
  if (!__atomic_load_n(&w->writes, __ATOMIC_SEQ_CST)) {
    /* n: the put this is. parts: what it changed - those go into the player's song */
    uint32_t n = __atomic_load_n(&w->n, __ATOMIC_SEQ_CST);
    uint32_t parts = w->parts;
    const uint8_t *a = (const uint8_t *)&w->song;
    uint8_t *b = (uint8_t *)players;
    for (int k = 0; k < SONG_WAY_PARTS; k++) {
      if (parts & (1u << k)) {
        __builtin_memcpy(b + song_way__at(k), a + song_way__at(k), (size_t)(song_way__at(k + 1) - song_way__at(k)));
      }
    }
    __atomic_store_n(&w->took, n, __ATOMIC_SEQ_CST);
    got = 1;
  }
  __atomic_store_n(&w->reads, 0, __ATOMIC_SEQ_CST);
  return got;
}

#endif
