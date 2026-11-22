using System.Linq;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;

namespace Inspector.Harness
{
    /// <summary>
    /// A song whose patterns have finer grids (firmware 1.6.0, song format 6): the demo song with pattern B at 32 steps,
    /// C and F at 64 - made by the engine (SfxNative.SongRegrid), then given three things no sixteenth holds: a step of
    /// FM BASS between two sixteenths of B, one of the hi-hat between two of C, and a length of 13 in B. What SfxSong
    /// says of it, what the Inspector shows, and what SEND says to which device.
    /// </summary>
    public class SfxSongGridTests
    {
        const int A = 0, B = 1, C = 2, F = 5, Bass = 6, Hat = 12, Strings = 15;
        const int BassBetween = 5, HatBetween = 2, OddLength = 13;        // step 6 of B (between sixteenths 3 and 4), step 3 of C, the strings' first note in B

        SfxSong song;

        // where a sound's row begins in a pattern's block of a format-6 song (core/sfx_song.h): -1 = it has none there
        static int RowAt(byte[] d, int pattern, int sound, out int steps)
        {
            int at = 147; steps = 16;
            for (int p = 0; ; p++)
            {
                steps = 16 << d[at];
                int rows = d[at + 1] | d[at + 2] << 7 | d[at + 3] << 14, lanes = d[at + 4] | d[at + 5] << 7 | d[at + 6] << 14 | d[at + 7] << 21, row = (steps + 6) / 7 + 3 * steps;
                if (p == pattern)
                {
                    if ((rows >> sound & 1) == 0) return -1;
                    int before = 0; for (int s = 0; s < sound; s++) before += rows >> s & 1;
                    return at + 8 + before * row;
                }
                int count = 0; for (int s = 0; s < 16; s++) count += rows >> s & 1;
                int lcount = 0; for (int l = 0; l < 24; l++) lcount += lanes >> l & 1;
                at += 8 + count * row + lcount * steps;
            }
        }

        /// <summary>The demo song with pattern B at 32 steps, C and F at 64, and the three things between.</summary>
        public static SfxSong Fine()
        {
            var song = SfxSongPanelTests.Demo();
            byte[] d = SfxNative.SongRegrid(song.data, B, 1);
            Assert.IsNotNull(d, "the engine is here (sfxforge.dll), and takes the demo song");
            d = SfxNative.SongRegrid(d, C, 2);
            d = SfxNative.SongRegrid(d, F, 2);
            int bass = RowAt(d, B, Bass, out int steps);                 // a row: the steps that are on (7 bits a byte), then a note, a velocity, a length for every step
            Assert.AreEqual(32, steps); Assert.GreaterOrEqual(bass, 0);
            int bits = (steps + 6) / 7;
            d[bass + BassBetween / 7] |= (byte)(1 << BassBetween % 7); d[bass + bits + BassBetween] = 52;      // E3
            int strings = RowAt(d, B, Strings, out steps);
            Assert.AreEqual(14, d[strings + bits + 2 * steps], "the strings' first note in B holds 7 sixteenths: 14 steps of 32");
            d[strings + bits + 2 * steps] = OddLength;
            int hat = RowAt(d, C, Hat, out steps);
            Assert.AreEqual(64, steps); Assert.GreaterOrEqual(hat, 0);
            d[hat + HatBetween / 7] |= (byte)(1 << HatBetween % 7);
            song.Set(song.sounds, d);
            Assert.IsTrue(song.IsValid);
            Assert.IsTrue(SfxNative.SongConvert(d).SequenceEqual(d), "and it is still a song the engine packs the same");
            return song;
        }

        static SfxSongPanel Panel(SfxSong s)
        {
            var panel = (SfxSongPanel)SfxSongEditor.Build(new SerializedObject(s));
            panel.Page = SfxSongPanel.PageSteps; panel.Intensity = 127;
            return panel;
        }

        [SetUp] public void SetUp() => song = Fine();
        [TearDown] public void TearDown() => Object.DestroyImmediate(song);

