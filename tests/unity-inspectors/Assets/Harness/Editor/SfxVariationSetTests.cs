using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using UnityEngine.TestTools;
using Object = UnityEngine.Object;

namespace Inspector.Harness
{
    /// <summary>
    /// A variation set with real asset files: what its variants are, what BAKE writes and leaves alone, when it is out
    /// of date, how a play is chosen, what its Inspector shows. The assets are made in a folder of their own, which is
    /// deleted afterwards.
    /// </summary>
    public class SfxVariationSetTests
    {
        public const string Folder = "Assets/VariationTemp";
        static readonly SfxPatch Step = SfxPatch.Create(SfxEngine.Impact, 30, 84, 25, 20, 60, 50, 90, 10, 34, 0);

        readonly List<string> warnings = new List<string>();
        void OnLog(string text, string stack, LogType type) { if (type == LogType.Warning) warnings.Add(text); }

        [SetUp]
        public void SetUp()
        {
            if (!AssetDatabase.IsValidFolder(Folder)) AssetDatabase.CreateFolder("Assets", "VariationTemp");
            warnings.Clear();
            Application.logMessageReceived += OnLog;
            SfxVariationSet.PretendNativeMissing = false; SfxUI.PretendNativeMissing = false;
        }

        [TearDown]
        public void TearDown()
        {
            Application.logMessageReceived -= OnLog;
            SfxVariationSet.PretendNativeMissing = false; SfxUI.PretendNativeMissing = false;
            AssetDatabase.DeleteAsset(Folder);
        }

        static string Unique(string word) => word + Guid.NewGuid().ToString("N").Substring(0, 8);

        /// <summary>A preset asset and a set of it, both saved in a folder.</summary>
        public static SfxVariationSet MakeSet(string name, int count, SfxPatch patch, string folder = Folder)
        {
            SfxPreset preset = SfxForgeEditorLink.CreatePreset(patch, folder, name + " Sound");
            var set = ScriptableObject.CreateInstance<SfxVariationSet>();
            set.preset = preset; set.count = count; set.seed = 0x3F2A91C4;
            AssetDatabase.CreateAsset(set, folder + "/" + name + ".asset");
            AssetDatabase.SaveAssetIfDirty(set);
            return set;
        }

        static SfxVariationSetPanel Panel(params Object[] sets) => (SfxVariationSetPanel)SfxVariationSetEditor.Build(new SerializedObject(sets));
        static string[] Wavs(string folder) { var files = Directory.Exists(folder) ? Directory.GetFiles(folder, "*.wav") : new string[0]; for (int i = 0; i < files.Length; i++) files[i] = Path.GetFileName(files[i]); Array.Sort(files, string.CompareOrdinal); return files; }

        [Test]
        public void ANewSetHasTheFormatAndWarnsWhenNewer()
        {
            Assert.AreEqual(1, SfxVariationSet.CurrentFormat);
            SfxVariationSet set = MakeSet("Fresh", 6, Step);
            string path = AssetDatabase.GetAssetPath(set), text = File.ReadAllText(path).Replace("\r\n", "\n");
            Assert.AreEqual(1, set.Format); Assert.IsFalse(set.IsFromNewerVersion);
            StringAssert.Contains("\n  format: 1\n", text); StringAssert.Contains("\n  savedBy: 1\n", text);
            Assert.IsFalse(Panel(set).NewerBanner.Shown);

            // the same file as a later version would have written it
            string name = Unique("Later");
            string later = text.Replace("  m_Name: Fresh\n", "  m_Name: " + name + "\n").Replace("\n  format: 1\n", "\n  format: 2\n").Replace("\n  savedBy: 1\n", "\n  savedBy: 2\n  somethingThisVersionDoesNotKnow: 7\n");
            string laterPath = Folder + "/" + name + ".asset";
            File.WriteAllText(laterPath, later);
            AssetDatabase.ImportAsset(laterPath, ImportAssetOptions.ForceSynchronousImport | ImportAssetOptions.ForceUpdate);
            var newer = AssetDatabase.LoadAssetAtPath<SfxVariationSet>(laterPath);
            Assert.IsNotNull(newer);
            Assert.AreEqual(2, newer.Format); Assert.IsTrue(newer.IsFromNewerVersion);
            Assert.AreSame(set.preset, newer.preset, "what this version reads of it is there"); Assert.AreEqual(6, newer.Count);
            Assert.AreEqual(1, warnings.FindAll(w => w.Contains("'" + name + "'") && w.Contains("newer SFX Forge")).Count, "one warning when it is loaded: " + string.Join(" / ", warnings));
            var panel = Panel(newer);
            Assert.IsTrue(panel.NewerBanner.Shown);
            Assert.AreEqual("FROM A NEWER SFX FORGE", panel.NewerBanner.Bold);
            StringAssert.Contains("asset format 2, this version knows 1", panel.NewerBanner.Small);

            newer.noRepeat = 3;
            EditorUtility.SetDirty(newer);
            AssetDatabase.SaveAssetIfDirty(newer);
            string saved = File.ReadAllText(laterPath);
            StringAssert.Contains("  format: 2", saved); StringAssert.Contains("  savedBy: 1", saved); StringAssert.DoesNotContain("somethingThisVersionDoesNotKnow", saved);
        }

