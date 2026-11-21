using System.Collections.Generic;
using System.IO;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using Object = UnityEngine.Object;

namespace Inspector.Harness
{
    /// <summary>
    /// The batch bake with real asset files: what a selection holds, which files a run writes and where, that it writes
    /// the same bytes again, what it refuses - and the bake window's view, fed with a model. The assets are made in a
    /// folder of their own, which is deleted afterwards.
    /// </summary>
    public class SfxBatchBakeTests
    {
        const string Folder = "Assets/BakeTemp", Sub = Folder + "/Sub";
        static readonly SfxPatch Thud = SfxPatch.Create(SfxEngine.Impact, 24, 90, 35, 0, 0, 5, 70, 0, 70, 0);

        SfxPreset laser, thud, twin;
        SfxVariationSet steps;

        [SetUp]
        public void SetUp()
        {
            SfxUI.PretendNativeMissing = false;
            if (AssetDatabase.IsValidFolder(Folder)) AssetDatabase.DeleteAsset(Folder);
            laser = SfxForgeEditorLink.CreatePreset(SfxPatch.Default, Folder, "Laser");
            thud = SfxForgeEditorLink.CreatePreset(Thud, Folder, "Thud");
            twin = SfxForgeEditorLink.CreatePreset(SfxGenerator.Make(SfxCategory.Zap, 5), Sub, "Laser");      // the same name in another folder
            steps = SfxVariationSetTests.MakeSet("Steps", 3, Thud, Folder);
        }

        [TearDown]
        public void TearDown()
        {
            SfxUI.PretendNativeMissing = false;
            AssetDatabase.DeleteAsset(Folder);
        }

        static List<string> Wavs(string folder)
        {
            var files = new List<string>();
            foreach (string file in Directory.GetFiles(folder, "*.wav", SearchOption.AllDirectories)) files.Add(file.Replace('\\', '/'));
            files.Sort(string.CompareOrdinal);
            return files;
        }

        [Test]
        public void CollectFindsPresetsAndSetsInAFolderOnce()
        {
            var folder = AssetDatabase.LoadAssetAtPath<Object>(Folder);
            Assert.IsNotNull(folder);
            List<Object> found = SfxBatchBake.Collect(new[] { thud, folder, laser, folder, steps.preset });
            var paths = found.ConvertAll(AssetDatabase.GetAssetPath);
            Assert.AreEqual(new[] { Folder + "/Laser.asset", Folder + "/Steps Sound.asset", Folder + "/Steps.asset", Folder + "/Sub/Laser.asset", Folder + "/Thud.asset" }, paths.ToArray(), "sorted by path, each once, the subfolder too");
            Assert.AreEqual(4 + 3, SfxBatchBake.FileCount(found, out int presets, out int sets));
            Assert.AreEqual(4, presets); Assert.AreEqual(1, sets);

            Assert.AreEqual(0, SfxBatchBake.Collect(null).Count); Assert.AreEqual(0, SfxBatchBake.Collect(new Object[] { null, new Texture2D(2, 2) }).Count);
            var one = SfxBatchBake.Collect(new Object[] { laser });
            Assert.AreEqual(1, one.Count); Assert.AreSame(laser, one[0]);
            Assert.AreEqual(1, SfxBatchBake.Collect(new[] { AssetDatabase.LoadAssetAtPath<Object>(Sub) }).Count, "a folder alone: what is in it");
        }