        [Test]
        public void EveryPatternHasItsGrid()
        {
            Assert.AreEqual(6, song.PackedFormat); Assert.AreEqual(2, song.Format);
            Assert.AreEqual(16, SfxSong.MaxSteps); Assert.AreEqual(64, SfxSong.MaxGridSteps); Assert.AreEqual(16, song.StepsPerPattern);
            Assert.IsTrue(song.HasFinerGrid); Assert.IsFalse(SfxSongPanelTests.Demo().HasFinerGrid);
            int[] grids = { 16, 32, 64, 16, 16, 64, 16, 16 };
            for (int p = 0; p < 8; p++)
            {
                Assert.AreEqual(grids[p], song.Grid(p), "pattern " + p);
                Assert.AreEqual(grids[p] / 16, song.StepsPerSixteenth(p));
                Assert.AreEqual(grids[p], song.GridSteps(p), "a bar of 16 sixteenths plays all of them");
            }
            Assert.AreEqual(0, song.Grid(8)); Assert.AreEqual(0, song.Grid(-1)); Assert.AreEqual(0, song.StepsPerSixteenth(8)); Assert.AreEqual(0, song.GridSteps(8));
            var none = ScriptableObject.CreateInstance<SfxSong>();
            Assert.AreEqual(0, none.Grid(0)); Assert.IsFalse(none.HasFinerGrid); Assert.AreEqual(0, none.PackedFormat);
            Object.DestroyImmediate(none);
        }

        // the accessors of before count in sixteenths: they say what stands on the first step of each
        [Test]
        public void TheOldAccessorsCountSixteenths()
        {
            var plain = SfxSongPanelTests.Demo();
            foreach (int p in new[] { B, C, F })
            {
                int div = song.StepsPerSixteenth(p);
                for (int s = 0; s < 16; s++) for (int i = 0; i < 16; i++)
                {
                    Assert.AreEqual(song.GridStepOn(p, s, i * div), song.StepOn(p, s, i), $"{p} {s} {i}");
                    Assert.AreEqual(song.GridStepNote(p, s, i * div), song.StepNote(p, s, i));
                    Assert.AreEqual(song.GridStepVelocity(p, s, i * div), song.StepVelocity(p, s, i));
                    Assert.AreEqual((song.GridStepLength(p, s, i * div) + div - 1) / div, song.StepLength(p, s, i));
                    // … which is what the song had before its grid was made finer
                    Assert.AreEqual(plain.StepOn(p, s, i), song.StepOn(p, s, i)); Assert.AreEqual(plain.StepNote(p, s, i), song.StepNote(p, s, i));
                    Assert.AreEqual(plain.StepVelocity(p, s, i), song.StepVelocity(p, s, i));
                }
                for (int l = 0; l < SfxSong.Lanes; l++) for (int i = 0; i < 16; i++)
                {
                    Assert.AreEqual(song.GridLanePoint(p, l, i * div), song.LanePoint(p, l, i));
                    Assert.AreEqual(plain.LanePoint(p, l, i), song.LanePoint(p, l, i));
                }
            }
            // what stands between is seen by the Grid accessors only
            Assert.IsTrue(song.GridStepOn(B, Bass, BassBetween)); Assert.AreEqual(52, song.GridStepNote(B, Bass, BassBetween));
            Assert.IsTrue(song.GridStepOn(C, Hat, HatBetween));
            Assert.AreEqual(plain.StepOn(B, Bass, 2), song.StepOn(B, Bass, 2), "the sixteenth it stands in says what its first step has");
            // a length is in the pattern's own steps; in sixteenths it is rounded up
            Assert.AreEqual(OddLength, song.GridStepLength(B, Strings, 0)); Assert.AreEqual(7, song.StepLength(B, Strings, 0), "13 steps of 32 are 6.5 sixteenths: 7");
            Assert.AreEqual(28, song.GridStepLength(C, Strings, 0)); Assert.AreEqual(7, song.StepLength(C, Strings, 0));
            Assert.AreEqual(7, plain.StepLength(C, Strings, 0));
            // out of range: as ever
            Assert.IsFalse(song.GridStepOn(B, Bass, 32)); Assert.IsFalse(song.GridStepOn(B, Bass, -1)); Assert.IsFalse(song.GridStepOn(A, Bass, 16)); Assert.IsFalse(song.StepOn(B, Bass, 16));
            Assert.AreEqual(5, song.GridStepVelocity(B, 0, 99)); Assert.AreEqual(0, song.GridStepNote(8, 0, 0)); Assert.AreEqual(-1, song.GridLanePoint(F, 0, 64)); Assert.AreEqual(0, song.GridStepLength(C, 16, 0));
            Object.DestroyImmediate(plain);
        }

