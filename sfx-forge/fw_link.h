/* fw_link.h – SysEx out and in: sounds, the song, the identity. A part of sfx-forge.c.
 *
 * Holds:  out   sysex_dump_k / sysex_dump (a sound), export_pump (16 sounds, then the song in pieces), ident_pump (the
 *               identity), dump_pump (a sound that was asked for), out_pump (all three, in order)
 *         in    song_piece (a song, piece by piece) with rx_seen / rx_again / rx_clear / rx_whole;
 *               sysex_in (every SysEx message that arrives)
 * Needs:  fw_protocol.h (the messages), fw_state.h (song_rx..., sent_..., export_at), fw_edit.h (say, changed,
 *         keep_undo, undo_mark), core (pack / unpack).
 * Read:   two things shape all of it.
 *         1  The host takes ONE SysEx a block (vibe_midi_send_sysex says no to a second). So nothing is sent where it
 *            is asked for: it is noted (ident_due, export_at, dump_due) and out_pump - called from every draw, every
 *            event and every request - sends what the host will take, in a fixed order.
 *         2  What the device sends can come straight back (a virtual MIDI cable's port is an input as well as an
 *            output). So what went out last is remembered (sent_dump, song_tx), and the same bytes coming in right
 *            after are our own echo, not a message.
 */

/* ---- out ------------------------------------------------------------------------------------------------------- */