        [Test]
        public void RunWritesOneFilePerPresetAndOnePerVariant()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            var options = new SfxBakeOptions { sampleRate = 24000, mono = true };
            int calls = 0;
            SfxBakeReport report = SfxBatchBake.Run(new Object[] { laser, steps, twin }, options, SfxBakeTo.NextToEachAsset, null, (file, done) => { calls++; return false; });
            Assert.AreEqual(new[] { Folder + "/Laser.wav", Folder + "/Steps_01.wav", Folder + "/Steps_02.wav", Folder + "/Steps_03.wav", Sub + "/Laser.wav" }, Wavs(Folder).ToArray());
            Assert.AreEqual(5, report.lines.Count, report.Text); Assert.AreEqual(5, report.Written); Assert.AreEqual(0, report.Failed); Assert.AreEqual(5, report.Planned); Assert.AreEqual(5, calls);
            Assert.AreEqual("5 files written \u2013 next to each asset", report.Summary);
            Assert.AreEqual(Sub + "/Laser.wav", report.LastFile);
            Assert.AreEqual(Folder + "/Laser.asset", report.lines[0].source); Assert.AreEqual(Folder + "/Laser.wav", report.lines[0].file);
            StringAssert.StartsWith("Laser.wav  0.", report.lines[0].Text);
            foreach (string file in Wavs(Folder))
            {
                var clip = AssetDatabase.LoadAssetAtPath<AudioClip>(file);
                Assert.IsNotNull(clip, file);
                Assert.AreEqual(24000, clip.frequency, file); Assert.AreEqual(1, clip.channels, file);
            }
            Assert.IsFalse(steps.IsBaked, "export files: the set's own clips and its stamp are not touched");
            Assert.IsFalse(AssetDatabase.IsValidFolder(Folder + "/Steps Variants"));

            // the lines say what there is to say: a sound over full scale, a normalised one, a silent one
            Assert.IsTrue(SfxPatchText.TryParse(SfxToolsPanelTests.ClippingText, out SfxPatch loud));
            SfxPreset hot = SfxForgeEditorLink.CreatePreset(loud, Folder, "Hot"), mute = SfxForgeEditorLink.CreatePreset(new SfxPatch { engine = SfxEngine.Impact }, Folder, "Mute");
            report = SfxBatchBake.Run(new Object[] { hot, mute }, new SfxBakeOptions(), SfxBakeTo.NextToEachAsset);
            Assert.IsTrue(report.lines[0].clips); Assert.IsTrue(report.lines[0].Warns); Assert.IsFalse(report.lines[0].failed);
            StringAssert.StartsWith("clips: +", report.lines[0].note); StringAssert.EndsWith("dB over full scale, the file is clamped \u2013 lower LEVEL or NORMALISE", report.lines[0].note);
            Assert.AreEqual("silent", report.lines[1].note); Assert.AreEqual(2, report.Written);
            report = SfxBatchBake.Run(new Object[] { hot }, new SfxBakeOptions { normaliseDb = -1f }, SfxBakeTo.NextToEachAsset);
            Assert.IsFalse(report.lines[0].clips); Assert.IsNull(report.lines[0].note); Assert.AreEqual(-1f, report.lines[0].peakDb, 0.01f); Assert.Less(report.lines[0].gainDb, -1f);

            // cancelled after the second file
            calls = 0;
            report = SfxBatchBake.Run(new Object[] { laser, steps, twin }, options, SfxBakeTo.NextToEachAsset, null, (file, done) => ++calls > 2);
            Assert.IsTrue(report.Cancelled); Assert.AreEqual(2, report.Written);
            Assert.AreEqual("cancelled after 2 of 5 files", report.Summary);
        }