        [Test]
        public void TheRowsShowThePatternsOwnSteps()
        {
            var panel = Panel(song);
            int[] grids = { 16, 32, 64, 16, 16, 64, 16, 16 };
            string[] names = { "1/16", "1/32", "1/64", "1/16", "1/16", "1/64", "1/16", "1/16" };
            for (int p = 0; p < 8; p++)
            {
                panel.Pattern = p;
                Assert.AreEqual("PATTERN " + (char)('A' + p) + "  " + names[p], panel.PatternTitle);
                for (int s = 0; s < 16; s++)
                {
                    Assert.AreEqual(grids[p], panel.RowCount(s), $"pattern {p}, sound {s}");
                    Assert.AreEqual(song.GridSteps(p), panel.Row(s).Length);
                    for (int i = 0; i < StepRow.MaxSteps; i++)
                    {
                        bool on = i < grids[p] && song.GridStepOn(p, s, i);
                        Assert.AreEqual(on, panel.Row(s).Level[i] > 0f, $"{p} {s} {i}");
                        if (on) Assert.AreEqual((6 + song.GridStepVelocity(p, s, i) * 2) / 20f, panel.Row(s).Level[i], 1e-6f);
                        Assert.AreEqual(on ? song.GridStepLength(p, s, i) : 0, panel.Row(s).Hold[i]);
                    }
                }
            }
            panel.Pattern = B;
            Assert.AreEqual("PATTERN B  1/32", panel.PatternTitle);
            Assert.Greater(panel.Row(Bass).Level[BassBetween], 0f, "the step between two sixteenths is in its row");
            Assert.AreEqual(OddLength, panel.Row(Strings).Hold[0]);
            panel.Pattern = A;
            Assert.AreEqual(16, panel.RowCount(0)); Assert.AreEqual("PATTERN A  1/16", panel.PatternTitle);
            Assert.AreEqual("1/16", SfxSongPanel.GridName(16)); Assert.AreEqual("1/32", SfxSongPanel.GridName(32)); Assert.AreEqual("1/64", SfxSongPanel.GridName(64));
        }

        [Test]
        public void ATileCountsWhatIsInASixteenthAndSaysItsGrid()
        {
            var panel = Panel(song);
            string[] corner = { "", "32", "64", "", "", "64", "", "" };
            for (int p = 0; p < 8; p++)
            {
                var tile = panel.PatternTileAt(p);
                Assert.AreEqual(corner[p], tile.GridText, "pattern " + p);
                int div = song.StepsPerSixteenth(p);
                for (int i = 0; i < 16; i++)
                {
                    int n = 0;
                    for (int s = 0; s < 16; s++) { bool any = false; for (int j = 0; j < div; j++) any |= song.GridStepOn(p, s, i * div + j); if (any) n++; }
                    Assert.AreEqual(n, tile.Column(i), $"pattern {p}, sixteenth {i}");
                }
            }
            // the bass's step between sixteenths 3 and 4 of B is counted in column 3 (its first step holds no bass)
            var plain = SfxSongPanelTests.Demo();
            Assert.IsFalse(plain.StepOn(B, Bass, 2));
            int without = 0; for (int s = 0; s < 16; s++) if (plain.StepOn(B, s, 2)) without++;
            Assert.AreEqual(without + 1, panel.PatternTileAt(B).Column(2));
            Object.DestroyImmediate(plain);
        }