        [Test]
        public void VariantOneIsThePresetTheOthersAreItsVariants()
        {
            SfxVariationSet set = MakeSet("Steps", 6, Step);
            Assert.AreEqual(6, set.Count);
            Assert.IsTrue(set.VariantPatch(0).Equals(set.preset.patch), "the first is the preset itself");
            var seen = new HashSet<SfxPatch>();
            for (int i = 0; i < set.Count; i++)
            {
                Assert.IsTrue(set.VariantPatch(i).Equals(SfxMutate.Variant(Step, 0x3F2A91C4u, i, 10, 0)), "variant " + i);
                Assert.AreEqual(Step.engine, set.VariantPatch(i).engine); Assert.AreEqual(Step.level, set.VariantPatch(i).level);
                for (int k = 0; k < SfxPatch.ParamCount; k++) Assert.LessOrEqual(Math.Abs(set.VariantPatch(i)[(SfxParam)k] - Step[(SfxParam)k]), SfxPatch.StepOf(10));
                seen.Add(set.VariantPatch(i));
            }
            Assert.AreEqual(6, seen.Count, "six different sounds");

            SfxPatch third = set.VariantPatch(3);
            set.count = 12;
            Assert.IsTrue(third.Equals(set.VariantPatch(3)), "more variants leave the earlier ones as they were");
            set.count = 99; Assert.AreEqual(SfxVariationSet.MaxCount, set.Count);
            set.count = 0; Assert.AreEqual(1, set.Count);
            set.count = 6;

            set.locked = unchecked((int)SfxUnits.PageMask(SfxUnits.PageOsc));
            for (int i = 0; i < 6; i++) Assert.AreEqual(Step.baseFreq, set.VariantPatch(i).baseFreq, "a kept page keeps its values");
            set.amount = 0;
            for (int i = 0; i < 6; i++) Assert.IsTrue(set.VariantPatch(i).Equals(Step), "amount 0: every variant is the preset");
            set.preset = null;
            Assert.IsTrue(set.VariantPatch(2).Equals(default(SfxPatch))); Assert.AreEqual(0, set.Stamp); Assert.IsNull(set.Clip(0)); Assert.IsFalse(set.IsBaked);
        }

        [Test]
        public void BakeWritesItsClipsNextToTheAsset()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            SfxVariationSet set = MakeSet("Steps", 4, Step);
            string folder = Folder + "/Steps Variants";
            Assert.AreEqual(folder, SfxVariationBaker.FolderOf(set));
            Assert.IsFalse(set.IsBaked); Assert.IsFalse(set.IsStale);

            SfxBakeReport report = SfxVariationBaker.Bake(set);
            Assert.AreEqual(new[] { "Steps_01.wav", "Steps_02.wav", "Steps_03.wav", "Steps_04.wav" }, Wavs(folder));
            Assert.AreEqual(4, report.lines.Count, report.Text); Assert.AreEqual(4, report.Written); Assert.AreEqual(0, report.Failed);
            Assert.AreEqual("4 files written – " + folder, report.Summary);
            StringAssert.StartsWith("Steps_01.wav  0.", report.lines[0].Text); StringAssert.Contains("s  peak -", report.lines[0].Text); StringAssert.Contains("dB  rms -", report.lines[0].Text);
            Assert.IsTrue(set.IsBaked); Assert.IsFalse(set.IsStale);
            for (int i = 0; i < 4; i++)
            {
                AudioClip clip = set.Clip(i);
                Assert.AreEqual(folder + "/Steps_0" + (i + 1) + ".wav", AssetDatabase.GetAssetPath(clip));
                Assert.AreEqual(2, clip.channels); Assert.AreEqual(48000, clip.frequency);
                SfxBaked baked = SfxBake.Render(set.VariantPatch(i), set.preset.holdSeconds);
                Assert.AreEqual(baked.frames, clip.samples, 2048, "the clip is the variant as it bakes");
            }
            StringAssert.Contains("  bakedStamp: " + set.Stamp, File.ReadAllText(AssetDatabase.GetAssetPath(set)), "the asset file has it");
            Assert.IsTrue(set.Prepare());

            set.mono = true;                                    // one channel: out of date until it is baked again
            Assert.IsTrue(set.IsStale);
            SfxVariationBaker.Bake(set);
            Assert.IsFalse(set.IsStale);
            for (int i = 0; i < 4; i++) Assert.AreEqual(1, set.Clip(i).channels);
        }

        [Test]
        public void EditingThePresetMakesTheSetStaleAndBakeCuresIt()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            SfxVariationSet set = MakeSet("Hits", 3, Step);
            SfxVariationBaker.Bake(set);
            Assert.IsTrue(set.IsBaked); Assert.IsFalse(set.IsStale);
            var guids = new string[3];
            for (int i = 0; i < 3; i++) guids[i] = AssetDatabase.AssetPathToGUID(AssetDatabase.GetAssetPath(set.Clip(i)));
            int frames = set.Clip(1).samples;

            set.preset.patch.ampDecay += 30;                    // the preset is another asset: the set notices by its stamp
            Assert.IsTrue(set.IsBaked); Assert.IsTrue(set.IsStale);
            Assert.AreEqual(frames, set.Clip(1).samples, "the baked clips still play: the old sound");
            var order = set.NewOrder(3);
            warnings.Clear();
            Assert.IsNotNull(set.Next(order, out _, out _)); Assert.IsNotNull(set.Next(order, out _, out _));
            Assert.AreEqual(1, warnings.FindAll(w => w.Contains("'Hits' is out of date")).Count, "said once when it is played: " + string.Join(" / ", warnings));

