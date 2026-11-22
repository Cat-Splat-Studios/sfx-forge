// The packed song in all its formats, played by the native engine: the format-5 fixture bit for
// bit, the formats 1 to 4, format 6 and its finer grids, raising and lowering a grid.
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
    // the format-5 song of firmware 1.5.0 (tests/data/DemoSong-format5.bytes):
    // the same samples as before the finer grid
    static void Format5Fixture()
    {
        byte[] fixture = File.ReadAllBytes(Path.Combine(root, "tests", "data", "DemoSong-format5.bytes"));
        fixSounds = fixture.Take(16 * SfxPatch.Bytes).ToArray();
        fix5 = fixture.Skip(16 * SfxPatch.Bytes).ToArray();

        // recorded with the library of firmware 1.5.0
        wantHash = new ulong[] { 0x663B73D61E165BFCUL, 0xAC323BECC54C7335UL, 0x7D5A2816477BE223UL };
        ulong[] got = ThreeHashes(fixSounds, fix5);
        Ok(
            fixture.Length == 8147 && fix5[0] == 5 && got.SequenceEqual(wantHash),
            "a format-5 song renders bit for bit as it did before the finer grid (16 bars, pattern F looped, "
                + "intensity 0.4): "
                + Hex(got)
        );
    }

    // every format this version reads plays, and its format-6 form plays the same
    static void OlderFormats()
    {
        fix6 = SfxNative.SongConvert(fix5);

        // the fixture in the formats 1 to 4 (made just below)
        olderFormats = new List<byte[]>();
        const int Head = SfxProtocol.SongHead;
        const int Row = 35;
        const int Points = Head + 128 * Row;
        var v4 = new byte[SfxProtocol.SongBytesV4];
        var v3 = new byte[SfxProtocol.SongBytesV3];
        var v2 = new byte[SfxProtocol.SongBytesV2];
        var v1 = new byte[SfxProtocol.SongBytesV1];
        Array.Copy(fix5, 0, v3, 0, 66);
        Array.Copy(fix5, 0, v2, 0, 66);
        Array.Copy(fix5, 0, v1, 0, 23);
        Array.Copy(fix5, 0, v4, 0, 66);
        Array.Copy(fix5, 66, v4, 82, 33);
        v4[0] = 4;
        v3[0] = 3;
        v2[0] = 2;
        v1[0] = 1;
        for (int k = 0; k < 128; k++)
        {
            Array.Copy(fix5, Head + k * Row, v4, 115 + k * 51, 35);
            Array.Copy(fix5, Head + k * Row, v3, 66 + k * 35, 35);
            Array.Copy(fix5, Head + k * Row, v2, 66 + k * 19, 19);
            Array.Copy(fix5, Head + k * Row, v1, 23 + k * 19, 19);
        }

        // (format 4 had one lane a sound: the fixture's two lanes - lane 0 moves a
        // knob of sound 7, lane 1 one of sound 16 - become those sounds' own)
        for (int l = 0; l < 2; l++)
        {
            int s = fix5[99 + l * 2] - 1;
            v4[66 + s] = fix5[100 + l * 2];
            for (int p = 0; p < 8; p++)
            {
                for (int i = 0; i < 16; i++)
                {
                    v4[115 + (p * 16 + s) * 51 + 35 + i] = fix5[Points + (p * 24 + l) * 16 + i];
                }
            }
        }

        bool all = true;
        string said = "";
        olderFormats.AddRange(new[] { v1, v2, v3, v4 });
        foreach (var old in new[] { v1, v2, v3, v4, fix5 })
        {
            byte[] six = SfxNative.SongConvert(old);
            ulong a = RenderHash(fixSounds, old, 0, false, 1f, 4);
            ulong b = six == null ? 1 : RenderHash(fixSounds, six, 0, false, 1f, 4);
            bool good =
                six != null
                && SfxPackedSong.Format(old) == old[0]
                && SfxPackedSong.Format(six) == 6
                && a != 0
                && a == b
                && SfxNative.SongConvert(six).SequenceEqual(six);
            all &= good;
            said += $" {old[0]}: {old.Length} -> {(six == null ? 0 : six.Length)} bytes{(good ? "" : " WRONG")};";
        }

        Ok(
            all,
            "formats 1 to 5 play; each as format 6 plays the same samples (4 bars), and converting that "
                + "again changes no byte:"
                + said
        );

        // formats 1 to 3 have no arrangement: as format 6 its length and its 32 bars are 0, whatever stood in memory
        Ok(
            new[] { v1, v2, v3 }.All(old =>
            {
                byte[] six = SfxNative.SongConvert(old);
                return six != null && six.Skip(66).Take(33).All(b => b == 0);
            }),
            "formats 1 to 3 as format 6 have an empty arrangement: bytes 66 to 98 are 0"
        );
        Ok(
            RenderHash(fixSounds, v4, 0, false, 1f, 16) == wantHash[0],
            "the fixture as format 4 (its two lanes as the lanes of their sounds) plays the samples of the "
                + "format-5 one"
        );
    }

    // format 6
    static void Format6()
    {
        byte[] demo6 = File.ReadAllBytes(Path.Combine(root, "unity", "Demo", "Assets", "SfxDemo", "DemoSong.bytes"));
        Ok(
            fix6 != null
                && fix6.Length == 2487
                && fix6[0] == 6
                && demo6.Length == SoundsBytes + 2487
                && demo6.Skip(SoundsBytes).SequenceEqual(fix6)
                && demo6.Take(SoundsBytes).SequenceEqual(fixSounds),
            $"the fixture as format 6 is 2487 bytes ({(fix6 == null ? 0 : fix6.Length)}): byte for byte the song of the demo's DemoSong.bytes, which firmware 1.6.0 sends"
        );
        bool same = true;
        int on = 0;
        int pts = 0;
        for (int p = 0; p < 8; p++)
        {
            same &= SfxPackedSong.Grid(fix6, p) == 16 && SfxPackedSong.Grid(fix5, p) == 16;
            for (int i = -1; i <= 16; i++)
            {
                for (int s = 0; s < 16; s++)
                {
                    same &=
                        SfxPackedSong.StepOn(fix6, p, s, i) == SfxPackedSong.StepOn(fix5, p, s, i)
                        && SfxPackedSong.StepNote(fix6, p, s, i) == SfxPackedSong.StepNote(fix5, p, s, i)
                        && SfxPackedSong.StepVelocity(fix6, p, s, i) == SfxPackedSong.StepVelocity(fix5, p, s, i)
                        && SfxPackedSong.StepLength(fix6, p, s, i) == SfxPackedSong.StepLength(fix5, p, s, i);
                    if (SfxPackedSong.StepOn(fix6, p, s, i))
                    {
                        on++;
                    }
                }

                for (int l = 0; l < 24; l++)
                {
                    same &= SfxPackedSong.LanePoint(fix6, p, l, i) == SfxPackedSong.LanePoint(fix5, p, l, i);
                    if (SfxPackedSong.LanePoint(fix6, p, l, i) >= 0)
                    {
                        pts++;
                    }
                }
            }
        }

        Ok(
            same
                && on > 200
                && pts == 24
                && fix6.Take(SfxProtocol.SongHead).Skip(1).SequenceEqual(fix5.Take(SfxProtocol.SongHead).Skip(1)),
            $"SfxPackedSong reads every step, note, velocity, length and point of it as from the format-5 bytes ({on} steps, {pts} points), and the head is the same"
        );

        // written here by the layout, from what SfxPackedSong reads of the format-5 song: the C packer's bytes
        var steps = new List<(int, int, int, int, int, int)>();
        var points = new List<(int, int, int, int)>();
        for (int p = 0; p < 8; p++)
        {
            for (int i = 0; i < 16; i++)
            {
                for (int s = 0; s < 16; s++)
                {
                    if (SfxPackedSong.StepOn(fix5, p, s, i))
                    {
                        steps.Add(
                            (
                                p,
                                s,
                                i,
                                SfxPackedSong.StepNote(fix5, p, s, i),
                                SfxPackedSong.StepVelocity(fix5, p, s, i),
                                SfxPackedSong.StepLength(fix5, p, s, i)
                            )
                        );
                    }
                }

                for (int l = 0; l < 24; l++)
                {
                    if (SfxPackedSong.LanePoint(fix5, p, l, i) >= 0)
                    {
                        points.Add((p, l, i, SfxPackedSong.LanePoint(fix5, p, l, i)));
                    }
                }
            }
        }

        Ok(
            Pack6(fix5, new int[8], steps, points).SequenceEqual(fix6),
            "the layout written out by hand in this test gives the same 2487 bytes"
        );

        var empty5 = (byte[])fix5.Clone();
        for (int i = SfxProtocol.SongHead; i < empty5.Length; i++)
        {
            empty5[i] = 0;
        }

        for (int k = 0; k < 128; k++)
        {
            for (int i = 0; i < 16; i++)
            {
                empty5[SfxProtocol.SongHead + k * 35 + 19 + i] = 5;
            }
        }

        byte[] empty6 = SfxNative.SongConvert(empty5);
        Ok(
            empty6 != null
                && empty6.Length == 211
                && empty6.Skip(SfxProtocol.SongHead).All(x => x == 0)
                && SfxPackedSong.Format(empty6) == 6,
            $"an empty song is 211 bytes: the head and eight empty blocks ({(empty6 == null ? 0 : empty6.Length)})"
        );

        // the longest song there is: every sound and every lane in all eight patterns at 64 steps
        var big = new List<byte>(fix6.Take(SfxProtocol.SongHead));
        for (int p = 0; p < 8; p++)
        {
            big.AddRange(new byte[] { 2, 0x7F, 0x7F, 3, 0x7F, 0x7F, 0x7F, 7 });
            for (int t = 0; t < 16; t++)
            {
                for (int b = 0; b < 10; b++)
                {
                    big.Add((byte)(b < 9 ? (p * 31 + t * 7 + b * 13) % 128 : (p + t) & 1));
                }

                for (int i = 0; i < 64; i++)
                {
                    big.Add((byte)(36 + (p * 5 + t * 3 + i) % 60));
                }

                for (int i = 0; i < 64; i++)
                {
                    big.Add((byte)((p + t + i) % 8));
                }

                for (int i = 0; i < 64; i++)
                {
                    big.Add((byte)((p * 11 + t * 5 + i * 3) % 64));
                }
            }

            for (int l = 0; l < 24; l++)
            {
                for (int i = 0; i < 64; i++)
                {
                    big.Add((byte)(1 + (p * 17 + l * 9 + i * 5) % 127));
                }
            }
        }

        byte[] longest = big.ToArray();
        byte[] again = SfxNative.SongConvert(longest);
        Ok(
            longest.Length == 38355
                && longest.Length == SfxProtocol.SongBytesMax
                && again != null
                && again.SequenceEqual(longest)
                && SfxPackedSong.Format(longest) == 6
                && SfxPackedSong.Grid(longest, 7) == 64
                && SfxPackedSong.StepNote(longest, 7, 15, 63) == 36 + (35 + 45 + 63) % 60
                && SfxPackedSong.LanePoint(longest, 7, 23, 63) == (7 * 17 + 23 * 9 + 63 * 5) % 127
                && RenderHash(fixSounds, longest, 0, false, 1f, 1) != 0,
            "everything at 64 steps is 38 355 bytes, and is taken: it comes back from the packer byte for "
                + "byte, and plays"
        );

        // what the player refuses - and it keeps the song it had
        // the first row of pattern A: 3 bytes of steps, 16 notes, 16 velocities, 16 lengths
        int rowA = SfxProtocol.SongHead + 8;
        byte[] With(int at, int value)
        {
            var c = (byte[])fix6.Clone();
            c[at] = (byte)value;
            return c;
        }

        var damaged = new List<(string what, byte[] song)>
        {
            ("one byte short", fix6.Take(fix6.Length - 1).ToArray()),
            ("one byte over", fix6.Append((byte)0).ToArray()),
            ("a grid of 3", With(SfxProtocol.SongHead, 3)),
            ("a velocity of 8", With(rowA + 3 + 16, 8)),
            ("a length of 16 at 16 steps", With(rowA + 3 + 32, 16)),
            ("a step bit from 16 on", With(rowA + 2, fix6[rowA + 2] | 4)),
            ("a spare bit in the sounds' mask", With(SfxProtocol.SongHead + 3, fix6[SfxProtocol.SongHead + 3] | 4)),
            ("a spare bit in the lanes' mask", With(SfxProtocol.SongHead + 7, 8)),
            ("a byte of 128", With(1000, 128)),
            ("a bar of 17", With(1, 17)),
            ("a bar of 0", With(1, 0)),
            ("six bytes", fix6.Take(6).ToArray()),
        };
        for (int p = 0; p <= 8; p++)
        {
            // where block p of the longest song begins (8: where the song ends)
            int at = SfxProtocol.SongHead + p * Block64;
            damaged.Add(($"the longest song cut one byte before block {p}", longest.Take(at - 1).ToArray()));
            if (p < 8)
            {
                damaged.Add(($"the longest song cut at block {p}", longest.Take(at).ToArray()));
            }

            damaged.Add(
                (
                    $"the longest song cut one byte after the start of block {p}",
                    p < 8 ? longest.Take(at + 1).ToArray() : longest.Append((byte)0).ToArray()
                )
            );
        }

        string taken = "";
        ulong kept = RenderHash(fixSounds, fix6, 0, false, 1f, 2, pl =>
        {
            foreach (var (what, song) in damaged)
            {
                if (SfxNative.PlayerSetSong(pl, song))
                {
                    taken += " [" + what + "]";
                }
            }
        });
        bool unreadable = damaged
            .Where(x =>
                !x.what.StartsWith("a spare bit")
                && x.what != "a velocity of 8"
                && x.what != "a length of 16 at 16 steps"
                && x.what != "a step bit from 16 on"
                && x.what != "a byte of 128"
                && !x.what.StartsWith("a bar")
            )
            .All(x => SfxPackedSong.Format(x.song) == 0);
        Ok(
            taken == ""
                && kept == RenderHash(fixSounds, fix6, 0, false, 1f, 2)
                && damaged.All(x => SfxNative.SongConvert(x.song) == null)
                && unreadable,
            $"the player refuses {damaged.Count} damaged songs and keeps the one it had: a byte short or over, a grid of 3, a velocity of 8, a length of a whole bar, a step bit past the grid, spare mask bits, a byte of 128, a bar of 0 or 17, the longest song cut at every block and a byte either side"
                + taken
        );
    }

    // SfxPackedSong.IsPlayable says what the player says: the damaged
    // songs above, and thousands of songs with a byte or three changed
    static void IsPlayable()
    {
        // (pattern F at 64 steps, E at 32)
        byte[] fine = SfxNative.SongRegrid(SfxNative.SongRegrid(fix6, 5, 2), 4, 1);
        var bases = new List<byte[]> { fix6, fine, fix5 };
        bases.AddRange(olderFormats);
        bool wholeOnes = bases.All(SfxPackedSong.IsPlayable);

        // a velocity of 9 in the first row of pattern A
        var bad8 = (byte[])fix6.Clone();
        bad8[SfxProtocol.SongHead + 8 + 3 + 16] = 9;
        var rnd = new Random(20261121);
        int tried = 0;
        int refused = 0;
        int apart = 0;
        string first = "";
        RenderHash(fixSounds, fix6, 0, false, 1f, 1, pl =>
        {
            foreach (var b in bases)
            {
                for (int n = 0; n < 4000; n++)
                {
                    var c = (byte[])b.Clone();
                    int changes = 1 + rnd.Next(3);
                    string what = "";
                    for (int k = 0; k < changes; k++)
                    {
                        // (a quarter of them in the head and the first block's masks)
                        int at = rnd.Next(4) == 0 ? rnd.Next(Math.Min(c.Length, 180)) : rnd.Next(c.Length);
                        int v =
                            rnd.Next(5) == 0 ? rnd.Next(256)
                            : rnd.Next(3) == 0 ? rnd.Next(128)
                            : rnd.Next(20);
                        c[at] = (byte)v;
                        what += $" [{at}]={v}";
                    }

                    bool says = SfxPackedSong.IsPlayable(c);
                    bool takes = SfxNative.PlayerSetSong(pl, c);
                    tried++;
                    if (!takes)
                    {
                        refused++;
                    }

                    if (says != takes)
                    {
                        apart++;
                        if (first == "")
                        {
                            first =
                                $" - first: format {b[0]}, {b.Length} bytes,{what}: IsPlayable {says}, the player {takes}";
                        }
                    }
                }
            }
        });
        Ok(
            wholeOnes
                && SfxPackedSong.IsValid(bad8)
                && !SfxPackedSong.IsPlayable(bad8)
                && !SfxPackedSong.IsPlayable(null)
                && !SfxPackedSong.IsPlayable(new byte[300]),
            "IsPlayable: the fixture in every format (1 to 6, and with patterns at 32 and 64 steps) is; a "
                + "song with a velocity of 9 is valid by its layout and not playable"
        );
        Ok(
            apart == 0 && refused > tried / 10 && refused < tried * 9 / 10,
            $"IsPlayable and the native player agree on {tried} songs with one to three bytes changed ({refused} refused by both){first}"
        );
    }

    // raising changes no sample
    static void RaisingTheGrid()
    {
        byte[] All(byte[] s, int grid)
        {
            for (int p = 0; p < 8; p++)
            {
                s = SfxNative.SongRegrid(s, p, grid);
            }

            return s;
        }

        byte[] at32 = All(fix6, 1);
        byte[] at64 = All(fix6, 2);
        ulong[] h32 = ThreeHashes(fixSounds, at32);
        ulong[] h64 = ThreeHashes(fixSounds, at64);
        Ok(
            SfxPackedSong.Grid(at32, 0) == 32
                && SfxPackedSong.Grid(at64, 7) == 64
                && h32.SequenceEqual(wantHash)
                && h64.SequenceEqual(wantHash),
            $"every pattern raised to 32 steps, and to 64: the same samples as the format-5 song, bit for bit ({at32.Length} and {at64.Length} bytes; {Hex(h64)})"
        );
        bool sweep = true;
        string off = "";
        int tried = 0;
        foreach (int swing in new[] { 0, 1, 20, 50, 99, 100 })
        {
            foreach (int bpm in new[] { 40, 97, 120, 173, 240 })
            {
                var a = (byte[])fix6.Clone();
                a[4] = (byte)swing;
                a[2] = (byte)(bpm >> 7);
                a[3] = (byte)(bpm & 127);
                var b = (byte[])at64.Clone();
                b[4] = a[4];
                b[2] = a[2];
                b[3] = a[3];
                var c = (byte[])at32.Clone();
                c[4] = a[4];
                c[2] = a[2];
                c[3] = a[3];
                ulong ha = RenderHash(fixSounds, a, 5, true, 1f, 2);
                bool good =
                    ha != 0
                    && ha == RenderHash(fixSounds, b, 5, true, 1f, 2)
                    && ha == RenderHash(fixSounds, c, 5, true, 1f, 2);
                tried++;
                sweep &= good;
                if (!good)
                {
                    off += $" [swing {swing} at {bpm}]";
                }
            }
        }

        Ok(
            sweep,
            $"swing 0, 1, 20, 50, 99 and 100 at 40, 97, 120, 173 and 240 BPM, two bars of pattern F: at 16, at 32 and at 64 steps the same samples ({tried} pairs)"
                + off
        );
        bool back = true;
        byte[] down = at64;
        for (int p = 0; p < 8; p++)
        {
            back &= SfxNative.SongRegridLoss(down, p, 1) == 0;
            down = SfxNative.SongRegrid(down, p, 1);
        }

        back &= down.SequenceEqual(at32);
        for (int p = 0; p < 8; p++)
        {
            back &= SfxNative.SongRegridLoss(down, p, 0) == 0;
            down = SfxNative.SongRegrid(down, p, 0);
        }

        Ok(
            back
                && down.SequenceEqual(fix6)
                && SfxNative.SongRegridLoss(fix6, 0, 2) == 0
                && SfxNative.SongRegridLoss(fix6, 8, 0) == -1
                && SfxNative.SongRegridLoss(fix6, 0, 3) == -1
                && SfxNative.SongRegrid(fix6, 0, 3) == null
                && SfxNative.SongRegrid(new byte[300], 0, 1) == null
                && SfxNative.SongRegridLoss(null, 0, 1) == -1,
            "64 back to 32 and to 16 loses nothing each time and gives the first bytes again; no such "
                + "pattern, no such grid, no song: -1 and null"
        );
        bool spread = SfxPackedSong.Grid(at64, 0) == 64;
        for (int s = 0; s < 16; s++)
        {
            for (int i = 0; i < 16; i++)
            {
                spread &=
                    SfxPackedSong.StepOn(at64, 0, s, i * 4) == SfxPackedSong.StepOn(fix6, 0, s, i)
                    && SfxPackedSong.StepNote(at64, 0, s, i * 4) == SfxPackedSong.StepNote(fix6, 0, s, i)
                    && SfxPackedSong.StepLength(at64, 0, s, i * 4) == SfxPackedSong.StepLength(fix6, 0, s, i) * 4
                    && !SfxPackedSong.StepOn(at64, 0, s, i * 4 + 1)
                    && !SfxPackedSong.StepOn(at64, 0, s, i * 4 + 2)
                    && !SfxPackedSong.StepOn(at64, 0, s, i * 4 + 3);
            }
        }

        Ok(
            spread,
            "raised, a step stands on the step that begins at the same moment, with its note; its length is "
                + "four times as many steps"
        );
    }

    // a finer grid sounds finer
    static void AFinerGridSoundsFiner()
    {
        const int Hat = 12;
        const int Organ = 10;
        const int Strings = 15;
        int[] G(int a, int b = 0)
        {
            var g = new int[8];
            g[0] = a;
            g[1] = b;
            return g;
        }

        // the hat on steps 0 and 3 of 64 at 120 BPM: a step is 31.25 ms
        var two = Pack6(PlainHead(fix6, 120, 0, 1), G(2), new[] { (0, Hat, 0, 0, 5, 0), (0, Hat, 3, 0, 5, 0) });
        var on = Onsets(RenderSong(fixSounds, two, 1.0, out _), 0, 1.0);
        double gap = on.Count == 2 ? (on[1] - on[0]) * 1000 : -1;
        Ok(
            on.Count == 2 && Math.Abs(gap - 93.75) < 256000.0 / 48000,
            $"a sound on steps 1 and 4 of 64 at 120 BPM: the second starts {gap:0.00} ms after the first (three steps are 93.75 ms)"
        );

        // swing 100: the odd sixteenth begins half a sixteenth late and
        // is half as long - step 4 of 32 is its second half
        var swung = Pack6(PlainHead(fix6, 120, 100, 1), G(1), new[] { (0, Hat, 0, 0, 5, 0), (0, Hat, 3, 0, 5, 0) });
        on = Onsets(RenderSong(fixSounds, swung, 1.0, out _), 0, 1.0);
        gap = on.Count == 2 ? (on[1] - on[0]) * 1000 : -1;
        Ok(
            on.Count == 2 && Math.Abs(gap - 1.75 * 125) < 256000.0 / 48000,
            $"swing 100, a step on step 4 of 32: it starts {gap / 125:0.000} sixteenths into the bar (1.75: the second half of a sixteenth that begins at 1.5 and is half as long)"
        );

        // a length counts in the pattern's own steps
        double Held(int len)
        {
            var s = Pack6(PlainHead(fix6, 120, 0, 1), G(2), new[] { (0, Strings, 0, 57, 7, len) });
            return Rms(RenderSong(fixSounds, s, 1.0, out _), 0.45, 0.75);
        }

        double l2 = Held(2);
        double l8 = Held(8);
        double l32 = Held(32);
        Ok(
            l2 < l8 * 0.6 && l8 < l32 * 0.6 && l32 > 0.02,
            $"LENGTH at 64 steps counts sixty-fourths: 2 is over before 8, 8 before 32 ({l2:0.000}, {l8:0.000}, {l32:0.000} between 0.45 and 0.75 s)"
        );

        // a note of length 3 on the last step of a 64 pattern, the next bar
        // a pattern of 16: it ends on that pattern's second step
        int Ends(int len, int gridB, out int state1, out int grid0, out int grid1, out bool hangs)
        {
            // (the organ: at its full level at once, so its fading is as long whenever it is let go)
            var s = Pack6(PlainHead(fix6, 120, 0, 2), G(2, gridB), new[] { (0, Organ, 63, 57, 7, len) });

            // (5.9 s: the two bars, and the first again up to just before its note comes round)
            int st = -9;
            int g0 = -9;
            int g1 = -9;
            var q = new int[4];
            var g = new int[2];

            // (block 192 ends 1.024 s into bar 1)
            RenderSong(fixSounds, s, 5.9, out uint[] playing, (pl, b) =>
            {
                if (b == 192)
                {
                    SfxNative.PlayerState(pl, q);
                    SfxNative.PlayerGrid(pl, g);
                    st = q[1];
                    g0 = g[0];
                    g1 = g[1];
                }
            });
            int last = Array.FindLastIndex(playing, m => (m >> Organ & 1) != 0);
            state1 = st;
            grid0 = g0;
            grid1 = g1;
            hangs = last >= playing.Length - 1;
            return last;
        }

        int byItself = Ends(0, 0, out _, out _, out _, out bool h0);
        int three = Ends(3, 0, out int sixteenth, out int step, out int per, out bool h3);
        int threeFine = Ends(3, 2, out _, out _, out _, out _);
        int long16 = Ends(63, 0, out _, out _, out _, out bool h63);
        double later = (three - byItself) * 256 / 48.0;
        double laterFine = (threeFine - byItself) * 256 / 48.0;
        Ok(
            !h0
                && !h3
                && !h63
                && later > 125 - 12
                && later < 125 + 12
                && laterFine > 62.5 - 12
                && laterFine < 62.5 + 12
                && long16 > three,
            $"a note of length 3 on the last step of a 64 pattern, the next bar a pattern of 16: it is let go {later:0.0} ms after the bar line - on that pattern's second step (125 ms), not later; into a pattern of 64 after {laterFine:0.0} ms (two more steps: 62.5); no note hangs"
        );
        Ok(
            sixteenth == 8 && step / 4 == 8 && per == 4 && step >= 32 && step <= 33,
            $"1.024 s into a 64 pattern at 120 BPM the player's state says sixteenth 9 ({sixteenth + 1}), its grid step {step + 1} of 4 a sixteenth ({per})"
        );
    }

    // lowering drops what stands between, and says how much
    static void LoweringTheGrid()
    {
        int[] g64 = { 2, 0, 0, 0, 0, 0, 0, 0 };
        int[] g32 = { 1, 0, 0, 0, 0, 0, 0, 0 };
        int[] g16 = new int[8];

        // lane 0: sound 7's fader
        var head = PlainHead(fix6, 120, 0, 1);
        head[99] = 7;
        head[100] = 26;
        var fine = Pack6(
            head,
            g64,
            new[] { (0, 2, 0, 0, 5, 0), (0, 2, 5, 0, 5, 0), (0, 6, 6, 40, 7, 5), (0, 6, 8, 43, 3, 63) },
            new[] { (0, 0, 3, 90), (0, 0, 8, 20), (0, 0, 62, 5) }
        );
        var half = Pack6(
            head,
            g32,
            new[] { (0, 2, 0, 0, 5, 0), (0, 6, 3, 40, 7, 3), (0, 6, 4, 43, 3, 31) },
            new[] { (0, 0, 4, 20), (0, 0, 31, 5) }
        );
        var coarse = Pack6(head, g16, new[] { (0, 2, 0, 0, 5, 0), (0, 6, 2, 43, 3, 15) }, new[] { (0, 0, 2, 20) });
        byte[] to32 = SfxNative.SongRegrid(fine, 0, 1);
        byte[] to16 = SfxNative.SongRegrid(fine, 0, 0);
        byte[] stepwise = to32 == null ? null : SfxNative.SongRegrid(to32, 0, 0);
        Ok(
            SfxNative.SongRegridLoss(fine, 0, 1) == 2
                && SfxNative.SongRegridLoss(fine, 0, 0) == 4
                && SfxNative.SongRegridLoss(fine, 0, 2) == 0
                && SfxNative.SongRegridLoss(half, 0, 0) == 2,
            $"what lowering would drop is counted, steps and points: 64 to 32 drops 2 (a step on 6, a point on 4), 64 to 16 drops 4 (two steps, two points), 32 to 16 two more ({SfxNative.SongRegridLoss(fine, 0, 1)}, {SfxNative.SongRegridLoss(fine, 0, 0)}, {SfxNative.SongRegridLoss(half, 0, 0)})"
        );
        Ok(
            to32 != null
                && to32.SequenceEqual(half)
                && SfxPackedSong.StepLength(to32, 0, 6, 3) == 3
                && SfxPackedSong.StepLength(to32, 0, 6, 4) == 31,
            "64 to 32: what stands on an even step stays, a length of 5 becomes 3 (rounded up), one of 63 "
                + "the 31 a bar of 32 can hold; the rest is gone - the bytes are those of the song written at 32"
        );
        Ok(
            to16 != null && to16.SequenceEqual(coarse) && stepwise != null && stepwise.SequenceEqual(coarse),
            "64 to 16 at once, or by way of 32: the same song, byte for byte, as written at 16"
        );
        byte[] up = SfxNative.SongRegrid(to16, 0, 2);
        bool above = up != null;
        for (int i = 0; above && i < 64; i++)
        {
            above =
                SfxPackedSong.StepOn(up, 0, 6, i) == (i == 8)
                && SfxPackedSong.StepOn(up, 0, 2, i) == (i == 0)
                && (SfxPackedSong.LanePoint(up, 0, 0, i) >= 0) == (i == 8);
        }

        Ok(
            above && SfxPackedSong.StepLength(up, 0, 6, 8) == 60,
            "and raised again nothing comes back from the cells above: lowering emptied them"
        );
    }
}
