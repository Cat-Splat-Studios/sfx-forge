// Music: the song the firmware sent in the test performance, played here by the same player - its
// steps, layers, patterns, the mixer, automation, the arrangement.
// Part of the test program: Program.cs has Main, which calls these in order.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using CatSplat.SfxForge;

static partial class Tests
{
    // music: the song the firmware sent (SEND on its SONG page), played here by the same player
    static void Music()
    {
        if (msgs != null)
        {
            var sounds = new SfxPatch[16];
            var arriving = new SfxSongTransfer();
            int seen = 0;
            int songPieces = 0;
            bool whole = false;
            foreach (
                var msg in msgs.Skip(msgs.FindLastIndex(b => b[0] == 0xF0 && b[4] == SfxProtocol.Dump && b[5] == 0))
            )
            {
                if (SfxProtocol.TryParsePatch(msg, out int slot, out var patch))
                {
                    sounds[slot] = patch;
                    seen |= 1 << slot;
                }
                else if (SfxSongTransfer.IsPiece(msg))
                {
                    songPieces++;
                    if (arriving.Take(msg))
                    {
                        whole = true;
                    }
                }
            }

            byte[] song = arriving.Song ?? new byte[0];
            Ok(
                whole
                    && seen == 0xFFFF
                    && song.Length > 211
                    && song[0] == 6
                    && songPieces == (song.Length + 239) / 240
                    && SfxPackedSong.Format(song) == 6,
                $"the firmware's song arrives whole: 16 sounds and {songPieces} pieces of up to 240 bytes - {song.Length} bytes of format 6"
            );

            // how a song's pieces are taken: in any order, twins left alone, another song's piece begins anew - in
            // the message of firmware 1.6 (the song the firmware just sent) and in the one before (the format-5
            // fixture: 39 pieces of 200)
            foreach (
                var (shape, first, build, size) in new (string, byte[], Func<byte[], byte[][]>, int)[]
                {
                    ("39 pieces of 200", fix5, SfxProtocol.BuildSong, 200),
                    ("pieces of 240", song, SfxProtocol.BuildSongPieces, 240),
                }
            )
            {
                var pieces = build(first);
                var songTwo = (byte[])first.Clone();

                // (another song: every piece differs)
                for (int k = 0; k < pieces.Length; k++)
                {
                    int at = Math.Min(k * size + 50, first.Length - 1);
                    songTwo[at] = (byte)(songTwo[at] ^ 1);
                }

                var piecesTwo = SfxProtocol.BuildPieces(songTwo);
                if (size == 200)
                {
                    piecesTwo = SfxProtocol.BuildSong(songTwo);
                }

                int n = pieces.Length;
                var order = Enumerable.Range(0, n).ToArray();
                (order[n - 3], order[n - 2]) = (order[n - 2], order[n - 3]);
                (order[2], order[3]) = (order[3], order[2]);
                Ok(
                    pieces.Length == (first.Length + size - 1) / size
                        && pieces.All(m => m.Length <= (size == 200 ? 208 : 251))
                        && (size != 200 || (pieces.Length == 39 && pieces[38].Length == 8 + 99)),
                    $"{shape}: a song of {first.Length} bytes is {pieces.Length} messages"
                );

                var t = new SfxSongTransfer();
                int done = 0;
                foreach (int k in order)
                {
                    if (t.Take(pieces[k]))
                    {
                        done++;
                    }
                }

                Ok(
                    done == 1 && t.Song != null && t.Song.SequenceEqual(first),
                    $"{shape}: a song whose pieces come out of order arrives whole all the same"
                );

                t = new SfxSongTransfer();
                done = 0;
                foreach (int k in order.Reverse())
                {
                    if (t.Take(pieces[k]))
                    {
                        done++;
                    }

                    if (t.Take(pieces[k]))
                    {
                        done++;
                    }
                }

                Ok(
                    done == 1 && t.Song.SequenceEqual(first),
                    $"{shape}: last piece first and every piece twice, it arrives whole, once"
                );
                foreach (int k in order)
                {
                    if (t.Take(piecesTwo[k]))
                    {
                        done++;
                    }

                    if (t.Take(piecesTwo[k]))
                    {
                        done++;
                    }
                }

                Ok(
                    done == 2 && t.Song.SequenceEqual(songTwo),
                    $"{shape}: and the next song, straight after and doubled too, arrives whole and is the next song (no piece of the first in it)"
                );

                t = new SfxSongTransfer();
                done = 0;

                // a song that lost a piece …
                foreach (int k in Enumerable.Range(0, n).Where(k => k != 5))
                {
                    t.Take(pieces[k]);
                }

                bool early = false;
                bool nothingYet = t.Song == null;

                // … and another, whole, after it
                foreach (int k in Enumerable.Range(0, n))
                {
                    bool pieceMadeItWhole = t.Take(piecesTwo[k]);
                    if (pieceMadeItWhole)
                    {
                        done++;
                        early |= k != n - 1;
                    }
                }

                Ok(
                    nothingYet && done == 1 && !early && t.Song.SequenceEqual(songTwo),
                    $"{shape}: after a song that lost a piece, the next is whole only with its own last piece, and is the next song"
                );

                t = new SfxSongTransfer();
                done = 0;
                foreach (int k in order.Take(n / 2))
                {
                    t.Take(pieces[k]);
                }

                // (what the link does after two seconds without a piece:
                // without it the halves of two songs would count as one)
                t.Reset();
                foreach (int k in order.Reverse())
                {
                    if (t.Take(piecesTwo[k]))
                    {
                        done++;
                    }
                }

                Ok(
                    done == 1 && t.Song != null && t.Song.SequenceEqual(songTwo),
                    $"{shape}: after a transfer that broke off and was forgotten, another song sent last piece first arrives whole"
                );

                // a second song soon after the first (no Reset between) that differs
                // from it in its last piece only - then in its first only
                var lateOnly = (byte[])first.Clone();
                lateOnly[first.Length - 1] = (byte)(lateOnly[first.Length - 1] ^ 1);
                var earlyOnly = (byte[])lateOnly.Clone();
                earlyOnly[3] = (byte)(earlyOnly[3] ^ 1);
                var piecesLate = size == 200 ? SfxProtocol.BuildSong(lateOnly) : SfxProtocol.BuildPieces(lateOnly);
                var piecesEarly = size == 200 ? SfxProtocol.BuildSong(earlyOnly) : SfxProtocol.BuildPieces(earlyOnly);
                foreach (bool twice in new[] { false, true })
                {
                    t = new SfxSongTransfer();
                    done = 0;
                    string got = "";
                    void Send(byte[][] ps)
                    {
                        foreach (var m in ps)
                        {
                            if (t.Take(m))
                            {
                                done++;
                                got +=
                                    t.Song.SequenceEqual(first) ? "1"
                                    : t.Song.SequenceEqual(lateOnly) ? "2"
                                    : t.Song.SequenceEqual(earlyOnly) ? "3"
                                    : "?";
                            }

                            if (twice && t.Take(m))
                            {
                                done++;
                                got += "!";
                            }
                        }
                    }

                    Send(pieces);
                    Send(piecesLate);
                    Send(piecesLate);
                    Send(piecesEarly);
                    Send(pieces.Take(n / 2).ToArray());
                    Send(piecesLate);
                    Ok(
                        got == "1232" && t.Song.SequenceEqual(lateOnly),
                        $"{shape}{(twice ? ", every piece twice" : "")}: a song, one that differs in its last piece only, that one again, one that differs in its first piece only, half of the very first and the second once more: each is whole once, and never a mix (whole: {got})"
                    );
                }

                var cut = pieces[3].Take(pieces[3].Length - 5).Append((byte)0xF7).ToArray();
                t = new SfxSongTransfer();
                Ok(
                    !t.Take(cut) && !SfxSongTransfer.IsPiece(cut) && t.Song == null,
                    $"{shape}: a piece of the wrong length is not taken"
                );
                t.Take(pieces[0]);
                t.Reset();
                done = 0;
                foreach (int k in order.Skip(1))
                {
                    if (t.Take(pieces[k]))
                    {
                        done++;
                    }
                }

                Ok(done == 0 && t.Song == null, $"{shape}: Reset forgets what was collected");
            }

            // the two shapes into the same class, the longest song, a single piece, a last
            // piece of one byte, a count that cannot be, another format mid-way
            {
                var t = new SfxSongTransfer();
                int done = 0;
                foreach (var m in SfxProtocol.BuildSongPieces(song).Take(5))
                {
                    t.Take(m);
                }

                foreach (var m in SfxProtocol.BuildSong(fix5))
                {
                    if (t.Take(m))
                    {
                        done++;
                    }
                }

                bool oldAfterNew = done == 1 && t.Song.SequenceEqual(fix5);
                foreach (var m in SfxProtocol.BuildSongPieces(song))
                {
                    if (t.Take(m))
                    {
                        done++;
                    }
                }

                Ok(
                    oldAfterNew && done == 2 && t.Song.SequenceEqual(song),
                    "the message of before into the same collector, in the middle of one in the new shape: the "
                        + "format-5 song arrives whole; then the new one does"
                );

                var longest = new byte[SfxProtocol.SongBytesMax];
                longest[0] = 6;
                for (int i = 1; i < longest.Length; i++)
                {
                    longest[i] = (byte)(i * 11 % 128);
                }

                var many = SfxProtocol.BuildPieces(longest);
                t = new SfxSongTransfer();
                done = 0;
                foreach (var m in many.Reverse())
                {
                    if (t.Take(m))
                    {
                        done++;
                    }
                }

                Ok(
                    many.Length == 160
                        && many.Length == SfxProtocol.SongPiecesMax
                        && many.All(m => m.Length <= 251)
                        && many[128][5] == 0
                        && many[128][6] == 1
                        && done == 1
                        && t.Song.SequenceEqual(longest),
                    "38 355 bytes are 160 pieces, none over 251 bytes; in reverse they arrive whole"
                );

                var tiny = new byte[211];
                tiny[0] = 6;
                var one = SfxProtocol.BuildPieces(tiny);
                var oneOver = new byte[241];
                oneOver[0] = 6;
                oneOver[240] = 77;
                var twoPieces = SfxProtocol.BuildPieces(oneOver);
                t = new SfxSongTransfer();
                bool single = one.Length == 1 && t.Take(one[0]) && t.Song.SequenceEqual(tiny) && !t.Take(one[0]);
                bool oneByte =
                    twoPieces.Length == 2
                    && twoPieces[1].Length == 12
                    && !t.Take(twoPieces[1])
                    && t.Song == null
                    && t.Take(twoPieces[0])
                    && t.Song.SequenceEqual(oneOver);
                Ok(
                    single && oneByte,
                    "a song in a single piece arrives with it (its twin is left alone); a last piece of one byte is "
                        + "a piece"
                );

                // (a first piece begins with the song's format)
                byte[] Piece(int k, int K, int format, int bytes)
                {
                    var m = new byte[11 + bytes];

                    // csharpier-ignore
                    new byte[]
                    {
                        0xF0, 0x7D, 0x56, 0x42, 5, (byte)(k & 127), (byte)(k >> 7), (byte)format, (byte)(K & 127),
                        (byte)(K >> 7),
                    }.CopyTo(m, 0);
                    if (k == 0 && bytes > 0)
                    {
                        m[10] = (byte)format;
                    }

                    m[m.Length - 1] = 0xF7;
                    return m;
                }

                t = new SfxSongTransfer();
                long spentBefore = GC.GetAllocatedBytesForCurrentThread();
                bool none =
                    !t.Take(Piece(0, 161, 6, 240))
                    && !t.Take(Piece(0, 16383, 6, 240))
                    && !t.Take(Piece(160, 16383, 6, 240))
                    && !t.Take(Piece(0, 0, 6, 240))
                    && !t.Take(Piece(3, 3, 6, 240))
                    && !t.Take(Piece(0, 2, 6, 100))
                    && !t.Take(Piece(0, 1, 6, 241))
                    && !t.Take(Piece(0, 1, 6, 0))
                    && !t.Take(Piece(0, 2, 0, 240))
                    && !t.Take(Piece(0, 2, 7, 240))
                    && t.Song == null;
                long spent = GC.GetAllocatedBytesForCurrentThread() - spentBefore;
                Ok(
                    none
                        && spent < 16000
                        && !SfxSongTransfer.IsPiece(Piece(0, 161, 6, 240))
                        && SfxSongTransfer.IsPiece(Piece(0, 160, 6, 240))
                        && SfxSongTransfer.IsPiece(Piece(159, 160, 6, 1)),
                    $"161 pieces, 16 383 pieces, none, a piece past the count, a short piece that is not the last, one too long, an empty one, format 0, format 7: not pieces, and nothing is set aside for them ({spent} bytes for the ten messages themselves)"
                );

                var a6 = SfxProtocol.BuildSongPieces(song);
                var as5 = (byte[])song.Clone();
                as5[0] = 5;
                var a5 = SfxProtocol.BuildPieces(as5);
                t = new SfxSongTransfer();
                done = 0;
                foreach (var m in a6.Take(a6.Length - 1))
                {
                    t.Take(m);
                }

                // the last piece, but it names another format: a new transfer
                if (t.Take(a5[a5.Length - 1]))
                {
                    done++;
                }

                bool anew = done == 0 && t.Song == null;
                foreach (var m in a5.Take(a5.Length - 1))
                {
                    if (t.Take(m))
                    {
                        done++;
                    }
                }

                Ok(
                    anew && done == 1 && t.Song.SequenceEqual(as5),
                    "a piece that names another format begins a new transfer: the pieces before it are not mixed "
                        + "into it"
                );

                // the pieces name format 5, the song begins with 6
                var lying = SfxProtocol
                    .BuildSongPieces(song)
                    .Select(m =>
                    {
                        var c = (byte[])m.Clone();
                        c[7] = 5;
                        return c;
                    })
                    .ToArray();
                t = new SfxSongTransfer();
                done = 0;
                foreach (var m in lying)
                {
                    if (t.Take(m))
                    {
                        done++;
                    }
                }

                Ok(
                    done == 0 && t.Song == null && !SfxSongTransfer.IsPiece(lying[0]),
                    "a first piece whose bytes begin with another format than its pieces name is no piece: the song "
                        + "does not arrive"
                );

                var sound = SfxProtocol.BuildLoadSongSound(3, sounds[3]);
                Ok(
                    sound[4] == SfxProtocol.Load
                        && sound[5] == 19
                        && sound.Length == 7 + SfxPatch.Bytes
                        && SfxPatch.TryUnpack(sound, 6, out var soundBack)
                        && soundBack.Pack().SequenceEqual(sounds[3].Pack()),
                    "a song's sound goes out as LOAD for slot 16 + sound (the MUSIC kit's pad)"
                );
                // the reader of the old shape alone still reads it
#pragma warning disable 618
                var got = new byte[SfxProtocol.SongBytesV5];
                ulong mask = 0;
                done = 0;
                foreach (var m in SfxProtocol.BuildSong(fix5).Reverse())
                {
                    if (SfxProtocol.TryTakeSongChunk(m, got, ref mask))
                    {
                        done++;
                    }

                    if (SfxProtocol.TryTakeSongChunk(m, got, ref mask))
                    {
                        done++;
                    }
                }

                Ok(
                    done == 1 && got.SequenceEqual(fix5) && !SfxProtocol.TryTakeSongChunk(a6[0], got, ref mask),
                    "TryTakeSongChunk (kept for those who call it) still takes the 39 pieces of before, in reverse "
                        + "and doubled, and none of the new shape"
                );
#pragma warning restore 618
            }

            // through the link: a song in the message of firmware 1.6 arrives with its own length
            using var link = new SfxLink();
            byte[] came = null;
            int times = 0;
            link.SongReceived += (s, _) =>
            {
                came = s;
                times++;
            };
            foreach (var m in SfxProtocol.BuildSongPieces(song).Reverse())
            {
                link.Take(m);
                link.Take(m);
            }

            Ok(
                times == 1 && came != null && came.SequenceEqual(song),
                $"the link hands a song in the new shape on, whole and once, with its own length ({(came == null ? 0 : came.Length)} bytes)"
            );

            IntPtr pl = Marshal.AllocHGlobal(SfxNative.PlayerSize);
            SfxNative.PlayerInit(pl, 48000f);
            bool loaded = SfxNative.PlayerSetSong(pl, song);
            for (int i = 0; i < 16; i++)
            {
                loaded &= SfxNative.PlayerSetPatch(pl, i, sounds[i].Pack());
            }

            var wrong = (byte[])song.Clone();
            wrong[0] = 9;
            Ok(
                loaded && !SfxNative.PlayerSetSong(pl, wrong),
                "the native player takes the song (and refuses a broken one)"
            );

            float[] Play(float intensity, int blocks, Action<int> each = null)
            {
                var all = new float[blocks * 256];
                var bl = new float[256];
                var br = new float[256];
                var none = SfxMods.None;
                SfxNative.PlayerSetLive(pl, ref none, intensity);
                for (int b = 0; b < blocks; b++)
                {
                    Array.Clear(bl, 0, 256);
                    Array.Clear(br, 0, 256);
                    each?.Invoke(b);
                    SfxNative.PlayerRender(pl, bl, br, 256);
                    Array.Copy(bl, 0, all, b * 256, 256);
                }

                return all;
            }

            // at intensity 0: the same as the device played from 21.0 s of the
            // test performance (one bar, with the step added there)
            SfxNative.PlayerStart(pl, 0);
            var low = Play(0f, 750);
            if (deviceAudioLeft != null)
            {
                double c = Correlate(low, deviceAudioLeft, 21 * 48000, 48000 * 2, out int off);
                Ok(
                    c > 0.97,
                    $"the native player and the Vibe play the song alike (correlation {c:0.0000} over 2 s, offset {off} samples)"
                );
            }

            var st = new int[4];
            SfxNative.PlayerState(pl, st);
            Ok(
                st[0] == 1 && st[3] == 1 && st[2] == 1,
                $"after 4 s at 120 BPM it is in bar 2, on pattern B of the chain (bar {st[3] + 1}, pattern {(char)('A' + st[2])})"
            );

            // intensity brings the layers in; a cue changes the pattern on the bar line
            SfxNative.PlayerStart(pl, 0);
            var high = Play(1f, 375);
            Ok(
                Rms(high, 0.26, 0.49) > Rms(low, 0.26, 0.49) * 2,
                $"intensity 1 plays more layers than intensity 0 ({Rms(low, 0.26, 0.49):0.000} -> {Rms(high, 0.26, 0.49):0.000} between two kicks)"
            );
            SfxNative.PlayerStart(pl, 0);
            int changedAt = -1;
            Play(1f, 600, b =>
            {
                if (b == 100)
                {
                    SfxNative.PlayerCue(pl, 5);
                }

                SfxNative.PlayerState(pl, st);
                if (st[2] == 5 && changedAt < 0)
                {
                    changedAt = b;
                }
            });
            Ok(
                changedAt > 372 && changedAt <= 377,
                $"a cue at 0.5 s changes the pattern on the bar line at 2.0 s (block {changedAt} of 256 samples)"
            );
            SfxNative.PlayerStop(pl);

            // the loop flag: the pattern repeats until a cue; a cued one starts
            // on the bar line and stays; off, the arrangement goes on
            Ok(SfxNative.PlayerSetLoop(pl, true), "the library has the loop flag (sfxn_player_set_loop)");
            SfxNative.PlayerStart(pl, 0);
            Play(0f, 750);
            SfxNative.PlayerState(pl, st);
            Ok(
                st[0] == 1 && st[3] == 1 && st[2] == 0,
                $"loop on: after 4 s it is in bar 2 and still on pattern A (bar {st[3] + 1}, pattern {(char)('A' + st[2])})"
            );
            SfxNative.PlayerStart(pl, 0);
            int loopChangedAt = -1;
            int loopLeft = -1;
            int atBlock1200 = -1;
            int afterOff = -1;
            int barOff = -1;
            Play(1f, 1700, b =>
            {
                if (b == 100)
                {
                    SfxNative.PlayerCue(pl, 5);
                }

                SfxNative.PlayerState(pl, st);
                if (st[2] == 5 && loopChangedAt < 0)
                {
                    loopChangedAt = b;
                }

                if (loopChangedAt >= 0 && b < 1200 && st[2] != 5 && loopLeft < 0)
                {
                    loopLeft = b;
                }

                if (b == 1200)
                {
                    atBlock1200 = st[2];
                    barOff = st[3];
                    SfxNative.PlayerSetLoop(pl, false);
                }

                // the pattern of the first bar after it was switched off
                if (b > 1200 && st[3] != barOff && afterOff < 0)
                {
                    afterOff = st[2];
                }
            });
            Ok(
                loopChangedAt >= 373 && loopChangedAt <= 377,
                $"loop on: a cue at 0.5 s changes the pattern on the bar line at 2.0 s (block {loopChangedAt})"
            );
            Ok(
                loopLeft < 0 && atBlock1200 == 5,
                $"and that pattern stays, bar after bar: still F three bars later (pattern {(char)('A' + atBlock1200)})"
            );
            Ok(
                afterOff == 0,
                $"loop off while it plays: on in the arrangement from F's bar - its last, so the top comes next (pattern {(char)('A' + afterOff)})"
            );
            var before = new int[4];
            SfxNative.PlayerState(pl, before);
            SfxNative.PlayerSetLoop(pl, false);
            SfxNative.PlayerState(pl, st);
            Ok(st.SequenceEqual(before), "switching it off a second time changes nothing");
            SfxNative.PlayerStop(pl);

            // a sound as a note: A4 is 440 Hz whatever the sound's own FREQ says
            // let everything ring out
            for (int i = 0; i < 60; i++)
            {
                Play(1f, 10);
            }

            SfxNative.PlayerNoteOn(pl, 6, 1, 1f, 69);
            var tone = Play(1f, 60);
            Ok(
                Math.Abs(Pitch(tone, 0.05, 0.2) - 440) < 6,
                $"a sound played as the note A4 is in tune ({Pitch(tone, 0.05, 0.2):0} Hz)"
            );

            // the band of the MUSIC kit, measured (nobody here can listen):
            // the drums are drums, the FM instruments are in tune
            float[] One(int slot, int note = 0, float hold = 0.3f)
            {
                SfxNative.Bake(sounds[slot], out var x, out _, 48000, 1f, note, 0f, hold);
                return x;
            }

            // zero crossings a second
            double Bright(float[] x, double t0, double t1) => Cycles(x, t0, t1) / (t1 - t0);
            var kick = One(2);
            var snare = One(1);
            var hat = One(12);
            var bass = One(6, 45);
            var keys = One(7, 69);
            Ok(
                Pitch(kick, 0.12, 0.25) < 110 && Rms(kick, 0.0, 0.05) > 0.15 && kick.Length < 48000,
                $"KICK: a low drum ({Pitch(kick, 0.12, 0.25):0} Hz after its fall, {kick.Length / 48000f:0.00} s)"
            );
            Ok(
                Bright(snare, 0.0, 0.1) > 1500 && Rms(snare, 0.4, 0.5) < Rms(snare, 0.0, 0.1) * 0.1,
                $"SNARE: noise over a tone ({Bright(snare, 0.0, 0.1):0} crossings a second, {snare.Length / 48000f:0.00} s)"
            );
            Ok(
                Bright(hat, 0.0, 0.03) > 6000 && hat.Length < 48000 * 0.35,
                $"HI-HAT: a short, bright tick ({Bright(hat, 0.0, 0.03):0} crossings a second, {hat.Length / 48000f:0.00} s)"
            );
            Ok(
                Math.Abs(Pitch(bass, 0.05, 0.25) - 110) < 2 && Rms(bass, 0.0, 0.1) > 0.1,
                $"FM BASS: the note A2 is {Pitch(bass, 0.05, 0.25):0.0} Hz, level {Rms(bass, 0.0, 0.1):0.00}"
            );
            Ok(
                Math.Abs(Pitch(keys, 0.05, 0.25) - 440) < 6 && Rms(keys, 0.0, 0.1) > 0.1,
                $"FM KEYS: the note A4 is {Pitch(keys, 0.05, 0.25):0} Hz, level {Rms(keys, 0.0, 0.1):0.00}"
            );
            Ok(
                Rms(bass, 0.0, 0.03) > Rms(bass, 0.2, 0.3) * 1.5 && Rms(keys, 0.0, 0.05) > Rms(keys, 0.5, 0.6) * 1.5,
                "both are plucked: loudest at the start, then they fall"
            );

            // a chord on the strings: 16 voices, held, then let go
            for (int i = 0; i < 60; i++)
            {
                Play(1f, 10);
            }

            int[] chord = { 45, 52, 57, 60, 64, 69, 72, 76 };
            foreach (int n in chord)
            {
                SfxNative.PlayerNoteOn(pl, 15, 100 + n, 0.8f, n);
            }

            var held = Play(1f, 300);
            foreach (int n in chord)
            {
                SfxNative.PlayerNoteOff(pl, 100 + n);
            }

            var after = Play(1f, 900);
            Ok(
                sounds[15].engine == SfxEngine.Wavetable
                    && Rms(held, 1.2, 1.6) > 0.05
                    && held.All(x => !float.IsNaN(x))
                    && Rms(after, 4.0, 4.8) < 0.002,
                $"an 8-note chord on the strings holds ({Rms(held, 1.2, 1.6):0.000}) and dies away after the keys are let go ({Rms(after, 4.0, 4.8):0.0000})"
            );

            // ---- the mixer: it is part of the song, and a game may move it
            for (int i = 0; i < 60; i++)
            {
                Play(1f, 10);
            }

            (double l, double r) Hit(int slot)
            {
                var bl = new float[256];
                var br = new float[256];
                double sl = 0;
                double sr = 0;
                SfxNative.PlayerNoteOn(pl, slot, 1, 1f);
                for (int b = 0; b < 60; b++)
                {
                    Array.Clear(bl, 0, 256);
                    Array.Clear(br, 0, 256);
                    SfxNative.PlayerRender(pl, bl, br, 256);
                    for (int i = 0; i < 256; i++)
                    {
                        sl += bl[i] * bl[i];
                        sr += br[i] * br[i];
                    }
                }

                SfxNative.PlayerNoteOff(pl, 1);
                for (int b = 0; b < 400; b++)
                {
                    SfxNative.PlayerRender(pl, bl, br, 256);
                }

                return (Math.Sqrt(sl / 15360), Math.Sqrt(sr / 15360));
            }

            SfxNative.PlayerSetTrack(pl, 2, 100, 64, 0);
            var full = Hit(2);
            SfxNative.PlayerSetTrack(pl, 2, 50, -1, -1);
            var half = Hit(2);
            Ok(
                Math.Abs(half.l / full.l - 0.5) < 0.02 && Math.Abs(full.l - full.r) < full.l * 0.001,
                $"MIX: a fader at 50 halves a sound ({full.l:0.000} -> {half.l:0.000}), the middle is the same on both sides"
            );
            SfxNative.PlayerSetTrack(pl, 2, 100, 0, -1);
            var leftOnly = Hit(2);
            SfxNative.PlayerSetTrack(pl, 2, -1, 127, -1);
            var rightOnly = Hit(2);
            Ok(
                leftOnly.r < full.l * 0.001
                    && Math.Abs(leftOnly.l - full.l) < full.l * 0.01
                    && rightOnly.l < full.l * 0.001
                    && Math.Abs(rightOnly.r - full.l) < full.l * 0.01,
                $"MIX: pan puts a sound left ({leftOnly.l:0.000} / {leftOnly.r:0.000}) or right ({rightOnly.l:0.000} / {rightOnly.r:0.000})"
            );
            SfxNative.PlayerSetTrack(pl, 2, -1, 64, 1);
            var muted = Hit(2);
            SfxNative.PlayerSetTrack(pl, 2, -1, -1, 0);
            var back = Hit(2);
            Ok(
                muted.l < 1e-6 && muted.r < 1e-6 && back.l > full.l * 0.99,
                $"MIX: a muted sound is silent ({muted.l:0.0000}), and back when the mute goes ({back.l:0.000})"
            );
            SfxNative.PlayerSetMaster(pl, SfxMaster.Drive, 127);
            var driven = Hit(2);
            SfxNative.PlayerSetMaster(pl, SfxMaster.Drive, 0);
            Ok(
                Math.Abs(driven.l - full.l) > full.l * 0.05,
                $"MASTER: DRIVE changes the sum ({full.l:0.000} -> {driven.l:0.000})"
            );
            var mixed = (byte[])song.Clone();

            // sound 3: fader 50, hard right
            mixed[7 + 16 + 2] = 50;
            mixed[7 + 32 + 2] = 127;
            Ok(SfxNative.PlayerSetSong(pl, mixed), "a song with another mix is taken");
            var fromSong = Hit(2);
            Ok(
                fromSong.l < full.l * 0.001 && Math.Abs(fromSong.r / full.l - 0.5) < 0.02,
                $"the mix is part of the song: fader and pan come with it ({fromSong.l:0.000} / {fromSong.r:0.000})"
            );

            // songs from before: format 4 (one lane a sound), 3 (no automation, no arrangement), 2 (no velocity, no
            // length) and 1 (no mixer either). The head is 147 bytes here (66, the arrangement's 33, 24 lanes of
            // 2), 115 in format 4, 66 in formats 2 and 3, 23 in format 1; a track is 3 + 32 bytes here and in
            // format 3, 3 + 48 in format 4, 3 + 16 before. The lanes' points follow the tracks: 16 for each lane of
            // each pattern. (made of the format-5 fixture: the song the firmware sends is format 6 now, with only
            // the rows that are used)
            const int Head = SfxProtocol.SongHead;
            const int Track = 35;
            const int Lanes = Head - 48;
            const int Points = Head + 128 * Track;
            var v3 = new byte[SfxProtocol.SongBytesV3];
            var v2 = new byte[SfxProtocol.SongBytesV2];
            var old = new byte[SfxProtocol.SongBytesV1];
            Array.Copy(mixed, 0, v3, 0, 66);
            Array.Copy(mixed, 0, v2, 0, 66);
            Array.Copy(fix5, 0, old, 0, 23);
            v3[0] = 3;
            v2[0] = 2;
            old[0] = 1;
            for (int k = 0; k < 128; k++)
            {
                Array.Copy(fix5, Head + k * Track, v3, 66 + k * 35, 35);
                Array.Copy(fix5, Head + k * Track, v2, 66 + k * 19, 19);
                Array.Copy(fix5, Head + k * Track, old, 23 + k * 19, 19);
            }

            bool took3 = SfxNative.PlayerSetSong(pl, v3);
            var from3 = Hit(2);
            Ok(
                took3 && from3.l < full.l * 0.001 && Math.Abs(from3.r / full.l - 0.5) < 0.02,
                "a format 3 song still loads, with its mix"
            );
            bool took2 = SfxNative.PlayerSetSong(pl, v2);
            var from2 = Hit(2);
            Ok(
                took2 && from2.l < full.l * 0.001 && Math.Abs(from2.r / full.l - 0.5) < 0.02,
                "a format 2 song still loads, with its mix"
            );
            Ok(
                SfxNative.PlayerSetSong(pl, old) && Math.Abs(Hit(2).l - full.l) < full.l * 0.01,
                "a format 1 song still loads, with every fader as designed"
            );

            // velocity and length: a step of the strings, soft and loud, held for 2 steps and for 8
            double[] Bar(int vel, int len, Action<byte[], int> edit = null)
            {
                // (a format-5 song: its rows stand at fixed places)
                var one = (byte[])fix5.Clone();

                // no lanes, no arrangement, no steps at all …
                for (int i = 66; i < one.Length; i++)
                {
                    one[i] = 0;
                }

                for (int k = 0; k < 128; k++)
                {
                    for (int i = 0; i < 16; i++)
                    {
                        one[Head + k * Track + 19 + i] = 5;
                    }
                }

                // … but pattern A, sound 16, step 1: the note A3
                int at = Head + 15 * Track;
                one[at] = 1;
                one[at + 3] = 57;
                one[at + 19] = (byte)(len << 3 | vel);
                one[6] = 1;
                edit?.Invoke(one, at);
                if (!SfxNative.PlayerSetSong(pl, one))
                {
                    throw new Exception("the test's song was refused");
                }

                for (int i = 0; i < 40; i++)
                {
                    Play(1f, 10);
                }

                SfxNative.PlayerStart(pl, 0);
                var a = Play(1f, 375); // one bar: 2 s
                SfxNative.PlayerStop(pl);
                return new[] { Rms(a, 0.15, 0.24), Rms(a, 0.6, 0.9), Rms(a, 1.75, 1.95) };
            }

            var soft = Bar(1, 8);
            var loud = Bar(7, 8);
            var shortNote = Bar(7, 2);
            var auto = Bar(7, 0);
            Ok(
                loud[0] > soft[0] * 2.5,
                $"VELOCITY: a hard step is louder than a soft one ({soft[0]:0.000} -> {loud[0]:0.000})"
            );
            Ok(
                shortNote[1] < loud[1] * 0.25 && loud[1] > 0.02,
                $"LENGTH: 2 steps is over where 8 steps still sounds ({shortNote[1]:0.000} against {loud[1]:0.000} at 0.6 s)"
            );
            Ok(
                auto[2] > loud[2] * 3,
                $"LENGTH AUTO holds to the bar line, 8 steps ends before it ({auto[2]:0.000} against {loud[2]:0.000} at 1.8 s)"
            );

            // automation: a lane is a sound (+ 1) and a knob in the head, and 16 points (value + 1) for each pattern
            // after the tracks the strings' fader: 100, from step 9 on 20
            var faded = Bar(7, 0, (b, at) =>
            {
                b[Lanes] = 16;
                b[Lanes + 1] = 26;
                b[Points] = 101;
                b[Points + 8] = 21;
            });

            // (a held chord of detuned oscillators is not the same twice:
            // compare how the second half stands to the first)
            double R(double[] x) => x[2] / x[1];
            double plain = R(auto);
            Ok(
                Math.Abs(R(faded) / plain - 0.2) < 0.06,
                $"AUTOMATION of a fader: a point at step 9 takes a held note from {faded[1]:0.000} to {faded[2]:0.000} ({R(faded) / plain:0.00} of what it would be)"
            );

            // the sound's own CUTOFF: 10 from step 9 on
            var dulled = Bar(7, 0, (b, at) =>
            {
                b[Lanes] = 16;
                b[Lanes + 1] = 1 + 6;
                b[Points + 8] = 11;
            });
            Ok(
                R(dulled) < plain * 0.5,
                $"AUTOMATION of a sound's own knob: CUTOFF closes on a held note ({dulled[1]:0.000} -> {dulled[2]:0.000})"
            );

            // two lanes on one sound: its fader …
            var both = Bar(7, 0, (b, at) =>
            {
                b[Lanes] = 16;
                b[Lanes + 1] = 26;
                b[Points] = 101;
                b[Points + 8] = 21;

                // … and its CUTOFF, together
                b[Lanes + 2] = 16;
                b[Lanes + 3] = 1 + 6;
                b[Points + 16 + 8] = 11;
            });

            // (what is left at the end is the reverb's tail)
            Ok(
                R(both) < R(faded) * 0.5 && R(both) < R(dulled) * 1.1,
                $"AUTOMATION of two knobs of one sound at once: quieter and duller than either alone ({R(faded):0.00}, {R(dulled):0.00}, both {R(both):0.00})"
            );

            // a lane of sound 4 moves the master's DRIVE
            var other = Bar(7, 0, (b, at) =>
            {
                b[Lanes] = 4;
                b[Lanes + 1] = 28 + 4;
                b[Points + 8] = 127;
            });
            Ok(
                R(other) > plain * 1.4,
                $"AUTOMATION of the master from any lane: DRIVE comes in at step 9 ({other[1]:0.000} -> {other[2]:0.000})"
            );

            // a lane closes sound 3's CUTOFF …
            Bar(7, 0, (b, at) =>
            {
                b[Lanes] = 3;
                b[Lanes + 1] = 1 + 6;
                b[Points] = 6;
            });

            // (the strings ring out)
            for (int i = 0; i < 80; i++)
            {
                Play(1f, 10);
            }

            // (its fader as when it was measured)
            SfxNative.PlayerSetTrack(pl, 2, 100, 64, 0);
            var untouched = Hit(2);
            Ok(
                Math.Abs(untouched.l / full.l - 1) < 0.01,
                $"and it writes nothing: sound 3 by hand is as it was made ({full.l:0.000} / {untouched.l:0.000})"
            );

            // the arrangement: A C B, a bar each, then from the top – and a cue
            var arranged = (byte[])song.Clone();
            arranged[66] = 3;
            arranged[67] = 0;
            arranged[68] = 2;
            arranged[69] = 1;
            SfxNative.PlayerSetSong(pl, arranged);
            SfxNative.PlayerStart(pl, 0);
            var seen2 = new System.Collections.Generic.List<int>();
            Play(0f, 2250, b =>
            {
                if (b % 375 == 180)
                {
                    SfxNative.PlayerState(pl, st);
                    seen2.Add(st[2]);
                }

                if (b == 1600)
                {
                    SfxNative.PlayerCue(pl, 7);
                }
            });
            Ok(
                seen2.Take(4).SequenceEqual(new[] { 0, 2, 1, 0 }),
                $"the ARRANGEMENT plays its bars in order, then again from the top ({string.Join(" ", seen2.Take(4).Select(x => (char)('A' + x)))})"
            );
            Ok(
                seen2[4] == 2 && seen2[5] == 7,
                $"a cued pattern that is not in the arrangement takes over on the bar line ({(char)('A' + seen2[4])} then {(char)('A' + seen2[5])})"
            );
            seen2.Clear();
            Play(0f, 1125, b =>
            {
                if (b % 375 == 180)
                {
                    SfxNative.PlayerState(pl, st);
                    seen2.Add(st[2]);
                }

                if (b == 400)
                {
                    SfxNative.PlayerCue(pl, 1);
                }
            });
            Ok(
                seen2.SequenceEqual(new[] { 7, 7, 1 }),
                $"and stays until the next cue; a cue to a pattern of the arrangement goes on from that bar ({string.Join(" ", seen2.Select(x => (char)('A' + x)))})"
            );
            SfxNative.PlayerStop(pl);

            // a format 4 song: one lane a sound, its target in the head after the
            // mix, its points at the end of each track (3 + 48 bytes)
            var v4 = new byte[SfxProtocol.SongBytesV4];
            Array.Copy(song, 0, v4, 0, 66);
            Array.Copy(song, 66, v4, 82, 33);
            v4[0] = 4;
            v4[82] = 0;
            v4[6] = 1;
            for (int i = 115; i < v4.Length; i++)
            {
                v4[i] = 0;
            }

            for (int k = 0; k < 128; k++)
            {
                for (int i = 0; i < 16; i++)
                {
                    v4[115 + k * 51 + 19 + i] = 5;
                }
            }

            // the strings' lane moves their fader …
            v4[66 + 15] = 26;
            {
                int at = 115 + 15 * 51;
                v4[at] = 1;
                v4[at + 3] = 57;
                v4[at + 19] = 7;
                v4[at + 35] = 101;
                v4[at + 35 + 8] = 21;
            } // … 100, then 20 from step 9
            bool took4 = SfxNative.PlayerSetSong(pl, v4);
            for (int i = 0; i < 40; i++)
            {
                Play(1f, 10);
            }

            SfxNative.PlayerStart(pl, 0);
            var four = Play(1f, 375);
            SfxNative.PlayerStop(pl);
            double r4 = Rms(four, 1.75, 1.95) / Rms(four, 0.6, 0.9);
            Ok(
                took4 && r4 / plain < 0.45,
                $"a format 4 song still loads, and its one lane a sound still moves its knob ({r4 / plain:0.00} of the level)"
            );
            Marshal.FreeHGlobal(pl);

            // the demo project's song: exactly what the firmware sent
            var demo = new byte[16 * SfxPatch.Bytes + song.Length];
            for (int i = 0; i < 16; i++)
            {
                sounds[i].Pack(demo, i * SfxPatch.Bytes);
            }

            song.CopyTo(demo, 16 * SfxPatch.Bytes);
            File.WriteAllBytes(Path.Combine(root, "out", "demo-song.bytes"), demo);
        }
    }
}
