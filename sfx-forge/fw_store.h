/* fw_store.h – what the device keeps when it is switched off. A part of sfx-forge.c.
 *
 * Holds:  bank_save (F6 SAVE, and the host's own save), save_say, bank_load (at the start).
 * Needs:  fw_state.h (kits, SONG, the undo's variables), fw_edit.h (say), core (sfx_patch_pack / unpack,
 *         sfx_song_pack / unpack), the SDK's store (vibe_store_read / write / size).
 * Read:   the three files.
 *
 *   kits.bin    both kits: 32 patches of 28 bytes (MUSIC's 16, then SFX's), then 32 bytes that say which named sound
 *               each pad holds (its row of PRESETS + 1, 0 = its own). BANK_BYTES in all.
 *   song6.bin   the packed song (format 6, core/sfx_song.h) and two bytes more - a check of the song.bin that lay
 *               beside it when it was written (0: there was none).
 *   song.bin    the file of the firmwares up to 1.5.0 (format 5): it is still read and never written again, so a
 *               device put back to an older firmware finds its song. And should that older firmware save a song there
 *               and the device come forward again: its song.bin no longer fits the check, so it is the later one and
 *               is loaded over song6.bin - the display says OLDER FIRMWARE'S SONG then, and the song of song6.bin is
 *               one F2 away (until the next SAVE, which keeps what is there).
 */

#define BANK_BYTES (2 * SLOTS * (SFX_PATCH_BYTES + 1))

/* a song as it is read and written: the packed song and the two bytes of the check */
static uint8_t store_buf[SFX_SONG_MAX + 2];

/* the check of a file's bytes: 1..16381 (so 0 can mean "there was no file"); it fits two 7-bit bytes */
static int store_check(const uint8_t *b, int n) {
  uint32_t h = 1;
  for (int i = 0; i < n; i++) {
    h = (h * 31 + b[i]) % 16381;
  }
  return 1 + (int)h;
}

/* Both kits and the song into the store. 0 = kept, -1 = the store did not take it */
static int bank_save(void) {
  static uint8_t b[BANK_BYTES];

  /* kits.bin: the patches, then which named sound each pad holds */
  for (int k = 0; k < 2; k++) {
    for (int i = 0; i < SLOTS; i++) {
      sfx_patch_pack(&kits[k][i], b + (k * SLOTS + i) * SFX_PATCH_BYTES);
      b[2 * SLOTS * SFX_PATCH_BYTES + k * SLOTS + i] = (uint8_t)(pad_preset_k[k][i] + 1);
    }
  }
  int failed = vibe_store_write("kits.bin", b, sizeof b) != 0;

  /* c: the check of the song.bin that is there now (0: none) */
  int old = vibe_store_size("song.bin");
  int c = 0;
  if (old >= 1 && old <= SFX_SONG_BYTES_V5 && vibe_store_read("song.bin", store_buf, old) == old) {
    c = store_check(store_buf, old);
  }

  /* song6.bin: the song, then that check */
  int n = sfx_song_pack(&SONG, store_buf);
  store_buf[n] = (uint8_t)(c & 0x7F);
  store_buf[n + 1] = (uint8_t)(c >> 7);
  if (vibe_store_write("song6.bin", store_buf, n + 2)) {
    failed = 1;
  }
  return failed ? -1 : 0;
}

/* F6 SAVE: it saves, and the display says how it went */
static void save_say(void) {
  say(bank_save() ? "NOT SAVED: STORE FULL" : "SAVED");
}

/* At the start, after factory(): what the store holds replaces the factory's sounds and song - whatever is missing or
   broken there leaves them as they are. */
static void bank_load(void) {
  static uint8_t b[BANK_BYTES];

  /* kits.bin. A broken patch keeps its factory sound */
  if (vibe_store_read("kits.bin", b, sizeof b) == (int)sizeof b) {
    for (int k = 0; k < 2; k++) {
      for (int i = 0; i < SLOTS; i++) {
        if (!sfx_patch_unpack(&kits[k][i], b + (k * SLOTS + i) * SFX_PATCH_BYTES)) {
          int n = b[2 * SLOTS * SFX_PATCH_BYTES + k * SLOTS + i];
          pad_preset_k[k][i] = (int8_t)(n >= 1 && n <= PRESET_COUNT ? n - 1 : -1);
        }
      }
    }
  }

  /* song6.bin. c: the check song6.bin kept, -1 while none was taken */
  int n6 = vibe_store_size("song6.bin");
  int c = -1;
  if (n6 >= SFX_SONG_MIN + 2 && n6 <= SFX_SONG_MAX + 2 && vibe_store_read("song6.bin", store_buf, n6) == n6 &&
      !sfx_song_unpack(&SONG, store_buf, n6 - 2)) {
    c = store_buf[n6 - 2] | store_buf[n6 - 1] << 7;
  }

  /* song.bin: taken when there is no song6.bin, or when it is not the song.bin that song6.bin was written beside */
  int n5 = vibe_store_size("song.bin");
  if (n5 >= 1 && n5 <= SFX_SONG_BYTES_V5 && vibe_store_read("song.bin", store_buf, n5) == n5 &&
      (c < 0 || store_check(store_buf, n5) != c)) {
    /* (the song of song6.bin: one F2 away) */
    if (c >= 0) {
      undo_song = SONG;
    }
    /* (one that does not unpack leaves the song as it is) */
    if (!sfx_song_unpack(&SONG, store_buf, n5) && c >= 0) {
      undo_has = 1;
      undo_whole = 1;
      undo_redo = 0;
      undo_run = 0;
      undo_pat = -1;
      undo_what = "OLDER SONG";
      /* both files held a song, and the one of the firmware before is the later: it is said, and F2 on SEQ brings the
         other */
      say("OLDER FIRMWARE'S SONG");
    }
  }
}
