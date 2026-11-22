using System;
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
    /// The tools of the sound's Inspector, without a window: the level line under the picture, and the drawer under the
    /// keys - NEW (a sound of a kind), VARY (amount, locks, eight variants), LEVEL (match), SHARE (text and .syx).
    /// What they do to the asset, to Undo, to several sounds at once.
    /// </summary>
    public class SfxToolsPanelTests
    {
        const string Folder = "Assets/ToolsTemp";

        /// <summary>
        /// A sound that is over full scale as it is baked: an FM voice at LEVEL 127 with full resonance,
        /// chorus and reverb.
        /// </summary>
        public const string ClippingText = "SFXF1:0202107A28185200547F2F000000732E2D3F543D1F0D7F007F7F2E17";

        SfxPreset a;
        SfxPreset b;
        SfxPreset c;
        static readonly SfxPatch Thud = SfxPatch.Create(SfxEngine.Impact, 24, 90, 35, 0, 0, 5, 70, 0, 70, 0);
        static readonly SfxPatch Hum = SfxPatch.Create(SfxEngine.SciFiFm, 22, 64, 0, 21, 55, 8, 78, 45, 60, 0, 0, 127);

        static SfxPreset Make(string name, SfxPatch patch)
        {
            var p = ScriptableObject.CreateInstance<SfxPreset>();
            p.name = name;
            p.patch = patch;
            return p;
        }

        static SfxPresetPanel Panel(params Object[] presets) =>
            (SfxPresetPanel)SfxPresetEditor.Build(new SerializedObject(presets));

        static void Plain()
        {
            SfxPresetPanel.MutateAmount = 15;
            SfxPresetPanel.Locked = 0;
            SfxUI.PretendNativeMissing = false;
        }

        [SetUp]
        public void SetUp()
        {
            a = Make("One", SfxPatch.Default);
            b = Make("Two", Thud);
            c = Make("Three", Hum);
            Plain();
            SfxPresetPanel.Seeds = new SfxRandom(7);
        }

        [TearDown]
        public void TearDown()
        {
            Plain();
            Object.DestroyImmediate(a);
            Object.DestroyImmediate(b);
            Object.DestroyImmediate(c);
            if (AssetDatabase.IsValidFolder(Folder))
            {
                AssetDatabase.DeleteAsset(Folder);
            }
        }

        // ---- NEW ----------------------------------------------------------------------------------------------------
        [Test]
        public void AGenerateKeyReplacesTheSoundAndUndoTakesItBack()
        {
            var panel = Panel(a);
            StringAssert.StartsWith("A NEW SOUND OF A KIND | ", panel.NewText);
            SfxPatch before = a.patch;
            EditorUtility.ClearDirty(a);
            uint seed = new SfxRandom(7).Next();
            panel.Generate(SfxCategory.Zap);
            Assert.IsTrue(
                a.patch.Equals(SfxGenerator.Make(SfxCategory.Zap, seed)),
                "the sound is the laser of the next seed"
            );
            Assert.AreEqual(seed, panel.LastSeed);
            Assert.IsTrue(SfxGenerator.Fits(SfxCategory.Zap, a.patch));
            Assert.IsTrue(EditorUtility.IsDirty(a));
            Assert.AreEqual("Generate SFX Preset One", Undo.GetCurrentGroupName());
            Assert.IsTrue(panel.CanUndoOwn, "the UNDO key lights for it");
            Assert.AreEqual(
                "ZAP | seed "
                    + SfxUnits.SeedText(seed)
                    + " – SfxGenerator.Make(SfxCategory.Zap, 0x"
                    + SfxUnits.SeedText(seed)
                    + ") gives it again",
                panel.NewText
            );
            float hold = a.holdSeconds;
            panel.Generate(SfxCategory.Hum); // another click: another one, of the next seed
            Assert.IsTrue(a.patch.IsSustained);
            Assert.AreEqual(hold, a.holdSeconds, "the hold is not changed");
            Undo.PerformUndo();
            Undo.PerformUndo();
            Assert.IsTrue(before.Equals(a.patch));
            for (int i = 0; i < SfxGenerator.Count; i++)
            {
                Assert.IsNotNull(panel.Key(SfxGenerator.Names[i]), SfxGenerator.Names[i]);
            }
        }

        [Test]
        public void GenerateGivesEverySelectedSoundItsOwn()
        {
            var panel = Panel(a, b, c);
            SfxPatch pa = a.patch;
            SfxPatch pb = b.patch;
            SfxPatch pc = c.patch;
            panel.Generate(SfxCategory.Chime);
            Assert.IsTrue(
                SfxGenerator.Fits(SfxCategory.Chime, a.patch)
                    && SfxGenerator.Fits(SfxCategory.Chime, b.patch)
                    && SfxGenerator.Fits(SfxCategory.Chime, c.patch)
            );
            Assert.IsFalse(a.patch.Equals(b.patch) && b.patch.Equals(c.patch), "each has its own seed");
            Assert.AreEqual("Generate 3 SFX Presets", Undo.GetCurrentGroupName());
            StringAssert.Contains(" and 2 more ", panel.NewText);
            Assert.IsTrue(panel.CanUndoOwn);
            panel.UndoOwn(); // one step for all three
            Assert.IsTrue(pa.Equals(a.patch) && pb.Equals(b.patch) && pc.Equals(c.patch));
        }

        [Test]
        public void TheToolTabsShowOneBoxAtATime()
        {
            var panel = Panel(a);
            try
            {
                for (int tab = 0; tab < 4; tab++)
                {
                    panel.ToolTab = tab;
                    Assert.AreEqual(tab, panel.ToolTabs.Index);
                    for (int i = 0; i < 4; i++)
                    {
                        Assert.AreEqual(i == tab, panel.ToolShown(i), "tab " + tab + ", box " + i);
                    }
                }

                Assert.AreEqual(4, panel.ToolTabs.Count);
                panel.ToolTab = SfxPresetPanel.ToolShare;
                Assert.IsTrue(Panel(b).ToolShown(SfxPresetPanel.ToolShare), "the tab is the same in every Inspector");
            }
            finally
            {
                panel.ToolTab = SfxPresetPanel.ToolNew;
            }
        }

        // ---- VARY ---------------------------------------------------------------------------------------------------
        [Test]
        public void MutateUsesTheAmountAndTheLocks()
        {
            var panel = Panel(a);
            SfxPatch before = a.patch;
            panel.SetAmount(5);
            Assert.AreEqual(5, SfxPresetPanel.MutateAmount);
            Assert.AreEqual("5%", panel.AmountTile.ValueText);
            panel.Page = SfxUnits.PageOsc;
            panel.ToggleLock(1); // FREQ
            Assert.AreEqual(SfxMutate.Bit(SfxParam.BaseFreq), SfxPresetPanel.Locked);
            bool moved = false;
            for (int run = 0; run < 20; run++)
            {
                SfxPatch from = a.patch;
                panel.Mutate();
                Assert.AreEqual(before.baseFreq, a.patch.baseFreq, "the locked value stays");
                for (int k = 0; k < SfxPatch.ParamCount; k++)
                {
                    Assert.LessOrEqual(Math.Abs(a.patch[(SfxParam)k] - from[(SfxParam)k]), 6, ((SfxParam)k).ToString());
                }

                moved |= !a.patch.Equals(from);
                Assert.AreEqual("Mutate SFX Preset One", Undo.GetCurrentGroupName());
            }

            Assert.IsTrue(moved);
            SfxPatch last = a.patch;
            panel.Mutate();
            Undo.PerformUndo();
            Assert.IsTrue(last.Equals(a.patch), "one undo step for one MUTATE");

            panel.TypedAmount("20%");
            Assert.AreEqual(20, SfxPresetPanel.MutateAmount);
            panel.TypedAmount("33");
            Assert.AreEqual(33, SfxPresetPanel.MutateAmount);
            panel.TypedAmount("loud");
            panel.TypedAmount("3s");
            Assert.AreEqual(33, SfxPresetPanel.MutateAmount);
            panel.SetAmount(500);
            Assert.AreEqual(100, SfxPresetPanel.MutateAmount);
            panel.SetAmount(-4);
            Assert.AreEqual(1, SfxPresetPanel.MutateAmount);
        }

        [Test]
        public void LockPageLocksItsFourValuesAndShowsIt()
        {
            var panel = Panel(a);
            panel.Page = SfxUnits.PageFilter;
            string[] names = { "CUTOFF", "RESO", "FILTER ENV", "F.ENV TIME" };
            for (int i = 0; i < 4; i++)
            {
                Assert.AreEqual("LOCK " + names[i], panel.LockKey(i).text);
                Assert.IsFalse(panel.LockKey(i).Lit);
                Assert.IsFalse(panel.Tile(i).Locked);
            }

            Assert.IsTrue(panel.Key("UNLOCK ALL").Off, "nothing is locked");

            panel.TogglePageLock();
            Assert.AreEqual(SfxUnits.PageMask(SfxUnits.PageFilter), SfxPresetPanel.Locked);
            for (int i = 0; i < 4; i++)
            {
                Assert.IsTrue(panel.LockKey(i).Lit);
                Assert.IsTrue(panel.Tile(i).Locked);
                Assert.AreEqual(names[i] + " *", panel.Tile(i).LabelText);
            }

            Assert.IsTrue(panel.Key("LOCK PAGE").Lit);
            Assert.IsFalse(panel.Key("UNLOCK ALL").Off);

            // one of them off again: the page is no longer all locked
            panel.ToggleLock(2);
            Assert.IsFalse(panel.LockKey(2).Lit);
            Assert.IsFalse(panel.Tile(2).Locked);
            Assert.IsFalse(panel.Key("LOCK PAGE").Lit);
            panel.TogglePageLock();
            Assert.IsTrue(panel.Key("LOCK PAGE").Lit);
            panel.TogglePageLock();
            Assert.AreEqual(0u, SfxPresetPanel.Locked, "all were locked: the key unlocks the page");

            panel.Page = SfxUnits.PageOsc; // the engine cannot be locked (MUTATE never changes it)
            Assert.AreEqual("ENGINE STAYS", panel.LockKey(0).text);
            Assert.IsTrue(panel.LockKey(0).Off);
            panel.ToggleLock(0);
            Assert.AreEqual(0u, SfxPresetPanel.Locked);
            panel.TogglePageLock();
            Assert.AreEqual(
                SfxMutate.Bit(SfxParam.BaseFreq)
                    | SfxMutate.Bit(SfxParam.PitchEnvAmount)
                    | SfxMutate.Bit(SfxParam.PitchEnvDecay),
                SfxPresetPanel.Locked
            );
            panel.Page = SfxUnits.PageFx;
            Assert.IsFalse(panel.Tile(0).Locked, "another page: its values are free");
            panel.UnlockAll();
            Assert.AreEqual(0u, SfxPresetPanel.Locked);
            panel.Page = SfxUnits.PageOsc;
            Assert.IsFalse(panel.Tile(1).Locked);
            Assert.AreEqual("FREQ", panel.Tile(1).LabelText);
            Assert.AreEqual(
                0x1FFFFFFu,
                SfxUnits.PageMask(0)
                    | SfxUnits.PageMask(1)
                    | SfxUnits.PageMask(2)
                    | SfxUnits.PageMask(3)
                    | SfxUnits.PageMask(4)
                    | SfxUnits.PageMask(5)
                    | SfxUnits.PageMask(6),
                "the pages hold every value"
            );
        }

        [Test]
        public void MoreLikeThisBranchesFromOneBase()
        {
            var panel = Panel(a);
            SfxPatch start = a.patch;
            Assert.IsFalse(panel.HasGrid);
            Assert.IsFalse(panel.GridShown);
            Assert.IsTrue(panel.Key("AGAIN").Off);
            Assert.IsTrue(panel.Key("BACK TO BASE").Off);
            EditorUtility.ClearDirty(a);

            panel.MoreLikeThis();
            Assert.IsTrue(panel.HasGrid);
            Assert.IsTrue(panel.GridShown);
            Assert.IsTrue(start.Equals(panel.GridBase));
            Assert.IsTrue(start.Equals(a.patch));
            Assert.IsFalse(EditorUtility.IsDirty(a), "looking at variants changes nothing");
            uint seed = panel.GridSeed;
            Assert.AreEqual(new SfxRandom(7).Next(), seed);
            for (int n = 1; n <= SfxPresetPanel.GridCount; n++)
            {
                Assert.IsTrue(panel.GridVariant(n).Equals(SfxMutate.Variant(start, seed, n, 15, 0)), "variant " + n);
                Assert.AreEqual(n.ToString(), panel.VariantTileAt(n).NumberText);
                Assert.IsFalse(panel.VariantTileAt(n).KeepKey.Lit);
            }

            Assert.AreEqual(
                "8 VARIANTS OF ONE | amount 15%, no value locked, seed "
                    + SfxUnits.SeedText(seed)
                    + " – all from the same base",
                panel.VaryText
            );
            Assert.IsFalse(panel.Key("AGAIN").Off);
            Assert.IsFalse(panel.Key("BACK TO BASE").Off);

            panel.Keep(3);
            Assert.IsTrue(a.patch.Equals(panel.GridVariant(3)));
            Assert.IsFalse(a.patch.Equals(start));
            Assert.IsTrue(start.Equals(panel.GridBase), "the base stays the base");
            Assert.AreEqual("Keep SFX Preset One", Undo.GetCurrentGroupName());
            Assert.IsTrue(panel.CanUndoOwn);
            Assert.IsTrue(panel.VariantTileAt(3).KeepKey.Lit);
            Assert.IsFalse(panel.VariantTileAt(4).KeepKey.Lit);

            // no such tile: nothing
            panel.Keep(0);
            panel.Keep(9);
            Assert.IsTrue(a.patch.Equals(panel.GridVariant(3)));

            panel.Again();
            Assert.AreNotEqual(seed, panel.GridSeed);
            Assert.IsTrue(start.Equals(panel.GridBase), "AGAIN changes the seed, not the base");
            Assert.IsFalse(panel.GridVariant(3).Equals(SfxMutate.Variant(start, seed, 3, 15, 0)));
            SfxPatch kept = a.patch;

            panel.BackToBase();
            Assert.IsTrue(start.Equals(a.patch));
            Assert.AreEqual("Keep SFX Preset One", Undo.GetCurrentGroupName());
            Undo.PerformUndo();
            Assert.IsTrue(kept.Equals(a.patch), "the first undo: the variant that was kept");
            Undo.PerformUndo();
            Assert.IsTrue(start.Equals(a.patch), "the second: the sound as it was");
            Undo.PerformRedo();
            panel.Refresh();
            Assert.IsTrue(kept.Equals(a.patch));
            Assert.IsTrue(start.Equals(panel.GridBase), "undo and redo do not move the base");

            panel.MoreLikeThis(); // only this takes a new base: the kept sound
            Assert.IsTrue(kept.Equals(panel.GridBase));
            Assert.IsTrue(panel.GridVariant(0).Equals(kept));
        }

        [Test]
        public void TheGridFollowsTheAmount()
        {
            var panel = Panel(a);
            panel.MoreLikeThis();
            uint seed = panel.GridSeed;
            SfxPatch small = panel.GridVariant(1);
            panel.SetAmount(60);
            Assert.AreEqual(seed, panel.GridSeed, "the same seed");
            Assert.IsTrue(panel.GridVariant(1).Equals(SfxMutate.Variant(a.patch, seed, 1, 60, 0)));
            Assert.IsFalse(panel.GridVariant(1).Equals(small), "the variants are made again");
            StringAssert.Contains("amount 60%", panel.VaryText);

            panel.Page = SfxUnits.PageOsc;
            SfxPatch free = panel.GridVariant(2);

            // FREQ stays in every variant, the other values are as before
            panel.ToggleLock(1);
            for (int n = 1; n <= SfxPresetPanel.GridCount; n++)
            {
                Assert.AreEqual(a.patch.baseFreq, panel.GridVariant(n).baseFreq);
            }

            SfxPatch held = panel.GridVariant(2);
            held.baseFreq = free.baseFreq;
            Assert.IsTrue(held.Equals(free), "locking a value changes no other value of a variant");
            StringAssert.Contains("1 value locked", panel.VaryText);
            Assert.AreEqual(seed, panel.GridSeed);
        }

        [Test]
        public void SeveralSoundsNoGrid()
        {
            var panel = Panel(a, b);
            foreach (string word in new[] { "MORE LIKE THIS", "AGAIN", "BACK TO BASE", "MAKE SET" })
            {
                Assert.IsTrue(panel.Key(word).Off, word);
            }

            panel.MoreLikeThis();
            Assert.IsFalse(panel.HasGrid);
            Assert.IsFalse(panel.GridShown);
            Assert.AreEqual(
                "MORE LIKE THIS WORKS ON ONE SOUND | 2 are selected. MUTATE changes all of them.",
                panel.VaryText
            );
            Assert.IsNull(panel.MakeSet());
            SfxPatch pa = a.patch;
            SfxPatch pb = b.patch;
            panel.Keep(1);
            panel.BackToBase();
            panel.Again();
            Assert.IsTrue(pa.Equals(a.patch) && pb.Equals(b.patch));
            panel.Mutate(); // MUTATE does: each with its own seed
            Assert.IsFalse(pa.Equals(a.patch));
            Assert.IsFalse(pb.Equals(b.patch));
            Assert.AreEqual("Mutate 2 SFX Presets", Undo.GetCurrentGroupName());
        }

        [Test]
        public void MakeSetCarriesTheGrid()
        {
            Assert.IsNull(Panel(a).MakeSet(), "a sound that is no asset: no set");
            StringAssert.StartsWith(
                "NO SET MADE | ",
                (
                    (Func<string>)(
                        () =>
                        {
                            var p = Panel(a);
                            p.MakeSet();
                            return p.VaryText;
                        }
                    )
                )()
            );

            SfxPreset asset = SfxForgeEditorLink.CreatePreset(Thud, Folder, "Grid");
            var panel = Panel(asset);
            SfxPresetPanel.MutateAmount = 22;
            SfxPresetPanel.Locked = SfxUnits.PageMask(SfxUnits.PageEnv);
            panel.MoreLikeThis();
            SfxVariationSet set = panel.MakeSet();
            Assert.IsNotNull(set);
            Assert.AreEqual(Folder + "/Grid Variations.asset", AssetDatabase.GetAssetPath(set));
            Assert.AreSame(asset, set.preset);
            Assert.AreEqual(SfxPresetPanel.GridCount + 1, set.Count);
            Assert.AreEqual(22, set.amount);
            Assert.AreEqual(panel.GridSeed, unchecked((uint)set.seed));
            Assert.AreEqual(SfxPresetPanel.Locked, unchecked((uint)set.locked));
            Assert.IsTrue(set.VariantPatch(0).Equals(asset.patch));
            for (int n = 1; n <= SfxPresetPanel.GridCount; n++)
            {
                Assert.IsTrue(
                    set.VariantPatch(n).Equals(panel.GridVariant(n)),
                    "the set's variant " + n + " is the grid's tile " + n
                );
            }

            Assert.IsFalse(set.IsBaked);
            StringAssert.StartsWith("SET MADE | " + Folder + "/Grid Variations.asset", panel.VaryText);

            var plain = Panel(asset); // without variants shown: six, of a new seed
            SfxVariationSet second = plain.MakeSet();
            Assert.AreEqual(Folder + "/Grid Variations 1.asset", AssetDatabase.GetAssetPath(second));
            Assert.AreEqual(6, second.Count);
        }

        /// <summary>
        /// MAKE SET makes a file: it is not an undo step, so no undo can leave the file behind without its set.
        /// </summary>
        [Test]
        public void UndoAfterMakeSetLeavesTheSetWhole()
        {
            SfxPreset asset = SfxForgeEditorLink.CreatePreset(Thud, Folder, "Kept");
            var panel = Panel(asset);
            panel.MoreLikeThis();
            panel.Keep(2); // an undo step of the sound's, before the set is made
            Assert.AreEqual("Keep SFX Preset Kept", Undo.GetCurrentGroupName());
            SfxVariationSet set = panel.MakeSet();
            string path = AssetDatabase.GetAssetPath(set);
            Assert.AreEqual("Keep SFX Preset Kept", Undo.GetCurrentGroupName(), "making the set is no undo step");
            Undo.PerformUndo(); // takes back the KEEP - and nothing of the set
            Assert.IsTrue(asset.patch.Equals(Thud));
            Assert.IsTrue(set != null, "the set is still there");
            Assert.IsTrue(File.Exists(path));
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceUpdate);
            var read = AssetDatabase.LoadAssetAtPath<SfxVariationSet>(path);
            Assert.IsNotNull(read, "and its file is a set");
            Assert.AreSame(asset, read.preset);
            Assert.AreEqual(SfxPresetPanel.GridCount + 1, read.Count);
            Undo.PerformRedo();
            Assert.IsNotNull(AssetDatabase.LoadAssetAtPath<SfxVariationSet>(path));
        }

        /// <summary>
        /// What a tool's note says is about the sounds as the key left them: an edit or an undo takes the note down.
        /// </summary>
        [Test]
        public void AToolsNoteGoesWhenTheSoundIsNoLongerWhatItSaid()
        {
            var panel = Panel(a);
            string kept = EditorGUIUtility.systemCopyBuffer;
            try
            {
                panel.CopyText();
                StringAssert.StartsWith("COPIED | ", panel.ShareText);
                panel.Refresh();
                StringAssert.StartsWith("COPIED | ", panel.ShareText, "nothing changed: it still says so");
                panel.Nudge(1, 3); // the text on the clipboard is no longer this sound's
                StringAssert.StartsWith("THIS SOUND AS TEXT | ", panel.ShareText);
            }
            finally
            {
                EditorGUIUtility.systemCopyBuffer = kept;
            }

            panel.Generate(SfxCategory.Zap);
            StringAssert.StartsWith("ZAP | seed ", panel.NewText);
            panel.Mutate();
            StringAssert.StartsWith(
                "A NEW SOUND OF A KIND | ",
                panel.NewText,
                "the seed named is not this sound's any more"
            );
            panel.Generate(SfxCategory.Door);
            StringAssert.StartsWith("DOOR | seed ", panel.NewText);
            Undo.PerformUndo();
            panel.Refresh();
            StringAssert.StartsWith("A NEW SOUND OF A KIND | ", panel.NewText, "nor after an undo");

            Assert.AreEqual(SfxTextResult.Ok, panel.PasteText(SfxPatchText.ToText(Thud)));
            StringAssert.StartsWith("PASTED | ", panel.ShareText);
            Undo.PerformUndo();
            panel.Refresh();
            StringAssert.StartsWith("THIS SOUND AS TEXT | ", panel.ShareText);
            Assert.AreEqual(SfxTextResult.NotOurs, panel.PasteText("hello"));
            StringAssert.StartsWith("NOT PASTED | ", panel.ShareText);
            panel.Refresh();
            StringAssert.StartsWith("NOT PASTED | ", panel.ShareText, "a refusal stays until something changes");

            if (!SfxNative.Available)
            {
                return;
            }

            var three = Panel(a, b, c);
            Assert.IsTrue(three.MatchLevels(SfxLevelMeasure.Rms));
            StringAssert.StartsWith("3 SOUNDS AT RMS ", three.MatchText);
            three.Page = SfxUnits.PageFx;

            // LEVEL of all three: they are no longer what the note says
            three.Nudge(3, -9);
            StringAssert.StartsWith("MATCH LEVEL ACROSS THE SELECTION | ", three.MatchText);
        }

        /// <summary>
        /// The variants' tiles say how each differs from the base; the engine's place among the lock keys says
        /// why it is no key.
        /// </summary>
        [Test]
        public void AVariantsTileSaysWhatMoved()
        {
            var panel = Panel(a);
            panel.MoreLikeThis();
            for (int n = 1; n <= SfxPresetPanel.GridCount; n++)
            {
                SfxPatch variant = panel.GridVariant(n);
                int moved = 0;
                int most = -1;
                int far = 0;
                for (int k = 0; k < SfxPatch.ParamCount; k++)
                {
                    int d = variant[(SfxParam)k] - panel.GridBase[(SfxParam)k];
                    if (d != 0)
                    {
                        moved++;
                    }

                    if (Math.Abs(d) > Math.Abs(far))
                    {
                        far = d;
                        most = k;
                    }
                }

                Assert.Greater(moved, 1);
                string says = SfxUnits.ParamName((SfxParam)most, variant.engine) + " " + (far > 0 ? "+" : "") + far;
                Assert.AreEqual(says, panel.VariantTileAt(n).WordText, "tile " + n);
            }

            SfxPresetPanel.Locked = SfxMutate.All; // nothing may move: every tile is the base
            panel.Refresh();
            Assert.AreEqual("THE BASE", panel.VariantTileAt(3).WordText);
            panel.Page = SfxUnits.PageOsc;
            Assert.AreEqual("ENGINE STAYS", panel.LockKey(0).text);
            Assert.IsTrue(panel.LockKey(0).Off);
            StringAssert.Contains("never change the engine", panel.LockKey(0).tooltip);
        }

        // ---- the level line, and LEVEL ------------------------------------------------------------------------------

        [Test]
        public void TheLevelLineReadsPeakAndRms()
        {
            Assume.That(SfxNative.Available, "the native library is for Windows x64");
            var panel = Panel(a); // (one sound: measured at once)
            SfxBaked baked = SfxBake.Render(a.patch, a.holdSeconds);
            Assert.AreEqual(baked.level.peak, panel.Level.peak);
            Assert.AreEqual(baked.level.rms, panel.Level.rms);
            Assert.AreEqual(
                "PEAK "
                    + SfxUnits.DbText(baked.level.PeakDb)
                    + "   RMS "
                    + SfxUnits.DbText(baked.level.RmsDb)
                    + " | as PLAY and BAKE WAV render it: "
                    + baked.Seconds.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture)
                    + "s with its effects' tail, velocity 100%",
                panel.LevelText
            );

            panel.Page = SfxUnits.PageFx;
            panel.Nudge(3, -20); // LEVEL 100 -> 80: the line follows, by 3.9 dB
            panel.MeasureNow();
            Assert.AreEqual(baked.level.PeakDb - 3.876f, panel.Level.PeakDb, 0.05f);
            StringAssert.StartsWith("PEAK " + SfxUnits.DbText(panel.Level.PeakDb), panel.LevelText);

            Assert.IsTrue(SfxPatchText.TryParse(ClippingText, out SfxPatch loud));
            a.patch = loud;
            panel = Panel(a);
            Assert.IsTrue(panel.Level.Clips, "peak " + panel.Level.peak);
            Assert.AreEqual(
                "CLIPS   PEAK "
                    + SfxUnits.DbText(panel.Level.PeakDb)
                    + "   RMS "
                    + SfxUnits.DbText(panel.Level.RmsDb)
                    + " | over full scale: a baked file is clamped. Lower LEVEL (FX page).",
                panel.LevelText
            );
            StringAssert.StartsWith("CLIPS   PEAK +", panel.LevelText);

            a.patch.level = 0;
            panel = Panel(a);
            Assert.IsTrue(panel.Level.Silent);
            Assert.AreEqual("SILENT | nothing above -120dB: LEVEL, CUTOFF or DECAY may be at 0", panel.LevelText);

            a.patch = SfxPatch.Default;
            panel = Panel(a, b, c); // several: a moment later - or now
            panel.MeasureNow();
            StringAssert.StartsWith("3 SOUNDS   PEAK ", panel.LevelText);
            StringAssert.Contains(" .. ", panel.LevelText);
            StringAssert.Contains("dB   RMS ", panel.LevelText);
            a.patch = loud;
            panel = Panel(a, b);
            panel.MeasureNow();
            StringAssert.StartsWith("CLIPS   2 SOUNDS   PEAK ", panel.LevelText);

            SfxUI.PretendNativeMissing = true;
            panel = Panel(a);
            Assert.AreEqual("NO LEVEL | the native library is not here", panel.LevelText);
            panel.MeasureNow();
            Assert.AreEqual("NO LEVEL | the native library is not here", panel.LevelText);
            Assert.IsTrue(Panel(a, b).Key("MATCH RMS").Off);
        }

        [Test]
        public void SfxUnitsWritesLevelsSeedsAndCents()
        {
            Assert.AreEqual("-3.2dB", SfxUnits.DbText(-3.2f));
            Assert.AreEqual("+1.3dB", SfxUnits.DbText(1.3f));
            Assert.AreEqual("0.0dB", SfxUnits.DbText(0f));
            Assert.AreEqual("0.0dB", SfxUnits.DbText(-0.04f));
            Assert.AreEqual("-14.8dB", SfxUnits.DbText(-14.84f));
            Assert.AreEqual("SILENT", SfxUnits.DbText(SfxLevels.Floor));
            Assert.AreEqual("SILENT", SfxUnits.DbText(-300f));
            Assert.AreEqual("±50ct", SfxUnits.CentsText(50f));
            Assert.AreEqual("OFF", SfxUnits.CentsText(0f));
            Assert.AreEqual("±1200ct", SfxUnits.CentsText(1200f));
            Assert.AreEqual("3F2A91C4", SfxUnits.SeedText(0x3F2A91C4u));
            Assert.AreEqual("00000001", SfxUnits.SeedText(1));
            Assert.IsTrue(SfxUnits.TryParseSeed("3f2a91c4", out uint seed) && seed == 0x3F2A91C4u);
            Assert.IsTrue(SfxUnits.TryParseSeed("0x10", out seed) && seed == 16);
            Assert.IsFalse(SfxUnits.TryParseSeed("xyz", out seed));
            Assert.IsFalse(SfxUnits.TryParseSeed("123456789", out seed));
            Assert.IsFalse(SfxUnits.TryParseSeed("", out seed));
            Assert.IsTrue(SfxUnits.TryParseDb("-3.5dB", out float db) && db == -3.5f);
            Assert.IsTrue(SfxUnits.TryParseDb("-6", out db) && db == -6f);
            Assert.IsTrue(SfxUnits.TryParseDb("+1 db", out db) && db == 1f);
            Assert.IsFalse(SfxUnits.TryParseDb("loud", out db));
            Assert.IsFalse(SfxUnits.TryParseDb("3s", out db));
            Assert.IsFalse(SfxUnits.TryParseDb("off", out db));
            Assert.AreEqual(
                SfxMutate.Bit(SfxParam.Table) | SfxMutate.Bit(SfxParam.Spread),
                SfxUnits.PageMask(SfxUnits.PageWave)
            );
            Assert.AreEqual(0u, SfxUnits.PageMask(9));
        }

        [Test]
        public void MatchWritesOnlyLevelAndOneUndoTakesItBack()
        {
            Assume.That(SfxNative.Available, "the native library is for Windows x64");
            var quiet = Make("Quiet", SfxPatch.Default);
            quiet.patch.level = 0;
            try
            {
                var panel = Panel(a, b, c, quiet);
                Assert.IsFalse(panel.Key("MATCH RMS").Off);
                Assert.IsFalse(panel.Key("MATCH PEAK").Off);
                StringAssert.StartsWith("MATCH LEVEL ACROSS THE SELECTION | ", panel.MatchText);
                SfxPatch pa = a.patch;
                SfxPatch pb = b.patch;
                SfxPatch pc = c.patch;
                float ha = a.holdSeconds;

                Assert.IsTrue(panel.MatchLevels(SfxLevelMeasure.Rms));
                Assert.AreEqual("Level 4 SFX Presets", Undo.GetCurrentGroupName());
                Assert.IsTrue(panel.CanUndoOwn, "the UNDO key lights for it");
                SfxPreset[] three = { a, b, c };
                SfxPatch[] were = { pa, pb, pc };
                float low = 99f;
                float high = -99f;
                bool changed = false;
                for (int i = 0; i < 3; i++)
                {
                    SfxPatch now = three[i].patch;
                    changed |= now.level != were[i].level;
                    now.level = were[i].level;
                    Assert.IsTrue(now.Equals(were[i]), three[i].name + ": nothing but LEVEL changed");
                    float rms = SfxBake.Render(three[i].patch, three[i].holdSeconds).level.RmsDb;
                    low = Mathf.Min(low, rms);
                    high = Mathf.Max(high, rms);
                    Assert.IsFalse(SfxBake.Render(three[i].patch, three[i].holdSeconds).level.Clips);
                }

                Assert.IsTrue(changed);
                Assert.LessOrEqual(high - low, 0.25f, "the three read the same RMS");
                Assert.AreEqual(0, quiet.patch.level, "a silent sound is left as it is");
                Assert.AreEqual(ha, a.holdSeconds);
                StringAssert.StartsWith("3 SOUNDS AT RMS -", panel.MatchText);
                StringAssert.Contains(
                    " | LEVEL: One 100 > "
                        + a.patch.level
                        + ", Two 100 > "
                        + b.patch.level
                        + ", Three 100 > "
                        + c.patch.level,
                    panel.MatchText
                );
                StringAssert.EndsWith(" – 'Quiet' is silent: left as it is", panel.MatchText);
                StringAssert.StartsWith("4 SOUNDS   PEAK ", panel.LevelText);

                Undo.PerformUndo(); // one step for all of them
                Assert.IsTrue(pa.Equals(a.patch) && pb.Equals(b.patch) && pc.Equals(c.patch));

                Assert.IsTrue(panel.MatchLevels(SfxLevelMeasure.Peak));
                low = 99f;
                high = -99f;
                foreach (SfxPreset p in three)
                {
                    float peak = SfxBake.Render(p.patch, p.holdSeconds).level.PeakDb;
                    low = Mathf.Min(low, peak);
                    high = Mathf.Max(high, peak);
                }

                Assert.LessOrEqual(high - low, 0.25f, "and the same peak");
                Assert.LessOrEqual(high, 0f);
                StringAssert.StartsWith("3 SOUNDS AT PEAK ", panel.MatchText);
            }
            finally
            {
                Object.DestroyImmediate(quiet);
            }
        }

        [Test]
        public void MatchNeedsTwoSounds()
        {
            Assume.That(SfxNative.Available, "the native library is for Windows x64");
            var panel = Panel(a);
            Assert.IsTrue(panel.Key("MATCH RMS").Off);
            Assert.IsTrue(panel.Key("MATCH PEAK").Off);
            SfxPatch before = a.patch;
            Assert.IsFalse(panel.MatchLevels(SfxLevelMeasure.Rms));
            Assert.AreEqual("NOTHING TO MATCH | two sounds that are not silent are needed", panel.MatchText);
            Assert.IsTrue(before.Equals(a.patch));

            b.patch = a.patch; // the same sound twice: they read the same already
            panel = Panel(a, b);
            Assert.IsFalse(panel.Key("MATCH RMS").Off);
            string top = Undo.GetCurrentGroupName();
            Assert.IsFalse(panel.MatchLevels(SfxLevelMeasure.Rms));
            Assert.AreEqual("ALREADY MATCHED | nothing was changed", panel.MatchText);
            Assert.AreEqual(top, Undo.GetCurrentGroupName(), "no undo step for nothing");

            b.patch.level = 0; // one of two is silent: one is not two
            panel = Panel(a, b);
            Assert.IsFalse(panel.MatchLevels(SfxLevelMeasure.Peak));
            StringAssert.StartsWith("NOTHING TO MATCH | ", panel.MatchText);
        }

        // ---- SHARE --------------------------------------------------------------------------------------------------
        [Test]
        public void PasteReplacesTheSoundAndUndoTakesItBack()
        {
            var panel = Panel(a);
            SfxPatch before = a.patch;
            a.holdSeconds = 2.25f;
            Assert.AreEqual(SfxPatchText.ToText(before), panel.PatchText);
            Assert.AreEqual(panel.PatchText, panel.ShareField.value, "the field shows the sound's text");
            Assert.AreEqual(
                "THIS SOUND AS TEXT | 62 characters: paste it in a chat, a commit, a comment.",
                panel.ShareText
            );

            Assert.AreEqual(
                SfxTextResult.Ok,
                panel.PasteText("  `" + SfxPatchText.ToText(Thud).ToLowerInvariant() + "`\n")
            );
            Assert.IsTrue(a.patch.Equals(Thud));
            Assert.AreEqual(2.25f, a.holdSeconds, "the hold is not part of the text");
            Assert.AreEqual("Paste SFX Preset One", Undo.GetCurrentGroupName());
            Assert.IsTrue(panel.CanUndoOwn);
            Assert.AreEqual("PASTED | IMPACT", panel.ShareText);
            Assert.AreEqual(SfxPatchText.ToText(Thud), panel.ShareField.value);
            Undo.PerformUndo();
            Assert.IsTrue(before.Equals(a.patch));

            string kept = EditorGUIUtility.systemCopyBuffer;
            try
            {
                panel.Refresh();
                panel.CopyText();
                Assert.AreEqual(SfxPatchText.ToText(before), EditorGUIUtility.systemCopyBuffer);
                StringAssert.StartsWith("COPIED | ", panel.ShareText);
            }
            finally
            {
                EditorGUIUtility.systemCopyBuffer = kept;
            }
        }

        [Test]
        public void ARefusedTextChangesNothingAndSaysWhy()
        {
            var panel = Panel(a);
            SfxPatch before = a.patch;
            EditorUtility.ClearDirty(a);
            Undo.IncrementCurrentGroup();
            string top = Undo.GetCurrentGroupName();
            string good = SfxPatchText.ToText(Thud);
            var refused = new (string text, SfxTextResult why)[]
            {
                ("", SfxTextResult.Empty),
                ("hello", SfxTextResult.NotOurs),
                ("SFXF2:" + good.Substring(6), SfxTextResult.NewerText),
                (good.Substring(0, 40), SfxTextResult.BadLength),
                (good.Substring(0, 30) + "Z" + good.Substring(31), SfxTextResult.BadCharacter),
                (good.Substring(0, 20) + (good[20] == '0' ? "1" : "0") + good.Substring(21), SfxTextResult.BadPatch),
            };
            foreach (var (text, why) in refused)
            {
                Assert.AreEqual(why, panel.PasteText(text), text);
                Assert.AreEqual("NOT PASTED | " + SfxPatchText.Message(why), panel.ShareText);
                Assert.IsTrue(before.Equals(a.patch));
                Assert.IsFalse(EditorUtility.IsDirty(a), "the asset is not touched");
                Assert.AreEqual(top, Undo.GetCurrentGroupName(), "and there is no undo step");
                Assert.AreEqual(
                    SfxPatchText.ToText(before),
                    panel.ShareField.value,
                    "the field shows the sound's text again"
                );
            }

            Assert.AreEqual(
                SfxTextResult.NoPatchInFile,
                panel.ImportSyx(Path.Combine(Application.temporaryCachePath, "there-is-no-such-file.syx"))
            );
            Assert.AreEqual("NOT IMPORTED | no SFX Forge patch in this file", panel.ShareText);
            Assert.IsTrue(before.Equals(a.patch));
        }

        [Test]
        public void PasteGoesToEverySelectedSound()
        {
            var panel = Panel(a, b, c);
            Assert.IsTrue(panel.Key("COPY").Off, "three different sounds: there is no one text of them");
            Assert.IsTrue(panel.Key("EXPORT .SYX").Off);
            StringAssert.StartsWith("THESE SOUNDS DIFFER | ", panel.ShareText);
            SfxPatch pa = a.patch;
            SfxPatch pc = c.patch;
            SfxPatch other = SfxGenerator.Make(SfxCategory.Door, 99);
            Assert.AreEqual(SfxTextResult.Ok, panel.PasteText(SfxPatchText.ToText(other)));
            Assert.IsTrue(a.patch.Equals(other) && b.patch.Equals(other) && c.patch.Equals(other));
            Assert.AreEqual("Paste 3 SFX Presets", Undo.GetCurrentGroupName());
            StringAssert.EndsWith(", to all 3 sounds", panel.ShareText);
            Assert.IsFalse(panel.Key("COPY").Off, "now they are the same sound");
            Undo.PerformUndo();
            Assert.IsTrue(pa.Equals(a.patch) && b.patch.Equals(Thud) && pc.Equals(c.patch));
        }

        [Test]
        public void TheFieldShowsTheTextAndTakesOne()
        {
            var panel = Panel(a);
            SfxPatch before = a.patch;
            Assert.AreEqual(SfxPatchText.Length, panel.ShareField.value.Length);
            Assert.AreEqual(panel.PatchText, panel.ShareField.value, "the field shows the sound's text");
            panel.Nudge(1, 4);
            Assert.AreEqual(SfxPatchText.ToText(a.patch), panel.ShareField.value, "... and follows the sound");

            // (a text put into the field is pasted: that needs a panel under the field - Shots.TheMouseOnTheTools does
            // it) as it stood in a sentence
            Assert.AreEqual(SfxTextResult.Ok, panel.PasteText("here it is: (" + SfxPatchText.ToText(Hum) + ")."));
            Assert.IsTrue(a.patch.Equals(Hum));
            Assert.AreEqual("Paste SFX Preset One", Undo.GetCurrentGroupName());
            Assert.AreEqual(
                SfxPatchText.ToText(Hum),
                panel.ShareField.value,
                "the field shows the sound's own text, not what was pasted"
            );
            Assert.AreEqual("PASTED | SCI-FI FM", panel.ShareText);
            Assert.AreEqual(SfxTextResult.BadLength, panel.PasteText("SFXF1:1234")); // cut off: nothing changes
            Assert.IsTrue(a.patch.Equals(Hum));
            Assert.AreEqual(SfxPatchText.ToText(Hum), panel.ShareField.value);
            Undo.PerformUndo();
            Assert.IsFalse(a.patch.Equals(Hum));
            panel.Refresh();
            Assert.AreEqual(SfxPatchText.ToText(a.patch), panel.ShareField.value);
        }

        [Test]
        public void ExportThenImportThroughAFile()
        {
            string path = Path.Combine(
                Application.temporaryCachePath,
                "sfx-forge-test-" + Guid.NewGuid().ToString("N") + ".syx"
            );
            try
            {
                SfxForgeEditorLink.ExportSyx(Hum, 3, path);
                Assert.AreEqual(SfxPatchText.ToSyx(Hum, 3), File.ReadAllBytes(path));
                Assert.AreEqual(35, new FileInfo(path).Length);
                Assert.AreEqual(SfxTextResult.Ok, SfxForgeEditorLink.ImportSyx(path, out SfxPatch read, out int pad));
                Assert.AreEqual(3, pad);
                Assert.IsTrue(read.Equals(Hum));

                var panel = Panel(a);
                SfxPatch before = a.patch;
                Assert.AreEqual(SfxTextResult.Ok, panel.ImportSyx(path));
                Assert.IsTrue(a.patch.Equals(Hum));
                Assert.AreEqual("IMPORTED PAD 4 | SCI-FI FM", panel.ShareText);
                Assert.AreEqual("Paste SFX Preset One", Undo.GetCurrentGroupName());
                Undo.PerformUndo();
                Assert.IsTrue(before.Equals(a.patch));

                // another device's SysEx
                File.WriteAllBytes(path, new byte[] { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 });
                Assert.AreEqual(SfxTextResult.NoPatchInFile, panel.ImportSyx(path));
                File.WriteAllBytes(path, new byte[70000]);
                Assert.AreEqual(SfxTextResult.TooLarge, panel.ImportSyx(path));
                Assert.AreEqual("NOT IMPORTED | too large to be a patch file", panel.ShareText);
                Assert.IsTrue(before.Equals(a.patch));
            }
            finally
            {
                if (File.Exists(path))
                {
                    File.Delete(path);
                }
            }
        }
    }
}