            SfxVariationBaker.Bake(set);
            Assert.IsFalse(set.IsStale);
            for (int i = 0; i < 3; i++) Assert.AreEqual(guids[i], AssetDatabase.AssetPathToGUID(AssetDatabase.GetAssetPath(set.Clip(i))), "the files are overwritten in place: the same GUIDs");
            Assert.Greater(set.Clip(1).samples, frames, "and are the new, longer sound");

            foreach (Action change in new Action[] { () => set.amount = 30, () => set.seed = 5, () => set.locked = 3, () => set.preset.holdSeconds = 2f })
            {
                change();
                Assert.IsTrue(set.IsStale);
                SfxVariationBaker.Bake(set);
                Assert.IsFalse(set.IsStale);
            }
            set.pitchCents = 300f; set.volumeDb = 9f; set.noRepeat = 0;     // how it is played is not what it is baked from
            Assert.IsFalse(set.IsStale);
        }

        [Test]
        public void LoweringTheCountDeletesOnlyItsOwnFiles()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            SfxVariationSet set = MakeSet("Coins", 5, Step);
            string folder = Folder + "/Coins Variants";
            SfxVariationBaker.Bake(set);
            Assert.AreEqual(5, Wavs(folder).Length);
            File.Copy(folder + "/Coins_01.wav", folder + "/Other.wav");            // someone else's files in its folder
            File.Copy(folder + "/Coins_01.wav", folder + "/Coins_09.wav");         // (even one that looks like its own: it never had it)
            AssetDatabase.Refresh();

            set.count = 3;
            Assert.IsTrue(set.IsBaked, "five clips for three variants: they still play"); Assert.IsTrue(set.IsStale, "... and the set is out of date");
            Assert.AreEqual(5, set.BakedCount); Assert.AreEqual(5, set.PlayCount);
            SfxBakeReport report = SfxVariationBaker.Bake(set);
            Assert.AreEqual(3, report.Written);
            Assert.AreEqual(new[] { "Coins_01.wav", "Coins_02.wav", "Coins_03.wav", "Coins_09.wav", "Other.wav" }, Wavs(folder));
            Assert.IsTrue(set.IsBaked); Assert.IsFalse(set.IsStale);
            Assert.IsNull(AssetDatabase.LoadAssetAtPath<AudioClip>(folder + "/Coins_04.wav"));
            Assert.IsNotNull(AssetDatabase.LoadAssetAtPath<AudioClip>(folder + "/Other.wav"));

            set.count = 4;                                      // and up again
            SfxVariationBaker.Bake(set);
            Assert.AreEqual(new[] { "Coins_01.wav", "Coins_02.wav", "Coins_03.wav", "Coins_04.wav", "Coins_09.wav", "Other.wav" }, Wavs(folder));
        }

        /// <summary>The count changed after BAKE: the clips that are there keep playing - also where there is no native library to make others.</summary>
        [Test]
        public void ACountChangedAfterBakeStillPlaysItsClips()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            SfxVariationSet set = MakeSet("Count", 6, Step);
            SfxVariationBaker.Bake(set);
            var baked = new AudioClip[6];
            for (int i = 0; i < 6; i++) baked[i] = set.Clip(i);
            SfxVariationSet.PretendNativeMissing = true;        // a player's platform
            foreach (int count in new[] { 4, 8 })
            {
                set.count = count;
                Assert.IsTrue(set.IsBaked, "count " + count); Assert.IsTrue(set.IsStale); Assert.AreEqual(6, set.BakedCount); Assert.AreEqual(6, set.PlayCount); Assert.AreEqual(count, set.Count);
                Assert.IsTrue(set.Prepare(), "everything a play chooses from is there");
                for (int i = 0; i < 6; i++) Assert.AreSame(baked[i], set.Clip(i));
                Assert.IsNull(set.Clip(6)); Assert.IsNull(set.Clip(-1));
                SfxShuffle order = set.NewOrder(5);
                Assert.AreEqual(6, order.Count);
                var seen = new HashSet<int>();
                for (int i = 0; i < 200; i++) { Assert.IsNotNull(set.Next(order, out _, out _)); seen.Add(order.Last); }
                Assert.AreEqual(6, seen.Count, "all six clips come up");
                var made = new SfxShuffle(16, 0, 3);            // an order made for more variants than there are clips: still a clip every time
                for (int i = 0; i < 60; i++) Assert.IsNotNull(set.Next(made, out _, out _));
                var panel = Panel(set);
                Assert.AreEqual("OUT OF DATE", panel.StateBanner.Bold);
            }
            Assert.AreEqual(0, warnings.FindAll(w => w.Contains("nothing plays")).Count, string.Join(" / ", warnings));
            SfxVariationSet.PretendNativeMissing = false;
            StringAssert.StartsWith("6 CLIPS BAKED | ", Panel(set).FooterText);
            StringAssert.Contains("6 variants where the set now has 8", Panel(set).FooterText);

            var go = new GameObject("Player", typeof(AudioSource), typeof(SfxVariationPlayer));
            try
            {
                var player = go.GetComponent<SfxVariationPlayer>();
                player.set = set; player.orderSeed = 9;
                for (int i = 0; i < 40; i++) Assert.That(player.Play(), Is.InRange(0, 5));
            }
            finally { Object.DestroyImmediate(go); }
        }

        /// <summary>BAKE is not an undo step - and an undo of what was done before it does not take the clips out of the set.</summary>
        [Test]
        public void UndoAfterBakeLeavesTheClipsInTheSet()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            SfxVariationSet set = MakeSet("Undone", 6, Step);
            var panel = Panel(set);
            panel.Nudge(SfxVariationSetPanel.TileCount, -1);
            panel.Typed(SfxVariationSetPanel.TilePitch, "80");
            Assert.AreEqual(5, set.count); Assert.AreEqual("Edit SFX Variation Set Undone", Undo.GetCurrentGroupName());
            SfxBakeReport report = panel.Bake();
            Assert.AreEqual(5, report.Written);
            Undo.PerformUndo();
            panel.Refresh();
            Assert.AreEqual(5, set.count, "the set is as it was baked"); Assert.AreEqual(80f, set.pitchCents);
            Assert.IsTrue(set.IsBaked); Assert.IsFalse(set.IsStale); Assert.AreEqual(5, set.BakedCount);
            for (int i = 0; i < 5; i++) Assert.IsNotNull(set.Clip(i));
            Undo.PerformRedo();
            Assert.IsTrue(set.IsBaked); Assert.AreEqual(5, set.BakedCount);

            panel.Nudge(SfxVariationSetPanel.TileAmount, 3);    // an edit after the bake is an undo step as ever, and leaves the clips
            Assert.IsTrue(set.IsStale);
            Undo.PerformUndo();
            Assert.AreEqual(10, set.amount); Assert.IsTrue(set.IsBaked); Assert.IsFalse(set.IsStale);
        }

        /// <summary>A set that was renamed bakes into its new folder; the clips it had stay where they are (they may be a copy's), and the report says so.</summary>
        [Test]
        public void ARenamedSetLeavesItsOldClipsAndSaysSo()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            SfxVariationSet set = MakeSet("Old", 3, Step);
            SfxBakeReport first = SfxVariationBaker.Bake(set);
            Assert.AreEqual(0, first.remarks.Count); Assert.IsFalse(first.Warns);
            Assert.AreEqual("", AssetDatabase.RenameAsset(AssetDatabase.GetAssetPath(set), "New"));
            Assert.AreEqual(Folder + "/New Variants", SfxVariationBaker.FolderOf(set));
            Assert.IsTrue(set.IsBaked, "its clips are still the old folder's");
            SfxBakeReport report = SfxVariationBaker.Bake(set);
            Assert.AreEqual(3, report.Written); Assert.AreEqual(0, report.Failed);
            Assert.AreEqual(new[] { "New_01.wav", "New_02.wav", "New_03.wav" }, Wavs(Folder + "/New Variants"));
            Assert.AreEqual(new[] { "Old_01.wav", "Old_02.wav", "Old_03.wav" }, Wavs(Folder + "/Old Variants"), "nothing of the old folder is deleted");
            Assert.AreEqual(1, report.remarks.Count, report.Text);
            StringAssert.Contains("stay in '" + Folder + "/Old Variants'", report.remarks[0]);
            Assert.IsTrue(report.Warns); StringAssert.Contains(report.remarks[0], report.Text);
            for (int i = 0; i < 3; i++) StringAssert.StartsWith(Folder + "/New Variants/", AssetDatabase.GetAssetPath(set.Clip(i)));
            Assert.AreEqual(0, SfxVariationBaker.Bake(set).remarks.Count, "said once: now its clips are its own folder's");
        }

        /// <summary>One clip cannot be written: nothing is written at all - no set whose clips are half the old sound and half the new.</summary>
        [Test]
        public void AClipThatCannotBeWrittenStopsTheBakeBeforeAnyFileChanges()
        {
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
            SfxVariationSet set = MakeSet("Locked", 4, Step);
            string folder = Folder + "/Locked Variants";
            SfxVariationBaker.Bake(set);
            var before = new byte[4][];
            for (int i = 0; i < 4; i++) before[i] = File.ReadAllBytes(folder + "/Locked_0" + (i + 1) + ".wav");
            string held = folder + "/Locked_03.wav";
            File.SetAttributes(held, File.GetAttributes(held) | FileAttributes.ReadOnly);
            try
            {
                set.amount = 60;                                // another sound: every file would change
                Assert.IsTrue(set.IsStale);
                UnityEngine.TestTools.LogAssert.ignoreFailingMessages = true;
                SfxBakeReport report = SfxVariationBaker.Bake(set);
                Assert.AreEqual(0, report.Written, report.Text); Assert.AreEqual(1, report.Failed); Assert.IsTrue(report.WriteFailed);
                StringAssert.StartsWith("SFX Forge: could not write '" + held + "': ", report.lines[0].note);
                Assert.AreEqual(1, report.remarks.Count); StringAssert.StartsWith("nothing was written", report.remarks[0]);
                for (int i = 0; i < 4; i++) Assert.AreEqual(before[i], File.ReadAllBytes(folder + "/Locked_0" + (i + 1) + ".wav"), "file " + (i + 1) + " is as it was");
                Assert.IsTrue(set.IsBaked); Assert.IsTrue(set.IsStale, "still the old clips, all of them, and it says so");
            }
            finally
            {
                File.SetAttributes(held, File.GetAttributes(held) & ~FileAttributes.ReadOnly);
                UnityEngine.TestTools.LogAssert.ignoreFailingMessages = false;
            }
            SfxBakeReport then = SfxVariationBaker.Bake(set);
            Assert.AreEqual(4, then.Written); Assert.IsFalse(set.IsStale);
            Assert.AreEqual(before[0], File.ReadAllBytes(folder + "/Locked_01.wav"), "(the first is the preset itself)");
            Assert.AreNotEqual(before[1], File.ReadAllBytes(folder + "/Locked_02.wav"), "now they are the new sound");
        }

        /// <summary>Two orders asked for in the same moment are not the same order; a player takes a new order when "no repeat" changes.</summary>
        [Test]
        public void OrdersMadeTogetherDifferAndThePlayerFollowsNoRepeat()
        {
            SfxVariationSet set = MakeSet("Orders", 6, Step);
            set.noRepeat = 0;
            int alike = 0;
            for (int pair = 0; pair < 20; pair++)
            {
                SfxShuffle one = set.NewOrder(), two = set.NewOrder();
                bool same = true;
                for (int i = 0; i < 24; i++) same &= set.NextIndex(one, out _, out _) == set.NextIndex(two, out _, out _);
                if (same) alike++;
            }
            Assert.AreEqual(0, alike, "two players that start in the same moment do not play the same variants");
            SfxShuffle a = set.NewOrder(77), b = set.NewOrder(77);
            for (int i = 0; i < 24; i++) Assert.AreEqual(set.NextIndex(a, out _, out _), set.NextIndex(b, out _, out _), "a seed that is given is kept");

            if (!SfxNative.Available) return;
            var go = new GameObject("Player", typeof(AudioSource), typeof(SfxVariationPlayer));
            try
            {
                var player = go.GetComponent<SfxVariationPlayer>();
                player.set = set; player.orderSeed = 4;
                for (int i = 0; i < 10; i++) Assert.That(player.Play(), Is.InRange(0, 5));
                set.noRepeat = 5;                               // every variant once before any comes again
                var last = new List<int>();
                for (int i = 0; i < 36; i++)
                {
                    int at = player.Play();
                    for (int k = 1; k <= 5 && k <= last.Count; k++) Assert.AreNotEqual(last[last.Count - k], at, "play " + i + ": the new 'no repeat' is kept at once");
                    last.Add(at);
                }
            }
            finally { Object.DestroyImmediate(go); set.ReleaseMadeClips(); }
        }

        /// <summary>Single values a set keeps (MAKE SET brings the sound's locks) are named in its Inspector, not only whole pages.</summary>
        [Test]
        public void ThePanelNamesWhatTheVariantsKeep()
        {
            SfxVariationSet set = MakeSet("Keeps", 6, Step);
            var panel = Panel(set);
            StringAssert.StartsWith("Nothing is kept", panel.KeepCaption);
            set.locked = unchecked((int)(SfxMutate.Bit(SfxParam.BaseFreq) | SfxMutate.Bit(SfxParam.AmpDecay)));
            panel.Refresh();
            Assert.IsFalse(panel.PageKey(SfxUnits.PageOsc).Lit); Assert.IsFalse(panel.PageKey(SfxUnits.PageEnv).Lit);
            StringAssert.Contains("the single values FREQ, DECAY", panel.KeepCaption);
            panel.TogglePage(SfxUnits.PageFilter);
            Assert.IsTrue(panel.PageKey(SfxUnits.PageFilter).Lit);
            StringAssert.Contains("the pages FILTER, and the single values FREQ, DECAY", panel.KeepCaption);
            panel.TogglePage(SfxUnits.PageOsc);                 // a click on a page with one value kept keeps all of it
            Assert.IsTrue(panel.PageKey(SfxUnits.PageOsc).Lit);
            Assert.AreEqual(SfxUnits.PageMask(SfxUnits.PageOsc), unchecked((uint)set.locked) & SfxUnits.PageMask(SfxUnits.PageOsc));
        }

        [Test]
        public void ASetOutsideAssetsOrUnsavedSaysSo()
        {
            var preset = ScriptableObject.CreateInstance<SfxPreset>();
            var set = ScriptableObject.CreateInstance<SfxVariationSet>();
            set.name = "Loose";
            try
            {
                Assert.IsNull(SfxVariationBaker.FolderOf(set));
                SfxBakeReport none = SfxVariationBaker.Bake(set);
                Assert.AreEqual(1, none.Failed); Assert.AreEqual(0, none.Written);
                StringAssert.EndsWith(SfxVariationBaker.NoPreset, none.lines[0].note);

                set.preset = preset;
                SfxBakeReport report = SfxVariationBaker.Bake(set);
                Assert.AreEqual(1, report.lines.Count); Assert.AreEqual(1, report.Failed); Assert.AreEqual(0, report.Written); Assert.IsNull(report.LastFile);
                Assert.AreEqual("SFX Forge: 'Loose' is not inside Assets – its clips are written next to it. Save or move it there first.", report.lines[0].note);
                Assert.AreEqual(report.lines[0].note, report.lines[0].Text);
                Assert.IsFalse(set.IsBaked);
                Assert.AreEqual(0, Wavs(Folder).Length); Assert.AreEqual(0, Directory.GetDirectories(Folder).Length, "nothing was written");
                Assert.IsFalse(Directory.Exists("Assets/Loose Variants"));

                var panel = Panel(set);                         // its Inspector says where the clips would go
                StringAssert.StartsWith("NOT BAKED | ", panel.FooterText);
            }
            finally { Object.DestroyImmediate(set); Object.DestroyImmediate(preset); }
        }

        [Test]
        public void NextAvoidsTheLastOnesAndStaysInsideTheRanges()
        {
            SfxVariationSet set = MakeSet("Next", 6, Step);
            Assert.AreEqual(50f, set.pitchCents); Assert.AreEqual(2f, set.volumeDb); Assert.AreEqual(2, set.noRepeat);
            float up = Mathf.Pow(2f, 50f / 1200f), down = Mathf.Pow(2f, -50f / 1200f), quiet = Mathf.Pow(10f, -2f / 20f);
            SfxShuffle order = set.NewOrder(11), twin = set.NewOrder(11);
            var hits = new int[6]; int last = -1, before = -1; float lowest = 9f, highest = 0f, softest = 9f, loudest = 0f;
            for (int i = 0; i < 500; i++)
            {
                int at = set.NextIndex(order, out float pitch, out float volume);
                Assert.That(at, Is.InRange(0, 5)); Assert.AreEqual(at, order.Last);
                Assert.AreNotEqual(last, at); Assert.AreNotEqual(before, at, "never one of the last two");
                Assert.That(pitch, Is.InRange(down - 1e-5f, up + 1e-5f)); Assert.That(volume, Is.InRange(quiet - 1e-5f, 1f));
                int same = set.NextIndex(twin, out float pitchTwin, out float volumeTwin);
                Assert.AreEqual(at, same); Assert.AreEqual(pitch, pitchTwin); Assert.AreEqual(volume, volumeTwin);
                before = last; last = at; hits[at]++;
                lowest = Mathf.Min(lowest, pitch); highest = Mathf.Max(highest, pitch); softest = Mathf.Min(softest, volume); loudest = Mathf.Max(loudest, volume);
            }
            foreach (int n in hits) Assert.Greater(n, 40, "every variant comes up");
            Assert.Less(lowest, 0.985f); Assert.Greater(highest, 1.015f); Assert.Less(softest, 0.85f); Assert.Greater(loudest, 0.95f);

            set.pitchCents = 0f; set.volumeDb = 0f;             // nothing varies but the variant
            for (int i = 0; i < 20; i++) { set.NextIndex(order, out float pitch, out float volume); Assert.AreEqual(1f, pitch); Assert.AreEqual(1f, volume); }
            Assert.AreEqual(-1, set.NextIndex(null, out _, out _));

            if (!SfxNative.Available) return;
            AudioClip clip = set.Next(order, out _, out _);     // not baked: made now, by the engine
            Assert.IsNotNull(clip);
            Assert.AreEqual(2, clip.channels);
            Assert.AreSame(clip, set.Clip(order.Last), "and kept");
            Assert.IsTrue(set.Prepare());
            set.amount = 40;                                    // what they are made from changed: they are made again
            AudioClip again = set.Clip(order.Last);
            Assert.IsNotNull(again);
            Assert.IsTrue(clip == null, "the one made before is gone");
            set.ReleaseMadeClips();
        }

        [Test]
        public void WithoutClipsAndWithoutTheLibraryNothingPlaysAndItSaysSoOnce()
        {
            SfxVariationSet set = MakeSet("Silent", 4, Step);
            SfxVariationSet.PretendNativeMissing = true;
            var order = set.NewOrder(2);
            warnings.Clear();
            Assert.IsNull(set.Clip(0));
            Assert.IsNull(set.Next(order, out float pitch, out float volume));
            Assert.IsNull(set.Next(order, out pitch, out volume));
            Assert.IsFalse(set.Prepare());
            const string says = "SFX Forge: variation set 'Silent' has no baked clips and the native library is not available on this platform - nothing plays. BAKE it in the editor.";
            Assert.AreEqual(1, warnings.FindAll(w => w == says).Count, string.Join(" / ", warnings));

            var go = new GameObject("Player", typeof(AudioSource), typeof(SfxVariationPlayer));
            try
            {
                var player = go.GetComponent<SfxVariationPlayer>();
                player.set = set;
                Assert.AreEqual(-1, player.Play()); Assert.AreEqual(-1, player.Last);
                player.set = null;
                Assert.AreEqual(-1, player.Play());
                Assert.AreEqual(-1, SfxVariationPlayer.PlayOn(null, set, order));
            }
            finally { Object.DestroyImmediate(go); }
            Assert.AreEqual(1, warnings.FindAll(w => w == says).Count, "and not again");
        }

        [Test]
        public void ThePanelShowsTheStateAndItsValuesAndEditsUndo()
        {
            SfxVariationSet set = MakeSet("Steps", 6, Step);
            SfxPreset preset = set.preset;
            set.preset = null;
            var panel = Panel(set);
            Assert.IsTrue(panel.StateBanner.Shown);
            Assert.AreEqual("NO PRESET", panel.StateBanner.Bold); Assert.AreEqual("Choose the sound this set varies.", panel.StateBanner.Small);
            Assert.AreEqual(0, panel.VariantCount);
            Assert.IsTrue(panel.Key("BAKE").Off); Assert.IsTrue(panel.Key("PLAY NEXT").Off); Assert.IsTrue(panel.Key("SHOW CLIPS").Off);

            set.preset = preset;
            panel = Panel(set);
            Assert.AreEqual("NOT BAKED", panel.StateBanner.Bold);
            Assert.AreEqual("It plays where the native library is (Windows x64). BAKE writes its clips so it plays everywhere.", panel.StateBanner.Small);
            Assert.AreEqual("6", panel.HeaderNumber);
            Assert.AreEqual(6, panel.VariantCount);
            Assert.AreEqual("1", panel.VariantTileAt(0).NumberText); Assert.AreEqual("THE PRESET", panel.VariantTileAt(0).WordText); Assert.AreEqual("VARIANT", panel.VariantTileAt(5).WordText);
            Assert.AreSame(preset, panel.PresetField.value);
            string[] labels = { "COUNT", "AMOUNT", "PITCH", "VOLUME", "NO REPEAT", "SEED" }, values = { "6", "10%", "±50ct", "-2.0dB", "LAST 2", "3F2A91C4" };
            for (int i = 0; i < SfxVariationSetPanel.Tiles; i++) { Assert.AreEqual(labels[i], panel.Tile(i).LabelText); Assert.AreEqual(values[i], panel.Tile(i).ValueText, labels[i]); }

            // a tile turned: the asset, the tile, the tiles of the variants, one undo step with the set's name
            EditorUtility.ClearDirty(set);
            panel.Nudge(SfxVariationSetPanel.TileCount, 2);
            Assert.AreEqual(8, set.count); Assert.AreEqual("8", panel.Tile(SfxVariationSetPanel.TileCount).ValueText); Assert.AreEqual(8, panel.VariantCount); Assert.AreEqual("8", panel.HeaderNumber);
            Assert.IsTrue(EditorUtility.IsDirty(set));
            Assert.AreEqual("Edit SFX Variation Set Steps", Undo.GetCurrentGroupName());
            Undo.PerformUndo();
            Assert.AreEqual(6, set.count);
            panel.Refresh();
            Assert.AreEqual(6, panel.VariantCount);
            panel.Nudge(SfxVariationSetPanel.TileCount, 500); Assert.AreEqual(16, set.count);
            panel.Nudge(SfxVariationSetPanel.TileCount, -500); Assert.AreEqual(1, set.count);
            panel.ResetValue(SfxVariationSetPanel.TileCount); Assert.AreEqual(6, set.count);

            panel.Typed(SfxVariationSetPanel.TileAmount, "25%"); Assert.AreEqual(25, set.amount); Assert.AreEqual("25%", panel.Tile(SfxVariationSetPanel.TileAmount).ValueText);
            panel.Typed(SfxVariationSetPanel.TilePitch, "120"); Assert.AreEqual(120f, set.pitchCents); Assert.AreEqual("±120ct", panel.Tile(SfxVariationSetPanel.TilePitch).ValueText);
            panel.Typed(SfxVariationSetPanel.TilePitch, "off"); Assert.AreEqual(0f, set.pitchCents); Assert.AreEqual("OFF", panel.Tile(SfxVariationSetPanel.TilePitch).ValueText);
            panel.Typed(SfxVariationSetPanel.TileVolume, "-3dB"); Assert.AreEqual(3f, set.volumeDb, 1e-4f); Assert.AreEqual("-3.0dB", panel.Tile(SfxVariationSetPanel.TileVolume).ValueText);
            panel.Nudge(SfxVariationSetPanel.TileVolume, 1); Assert.AreEqual(3.5f, set.volumeDb, 1e-4f);
            panel.Typed(SfxVariationSetPanel.TileNoRepeat, "last 4"); Assert.AreEqual(4, set.noRepeat); Assert.AreEqual("LAST 4", panel.Tile(SfxVariationSetPanel.TileNoRepeat).ValueText);
            panel.Typed(SfxVariationSetPanel.TileNoRepeat, "0"); Assert.AreEqual("OFF", panel.Tile(SfxVariationSetPanel.TileNoRepeat).ValueText);
            panel.Typed(SfxVariationSetPanel.TileSeed, "c0ffee"); Assert.AreEqual(0xC0FFEE, set.seed); Assert.AreEqual("00C0FFEE", panel.Tile(SfxVariationSetPanel.TileSeed).ValueText);
            panel.Typed(SfxVariationSetPanel.TileSeed, "not hex"); panel.Typed(SfxVariationSetPanel.TileAmount, "3s"); panel.Typed(SfxVariationSetPanel.TileCount, "many");
            Assert.AreEqual(0xC0FFEE, set.seed); Assert.AreEqual(25, set.amount); Assert.AreEqual(6, set.count);
            panel.NewSeed();
            Assert.AreNotEqual(0xC0FFEE, set.seed); Assert.AreEqual("Edit SFX Variation Set Steps", Undo.GetCurrentGroupName());
            Undo.PerformUndo(); Assert.AreEqual(0xC0FFEE, set.seed);
            for (int i = 0; i < SfxVariationSetPanel.Tiles; i++) panel.ResetValue(i);
            Assert.AreEqual(10, set.amount); Assert.AreEqual(50f, set.pitchCents); Assert.AreEqual(2f, set.volumeDb); Assert.AreEqual(2, set.noRepeat); Assert.AreEqual(1, set.seed);

            // VARIANTS KEEP: a page's values
            Assert.IsFalse(panel.PageKey(SfxUnits.PageFilter).Lit);
            panel.TogglePage(SfxUnits.PageFilter);
            Assert.AreEqual(SfxUnits.PageMask(SfxUnits.PageFilter), unchecked((uint)set.locked)); Assert.IsTrue(panel.PageKey(SfxUnits.PageFilter).Lit); Assert.IsFalse(panel.PageKey(SfxUnits.PageEnv).Lit);
            for (int i = 0; i < 6; i++) Assert.AreEqual(Step.filterCutoff, set.VariantPatch(i).filterCutoff);
            panel.TogglePage(SfxUnits.PageFilter);
            Assert.AreEqual(0, set.locked);
            panel.ToggleMono(); Assert.IsTrue(set.mono); Assert.IsTrue(panel.Key("MONO").Lit);
            panel.ToggleMono(); Assert.IsFalse(set.mono);

            // several sets: what differs reads "-", and they are edited together
            SfxVariationSet other = MakeSet("Hits", 3, SfxPatch.Default);
            panel = Panel(set, other);
            Assert.AreEqual("-", panel.Tile(SfxVariationSetPanel.TileCount).ValueText); Assert.AreEqual("-", panel.HeaderNumber);
            Assert.AreEqual("10%", panel.Tile(SfxVariationSetPanel.TileAmount).ValueText, "the same in both: shown");
            Assert.AreEqual("2 SETS: select one to see its variants", panel.VariantsCaption); Assert.AreEqual(0, panel.VariantCount);
            panel.Nudge(SfxVariationSetPanel.TileCount, 1);     // each from where it stands
            Assert.AreEqual(7, set.count); Assert.AreEqual(4, other.count);
            Assert.AreEqual("Edit 2 SFX Variation Sets", Undo.GetCurrentGroupName());
            Undo.PerformUndo();
            Assert.AreEqual(6, set.count); Assert.AreEqual(3, other.count);

            if (!SfxNative.Available) return;
            panel = Panel(set);
            SfxBakeReport report = panel.Bake();                // the BAKE key
            Assert.AreEqual(6, report.Written);
            Assert.IsFalse(panel.StateBanner.Shown, "baked and current: nothing to say");
            Assert.IsFalse(panel.Key("SHOW CLIPS").Off);
            StringAssert.StartsWith("6 CLIPS BAKED | " + Folder + "/Steps Variants – 48 kHz stereo, ", panel.FooterText);
            string top = Undo.GetCurrentGroupName();
            Assert.AreNotEqual("Bake", top);
            panel.Nudge(SfxVariationSetPanel.TileAmount, 5);
            Assert.AreEqual("OUT OF DATE", panel.StateBanner.Bold);
            Assert.AreEqual("The preset or the set changed since BAKE: the clips are the old sound. BAKE again.", panel.StateBanner.Small);
            Assert.That(panel.PlayNext(), Is.InRange(0, 5));
            StringAssert.StartsWith("VARIANT ", panel.FooterText);
            Undo.PerformUndo();                                 // what PLAY NEXT said is about the set as it was: an undo takes it down
            panel.Refresh();
            StringAssert.StartsWith("6 CLIPS BAKED | ", panel.FooterText);

            SfxUI.PretendNativeMissing = true;
            panel = Panel(set);
            Assert.IsTrue(panel.Key("BAKE").Off); Assert.IsTrue(panel.Key("PLAY NEXT").Off);
            Assert.AreEqual("NATIVE LIBRARY NOT FOUND | sfxforge is here for Windows x64 only: no BAKE here. Clips baked elsewhere still play.", panel.FooterText);
            Assert.IsFalse(panel.VariantTileAt(0).HasPicture); Assert.IsTrue(panel.VariantTileAt(0).PlayKey.Off);
        }

        /// <summary>In play mode: the player plays the set through its AudioSource, a variant after the other, tuned inside the set's range.</summary>
        [UnityTest]
        public IEnumerator ThePlayerPlaysThroughItsAudioSource()
        {
            yield return new EnterPlayMode();
            if (SfxNative.Available)
            {
                var preset = ScriptableObject.CreateInstance<SfxPreset>();
                preset.patch = Step;
                var set = ScriptableObject.CreateInstance<SfxVariationSet>();
                set.name = "Played"; set.preset = preset; set.count = 5; set.seed = 77;
                var go = new GameObject("Player", typeof(AudioSource));
                var player = go.AddComponent<SfxVariationPlayer>();
                var source = go.GetComponent<AudioSource>();
                Assert.AreEqual(-1, player.Play(), "no set: nothing");
                player.set = set; player.orderSeed = 9;
                float up = Mathf.Pow(2f, 50f / 1200f), down = Mathf.Pow(2f, -50f / 1200f);
                int last = -1; var seen = new HashSet<int>(); var played = new List<int>();
                for (int i = 0; i < 40; i++)
                {
                    int at = player.Play(0.2f);
                    Assert.That(at, Is.InRange(0, 4)); Assert.AreEqual(at, player.Last);
                    Assert.AreNotEqual(last, at, "never the same twice running");
                    Assert.That(source.pitch, Is.InRange(down - 1e-5f, up + 1e-5f));
                    last = at; seen.Add(at); played.Add(at);
                    if (i % 10 == 9) yield return null;
                }
                Assert.AreEqual(5, seen.Count);
                Assert.IsTrue(set.Prepare());
                Assert.AreEqual(Mathf.Min(AudioSettings.outputSampleRate, 48000), set.Clip(0).frequency);

                var twin = new GameObject("Twin", typeof(AudioSource)).AddComponent<SfxVariationPlayer>();     // the same order seed: the same order
                twin.set = set; twin.orderSeed = 9;
                for (int i = 0; i < 40; i++) Assert.AreEqual(played[i], twin.Play(0f));
                twin.PlayNext();
                Object.Destroy(go); Object.Destroy(twin.gameObject);
                set.ReleaseMadeClips();
            }
            yield return new ExitPlayMode();
            if (!SfxNative.Available) Assert.Ignore("the native library is for Windows x64");
        }
    }
}