/* n bytes of a and b are the same */
static int same(const uint8_t *a, const uint8_t *b, int n) {
  for (int i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

/* DUMP: the sound on a pad of kit k. 0 = sent (and remembered, to know its echo by), -1 = the host took none now */
static int sysex_dump_k(int k, int slot) {
  uint8_t b[7 + SFX_PATCH_BYTES] = {SX_HEAD, SX_DUMP, (uint8_t)slot};
  sfx_patch_pack(&kits[k][slot], b + 6);
  b[6 + SFX_PATCH_BYTES] = 0xF7;
  if (vibe_midi_send_sysex(b, sizeof b)) {
    return -1;
  }
  for (int i = 0; i < SFX_PATCH_BYTES; i++) {
    sent_dump[slot][i] = b[6 + i];
  }
  sent_dump_any |= 1u << slot;
  sent_dump_t = vibe_frames();
  return 0;
}

static int sysex_dump(int slot) {
  return sysex_dump_k(kit, slot);
}

/* the 16 sounds, then the song in pieces. The host takes one SysEx a block, so this is called again and again (from
   every draw and every event) until all are out. export_at: 0-15 the sound to send next, 16 + k piece k of the song,
   -1 nothing. (Two tries a call: the first may be refused.) */
static void export_pump(void) {
  static uint8_t b[11 + SONG_PIECE];
  for (int guard = 0; guard < 2 && export_at >= 0; guard++) {
    if (export_at < SLOTS) {
      /* (the song's sounds: the MUSIC kit) */
      if (sysex_dump_k(KIT_MUSIC, export_at)) {
        return;
      }
    } else {
      int k = export_at - SLOTS;
      /* the first piece: the song is packed now, and all its pieces are cut from these bytes */
      if (k == 0) {
        song_tx_n = sfx_song_pack(&SONG, song_tx);
        song_tx_K = (song_tx_n + SONG_PIECE - 1) / SONG_PIECE;
      }
      /* n: this piece's bytes (the last piece: what is left) */
      int n = k == song_tx_K - 1 ? song_tx_n - k * SONG_PIECE : SONG_PIECE;
      b[0] = 0xF0;
      b[1] = 0x7D;
      b[2] = 0x56;
      b[3] = 0x42;
      b[4] = SX_SONG;
      /* the piece's number, low and high */
      b[5] = (uint8_t)(k & 0x7F);
      b[6] = (uint8_t)(k >> 7);
      /* the song's format: its first byte */
      b[7] = song_tx[0];
      /* how many pieces, low and high */
      b[8] = (uint8_t)(song_tx_K & 0x7F);
      b[9] = (uint8_t)(song_tx_K >> 7);
      for (int i = 0; i < n; i++) {
        b[10 + i] = song_tx[k * SONG_PIECE + i];
      }
      b[10 + n] = 0xF7;
      if (vibe_midi_send_sysex(b, 11 + n)) {
        return;
      }
      sent_song_any = 1;
      sent_song_t = vibe_frames();
    }
    /* the next one; after the song's last piece: done */
    if (++export_at > SLOTS && export_at >= SLOTS + song_tx_K) {
      export_at = -1;
      say("SONG SENT");
    }
  }
}

/* The answer to SX_REQ_IDENT. The host takes one SysEx a block: an answer that does not fit now (a second request in
   the same block, a song going out) waits, and goes with the next draw or event. ident_due: the answers still owed. */
static int ident_due;

static void ident_pump(void) {
  // clang-format off
  static const uint8_t b[6 + 18] = {
    SX_HEAD, SX_IDENT,
    ID_HARDWARE,                     /*  1  the hardware */
    FW_MAJOR, FW_MINOR, FW_PATCH,    /*  2-4  the firmware's version */
    ID_PROFILE,                      /*  5 */
    SFX_PATCH_VERSION,               /*  6  the format of a patch */
    SFX_SONG_VERSION,                /*  7  the format of a song */
    SLOTS,                           /*  8  the sounds of a kit */
    2,                               /*  9  kits */
    SFX_PATTERNS,                    /* 10 */
    SFX_STEPS,                       /* 11  the most steps a pattern holds */
    SFX_LANES,                       /* 12 */
    SFX_ARR_MAX,                     /* 13  the bars of an arrangement */
    SFX_PLAYER_VOICES,               /* 14 */
    SFX_ENGINE_COUNT,                /* 15 */
    SFX_WT_TABLES,                   /* 16 */
    SFX_P_COUNT,                     /* 17  the values of a sound */
    ID_FLAGS,                        /* 18 */
    0xF7 };
  // clang-format on
  while (ident_due > 0 && !vibe_midi_send_sysex(b, sizeof b)) {
    ident_due--;
  }
}

/* A pad's dump that was asked for (SX_REQUEST, or EXPORT on the device) waits in the same way: without this an identity
   that holds the host's one SysEx of the block would make the dump vanish. One bit for each pad; the dump is made of
   the kit that is selected when it goes. While the sounds and the song go out it waits for them to end, so that a
   pad of the selected kit does not land among the song's sounds. */
static uint32_t dump_due;

static void dump_pump(void) {
  if (export_at >= 0) {
    return;
  }
  for (int i = 0; i < SLOTS && dump_due; i++) {
    if (dump_due >> i & 1) {
      if (sysex_dump(i)) {
        return;
      }
      dump_due &= ~(1u << i);
    }
  }
}

/* all that waits to go out, in this order; called from every draw, every event and every request */
static void out_pump(void) {
  if (ident_due) {
    ident_pump();
  }
  if (export_at >= 0) {
    export_pump();
  }
  if (dump_due) {
    dump_pump();
  }
}

/* ---- in: a song, piece by piece -------------------------------------------------------------------------------- */
/* A song from the game, piece by piece. Every piece is put where it belongs, and the song is taken when all are here.
   So the pieces may come in any order: a MIDI path can swap two neighbours (a browser's Web MIDI output does, now and
   then). And more than once: a host can deliver everything twice (the emulator does), and a piece that is here already,
   byte for byte, is left alone. A piece that is here already with OTHER bytes belongs to another song: that begins a
   new transfer - as does a piece that names another count of pieces, another format, or has the other shape. A
   transfer that broke off is forgotten after two seconds without a piece.

   After a song is taken its pieces stay here, so that its own late twins do not begin a transfer. What comes then is
   such a twin - or a piece of another song of as many pieces, which may differ from the one taken in one late piece
   only (a tempo is in the first, a step of pattern H in the last). So from then on the pieces are counted again: once
   every piece has come again and one of them had other bytes, that is the next song, and it is taken. A twin is told
   by a count: the same bytes in a piece that has come fewer times than the others (a host that delivers twice still
   owes it).

   Two shapes (b[6] tells them apart: 39 is the old one's count of pieces, the new one has the piece number's high bits
   there, 0 or 1):
     old  F0 7D 56 42 05 <k> 39 <200 bytes, the last piece 99> F7            - a format-5 song, 7699 bytes
     new  F0 7D 56 42 05 <k lo> <k hi> <format> <K lo> <K hi> <1..240> F7    - any length up to 160 pieces
   (format: the song's first byte, in every piece - a first piece that begins with another byte is no piece)

   The variables are in fw_state.h (song_rx...). In short: song_rx holds the bytes, song_rx_seen a bit for each piece
   that is here, song_rx_n how often each came; after a song was taken (song_rx_done) song_rx_again and song_rx_n2
   count the same anew, and song_rx_other says that one of the pieces that came since had other bytes. */

/* piece k is here */
static int rx_seen(int k) {
  return (int)(song_rx_seen[k >> 5] >> (k & 31) & 1);
}

/* piece k came again since the song was taken */
static int rx_again(int k) {
  return (int)(song_rx_again[k >> 5] >> (k & 31) & 1);
}

/* the count since the song was taken begins anew */
static void rx_again_clear(void) {
  for (unsigned i = 0; i < sizeof song_rx_again / sizeof song_rx_again[0]; i++) {
    song_rx_again[i] = 0;
  }
  for (int i = 0; i < SONG_PIECES_MAX; i++) {
    song_rx_n2[i] = 0;
  }
  song_rx_other = 0;
}

/* a new transfer begins: no piece is here */
static void rx_clear(void) {
  for (unsigned i = 0; i < sizeof song_rx_seen / sizeof song_rx_seen[0]; i++) {
    song_rx_seen[i] = 0;
  }
  for (int i = 0; i < SONG_PIECES_MAX; i++) {
    song_rx_n[i] = 0;
  }
  song_rx_done = 0;
  rx_again_clear();
}

/* all its pieces are here (take 0: the song that is here already, sent once more) */
static void rx_whole(int take) {
  song_rx_done = 1;
  song_rx_most = 0;
  for (int i = 0; i < song_rx_K; i++) {
    if (song_rx_n[i] > song_rx_most) {
      song_rx_most = song_rx_n[i];
    }
  }
  rx_again_clear();
  if (!take) {
    return;
  }
  undo_mark("LOAD", 1);
  // clang-format off
  say(!sfx_song_unpack(&SONG, song_rx, (song_rx_K - 1) * song_rx_size + song_rx_last) ? "SONG LOADED"
      : song_rx[0] > SFX_SONG_VERSION                                                 ? "NEWER SONG: UPDATE"
      :                                                                                 "BAD SONG");
  // clang-format on
  view_sync();
}

/* One SX_SONG message: b[0..n-1], F0 to F7. now: the audio's clock, in frames. In this order:
     1  the piece is read in its shape (old or new) and refused if anything about it is out of range - or if it is our
        own echo
     2  a transfer that is not this piece's (too old, another count, size or format) is forgotten
     3  after a song was taken: twins are counted, and a piece with other bytes begins the count for the next song
     4  else: the piece is put in its place, and when all are here the song is taken (rx_whole) */
static void song_piece(const uint8_t *b, int n, uint64_t now) {
  /* k: this piece's number. K: how many the song has. size: a full piece's bytes. len: this piece's bytes, at `in` */
  int k;
  int K;
  int size;
  int len;
  int format;
  const uint8_t *in;

  /* 1. the two shapes */
  if (b[6] == SONG_OLD_PIECES) {
    k = b[5];
    K = SONG_OLD_PIECES;
    size = SONG_OLD_PIECE;
    len = n - 8;
    in = b + 7;
    format = 0;
    if (k >= K || len != (k == K - 1 ? SFX_SONG_BYTES_V5 - k * size : size)) {
      return;
    }
  } else if (b[6] <= 1 && n >= 12) {
    k = b[5] | b[6] << 7;
    K = b[8] | b[9] << 7;
    size = SONG_PIECE;
    len = n - 11;
    in = b + 10;
    format = b[7];
    if (b[9] > 1 || K < 1 || K > SONG_PIECES_MAX || k >= K || len < 1 || len > size || (k < K - 1 && len != size) ||
        format < 1) {
      return;
    }
    /* a later firmware's song: nothing of it is collected */
    if (format > SFX_SONG_VERSION) {
      if (k == 0) {
        say("NEWER SONG: UPDATE");
      }
      return;
    }
    /* (the format its pieces name is not the song's own) */
    if (k == 0 && in[0] != format) {
      return;
    }
    /* our own echo: the piece that just went out, come back while the song is going out or just after */
    if (sent_song_any && (export_at >= SLOTS || now - sent_song_t < ECHO_FRAMES) && K == song_tx_K &&
        len == (k == K - 1 ? song_tx_n - k * size : size) && same(in, song_tx + k * size, len)) {
      return;
    }
  } else {
    return;
  }

  /* 2. at: where this piece belongs. here: it is there already, byte for byte */
  uint8_t *at = song_rx + k * size;
  if (now - song_rx_t > (uint64_t)SR * 2 || K != song_rx_K || size != song_rx_size || format != song_rx_format) {
    rx_clear();
  }
  song_rx_t = now;
  int here = rx_seen(k) && len == (k == K - 1 ? song_rx_last : size) && same(in, at, len);

  /* 3. the song before was taken: its twin, or a piece of the next */
  if (song_rx_done) {
    /* the twin this piece still owed */
    if (here && !song_rx_other && song_rx_n[k] < song_rx_most) {
      song_rx_n[k]++;
      return;
    }
    if (!here) {
      /* (a second piece with other bytes in the same place: yet another song begins) */
      if (rx_again(k)) {
        rx_again_clear();
      }
      for (int i = 0; i < len; i++) {
        at[i] = in[i];
      }
      if (k == K - 1) {
        song_rx_last = len;
      }
      song_rx_other = 1;
    }
    song_rx_again[k >> 5] |= 1u << (k & 31);
    if (song_rx_n2[k] < 255) {
      song_rx_n2[k]++;
    }
    for (int i = 0; i < K; i++) {
      if (!rx_again(i)) {
        return;
      }
    }
    int other = song_rx_other;
    for (int i = 0; i < K; i++) {
      song_rx_n[i] = song_rx_n2[i];
    }
    /* (every piece again and all the same: the same song, sent once more - nothing to take) */
    rx_whole(other);
    return;
  }

  /* 4. a transfer that runs. A piece that is here already: the same bytes are counted, other bytes begin anew */
  if (rx_seen(k)) {
    if (here) {
      if (song_rx_n[k] < 255) {
        song_rx_n[k]++;
      }
      return;
    }
    rx_clear();
  }
  song_rx_K = K;
  song_rx_size = size;
  song_rx_format = format;
  for (int i = 0; i < len; i++) {
    at[i] = in[i];
  }
  song_rx_seen[k >> 5] |= 1u << (k & 31);
  song_rx_n[k] = 1;
  if (k == K - 1) {
    song_rx_last = len;
  }
  for (int i = 0; i < K; i++) {
    if (!rx_seen(i)) {
      return;
    }
  }
  rx_whole(1);
}

/* ---- in: every SysEx message ----------------------------------------------------------------------------------- */
/* b[0..n-1], F0 to F7. Whatever is not ours (another head, no F7) is left alone. In this order: the identity and its
   request; our own echo of a dump; then one branch for each message (fw_protocol.h): LOAD to the MUSIC kit, LOAD or
   DUMP to the selected kit, TEXT, REQUEST, REQ_SONG, SONG. */
static void sysex_in(const uint8_t *b, int n) {
  if (n < 6 || b[0] != 0xF0 || b[1] != 0x7D || b[2] != 0x56 || b[3] != 0x42 || b[n - 1] != 0xF7) {
    return;
  }
  /* an identity (our own, echoed) is nobody's request; a request is 6 bytes */
  if (b[4] == SX_IDENT || (b[4] == SX_REQ_IDENT && n != 6)) {
    return;
  }
  linked = 1;
  if (b[4] == SX_REQ_IDENT) {
    if (ident_due < 4) {
      ident_due++;
    }
    out_pump();
    return;
  }
  uint64_t now = vibe_frames();

  /* Our own echo. A virtual MIDI cable's port is an input as well as an output, and a host that listens on every input
     (the emulator does) hands back what was just sent. A dump that comes in while the sounds and the song are going
     out, or within a tenth of a second after a dump, and that is byte for byte what went out for that slot, is that:
     taking it would put the song's sounds into the kit that is selected. Another patch at that moment is taken. */
  if (b[4] == SX_DUMP && n == 7 + SFX_PATCH_BYTES && b[5] < SLOTS && (sent_dump_any >> b[5] & 1) &&
      (export_at >= 0 || now - sent_dump_t < ECHO_FRAMES) && same(b + 6, sent_dump[b[5]], SFX_PATCH_BYTES)) {
    return;
  }

  if (b[4] == SX_LOAD && n == 7 + SFX_PATCH_BYTES && b[5] >= SLOTS && b[5] < 2 * SLOTS) {
    /* LOAD, slot 16-31. A song's sound: the MUSIC kit's pad */
    sfx_patch p;
    int slot = b[5] - SLOTS;
    int was = kit;
    if (sfx_patch_unpack(&p, b + 6)) {
      say("BAD PATCH");
      return;
    }
    kit = KIT_MUSIC;
    patches[slot] = p;
    pad_preset[slot] = -1;
    changed(slot);
    kit = was;
    say("SOUND LOADED");
  } else if ((b[4] == SX_LOAD || b[4] == SX_DUMP) && n == 7 + SFX_PATCH_BYTES && b[5] < SLOTS) {
    /* LOAD or DUMP, slot 0-15: onto that pad of the selected kit, which becomes the selected sound (F3 brings the one
       before back) */
    sfx_patch p;
    if (sfx_patch_unpack(&p, b + 6)) {
      say("BAD PATCH");
      return;
    }
    keep_undo(b[5]);
    patches[b[5]] = p;
    pad_preset[b[5]] = -1;
    sel = b[5];
    changed(sel);
    say("PATCH LOADED");
  } else if (b[4] == SX_TEXT) {
    /* TEXT: up to 24 characters for the LINK page; what is not printable becomes a space */
    int k = 0;
    for (int i = 5; i < n - 1 && k < 24; i++) {
      link_text[k++] = b[i] >= 32 && b[i] < 127 ? (char)b[i] : ' ';
    }
    link_text[k] = 0;
  } else if (b[4] == SX_REQUEST && n == 7 && b[5] < SLOTS) {
    dump_due |= 1u << b[5];
    out_pump();
  } else if (b[4] == SX_REQ_SONG) {
    export_at = 0;
    out_pump();
  } else if (b[4] == SX_SONG && n > 8) {
    song_piece(b, n, now);
  }
}