        [Test]
        public void TheNotesAreWrittenInTheCellsAtSixteenStepsOnly()
        {
            var panel = Panel(song);
            panel.Sound = Bass; panel.Pattern = A;
            Assert.AreEqual("E1", panel.NoteOnStep(0));
            StringAssert.Contains("pattern A:  E1 · · E1", panel.SoundText);
            panel.Pattern = B;
            for (int i = 0; i < 16; i++) Assert.AreEqual("", panel.NoteOnStep(i), "at 32 steps a cell has no room for a note: " + i);
            // … they are said in words instead: the steps that are set, each with its number
            string said = panel.SoundText;
            StringAssert.Contains("pattern B (1/32):  1 E1 · 6 E3 · 7 E1", said);
            Assert.AreEqual(Enumerable.Range(0, 32).Count(i => song.GridStepOn(B, Bass, i)), said.Split('|')[1].Split(new[] { " · " }, System.StringSplitOptions.None).Count(part => part.Trim().Length > 0 && !part.Contains("lane")), said);
            // more than 16 set steps: the first 16, then how many more
            panel.Sound = Hat; panel.Pattern = C;
            int hats = Enumerable.Range(0, 64).Count(i => song.GridStepOn(C, Hat, i));
            Assert.AreEqual(17, hats, "the hat on every sixteenth of C, and one between");
            said = panel.SoundText;
            StringAssert.Contains("pattern C (1/64):  1 x · 3 x · 5 x", said);
            StringAssert.EndsWith(" · +1 more", said);
            panel.Sound = 0; panel.Pattern = F;
            StringAssert.Contains("pattern F:  it rests", panel.SoundText);
        }

        [Test]
        public void ALaneHasItsPointsOnTheFinerSteps()
        {
            var panel = Panel(song);
            panel.Page = SfxSongPanel.PageAuto; panel.Pattern = F;
            Assert.AreEqual(2, panel.LaneCount);
            var row = panel.LaneRowAt(0);                       // FM BASS's CUTOFF rises through F: a point on every sixteenth
            Assert.AreEqual(64, row.Count); Assert.AreEqual(64, row.Length);
            for (int i = 0; i < 64; i++)
            {
                Assert.AreEqual(i % 4 == 0 ? 61 + i / 4 * 3 : -1, row.Point[i], "step " + i);
                Assert.AreEqual(song.GridStepOn(F, Bass, i), row.Plays[i]);
            }
            Assert.AreEqual($"7 FM | CUTOFF | F: 16 points, {SfxUnits.ParamText(SfxParam.FilterCutoff, 61)} to {SfxUnits.ParamText(SfxParam.FilterCutoff, 106)}", panel.LaneText(0));
            Assert.AreEqual("16 WAVE | VOLUME | no points in F – it moves in E", panel.LaneText(1));
            panel.Pattern = 4;
            Assert.AreEqual(16, panel.LaneRowAt(0).Count);
            Assert.AreEqual("7 FM | CUTOFF | no points in E – it moves in F", panel.LaneText(0), "a lane's points are looked for on every step of a finer pattern");
            panel.Page = SfxSongPanel.PageSteps;
        }

        [Test]
        public void TheFiveNumbersStillChangeAFinerSong()
        {
            var panel = Panel(song);
            int length = song.data.Length;
            Assert.IsTrue(panel.SetTempo(97)); Assert.AreEqual(97, song.Bpm);
            Assert.IsTrue(panel.SetSwing(40)); Assert.AreEqual(40, song.Swing);
            Assert.IsTrue(panel.SetLayer(Bass, 12)); Assert.AreEqual(12, song.LayerRaw(Bass));
            Assert.IsTrue(panel.SetVolume(Bass, 90)); Assert.AreEqual(90, song.VolumeRaw(Bass));
            Assert.IsTrue(panel.SetPan(Bass, 20)); Assert.AreEqual(20, song.PanRaw(Bass));
            Assert.AreEqual(length, song.data.Length); Assert.IsTrue(song.IsValid); Assert.AreEqual(32, song.Grid(B));
            Assert.IsNotNull(SfxSongPanel.RenderBar(song, B, 1f), "and the engine plays a bar of the pattern of 32");
        }

