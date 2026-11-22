/* fw_protocol.h – the numbers of the link between the device and a game (or a DAW). A part of sfx-forge.c.
 *
 * Holds:  the sizes everything else counts in (SLOTS, PADS); the SysEx messages; the identity; the song's pieces;
 *         the CCs and notes.
 * Needs:  core/sfx_song.h (the sizes of a song), FW_MAJOR / FW_MINOR / FW_PATCH (sfx-forge.c).
 * Read:   this file for what goes over the wire, fw_link.h for the code that sends and takes it, and the README
 *         ("The link") for the same from the game's side. The Unity package has its own copy of these numbers
 *         (unity/com.catsplat.sfxforge/Runtime/Core/SfxProtocol.cs): a number changed here must change there.
 */

#define SLOTS SFX_TRACKS /* the sounds of a kit: 16 */
#define PADS  16         /* the pads, and the steps they show at a time */

/* ---- the SysEx messages ---------------------------------------------------------------------------------------- */
/* Every message is  F0 7D 56 42 <message> ... F7  (SX_HEAD below, then the message's number).
 * "in" is to the device, "out" from it.
 *
 *   01 DUMP        out (and in)   F0 7D 56 42 01 <slot> <28 patch bytes> F7
 *                                 in: taken as LOAD is - unless it is our own, come straight back (fw_link.h)
 *   02 LOAD        in             F0 7D 56 42 02 <slot> <28 patch bytes> F7
 *                                 slot 0-15: the pad of the kit that is selected.
 *                                 slot 16-31 (1.3.1): pad 1-16 of the MUSIC kit, whatever is selected - a song's sounds
 *   03 TEXT        in             F0 7D 56 42 03 <ASCII, up to 24> F7
 *   04 REQUEST     in             F0 7D 56 42 04 <slot> F7             -> answers with a dump
 *   05 SONG        both ways      (1.6.0) F0 7D 56 42 05 <piece lo> <piece hi> <format> <pieces lo> <pieces hi>
 *                                         <1..240 song bytes> F7, in any order.
 *                                 in, also the shape of before: F0 7D 56 42 05 <piece> 39 <up to 200 bytes> F7
 *                                 (a format-5 song)
 *   06 REQ_SONG    in             F0 7D 56 42 06 F7                    -> answers with 16 dumps and the song
 *   07 REQ_IDENT   in (1.4.0)     F0 7D 56 42 07 F7 (these 6 bytes, no others) -> answers with the identity
 *   08 IDENT       out            F0 7D 56 42 08 <18 bytes, each 0..127> F7. One that comes in is left alone
 */
/* how every message of ours begins: SysEx, the id for private use, 'V', 'B' */
#define SX_HEAD 0xF0, 0x7D, 0x56, 0x42

#define SX_DUMP      0x01
#define SX_LOAD      0x02
#define SX_TEXT      0x03
#define SX_REQUEST   0x04
#define SX_SONG      0x05
#define SX_REQ_SONG  0x06
#define SX_REQ_IDENT 0x07
#define SX_IDENT     0x08

/* ---- the identity ---------------------------------------------------------------------------------------------- */
/* What this device is and what it holds, so that a game can ask before it sends. Fields are only ever added at the
   end: a reader takes the first 18 and ignores what follows. A firmware before 1.4.0 never answers.
   The 18 bytes are written out in fw_link.h (ident_pump); the version among them is FW_MAJOR.. in sfx-forge.c. */

/* 0 unknown, 1 VIBE FM (the SDK's host: emulator, plugin, VIBE Box - not told apart from here) */
#define ID_HARDWARE 1
/* what SFX FORGE 1.x offers on VIBE FM */
#define ID_PROFILE 1
/* bit0 pads, bit1 encoders for the game (LINK), bit2 takes a song's sounds (LOAD 16-31), bit3 takes telemetry */
#define ID_FLAGS 0x0F

/* ---- the song in pieces (SX_SONG) ------------------------------------------------------------------------------ */
/* the song message up to 1.5.0: 39 pieces of 200 bytes (the last 99), a 7-bit piece number */
#define SONG_OLD_PIECE  200
#define SONG_OLD_PIECES 39
/* from 1.6.0: pieces of 240 bytes (the last what is left), 14-bit piece number and count */
#define SONG_PIECE      240
#define SONG_PIECES_MAX 160

_Static_assert(SFX_SONG_MAX <= SONG_PIECES_MAX * SONG_PIECE && SFX_SONG_BYTES_V5 <= SONG_OLD_PIECES * SONG_OLD_PIECE,
               "a song fits its pieces");

/* ---- CCs and notes --------------------------------------------------------------------------------------------- */
/* in, channel 2: CC 10..17. 10-13 are the telemetry (pitch, cutoff, timbre, level), 14 the intensity, 15 cues a
   pattern; 16 and 17 do nothing yet: they are kept, and the link's light comes on */
#define CC_TELEMETRY 10
#define CC_ENCODERS  20 /* out, channel 1: CC 20..25 */
#define NOTE_PADS    36 /* pads <-> notes 36..51 */