        [Test]
        public void OneFolderPutsThemTogetherAndNumbersEqualNames()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            string into = Folder + "/Out/Deep";
            var loose = ScriptableObject.CreateInstance<SfxPreset>();        // a sound that is no asset: one folder takes it too
            loose.name = "Loose";
            try
            {
                SfxBakeReport report = SfxBatchBake.Run(SfxBatchBake.Collect(new Object[] { AssetDatabase.LoadAssetAtPath<Object>(Folder), loose }), new SfxBakeOptions(), SfxBakeTo.OneFolder, into + "/");
                Assert.AreEqual(new[] { into + "/Laser 2.wav", into + "/Laser.wav", into + "/Loose.wav", into + "/Steps Sound.wav", into + "/Steps_01.wav", into + "/Steps_02.wav", into + "/Steps_03.wav", into + "/Thud.wav" },
                                Wavs(Folder).ToArray(), report.Text);
                Assert.AreEqual(8, report.Written); Assert.AreEqual("8 files written \u2013 " + into, report.Summary);
                foreach (var line in report.lines) if (line.file == into + "/Laser 2.wav") Assert.AreEqual(Sub + "/Laser.asset", line.source, "the second of the name, in path order");
                Assert.IsNotNull(AssetDatabase.LoadAssetAtPath<AudioClip>(into + "/Laser 2.wav"));

                report = SfxBatchBake.Run(new Object[] { loose, laser }, new SfxBakeOptions(), SfxBakeTo.NextToEachAsset);   // next to an asset that is none
                Assert.AreEqual(1, report.Failed); Assert.AreEqual(1, report.Written);
                Assert.AreEqual("'Loose': not inside Assets \u2013 choose ONE FOLDER", report.lines[0].note);
            }
            finally { Object.DestroyImmediate(loose); }
        }

        /// <summary>No two assets of a run write the same file: the later one is numbered until its files are its own.</summary>
        [Test]
        public void NoTwoAssetsOfARunWriteTheSameFile()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            string into = Folder + "/Together", sub = Folder + "/Names";
            AssetDatabase.CreateFolder(Folder, "Names");
            AssetDatabase.CreateFolder(sub, "Deeper");
            // one folder: "Hit", another "Hit", and a preset that is itself called "Hit 2"
            Object first = SfxForgeEditorLink.CreatePreset(SfxPatch.Default, sub, "Hit");
            Object second = SfxForgeEditorLink.CreatePreset(SfxGenerator.Make(SfxCategory.Thud, 3), sub + "/Deeper", "Hit");
            Object third = SfxForgeEditorLink.CreatePreset(SfxGenerator.Make(SfxCategory.Thud, 4), sub, "Hit 2");
            List<Object> assets = SfxBatchBake.Collect(new Object[] { AssetDatabase.LoadAssetAtPath<Object>(sub) });
            Assert.AreEqual(3, assets.Count);
            SfxBakeReport report = SfxBatchBake.Run(assets, null, SfxBakeTo.OneFolder, into);
            Assert.AreEqual(3, report.Written, report.Text); Assert.AreEqual(0, report.Failed);
            var files = new HashSet<string>();
            foreach (var line in report.lines) Assert.IsTrue(files.Add(line.file), line.file + " is written twice");
            Assert.AreEqual(3, Directory.GetFiles(into, "*.wav").Length, "three sounds, three files");
            Assert.IsTrue(files.SetEquals(new[] { into + "/Hit.wav", into + "/Hit 2.wav", into + "/Hit 3.wav" }), string.Join(", ", files));

            // next to each asset: a set "Foot" writes Foot_01 ... Foot_03, and a preset beside it is called "Foot_02"
            SfxVariationSet foot = SfxVariationSetTests.MakeSet("Foot", 3, SfxPatch.Create(SfxEngine.Impact, 30, 84, 25, 20, 60, 50, 90, 10, 34, 0), sub + "/Deeper");
            Object clash = SfxForgeEditorLink.CreatePreset(SfxPatch.Default, sub + "/Deeper", "Foot_02");
            report = SfxBatchBake.Run(new Object[] { foot, clash }, null, SfxBakeTo.NextToEachAsset);
            Assert.AreEqual(4, report.Written, report.Text);
            files.Clear();
            foreach (var line in report.lines) Assert.IsTrue(files.Add(line.file), line.file + " is written twice");
            Assert.IsTrue(files.SetEquals(new[] { sub + "/Deeper/Foot_01.wav", sub + "/Deeper/Foot_02.wav", sub + "/Deeper/Foot_03.wav", sub + "/Deeper/Foot_02 2.wav" }), string.Join(", ", files));
            Assert.AreEqual(4, Directory.GetFiles(sub + "/Deeper", "*.wav").Length);
            // ... and the other way round in one folder: a set whose second file a preset already took gets a number as a whole
            report = SfxBatchBake.Run(new Object[] { clash, foot }, null, SfxBakeTo.OneFolder, into + "2");
            files.Clear();
            foreach (var line in report.lines) files.Add(line.file);
            Assert.IsTrue(files.SetEquals(new[] { into + "2/Foot_02.wav", into + "2/Foot 2_01.wav", into + "2/Foot 2_02.wav", into + "2/Foot 2_03.wav" }), string.Join(", ", files));
        }

        [Test]
        public void TheSameRunTwiceWritesTheSameBytesAndKeepsTheGuids()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            var assets = new Object[] { laser, thud, steps };
            var options = new SfxBakeOptions { sampleRate = 44100, trimDb = -48f, normaliseDb = -3f, semitones = 3f, velocity = 0.8f };
            SfxBatchBake.Run(assets, options, SfxBakeTo.NextToEachAsset);
            var files = Wavs(Folder);
            Assert.AreEqual(5, files.Count);
            var bytes = files.ConvertAll(File.ReadAllBytes); var guids = files.ConvertAll(AssetDatabase.AssetPathToGUID);
            SfxBatchBake.Run(assets, options, SfxBakeTo.NextToEachAsset);
            for (int i = 0; i < files.Count; i++)
            {
                Assert.AreEqual(bytes[i], File.ReadAllBytes(files[i]), files[i]);
                Assert.AreEqual(guids[i], AssetDatabase.AssetPathToGUID(files[i]), "overwritten in place: " + files[i]);
                Assert.IsNotEmpty(guids[i]);
            }
            SfxBatchBake.Run(assets, new SfxBakeOptions(), SfxBakeTo.NextToEachAsset);       // other options: other bytes, the same files
            Assert.AreNotEqual(bytes[0], File.ReadAllBytes(files[0]));
            Assert.AreEqual(guids[0], AssetDatabase.AssetPathToGUID(files[0]));
        }

        [Test]
        public void AFolderOutsideAssetsIsRefusedAndNothingIsWritten()
        {
            foreach (string folder in new[] { "../Elsewhere", "Packages/com.catsplat.sfxforge", "", "/Outside", "Assets/../../Out", "AssetsBeside" })
            {
                SfxBakeReport report = SfxBatchBake.Run(new Object[] { laser, steps }, new SfxBakeOptions(), SfxBakeTo.OneFolder, folder);
                if (!SfxNative.Available) { Assert.AreEqual(1, report.Failed); continue; }
                Assert.AreEqual(1, report.lines.Count); Assert.AreEqual(1, report.Failed); Assert.AreEqual(0, report.Written);
                Assert.AreEqual("'" + folder + "' is not inside Assets", report.lines[0].note);
                Assert.AreEqual("0 files written, 1 failed", report.Summary);
            }
            Assert.AreEqual(0, Wavs(Folder).Count);
            Assert.IsFalse(Directory.Exists("../Elsewhere"));

            SfxBakeReport refused = SfxBatchBake.Run(new Object[] { laser }, new SfxBakeOptions { sampleRate = 96000 }, SfxBakeTo.NextToEachAsset);
            Assert.AreEqual(1, refused.Failed); Assert.AreEqual("options: sample rate 96000 is outside 8000..48000", refused.lines[0].note);
            Assert.AreEqual(0, Wavs(Folder).Count);

            if (!SfxNative.Available) return;
            steps.preset = null;                                // a set without its sound
            SfxBakeReport report2 = SfxBatchBake.Run(new Object[] { steps, laser }, new SfxBakeOptions(), SfxBakeTo.NextToEachAsset);
            Assert.AreEqual(Folder + "/Steps.asset: no preset", report2.lines[0].note); Assert.IsTrue(report2.lines[0].failed);
            Assert.AreEqual(1, report2.Written); Assert.AreEqual("1 file written, 1 failed \u2013 next to each asset", report2.Summary);
        }

        [Test]
        public void BakeWavOfThePresetKeyIsTheDefaultRun()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            AudioClip clip = SfxForgeEditorLink.BakeWav(laser);
            string file = Folder + "/Laser.wav";
            Assert.AreEqual(file, AssetDatabase.GetAssetPath(clip));
            Assert.AreEqual(2, clip.channels); Assert.AreEqual(48000, clip.frequency);
            byte[] key = File.ReadAllBytes(file);
            // what the key wrote before there were options: the engine's samples, 48 kHz, stereo, 16 bit
            float[] samples = SfxNative.BakeInterleaved(laser.patch, 48000, 1f, 0, 0f, laser.holdSeconds);
            string plain = Path.Combine(Application.temporaryCachePath, "sfx-forge-plain.wav");
            SfxWav.Write(plain, samples, 48000, 2);
            try { Assert.AreEqual(File.ReadAllBytes(plain), key, "BAKE WAV writes the file it always wrote"); }
            finally { File.Delete(plain); }

            string guid = AssetDatabase.AssetPathToGUID(file);
            SfxBakeReport report = SfxBatchBake.Run(new Object[] { laser }, new SfxBakeOptions(), SfxBakeTo.NextToEachAsset);
            Assert.AreEqual(file, report.LastFile);
            Assert.AreEqual(key, File.ReadAllBytes(file), "and a run with the default options writes that file too");
            Assert.AreEqual(guid, AssetDatabase.AssetPathToGUID(file));

            AudioClip other = SfxForgeEditorLink.BakeWav(laser, new SfxBakeOptions { mono = true, sampleRate = 32000 }, Folder + "/LaserMono.wav");
            Assert.AreEqual(1, other.channels); Assert.AreEqual(32000, other.frequency);
            UnityEngine.TestTools.LogAssert.Expect(LogType.Warning, "SFX Forge: nothing baked - sample rate 1 is outside 8000..48000.");
            Assert.IsNull(SfxForgeEditorLink.BakeWav(laser, new SfxBakeOptions { sampleRate = 1 }));
        }

        // ---- the window's view, fed with a model -----------------------------------------------------------------------------
        /// <summary>A selection of 12 presets and 2 sets, and a run of it that warned once and failed once.</summary>
        public static SfxBakeModel Baked()
        {
            var m = Selected();
            var report = new SfxBakeReport { Planned = 14, Where = "Assets/Audio/Baked" };
            string[] names = { "Chime", "Coin", "Door", "EngineHum", "Jump", "Laser", "Step_01", "Step_02", "Step_03", "Step_04", "Thud", "Wind" };
            float[] seconds = { 1.42f, 0.31f, 0.88f, 2.10f, 0.27f, 0.42f, 0.19f, 0.21f, 0.18f, 0.2f, 0.91f, 3.65f }, peaks = { -4.2f, -6.8f, -9.1f, -12.4f, -5.0f, -3.1f, -7.7f, -7.2f, -8.0f, -7.5f, 1.3f, -15.6f };
            for (int i = 0; i < names.Length; i++)
            {
                var line = new SfxBakeReport.Line { source = "Assets/Audio/" + names[i] + ".asset", file = "Assets/Audio/Baked/" + names[i] + ".wav", seconds = seconds[i], peakDb = peaks[i], rmsDb = peaks[i] - 10.3f };
                if (peaks[i] > 0f) { line.clips = true; line.note = "clips: +1.3dB over full scale, the file is clamped \u2013 lower LEVEL or NORMALISE"; }
                report.lines.Add(line);
            }
            report.Fail("Assets/Audio/Old Steps.asset", "Assets/Audio/Old Steps.asset: no preset");
            m.report = report;
            m.to = SfxBakeTo.OneFolder; m.folder = "Assets/Audio/Baked";
            m.options = new SfxBakeOptions { sampleRate = 44100, mono = true, normaliseDb = -3f, trimDb = -60f };
            return m;
        }

        public static SfxBakeModel Selected() => new SfxBakeModel { presets = 12, sets = 2, files = 14 };

        static SfxBakeView View(SfxBakeModel m) => (SfxBakeView)SfxBakeWindow.Build(m);

        [Test]
        public void TheViewShowsTheSelectionAndTheReport()
        {
            var view = View(Selected());
            Assert.AreEqual("14", view.HeaderNumber); Assert.AreEqual("BAKE TO WAV", view.HeaderName); Assert.AreEqual("12 PRESETS, 2 SETS", view.HeaderSub);
            Assert.AreEqual("14 FILES | from 12 presets and 2 variation sets in the selection", view.FooterText);
            string[] labels = { "RATE", "CHANNELS", "PITCH", "NOTE", "VELOCITY", "TRIM", "NORMALISE" }, values = { "48000Hz", "STEREO", "0st", "AS DESIGNED", "100%", "OFF", "OFF" };
            for (int i = 0; i < SfxBakeView.Tiles; i++) { Assert.AreEqual(labels[i], view.Tile(i).LabelText); Assert.AreEqual(values[i], view.Tile(i).ValueText, labels[i]); }
            Assert.IsFalse(view.Key("BAKE").Off); Assert.IsFalse(view.Key("BAKE SETS").Off); Assert.IsTrue(view.Key("SHOW").Off, "nothing written yet");
            Assert.AreEqual("NEXT TO EACH ASSET", view.ToKey(SfxBakeTo.NextToEachAsset).text); Assert.AreEqual("INTO ONE FOLDER", view.ToKey(SfxBakeTo.OneFolder).text);
            Assert.IsTrue(view.ToKey(SfxBakeTo.NextToEachAsset).Lit, "the lit key is where the files go"); Assert.IsFalse(view.ToKey(SfxBakeTo.OneFolder).Lit);
            Assert.AreEqual(SfxBakeModel.DefaultFolder, view.FolderField.value);
            Assert.AreEqual(0, view.LineCount); Assert.AreEqual("", view.MoreText);

            // the tiles change the model's options, and say so
            int told = 0;
            view.Model.optionsChanged = () => told++;
            SfxBakeOptions o = view.Model.options;
            view.Nudge(SfxBakeView.TileRate, -1); Assert.AreEqual(44100, o.sampleRate); Assert.AreEqual("44100Hz", view.Tile(SfxBakeView.TileRate).ValueText);
            view.Nudge(SfxBakeView.TileRate, -9); Assert.AreEqual(22050, o.sampleRate);
            view.Choose(SfxBakeView.TileRate, 2); Assert.AreEqual(32000, o.sampleRate);
            view.Typed(SfxBakeView.TileRate, "24k"); Assert.AreEqual(24000, o.sampleRate);
            view.Nudge(SfxBakeView.TileChannels, -1); Assert.IsTrue(o.mono); Assert.AreEqual("MONO", view.Tile(SfxBakeView.TileChannels).ValueText);
            view.Typed(SfxBakeView.TileChannels, "stereo"); Assert.IsFalse(o.mono);
            view.Nudge(SfxBakeView.TilePitch, 3); Assert.AreEqual(3f, o.semitones); Assert.AreEqual("+3st", view.Tile(SfxBakeView.TilePitch).ValueText);
            view.Nudge(SfxBakeView.TilePitch, -99); Assert.AreEqual(-24f, o.semitones); Assert.AreEqual("-24st", view.Tile(SfxBakeView.TilePitch).ValueText);
            view.Typed(SfxBakeView.TilePitch, "+12"); Assert.AreEqual(12f, o.semitones);
            view.Nudge(SfxBakeView.TileNote, 1); Assert.AreEqual(24, o.midiNote); Assert.AreEqual("C1", view.Tile(SfxBakeView.TileNote).ValueText);
            view.Nudge(SfxBakeView.TileNote, -1); Assert.AreEqual(0, o.midiNote); Assert.AreEqual("AS DESIGNED", view.Tile(SfxBakeView.TileNote).ValueText);
            view.Typed(SfxBakeView.TileNote, "a4"); Assert.AreEqual(69, o.midiNote); Assert.AreEqual("A4", view.Tile(SfxBakeView.TileNote).ValueText);
            view.Nudge(SfxBakeView.TileNote, 99); Assert.AreEqual(96, o.midiNote);
            view.Typed(SfxBakeView.TileNote, "as designed"); Assert.AreEqual(0, o.midiNote);
            view.Nudge(SfxBakeView.TileVelocity, -20); Assert.AreEqual(0.8f, o.velocity, 1e-5f); Assert.AreEqual("80%", view.Tile(SfxBakeView.TileVelocity).ValueText);
            view.Nudge(SfxBakeView.TileVelocity, -500); Assert.AreEqual(0.01f, o.velocity, 1e-5f);
            view.Typed(SfxBakeView.TileVelocity, "50%"); Assert.AreEqual(0.5f, o.velocity, 1e-5f);
            view.Nudge(SfxBakeView.TileTrim, 1); Assert.AreEqual(-60f, o.trimDb); Assert.AreEqual("-60dB", view.Tile(SfxBakeView.TileTrim).ValueText);
            view.Choose(SfxBakeView.TileTrim, 3); Assert.AreEqual(-36f, o.trimDb);
            view.Typed(SfxBakeView.TileTrim, "-50"); Assert.AreEqual(-48f, o.trimDb);
            view.Typed(SfxBakeView.TileTrim, "off"); Assert.AreEqual(0f, o.trimDb);
            view.Nudge(SfxBakeView.TileNormalise, 1); Assert.AreEqual(-24f, o.normaliseDb); Assert.AreEqual("-24dB", view.Tile(SfxBakeView.TileNormalise).ValueText);
            view.Nudge(SfxBakeView.TileNormalise, 21); Assert.AreEqual(-3f, o.normaliseDb); Assert.AreEqual("-3dB", view.Tile(SfxBakeView.TileNormalise).ValueText);
            view.Nudge(SfxBakeView.TileNormalise, 99); Assert.AreEqual(0f, o.normaliseDb); Assert.AreEqual("0dB", view.Tile(SfxBakeView.TileNormalise).ValueText);
            view.Nudge(SfxBakeView.TileNormalise, -25); Assert.IsTrue(float.IsNaN(o.normaliseDb), "below -24: OFF"); Assert.AreEqual("OFF", view.Tile(SfxBakeView.TileNormalise).ValueText);
            view.Typed(SfxBakeView.TileNormalise, "-40.5dB"); Assert.AreEqual(-24f, o.normaliseDb, "typed below the tile's range: its end"); Assert.AreEqual("-24dB", view.Tile(SfxBakeView.TileNormalise).ValueText);
            view.Nudge(SfxBakeView.TileNormalise, 1); Assert.AreEqual(-23f, o.normaliseDb, "and the next step is one step");
            view.Typed(SfxBakeView.TileNormalise, "-6.5dB"); Assert.AreEqual(-6.5f, o.normaliseDb); Assert.AreEqual("-6.5dB", view.Tile(SfxBakeView.TileNormalise).ValueText);
            view.Typed(SfxBakeView.TileNormalise, "nonsense"); view.Typed(SfxBakeView.TilePitch, "3s"); view.Typed(SfxBakeView.TileNote, "H9");
            Assert.AreEqual(-6.5f, o.normaliseDb);
            Assert.Greater(told, 20); Assert.IsNull(o.Problem(), "whatever the tiles do, the options stay inside their ranges");
            for (int i = 0; i < SfxBakeView.Tiles; i++) { view.ResetValue(i); Assert.AreEqual(values[i], view.Tile(i).ValueText, labels[i]); }
            Assert.AreEqual(SfxBakeWindow.ToJson(new SfxBakeOptions()), SfxBakeWindow.ToJson(o));

            // the options as they are kept
            var kept = new SfxBakeOptions { sampleRate = 32000, mono = true, semitones = -5f, midiNote = 60, velocity = 0.4f, trimDb = -48f, normaliseDb = -6f };
            SfxBakeOptions back = SfxBakeWindow.FromJson(SfxBakeWindow.ToJson(kept));
            Assert.AreEqual(32000, back.sampleRate); Assert.IsTrue(back.mono); Assert.AreEqual(-5f, back.semitones); Assert.AreEqual(60, back.midiNote); Assert.AreEqual(0.4f, back.velocity); Assert.AreEqual(-48f, back.trimDb); Assert.AreEqual(-6f, back.normaliseDb);
            Assert.IsTrue(float.IsNaN(SfxBakeWindow.FromJson(SfxBakeWindow.ToJson(new SfxBakeOptions())).normaliseDb));
            Assert.AreEqual(48000, SfxBakeWindow.FromJson("{\"sampleRate\":5}").sampleRate, "kept options that are out of range: the defaults");
            Assert.AreEqual(48000, SfxBakeWindow.FromJson("not json at all").sampleRate); Assert.AreEqual(48000, SfxBakeWindow.FromJson(null).sampleRate);

            // nothing selected; no native library
            view = View(new SfxBakeModel());
            Assert.AreEqual("0", view.HeaderNumber); Assert.AreEqual("0 PRESETS, 0 SETS", view.HeaderSub);
            Assert.IsTrue(view.Key("BAKE").Off); Assert.IsTrue(view.Key("BAKE SETS").Off);
            Assert.AreEqual("NOTHING TO BAKE | Select presets, variation sets or folders in the Project window.", view.FooterText);
            view = View(new SfxBakeModel { presets = 1, files = 1 });
            Assert.AreEqual("1 PRESET, 0 SETS", view.HeaderSub); Assert.AreEqual("1 FILE | from 1 preset and 0 variation sets in the selection", view.FooterText);
            Assert.IsFalse(view.Key("BAKE").Off); Assert.IsTrue(view.Key("BAKE SETS").Off, "no variation set among them");
            var missing = Selected(); missing.nativeThere = false;
            view = View(missing);
            Assert.IsTrue(view.Key("BAKE").Off); Assert.IsTrue(view.Key("BAKE SETS").Off);
            Assert.AreEqual("NATIVE LIBRARY NOT FOUND | sfxforge is here for Windows x64 only: nothing can be baked here.", view.FooterText);

            // after a run: its newest lines, what warns in the warning colour, the rest in the Console
            view = View(Baked());
            Assert.IsTrue(view.ToKey(SfxBakeTo.OneFolder).Lit); Assert.IsFalse(view.ToKey(SfxBakeTo.NextToEachAsset).Lit); Assert.AreEqual("Assets/Audio/Baked", view.FolderField.value);
            Assert.AreEqual("44100Hz", view.Tile(SfxBakeView.TileRate).ValueText); Assert.AreEqual("MONO", view.Tile(SfxBakeView.TileChannels).ValueText);
            Assert.AreEqual("-60dB", view.Tile(SfxBakeView.TileTrim).ValueText); Assert.AreEqual("-3dB", view.Tile(SfxBakeView.TileNormalise).ValueText);
            Assert.AreEqual(8, view.LineCount);
            Assert.AreEqual("\u2026 and 5 more: see the Console", view.MoreText);
            Assert.AreEqual("Laser.wav  0.42s  peak -3.1dB  rms -13.4dB", view.LineText(0)); Assert.IsFalse(view.LineWarns(0));
            Assert.AreEqual("Thud.wav  0.91s  peak +1.3dB  rms -9.0dB  clips: +1.3dB over full scale, the file is clamped \u2013 lower LEVEL or NORMALISE", view.LineText(5)); Assert.IsTrue(view.LineWarns(5));
            Assert.AreEqual("Assets/Audio/Old Steps.asset: no preset", view.LineText(7)); Assert.IsTrue(view.LineWarns(7)); Assert.IsFalse(view.LineWarns(6));
            Assert.AreEqual("BAKED, NOT ALL OF IT | 12 files written, 1 failed \u2013 Assets/Audio/Baked", view.FooterText);
            Assert.IsFalse(view.Key("SHOW").Off);
            view.Model.report.Cancelled = true; view.Model.report.Planned = 14;
            view.Refresh();
            Assert.AreEqual("CANCELLED | cancelled after 12 of 14 files", view.FooterText);
        }
    }
}
