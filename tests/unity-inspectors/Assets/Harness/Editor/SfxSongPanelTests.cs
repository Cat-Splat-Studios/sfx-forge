using System.Collections.Generic;
using System.Linq;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Inspector.Harness
{
    /// <summary>
    /// The song's Inspector without a window: every number it shows against the demo song's data (the firmware's own demo
    /// song: 120 BPM, 16 steps, swing 20, the arrangement A B A B C D C D E E A B C D C F), and the five numbers it can change.
    /// </summary>
    public class SfxSongPanelTests
    {
        const string DemoPath = "Assets/Data/DemoSong.asset";
        static readonly int[] Layers = { 0, 50, 0, 0, 0, 90, 35, 72, 0, 0, 0, 0, 20, 100, 60, 60 };
        static readonly int[] Volumes = { 100, 100, 68, 100, 100, 54, 52, 24, 100, 100, 100, 100, 50, 56, 52, 46 };
        static readonly int[] Pans = { 64, 64, 64, 64, 64, 48, 64, 76, 64, 64, 64, 64, 84, 36, 70, 64 };
        // FM BASS (sound 7) in pattern A, as docs/img/page-seq.png shows it
        static readonly string[] BassNotes = { "E1", null, null, "E1", null, "E2", null, "E1", "G1", null, "A1", null, null, "B1", "D2", null };

        SfxSong song;

        public static SfxSong Demo()
        {
            var asset = AssetDatabase.LoadAssetAtPath<SfxSong>(DemoPath);
            Assert.IsNotNull(asset, DemoPath);
            var copy = Object.Instantiate(asset);              // (the tests change it: never the asset itself)
            copy.name = "DemoSong";
            return copy;
        }

        static SfxSongPanel Panel(params Object[] songs)
        {
            var panel = (SfxSongPanel)SfxSongEditor.Build(new SerializedObject(songs));
            if (panel.ShowsSong) { panel.Page = SfxSongPanel.PageSteps; panel.Intensity = 127; }
            return panel;
        }

        [SetUp] public void SetUp() => song = Demo();
        [TearDown] public void TearDown() => Object.DestroyImmediate(song);

        [Test]
        public void TheSongReadsAsItsDataSays()
        {
            Assert.IsTrue(song.IsValid);
            Assert.AreEqual(120, song.Bpm);
            Assert.AreEqual(16, song.StepsPerPattern);
            Assert.AreEqual(20, song.Swing);
            Assert.AreEqual(2, song.Scale);
            Assert.AreEqual(6, song.Chain);
            Assert.AreEqual(16, song.ArrangementBars);
            Assert.AreEqual("ABABCDCDEEABCDCF", new string(Enumerable.Range(0, 16).Select(i => (char)('A' + song.ArrangementPattern(i))).ToArray()));
            for (int s = 0; s < 16; s++)
            {
                Assert.AreEqual(Layers[s], song.LayerRaw(s), "layer " + s);
                Assert.AreEqual(Layers[s] / 127f, song.Layer(s), 1e-6f);
                Assert.AreEqual(Volumes[s], song.VolumeRaw(s), "volume " + s);
                Assert.AreEqual(Pans[s], song.PanRaw(s), "pan " + s);
                Assert.IsFalse(song.Muted(s));
            }
            Assert.AreEqual(new[] { 2, 64, 91, 40, 0 }, new[] { song.Master(SfxMaster.DelayTime), song.Master(SfxMaster.DelayFeedback), song.Master(SfxMaster.ReverbSize), song.Master(SfxMaster.ReverbDamping), song.Master(SfxMaster.Drive) });

            for (int i = 0; i < 16; i++)
            {
                Assert.AreEqual(BassNotes[i] != null, song.StepOn(0, 6, i), "step " + i);
                if (BassNotes[i] != null) Assert.AreEqual(BassNotes[i], SfxUnits.NoteText(song.StepNote(0, 6, i)));
                Assert.AreEqual(i == 5 ? 7 : 5, song.StepVelocity(0, 6, i));
                Assert.AreEqual(0, song.StepLength(0, 6, i));
            }
            for (int i = 0; i < 16; i++) { Assert.IsFalse(song.StepOn(6, 6, i)); Assert.IsFalse(song.StepOn(7, 2, i)); }   // G and H are empty
            Assert.IsFalse(song.StepOn(8, 0, 0)); Assert.IsFalse(song.StepOn(0, 16, 0)); Assert.IsFalse(song.StepOn(0, 0, 16)); Assert.IsFalse(song.StepOn(-1, 0, 0));

            // two lanes: FM BASS's CUTOFF rises through pattern F, the STRINGS' VOLUME swells through E
            Assert.AreEqual(6, song.LaneSound(0)); Assert.AreEqual(SfxAutoTarget.FirstParam + (int)SfxParam.FilterCutoff, song.LaneTarget(0));
            Assert.AreEqual(15, song.LaneSound(1)); Assert.AreEqual(SfxAutoTarget.Volume, song.LaneTarget(1));
            for (int l = 2; l < SfxSong.Lanes; l++) { Assert.AreEqual(-1, song.LaneSound(l)); Assert.AreEqual(0, song.LaneTarget(l)); }
            for (int i = 0; i < 16; i++)
            {
                Assert.AreEqual(61 + i * 3, song.LanePoint(5, 0, i));
                Assert.AreEqual(i % 2 == 0 ? 18 + i * 3 : -1, song.LanePoint(4, 1, i));
                Assert.AreEqual(-1, song.LanePoint(0, 0, i));
            }
            Assert.AreEqual(1, song.AutomationLanes(6)); Assert.AreEqual(1, song.AutomationLanes(15)); Assert.AreEqual(0, song.AutomationLanes(0));
        }

        // the same song in the forms before this one: the accessors find the steps where each form keeps them
        [Test]
        public void OlderFormsOfASongReadTheSame()
        {
            byte[] v5 = song.data;
            const int Head5 = 147, Block5 = 35;

            var v3 = new List<byte>(v5.Take(66)); v3[0] = 3;
            v3.AddRange(v5.Skip(Head5).Take(8 * 16 * Block5));
            var v2 = new List<byte>(v5.Take(66)); v2[0] = 2;
            var v1 = new List<byte>(v5.Take(23)); v1[0] = 1;
            var v4 = new List<byte>(v5.Take(66)); v4[0] = 4;
            for (int t = 0; t < 16; t++) v4.Add((byte)(t == 6 ? 7 : 0));                 // one lane a sound: FM BASS's moves its CUTOFF
            v4.AddRange(v5.Skip(66).Take(33));
            for (int k = 0; k < 8 * 16; k++)
            {
                var block = v5.Skip(Head5 + k * Block5).Take(Block5).ToArray();
                v2.AddRange(block.Take(19)); v1.AddRange(block.Take(19));
                v4.AddRange(block);
                for (int i = 0; i < 16; i++) v4.Add((byte)(k == 5 * 16 + 6 ? 62 + i * 3 : 0));
            }
            Assert.AreEqual(SfxProtocol.SongBytesV3, v3.Count); Assert.AreEqual(SfxProtocol.SongBytesV2, v2.Count);
            Assert.AreEqual(SfxProtocol.SongBytesV1, v1.Count); Assert.AreEqual(SfxProtocol.SongBytesV4, v4.Count);

            foreach (var form in new[] { v1, v2, v3, v4 })
            {
                var old = ScriptableObject.CreateInstance<SfxSong>();
                old.Set(song.sounds, form.ToArray());
                int ver = form[0];
                Assert.IsTrue(old.IsValid, "form " + ver);
                Assert.AreEqual(120, old.Bpm); Assert.AreEqual(20, old.Swing); Assert.AreEqual(6, old.Chain);
                for (int p = 0; p < 8; p++) for (int s = 0; s < 16; s++) for (int i = 0; i < 16; i++)
                {
                    Assert.AreEqual(song.StepOn(p, s, i), old.StepOn(p, s, i), $"form {ver} {p} {s} {i}");
                    Assert.AreEqual(song.StepNote(p, s, i), old.StepNote(p, s, i));
                    Assert.AreEqual(ver >= 3 ? song.StepVelocity(p, s, i) : 5, old.StepVelocity(p, s, i));
                }
                for (int s = 0; s < 16; s++)
                {
                    Assert.AreEqual(Layers[s], old.LayerRaw(s));
                    Assert.AreEqual(ver >= 2 ? Volumes[s] : 100, old.VolumeRaw(s));
                    Assert.AreEqual(ver >= 2 ? Pans[s] : 64, old.PanRaw(s));
                }
                Assert.AreEqual(91, old.Master(SfxMaster.ReverbSize));
                Assert.AreEqual(ver >= 4 ? 16 : 0, old.ArrangementBars);
                Assert.AreEqual(ver == 4 ? 6 : -1, old.LaneSound(6));
                Assert.AreEqual(ver == 4 ? 7 : 0, old.LaneTarget(6));
                Assert.AreEqual(ver == 4 ? 61 + 3 * 3 : -1, old.LanePoint(5, 6, 3));
                Assert.AreEqual(-1, old.LanePoint(0, 6, 3));

                var panel = Panel(old);                         // … and the Inspector shows each without a complaint
                Assert.IsTrue(panel.ShowsSong);
                for (int page = 0; page < 4; page++) panel.Page = page;
                Assert.AreEqual(ver >= 4 ? "ABABCDCDEEABCDCF" : "", panel.ArrangementText);
                Assert.AreEqual(ver >= 4 ? "CHAIN (unused)" : "CHAIN", panel.SongTile(SfxSongPanel.TileChain).LabelText);
                Assert.AreEqual(ver == 4 ? 1 : 0, panel.LaneCount);
                Assert.AreEqual(ver >= 2, panel.SetVolume(0, 90), "a song from before the mixer has no fader to move");
                Assert.IsTrue(old.IsValid);
                Assert.IsNotNull(SfxSongPanel.RenderBar(old, 0, 1f), "the engine takes form " + ver);
                Object.DestroyImmediate(old);
            }
        }

        [Test]
        public void ThePanelShowsTheSongsNumbers()
        {
            var panel = Panel(song);
            Assert.IsTrue(panel.ShowsSong);
            Assert.AreEqual("TEMPO", panel.SongTile(SfxSongPanel.TileTempo).LabelText); Assert.AreEqual("120 BPM", panel.SongTile(SfxSongPanel.TileTempo).ValueText);
            Assert.AreEqual("STEPS", panel.SongTile(SfxSongPanel.TileSteps).LabelText); Assert.AreEqual("16", panel.SongTile(SfxSongPanel.TileSteps).ValueText);
            Assert.AreEqual("SWING", panel.SongTile(SfxSongPanel.TileSwing).LabelText); Assert.AreEqual("20%", panel.SongTile(SfxSongPanel.TileSwing).ValueText);
            Assert.AreEqual("CHAIN (unused)", panel.SongTile(SfxSongPanel.TileChain).LabelText); Assert.AreEqual("A-F", panel.SongTile(SfxSongPanel.TileChain).ValueText);   // as out/song.png
            Assert.AreEqual("ABABCDCDEEABCDCF", panel.ArrangementText);
            StringAssert.StartsWith("ARRANGEMENT   16 bars", panel.ArrangementCaption);
            Assert.AreEqual(0, panel.Pattern, "it opens on the arrangement's first pattern");
            Assert.AreEqual("SONG | PATTERN A | scale MINOR", panel.TitleText);
            // the title follows the page, as the device's: MIXER, MASTER EFFECTS, AUTOMATION PATTERN A (docs/img/page-mix.png, page-master.png, page-auto.png)
            panel.Page = SfxSongPanel.PageMix; Assert.AreEqual("MIXER", panel.TitleText);
            panel.Page = SfxSongPanel.PageMaster; Assert.AreEqual("MASTER EFFECTS", panel.TitleText);
            panel.Page = SfxSongPanel.PageAuto; panel.Pattern = 5; Assert.AreEqual("AUTOMATION | PATTERN F", panel.TitleText);
            panel.Pattern = 0; panel.Page = SfxSongPanel.PageSteps;
            Assert.AreEqual("SONG | PATTERN A | scale MINOR", panel.TitleText);

            panel.Sound = 6;                                    // FM BASS: docs/img/page-song.png says LAYER 28%, page-mix.png VOLUME 52, PAN C
            Assert.AreEqual("28%", panel.SoundTile(SfxSongPanel.TileLayer).ValueText);
            Assert.AreEqual("52", panel.SoundTile(SfxSongPanel.TileVolume).ValueText);
            Assert.AreEqual("C", panel.SoundTile(SfxSongPanel.TilePan).ValueText);
            Assert.AreEqual("SOUND 7 plays from 28%", panel.LayerBar.Text);
            Assert.AreEqual("100%", panel.Heat.Text);
            Assert.AreEqual("SOUND 7   SCI-FI FM | pattern A:  E1 · · E1 · E2 · E1 G1 · A1 · · B1 D2 ·   · 1 lane", panel.SoundText);
            panel.Sound = 7;                                    // FM KEYS: a little to the right
            Assert.AreEqual("57%", panel.SoundTile(SfxSongPanel.TileLayer).ValueText);
            Assert.AreEqual("24", panel.SoundTile(SfxSongPanel.TileVolume).ValueText);
            Assert.AreEqual("R12", panel.SoundTile(SfxSongPanel.TilePan).ValueText);
            panel.Sound = 0; panel.Pattern = 6;
            StringAssert.EndsWith("pattern G:  it rests", panel.SoundText);

            // the marks over INTENSITY: one for each sound that plays anywhere in the song, where its LAYER is
            var marks = new SortedSet<string>();
            int marked = 0;
            for (int s = 0; s < 16; s++)
            {
                if (panel.Heat.Mark(s) < 0) continue;
                marked++;
                Assert.AreEqual(Layers[s], panel.Heat.Mark(s));
                marks.Add((panel.Heat.Mark(s) / 127f).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture));
            }
            Assert.AreEqual(9, marked, "sounds 2 3 6 7 8 13 14 15 16 have steps");
            Assert.AreEqual("0.00 0.16 0.28 0.39 0.47 0.57 0.71 0.79", string.Join(" ", marks));

            // MIX: a tile a sound; MASTER: the song's own (the device's plain ones)
            for (int s = 0; s < 16; s++) { Assert.AreEqual(SfxSongPanel.SoundName(song, s), panel.MixTileAt(s).NameText); Assert.IsFalse(panel.MixTileAt(s).ShowsMute); }
            Assert.AreEqual("7 FM", panel.MixTileAt(6).NameText); Assert.AreEqual("13 FOLEY", panel.MixTileAt(12).NameText); Assert.AreEqual("1 IMPACT", panel.MixTileAt(0).NameText);
            string[] masterNames = { "DELAY TIME", "FEEDBACK", "ROOM SIZE", "DAMPING", "DRIVE" }, masterValues = { "3/16", "50%", "72%", "31%", "0%" };
            for (int i = 0; i < 5; i++) { Assert.AreEqual(masterNames[i], panel.MasterTile(i).LabelText); Assert.AreEqual(masterValues[i], panel.MasterTile(i).ValueText); Assert.IsFalse(panel.MasterTile(i).Interactive); }
            Assert.IsFalse(panel.SongTile(SfxSongPanel.TileSteps).Interactive); Assert.IsFalse(panel.SongTile(SfxSongPanel.TileChain).Interactive);

            // what only shows looks it: an outline and a dim dial - what can be changed is a lit tile
            foreach (var view in new[] { panel.SongTile(SfxSongPanel.TileSteps), panel.SongTile(SfxSongPanel.TileChain), panel.MasterTile(0), panel.MasterTile(4) })
            {
                Assert.IsTrue(view.ClassListContains("sfx-tile--view"), view.LabelText); Assert.IsTrue(view.Knob.Unlit); Assert.IsFalse(view.focusable);
            }
            foreach (var live in new[] { panel.SongTile(SfxSongPanel.TileTempo), panel.SongTile(SfxSongPanel.TileSwing), panel.SoundTile(SfxSongPanel.TileLayer), panel.SoundTile(SfxSongPanel.TileVolume), panel.SoundTile(SfxSongPanel.TilePan) })
            {
                Assert.IsFalse(live.ClassListContains("sfx-tile--view"), live.LabelText); Assert.IsFalse(live.Knob.Unlit); Assert.IsTrue(live.focusable);
            }
        }

        // a lane whose sound is gone is free, whatever knob it still names (sfx_song_unpack takes such a song; the player leaves the lane alone)
        [Test]
        public void ALaneWithoutASoundIsFree()
        {
            int free = -1;
            for (int l = 0; l < SfxSong.Lanes && free < 0; l++) if (song.LaneSound(l) < 0) free = l;
            Assert.GreaterOrEqual(free, 0);
            int at = 66 + 1 + SfxSong.MaxBars + free * 2;       // its two bytes: the sound + 1, the knob
            Assert.AreEqual(0, song.data[at]); Assert.AreEqual(0, song.data[at + 1]);
            song.data[at + 1] = 7;                              // CUTOFF, of no sound
            Assert.IsTrue(song.IsValid);
            Assert.AreEqual(-1, song.LaneSound(free));
            Assert.AreEqual(0, song.LaneTarget(free), "a free lane moves nothing");
            var panel = Panel(song);
            panel.Page = SfxSongPanel.PageAuto;
            Assert.AreEqual(2, panel.LaneCount);
            Assert.AreEqual(16, SfxSong.MaxSteps);
            panel.Page = SfxSongPanel.PageSteps;
        }

        // the panel builds itself again when the song goes and comes back (Reset, then Undo): its lanes come back with it
        [Test]
        public void TheLanesComeBackWhenTheSongDoes()
        {
            var panel = Panel(song);
            panel.Page = SfxSongPanel.PageAuto; panel.Pattern = 5;
            Assert.AreEqual(2, panel.Query(className: "sfx-lane").ToList().Count);
            string first = panel.LaneText(0);
            byte[] data = song.data;
            song.data = new byte[0];                            // the song is gone
            panel.Refresh();
            Assert.IsFalse(panel.ShowsSong);
            Assert.AreEqual(0, panel.Query(className: "sfx-lane").ToList().Count);
            song.data = data;                                   // ... and back
            panel.Refresh();
            Assert.IsTrue(panel.ShowsSong);
            panel.Page = SfxSongPanel.PageAuto; panel.Pattern = 5;
            Assert.AreEqual(2, panel.LaneCount);
            Assert.AreEqual(2, panel.Query(className: "sfx-lane").ToList().Count, "the lanes are in the page that shows");
            Assert.AreEqual(first, panel.LaneText(0));
            panel.Page = SfxSongPanel.PageSteps;
        }

        [Test]
        public void TheStepsAreTheSongs()
        {
            var panel = Panel(song);
            for (int p = 0; p < 8; p++)
            {
                panel.Pattern = p;
                for (int s = 0; s < 16; s++) for (int i = 0; i < 16; i++)
                {
                    Assert.AreEqual(song.StepOn(p, s, i), panel.Row(s).Level[i] > 0f, $"{p} {s} {i}");
                    if (song.StepOn(p, s, i)) Assert.AreEqual((6 + song.StepVelocity(p, s, i) * 2) / 20f, panel.Row(s).Level[i], 1e-6f);
                }
            }
            panel.Pattern = 0; panel.Sound = 6;
            Assert.AreEqual(1f, panel.Row(6).Level[5], 1e-6f, "the E2 is played full");
            Assert.AreEqual(0.8f, panel.Row(6).Level[0], 1e-6f);
            Assert.IsTrue(panel.Row(6).Selected); Assert.IsFalse(panel.Row(5).Selected);
            for (int i = 0; i < 16; i++) Assert.AreEqual(BassNotes[i] ?? "", panel.NoteOnStep(i), "the note on step " + i);

            // looked at with less intensity: the sounds whose LAYER is above it go dim (they would not play)
            for (int s = 0; s < 16; s++) Assert.IsFalse(panel.Row(s).Dimmed);
            panel.Intensity = 34;
            for (int s = 0; s < 16; s++) Assert.AreEqual(Layers[s] > 34, panel.Row(s).Dimmed, "sound " + s);
            Assert.AreEqual("27%", panel.Heat.Text);
            panel.Intensity = 35;
            Assert.IsFalse(panel.Row(6).Dimmed, "FM BASS comes in at 35 of 127: 28%");
            Assert.AreEqual(song.data, Demo().data, "looking changes nothing");
            panel.Intensity = 127;
        }

        [Test]
        public void TheLanesAreListed()
        {
            var panel = Panel(song);
            panel.Page = SfxSongPanel.PageAuto;
            Assert.AreEqual(2, panel.LaneCount);
            panel.Pattern = 5;
            Assert.AreEqual($"7 FM | CUTOFF | F: 16 points, {SfxUnits.ParamText(SfxParam.FilterCutoff, 61)} to {SfxUnits.ParamText(SfxParam.FilterCutoff, 106)}", panel.LaneText(0));
            Assert.AreEqual("16 WAVE | VOLUME | no points in F – it moves in E", panel.LaneText(1));
            panel.Pattern = 4;
            Assert.AreEqual("7 FM | CUTOFF | no points in E – it moves in F", panel.LaneText(0));
            Assert.AreEqual("16 WAVE | VOLUME | E: 8 points, 18 to 60", panel.LaneText(1));
            panel.Page = SfxSongPanel.PageSteps;
        }

        [Test]
        public void TheAutomationTextsAreTheDevices()
        {
            Assert.AreEqual("OFF", SfxUnits.AutoName(0, SfxEngine.Impact));
            Assert.AreEqual("FREQ", SfxUnits.AutoName(1, SfxEngine.Impact));
            Assert.AreEqual("CUTOFF", SfxUnits.AutoName(7, SfxEngine.SciFiFm));
            Assert.AreEqual("BAND", SfxUnits.AutoName(7, SfxEngine.Foley));
            Assert.AreEqual("POSITION", SfxUnits.AutoName(4, SfxEngine.Wavetable));
            Assert.AreEqual("TABLE", SfxUnits.AutoName(25, SfxEngine.Wavetable));
            Assert.AreEqual("VOLUME", SfxUnits.AutoName(26, SfxEngine.Impact));
            Assert.AreEqual("PAN", SfxUnits.AutoName(27, SfxEngine.Impact));
            string[] master = { "DELAY TIME", "FEEDBACK", "ROOM SIZE", "DAMPING", "DRIVE" };
            for (int i = 0; i < 5; i++) Assert.AreEqual(master[i], SfxUnits.AutoName(28 + i, SfxEngine.Impact));
            Assert.AreEqual("OFF", SfxUnits.AutoName(33, SfxEngine.Impact));

            Assert.AreEqual(SfxUnits.ParamText(SfxParam.FilterCutoff, 100), SfxUnits.AutoText(7, 100));
            Assert.AreEqual("VOWEL", SfxUnits.AutoText(25, 3));
            Assert.AreEqual("100", SfxUnits.AutoText(26, 100));
            Assert.AreEqual("L12", SfxUnits.AutoText(27, 52)); Assert.AreEqual("C", SfxUnits.AutoText(27, 64));
            Assert.AreEqual("1/4", SfxUnits.AutoText(28, 3));
            Assert.AreEqual("50%", SfxUnits.AutoText(29, 64));
            Assert.AreEqual(5, SfxUnits.AutoHi(28)); Assert.AreEqual(5, SfxUnits.AutoHi(25)); Assert.AreEqual(126, SfxUnits.AutoHi(7)); Assert.AreEqual(126, SfxUnits.AutoHi(26));
        }

        [Test]
        public void TheFiveNumbersChangeTheAssetAndUndoTakesThemBack()
        {
            var panel = Panel(song);
            EditorUtility.ClearDirty(song);
            byte[] before = (byte[])song.data.Clone();

            Assert.IsTrue(panel.SetTempo(140));
            Assert.AreEqual(140, song.Bpm);
            Assert.AreEqual("140 BPM", panel.SongTile(SfxSongPanel.TileTempo).ValueText);
            Assert.IsTrue(EditorUtility.IsDirty(song));
            Assert.IsFalse(panel.SetTempo(140), "the same again: nothing changed");
            Undo.PerformUndo();
            panel.Refresh();
            Assert.AreEqual(120, song.Bpm);
            Assert.AreEqual(before, song.data);
            Undo.PerformRedo();
            panel.Refresh();
            Assert.AreEqual(140, song.Bpm);
            Assert.AreEqual("140 BPM", panel.SongTile(SfxSongPanel.TileTempo).ValueText);

            panel.SetTempo(999); Assert.AreEqual(240, song.Bpm);
            panel.SetTempo(1); Assert.AreEqual(40, song.Bpm);
            panel.SetSwing(250); Assert.AreEqual(100, song.Swing); Assert.AreEqual("100%", panel.SongTile(SfxSongPanel.TileSwing).ValueText);
            panel.SetSwing(-3); Assert.AreEqual(0, song.Swing);

            panel.Sound = 6;
            Assert.IsTrue(panel.SetLayer(6, 64));
            Assert.AreEqual(64, song.LayerRaw(6)); Assert.AreEqual(64 / 127f, song.Layer(6), 1e-6f);
            Assert.AreEqual("50%", panel.SoundTile(SfxSongPanel.TileLayer).ValueText);
            Assert.AreEqual("SOUND 7 plays from 50%", panel.LayerBar.Text);
            Assert.AreEqual(64, panel.Heat.Mark(6));
            Assert.IsTrue(panel.SetVolume(6, 300)); Assert.AreEqual(127, song.VolumeRaw(6)); Assert.AreEqual(1.27f, song.Volume(6), 1e-6f);
            Assert.IsTrue(panel.SetPan(6, 0)); Assert.AreEqual(0, song.PanRaw(6)); Assert.AreEqual(-1f, song.Pan(6), 1e-6f);
            Assert.AreEqual("L64", panel.SoundTile(SfxSongPanel.TilePan).ValueText);
            Assert.IsFalse(panel.SetLayer(16, 5)); Assert.IsFalse(panel.SetVolume(-1, 5));

            // nothing else moved, and the engine still takes it
            Assert.IsTrue(song.IsValid);
            var changed = new List<int>();
            for (int i = 0; i < before.Length; i++) if (before[i] != song.data[i]) changed.Add(i);
            Assert.AreEqual(new[] { 3, 4, 7 + 6, 23 + 6, 39 + 6 }, changed.ToArray());   // (tempo 40: its high byte is 0 as before)
            if (SfxNative.Available) Assert.IsNotNull(SfxSongPanel.RenderBar(song, 0, 1f));
        }

        [Test]
        public void OneBarSoundsAndLessIntensityIsLess()
        {
            if (!SfxNative.Available) Assert.Ignore("no native library here");
            float[] full = SfxSongPanel.RenderBar(song, 0, 1f), low = SfxSongPanel.RenderBar(song, 0, 0f);
            int bar = (int)(16 * 15.0 / 120 * 48000);           // 2 s
            Assert.Greater(full.Length / 2, bar - 100); Assert.LessOrEqual(full.Length / 2, bar + 4 * 48000);
            double a = 0, b = 0;
            foreach (float v in full) a += v * v;
            foreach (float v in low) b += v * v;
            Assert.Greater(b, 1.0, "at intensity 0 the kick and what else has no layer still play");
            Assert.Greater(a, b * 1.05, "the layers add to it");
            float peak = full.Max(v => v < 0f ? -v : v);
            Assert.Less(peak, 1.5f);
            Assert.IsNull(SfxSongPanel.RenderBar(ScriptableObject.CreateInstance<SfxSong>(), 0, 1f));
            UnityEngine.Debug.Log($"SONG BAR {full.Length / 2} samples at full intensity (rms {System.Math.Sqrt(a / full.Length):0.000}), {low.Length / 2} at none (rms {System.Math.Sqrt(b / low.Length):0.000})");
        }

        [Test]
        public void NoSongAndSeveralSongsOnlySaySo()
        {
            var empty = ScriptableObject.CreateInstance<SfxSong>();
            empty.name = "Empty";
            var none = Panel(empty);
            Assert.IsFalse(none.ShowsSong);
            Assert.IsFalse(none.SetTempo(100));
            none.Refresh();
            Assert.IsNotNull(none.Q<TextElement>(className: "sfx-song-message"));

            empty.Set(song.sounds, song.data);                  // a song is fetched into it: the panel becomes the song
            none.Refresh();
            Assert.IsTrue(none.ShowsSong);
            Assert.AreEqual("120 BPM", none.SongTile(SfxSongPanel.TileTempo).ValueText);

            var other = Demo();
            var both = Panel(song, other);
            Assert.IsFalse(both.ShowsSong);
            Assert.IsFalse(both.SetLayer(0, 9));
            both.Refresh();
            Assert.AreEqual(0, other.LayerRaw(0));
            Object.DestroyImmediate(other); Object.DestroyImmediate(empty);
        }
    }
}