        // SEND: a device takes a song in the format it speaks. One that said nothing is taken for a firmware before 1.6.
        [Test]
        public void SendIsOffForASongTheDeviceCannotTake()
        {
            var five = SfxSongPanelTests.Demo5();
            var dev15 = new SfxDeviceInfo { hardware = 1, major = 1, minor = 5, profile = SfxProfile.VibeFm15, flags = 15 };
            var dev16 = new SfxDeviceInfo { hardware = 1, major = 1, minor = 6, profile = SfxProfile.VibeFm1, flags = 15 };
            Assert.AreEqual(5, five.PackedFormat); Assert.AreEqual(6, song.PackedFormat);

            bool off = SfxSongPanel.LinkState(true, "VIBE", dev15, song.data, out string bold, out string small, out _);
            Assert.IsTrue(off); Assert.AreEqual("ANOTHER FORMAT", bold);
            StringAssert.StartsWith("SEND is off: ", small); StringAssert.Contains("speaks song format 5", small); StringAssert.Contains("update the device's firmware", small);

            off = SfxSongPanel.LinkState(true, "VIBE", null, song.data, out bold, out small, out _);
            Assert.IsTrue(off); Assert.AreEqual("ANOTHER FORMAT", bold);
            StringAssert.Contains("did not say that it takes song format 6", small);

            foreach (SfxDeviceInfo? device in new SfxDeviceInfo?[] { dev15, null, dev16 })
            {
                off = SfxSongPanel.LinkState(true, "VIBE", device, five.data, out bold, out small, out _);
                Assert.IsFalse(off, "a format-5 song goes to any of them"); Assert.AreEqual("LINKED", bold); StringAssert.Contains("on VIBE", small);
            }
            off = SfxSongPanel.LinkState(true, "VIBE", dev16, song.data, out bold, out _, out _);
            Assert.IsFalse(off, "a format-6 song goes to a device that said 1.6"); Assert.AreEqual("LINKED", bold);
            off = SfxSongPanel.LinkState(true, "VIBE", dev16, SfxSongPanelTests.Demo().data, out bold, out _, out _);
            Assert.IsFalse(off); Assert.AreEqual("LINKED", bold);

            // a song with a value the device would answer BAD SONG to (a swing of 101): valid by its layout, sent to nobody
            var damaged = (byte[])song.data.Clone(); damaged[4] = 101;
            Assert.IsTrue(SfxPackedSong.IsValid(damaged)); Assert.IsFalse(SfxPackedSong.IsPlayable(damaged)); Assert.IsTrue(SfxPackedSong.IsPlayable(song.data)); Assert.IsTrue(SfxPackedSong.IsPlayable(five.data));
            foreach (SfxDeviceInfo? device in new SfxDeviceInfo?[] { dev15, null, dev16 })
            {
                off = SfxSongPanel.LinkState(true, "VIBE", device, damaged, out bold, out small, out _);
                Assert.IsTrue(off); Assert.AreEqual("A DAMAGED SONG", bold); StringAssert.Contains("out of range", small); StringAssert.Contains("fetch it again from the device", small);
            }

            off = SfxSongPanel.LinkState(false, null, dev16, song.data, out bold, out _, out _);
            Assert.IsTrue(off, "without an output SEND is off, and nothing is said of formats"); Assert.AreEqual("NOT CONNECTED", bold);
            // this editor has no port open: the panel says so, for either song
            StringAssert.StartsWith("NOT CONNECTED | ", Panel(song).LinkText); StringAssert.StartsWith("NOT CONNECTED | ", Panel(five).LinkText);
            Object.DestroyImmediate(five);
        }
    }
}
