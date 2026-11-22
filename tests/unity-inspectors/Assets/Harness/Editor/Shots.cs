using System;
using System.Collections;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UIElements;

namespace Inspector.Harness
{
    /// <summary>
    /// The pictures in out/inspector-shots: every line of Pictures is one, taken at each of Widths. A new picture = a new line.
    /// </summary>
    public class Shots
    {
        static readonly int[] Widths = { 340, 640 };
        // the ends of the range a layout must reflow over: only these pictures are taken there too
        static readonly int[] EndWidths = { 300, 700 };
        static readonly string[] AtTheEnds = { "sound-env", "kit", "song", "song-mix", "song-master", "song-32", "song-64", "song-auto-64" };

        static readonly (string name, Func<VisualElement> build)[] Pictures =
        {
            ("sound-osc",      () => Sound("Jump", SfxUnits.PageOsc)),
            ("sound-tone",     () => Sound("Laser", SfxUnits.PageTone)),
            ("sound-filter",   () => Sound("Woofer", SfxUnits.PageFilter)),
            ("sound-wave",     () => Sound(Wavetable(), SfxUnits.PageWave)),
            ("sound-env",      () => Sound("RollHum", SfxUnits.PageEnv)),
            ("sound-lfo",      () => Sound(Wavetable(), SfxUnits.PageLfo)),
            ("sound-fx",       () => Sound("Wind", SfxUnits.PageFx)),
            ("sound-multi",    () => Sound(new UnityEngine.Object[] { Preset("Jump"), Preset("Chime"), Preset("RollHum") }, SfxUnits.PageOsc)),
            ("sound-newer",    () => Sound(NewerSound(), SfxUnits.PageOsc)),     // an asset a later version made
            ("sound-nonative", () => { SfxUI.PretendNativeMissing = true; try { return Sound("Jump", SfxUnits.PageOsc); } finally { SfxUI.PretendNativeMissing = false; } }),
            ("kit",            KitSheet.Build),
            // the demo song with FM BASS (sound 7) chosen, as the device's pictures have it
            ("song",           () => Song(SfxSongPanel.PageSteps, 0, 127)),
            ("song-layers",    () => Song(SfxSongPanel.PageSteps, 3, 50)),      // pattern D at intensity 39%: the layers above it dim
            ("song-mix",       () => Song(SfxSongPanel.PageMix, 0, 127)),
            ("song-master",    () => Song(SfxSongPanel.PageMaster, 0, 127)),
            ("song-auto",      () => Song(SfxSongPanel.PageAuto, 5, 127)),      // pattern F: the bass's CUTOFF rises
            // the same song with pattern B at 32 steps, C and F at 64 (firmware 1.6.0): the rows show the pattern's own steps
            ("song-32",        () => FineSong(SfxSongPanel.PageSteps, 1, 127)),
            ("song-64",        () => FineSong(SfxSongPanel.PageSteps, 2, 127)),
            ("song-auto-64",   () => FineSong(SfxSongPanel.PageAuto, 5, 127)),
            ("song-none",      () => SfxSongEditor.Build(new SerializedObject(EmptySong()))),
            ("song-newer",     () => SfxSongEditor.Build(new SerializedObject(NewerSong()))),
            // the tools of a sound: the drawer under its keys, a tab each (the level line is over the keys in every sound picture)
            ("sound-tools-new",   ToolsNew),                    // after a click on ZAP
            ("sound-tools-vary",  ToolsVary),                   // MORE LIKE THIS, FREQ locked
            ("sound-tools-level", ToolsLevel),                  // three sounds, after MATCH RMS
            ("sound-tools-share", ToolsShare),
            // a variation set: baked, baked and then changed, and one without a preset (the first two are assets made for the pictures)
            ("variation-set",       () => VariationSet(ShotFolder + "/Footstep.asset")),
            ("variation-set-stale", () => VariationSet(ShotFolder + "/Coin.asset")),
            ("variation-set-none",  () => SfxVariationSetEditor.Build(new SerializedObject(EmptySet()))),
        };

        // Window > SFX Forge: its view, fed with a made-up state (no MIDI port is opened). 340 is the window's smallest width.
        static readonly int[] WindowWidths = { 340, 420, 700 };
        static readonly (string name, Func<VisualElement> build)[] WindowPictures =
        {
            ("window",          () => SfxForgeWindow.Build(SfxForgeViewTests.Linked())),
            ("window-known",    () => SfxForgeWindow.Build(SfxForgeViewTests.LinkedAndKnown())),      // the device said what it is
            ("window-off",      () => SfxForgeWindow.Build(SfxForgeViewTests.NotConnected())),
            ("window-noports",  () => SfxForgeWindow.Build(new SfxForgeModel())),
            // Window > SFX Forge Bake: after a run with one warning and one failure, and with nothing selected
            ("bake-window",       () => SfxBakeWindow.Build(SfxBatchBakeTests.Baked())),
            ("bake-window-empty", () => SfxBakeWindow.Build(new SfxBakeModel())),
        };

        // ---- the tools' pictures ----
        const string ShotFolder = "Assets/ShotTemp";

        // the tools' state is the session's (amount, locks) and the domain's (seeds): every picture starts from the same
        static void PlainTools()
        {
            SfxPresetPanel.Locked = 0;
            SfxPresetPanel.MutateAmount = 15;
            SfxPresetPanel.Seeds = new SfxRandom(0x3F2A91C4u);
        }

        // a preset of the project as an object of its own: what a picture changes is not the asset
        static SfxPreset Copy(string name)
        {
            var preset = UnityEngine.Object.Instantiate(Preset(name));
            preset.name = name;
            return preset;
        }

        static VisualElement ToolsNew()
        {
            PlainTools();
            var panel = (SfxPresetPanel)Sound(Copy("Jump"), SfxUnits.PageOsc);
            panel.ToolTab = SfxPresetPanel.ToolNew;
            panel.Generate(SfxCategory.Zap);
            panel.MeasureNow();
            return panel;
        }

        static VisualElement ToolsVary()
        {
            PlainTools();
            SfxPresetPanel.Locked = SfxMutate.Bit(SfxParam.BaseFreq);
            var panel = (SfxPresetPanel)Sound("Laser", SfxUnits.PageOsc);
            panel.ToolTab = SfxPresetPanel.ToolVary;
            panel.MoreLikeThis();
            panel.FinishPictures();
            return panel;
        }

        static VisualElement ToolsLevel()
        {
            PlainTools();
            var panel = (SfxPresetPanel)Sound(new UnityEngine.Object[] { Copy("Jump"), Copy("Laser"), Copy("Chime") }, SfxUnits.PageFx);
            panel.ToolTab = SfxPresetPanel.ToolLevel;
            panel.MatchLevels(SfxLevelMeasure.Rms);
            return panel;
        }

        static VisualElement ToolsShare()
        {
            PlainTools();
            var panel = (SfxPresetPanel)Sound("Wind", SfxUnits.PageOsc);
            panel.ToolTab = SfxPresetPanel.ToolShare;
            return panel;
        }

        static VisualElement VariationSet(string path)
        {
            var set = AssetDatabase.LoadAssetAtPath<SfxVariationSet>(path);
            Assert.IsNotNull(set, path);
            var panel = (SfxVariationSetPanel)SfxVariationSetEditor.Build(new SerializedObject(set));
            panel.FinishPictures();
            return panel;
        }

        static SfxVariationSet EmptySet()
        {
            var set = ScriptableObject.CreateInstance<SfxVariationSet>();
            set.name = "New Variation Set";
            return set;
        }

        // The sets of the pictures are assets with baked clips: made before play mode (they are files, they live through
        // it), deleted after it.
        static void MakeShotSets()
        {
            if (AssetDatabase.IsValidFolder(ShotFolder)) AssetDatabase.DeleteAsset(ShotFolder);
            SfxVariationSet steps = SfxVariationSetTests.MakeSet("Footstep", 6, SfxPatch.Create(SfxEngine.Foley, 34, 78, 20, 0, 0, 120, 82, 30, 30, 0), ShotFolder);
            SfxVariationSet coins = SfxVariationSetTests.MakeSet("Coin", 4, SfxGenerator.Make(SfxCategory.Chime, 4), ShotFolder);
            if (!SfxNative.Available) return;
            SfxVariationBaker.Bake(steps);
            SfxVariationBaker.Bake(coins);
            coins.amount = 25;                                  // changed after its bake: out of date
            EditorUtility.SetDirty(coins);
            AssetDatabase.SaveAssetIfDirty(coins);
        }

        public static SfxPreset Preset(string name)
        {
            var preset = AssetDatabase.LoadAssetAtPath<SfxPreset>($"Assets/Data/Presets/{name}.asset");
            Assert.IsNotNull(preset, name);
            return preset;
        }

        // the WT SWEEP of the device (there is no wavetable among the copied presets)
        public static SfxPreset Wavetable()
        {
            var preset = ScriptableObject.CreateInstance<SfxPreset>();
            preset.name = "WT Sweep";
            preset.patch = SfxPatch.Create(SfxEngine.Wavetable, 46, 64, 0, 10, 20, 0, 100, 30, 80, 0, 5, 90, 60, 90, 70, 30, 0, 20, 80, 35, 50, 30, 40, 100, 0);
            return preset;
        }

        static VisualElement Song(int page, int pattern, int intensity)
        {
            var panel = (SfxSongPanel)SfxSongEditor.Build(new SerializedObject(SfxSongPanelTests.Demo()));
            panel.Sound = 6; panel.Pattern = pattern; panel.Intensity = intensity; panel.Page = page;
            return panel;
        }

        static VisualElement FineSong(int page, int pattern, int intensity)
        {
            var panel = (SfxSongPanel)SfxSongEditor.Build(new SerializedObject(SfxSongGridTests.Fine()));
            panel.Sound = 6; panel.Pattern = pattern; panel.Intensity = intensity; panel.Page = page;
            return panel;
        }

        static SfxPreset NewerSound()
        {
            var preset = UnityEngine.Object.Instantiate(Preset("Jump"));
            preset.name = "Jump";
            return SfxPresetPanelTests.FromFormat(preset, 2);
        }

        // a song asset a later version made, whose packed song is of a later format too
        static SfxSong NewerSong()
        {
            var song = SfxSongPanelTests.Demo();
            byte[] later = (byte[])song.data.Clone();
            later[0] = 7;
            song.Set(song.sounds, later);
            return SfxPresetPanelTests.FromFormat(song, 3);      // (a song asset's own number: 2 is this version's)
        }

        static SfxSong EmptySong()
        {
            var song = ScriptableObject.CreateInstance<SfxSong>();
            song.name = "New Song";
            return song;
        }

        static VisualElement Sound(string name, int page) => Sound(new UnityEngine.Object[] { Preset(name) }, page);
        static VisualElement Sound(SfxPreset preset, int page) => Sound(new UnityEngine.Object[] { preset }, page);

        static VisualElement Sound(UnityEngine.Object[] presets, int page)
        {
            var panel = (SfxPresetPanel)SfxPresetEditor.Build(new SerializedObject(presets));
            panel.Page = page;
            panel.MeasureNow();                                 // (several sounds are measured a moment later in an Inspector: the picture does not wait)
            return panel;
        }

        static void Mouse(VisualElement on, EventType type, Vector2 at, int clicks = 1, float wheel = 0f)
        {
            var mouse = new Event { type = type, mousePosition = at, button = 0, clickCount = clicks, delta = new Vector2(0f, wheel) };
            using (EventBase e = type == EventType.MouseDown ? PointerDownEvent.GetPooled(mouse) : type == EventType.MouseUp ? PointerUpEvent.GetPooled(mouse)
                : type == EventType.ScrollWheel ? (EventBase)WheelEvent.GetPooled(mouse) : PointerMoveEvent.GetPooled(mouse))
                on.panel.visualTree.SendEvent(e);
        }

        // (statics: what a test holds in its own variables does not live through entering play mode)
        static SfxPreset mousePreset;

        /// <summary>The mouse on a value tile, in a real panel: a drag turns it and is one undo step, the wheel over the ring, a double-click on the ring.</summary>
        [UnityTest]
        public IEnumerator TheMouseOnATile()
        {
            yield return new EnterPlayMode();
            mousePreset = ScriptableObject.CreateInstance<SfxPreset>();
            mousePreset.name = "Mouse";
            yield return Shot.Take("mouse", 640, MousePanel, MouseOnIt);
            yield return new ExitPlayMode();
        }

        static VisualElement MousePanel() => Sound(mousePreset, SfxUnits.PageOsc);

        static void MouseOnIt(VisualElement element)
        {
            SfxPreset preset = mousePreset;
            int before = preset.patch.baseFreq;
            var tile = ((SfxPresetPanel)element).Tile(1);                 // FREQ
            Vector2 ring = tile.Knob.worldBound.center, text = new Vector2(tile.worldBound.xMin + 30f, tile.worldBound.center.y);
            Assert.Greater(tile.worldBound.width, 60f);

            Mouse(tile, EventType.MouseDown, text);
            Mouse(tile, EventType.MouseDrag, text + new Vector2(0f, -10f));
            Mouse(tile, EventType.MouseDrag, text + new Vector2(0f, -20f));
            Mouse(tile, EventType.MouseUp, text + new Vector2(0f, -20f));
            Assert.AreEqual(before + 10, preset.patch.baseFreq, "dragged 20 px up: ten steps");
            Undo.PerformUndo();
            Assert.AreEqual(before, preset.patch.baseFreq, "a drag is one undo step");

            Mouse(tile, EventType.ScrollWheel, ring, 1, -3f);
            Assert.AreEqual(before + 1, preset.patch.baseFreq, "the wheel over the ring");
            Mouse(tile, EventType.ScrollWheel, text, 1, -3f);
            Assert.AreEqual(before + 1, preset.patch.baseFreq, "the wheel beside the ring: nothing (the Inspector scrolls)");

            Mouse(tile, EventType.MouseDown, ring, 2);
            Assert.AreEqual(SfxPatch.Create(preset.patch.engine).baseFreq, preset.patch.baseFreq, "a double-click on the ring: the plain value");
            Assert.AreEqual(SfxUnits.ParamText(SfxParam.BaseFreq, preset.patch.baseFreq), tile.ValueText);
        }

        static SfxSong mouseSong;

        /// <summary>The mouse on the song, in a real panel: a pattern tile, a bar of the arrangement, the intensity, a sound's name, the LAYER tile (one undo step), the keys.</summary>
        [UnityTest]
        public IEnumerator TheMouseOnTheSong()
        {
            yield return new EnterPlayMode();
            mouseSong = SfxSongPanelTests.Demo();
            // (a page that was hidden has no place before the next frame: each page gets a panel of its own)
            yield return Shot.Take("mouse-song", 640, () => MouseSong(SfxSongPanel.PageMix), MouseOnTheMix);
            yield return Shot.Take("mouse-song", 640, () => MouseSong(SfxSongPanel.PageSteps), MouseOnTheSong);
            yield return new ExitPlayMode();
        }

        static void Click(VisualElement on)
        {
            Mouse(on, EventType.MouseDown, on.worldBound.center);
            Mouse(on, EventType.MouseUp, on.worldBound.center);
        }

        static VisualElement MouseSong(int page)
        {
            var panel = (SfxSongPanel)SfxSongEditor.Build(new SerializedObject(mouseSong));
            panel.Page = page; panel.Sound = 6; panel.Intensity = 127;
            return panel;
        }

        static void MouseOnTheMix(VisualElement element)
        {
            var panel = (SfxSongPanel)element;
            Click(panel.MixTileAt(12));                               // (it plays it too)
            Assert.AreEqual(12, panel.Sound, "a click on a MIX tile");
            Click(panel.Query<TextElement>(className: "sfx-tab").AtIndex(SfxSongPanel.PageAuto));
            Assert.AreEqual(SfxSongPanel.PageAuto, panel.Page, "a click on a tab");
        }

        static void MouseOnTheSong(VisualElement element)
        {
            var panel = (SfxSongPanel)element;
            SfxSong song = mouseSong;
            Assert.AreEqual(0, panel.Pattern);

            Click(panel.PatternTileAt(2));
            Assert.AreEqual(2, panel.Pattern, "a click on tile C");
            Click(panel.Query<TextElement>(className: "sfx-arr__bar").AtIndex(15));
            Assert.AreEqual(5, panel.Pattern, "a click on the arrangement's last bar: F");
            Click(panel.Query<TextElement>(className: "sfx-song-name").AtIndex(12));       // (it plays it too)
            Assert.AreEqual(12, panel.Sound, "a click on a sound's name");
            Click(panel.Query<TextElement>(className: "sfx-song-name").AtIndex(6));
            Assert.AreEqual(6, panel.Sound);

            Rect strip = panel.Heat.worldBound;
            Mouse(panel.Heat, EventType.MouseDown, new Vector2(strip.xMin + 4f + (strip.width - 8f) * 0.5f, strip.center.y));
            Assert.AreEqual(64, panel.Intensity, 1, "pressed in the middle of INTENSITY");
            Mouse(panel.Heat, EventType.MouseDrag, new Vector2(strip.xMin - 20f, strip.center.y));
            Assert.AreEqual(0, panel.Intensity, "dragged off its left end");
            Mouse(panel.Heat, EventType.MouseUp, new Vector2(strip.xMin - 20f, strip.center.y));
            Assert.IsTrue(panel.Row(6).Dimmed); Assert.IsFalse(panel.Row(2).Dimmed);
            Assert.AreEqual(35, song.LayerRaw(6), "looking with another intensity changes no layer");

            var tile = panel.SoundTile(SfxSongPanel.TileLayer);
            Vector2 text = new Vector2(tile.worldBound.xMin + 60f, tile.worldBound.center.y);
            Mouse(tile, EventType.MouseDown, text);
            Mouse(tile, EventType.MouseDrag, text + new Vector2(0f, -10f));
            Mouse(tile, EventType.MouseDrag, text + new Vector2(0f, -20f));
            Mouse(tile, EventType.MouseUp, text + new Vector2(0f, -20f));
            Assert.AreEqual(45, song.LayerRaw(6), "LAYER dragged 20 px up: ten steps");
            Assert.AreEqual(SfxUnits.Percent(45), tile.ValueText);
            Undo.PerformUndo();
            Assert.AreEqual(35, song.LayerRaw(6), "a drag is one undo step");

            var steps = panel.SongTile(SfxSongPanel.TileSteps);     // view only: the mouse does nothing to it
            byte[] before = (byte[])song.data.Clone();
            Vector2 on = new Vector2(steps.worldBound.xMin + 60f, steps.worldBound.center.y);
            Mouse(steps, EventType.MouseDown, on); Mouse(steps, EventType.MouseDrag, on + new Vector2(0f, -30f)); Mouse(steps, EventType.MouseUp, on + new Vector2(0f, -30f));
            Mouse(steps, EventType.ScrollWheel, steps.Knob.worldBound.center, 1, -3f);
            Assert.AreEqual(before, song.data);

            foreach (string word in new[] { "PLAY SOUND", "PLAY BAR", "STOP", "SEND" })      // the keys: none throws (SEND is off: not connected)
            {
                var key = panel.Query<SoftKey>().Where(k => k.text == word).First();
                Assert.IsNotNull(key, word);
                Click(key);
            }
            Assert.AreEqual(before, song.data);
        }

        [UnityTest]
        public IEnumerator TakeAll()
        {
            MakeShotSets();
            yield return new EnterPlayMode();
            PlainTools();
            foreach (var picture in Pictures)
            {
                foreach (int width in Widths) yield return Shot.Take(picture.name, width, picture.build);
                if (Array.IndexOf(AtTheEnds, picture.name) >= 0)
                    foreach (int width in EndWidths) yield return Shot.Take(picture.name, width, picture.build);
            }
            foreach (var picture in WindowPictures)
                foreach (int width in WindowWidths) yield return Shot.Take(picture.name, width, picture.build);
            PlainTools();
            yield return new ExitPlayMode();
            AssetDatabase.DeleteAsset(ShotFolder);
        }

        // ---- the mouse on the tools ----
        static SfxPreset toolsPreset;
        static SfxBakeModel bakeModel;
        static string bakeSaid;

        /// <summary>The mouse on a sound's tools, in a real panel: a NEW key, the text field of SHARE, the tool tabs, the locks and MORE LIKE THIS, a variant's KEEP.</summary>
        [UnityTest]
        public IEnumerator TheMouseOnTheTools()
        {
            yield return new EnterPlayMode();
            PlainTools();
            toolsPreset = ScriptableObject.CreateInstance<SfxPreset>();
            toolsPreset.name = "Tools";
            // (a box that was hidden has no place before the next frame: each tab gets a panel of its own)
            yield return Shot.Take("mouse-tools", 640, () => ToolsPanel(SfxPresetPanel.ToolNew, false), MouseOnNewAndShare);
            yield return Shot.Take("mouse-tools", 640, () => ToolsPanel(SfxPresetPanel.ToolVary, false), MouseOnVary);
            yield return Shot.Take("mouse-tools", 640, () => ToolsPanel(SfxPresetPanel.ToolVary, true), MouseOnTheVariants);
            foreach (int width in new[] { 300, 340, 640 })      // the sound's text shows whole, however narrow the Inspector
                yield return Shot.Take("mouse-tools", width, () => ToolsPanel(SfxPresetPanel.ToolShare, false), TheWholeTextShows);
            PlainTools();
            yield return new ExitPlayMode();
        }

        static VisualElement ToolsPanel(int tab, bool grid)
        {
            PlainTools();
            toolsPreset.patch = SfxPatch.Default;
            var panel = (SfxPresetPanel)Sound(toolsPreset, SfxUnits.PageOsc);
            panel.ToolTab = tab;
            if (grid) panel.MoreLikeThis();
            return panel;
        }

        static void MouseOnNewAndShare(VisualElement element)
        {
            var panel = (SfxPresetPanel)element;
            SfxPreset preset = toolsPreset;
            SfxPatch before = preset.patch;
            Assert.Greater(panel.Key("ZAP").worldBound.width, 40f);
            Click(panel.Key("BLAST"));
            Assert.IsTrue(SfxGenerator.Fits(SfxCategory.Blast, preset.patch), "a click on a NEW key");
            Assert.AreEqual(SfxGenerator.Make(SfxCategory.Blast, panel.LastSeed), preset.patch);
            StringAssert.StartsWith("BLAST | seed ", panel.NewText);
            Click(panel.Key("UNDO"));
            Assert.IsTrue(before.Equals(preset.patch), "the UNDO key takes a generated sound back");

            // SHARE's field (its box is not shown: the field takes a text all the same)
            SfxPatch other = SfxGenerator.Make(SfxCategory.Chime, 12);
            Assert.AreEqual(panel.PatchText, panel.ShareField.value);
            panel.ShareField.value = SfxPatchText.ToText(other);
            Assert.IsTrue(preset.patch.Equals(other), "a text typed into the field is pasted");
            Assert.AreEqual(SfxPatchText.ToText(other), panel.ShareField.value);
            StringAssert.StartsWith("PASTED | ", panel.ShareText);
            panel.ShareField.value = "the first again: (" + SfxPatchText.ToText(before) + ").";     // as it stands in a sentence
            Assert.IsTrue(preset.patch.Equals(before), "a text in a sentence is read out of it");
            Assert.AreEqual(SfxPatchText.ToText(before), panel.ShareField.value, "and the field shows the sound's own text");
            panel.ShareField.value = SfxPatchText.ToText(other);
            panel.ShareField.value = "not a sound";
            Assert.IsTrue(preset.patch.Equals(other), "a text that is none changes nothing");
            Assert.AreEqual(SfxPatchText.ToText(other), panel.ShareField.value, "and the field shows the sound's text again");
            Assert.AreEqual("NOT PASTED | " + SfxPatchText.Message(SfxTextResult.NotOurs), panel.ShareText);

            Click(panel.ToolTabs.Query<TextElement>(className: "sfx-tab").AtIndex(SfxPresetPanel.ToolLevel));
            Assert.AreEqual(SfxPresetPanel.ToolLevel, panel.ToolTab, "a click on a tool tab");
            Assert.IsTrue(panel.ToolShown(SfxPresetPanel.ToolLevel)); Assert.IsFalse(panel.ToolShown(SfxPresetPanel.ToolNew));
        }

        static void TheWholeTextShows(VisualElement element)
        {
            var panel = (SfxPresetPanel)element;
            TextElement letters = panel.ShareField.Q<TextElement>();
            Assert.AreEqual(SfxPatchText.Length, panel.ShareField.value.Length);
            float wide = letters.MeasureTextSize(panel.ShareField.value, 0, VisualElement.MeasureMode.Undefined, 0, VisualElement.MeasureMode.Undefined).x;
            Assert.Greater(letters.contentRect.width, 100f);
            Assert.LessOrEqual(wide, letters.contentRect.width + 0.5f, "all 62 characters fit the field (letters of " + panel.ShareFontSize + " px in " + panel.worldBound.width + " px)");
            Assert.GreaterOrEqual(panel.ShareFontSize, 6f);
        }

        static void MouseOnVary(VisualElement element)
        {
            var panel = (SfxPresetPanel)element;
            Assert.AreEqual("LOCK FREQ", panel.LockKey(1).text);
            Click(panel.LockKey(1));
            Assert.AreEqual(SfxMutate.Bit(SfxParam.BaseFreq), SfxPresetPanel.Locked, "a click on a lock key");
            Assert.IsTrue(panel.LockKey(1).Lit); Assert.IsTrue(panel.Tile(1).Locked);
            Click(panel.LockKey(0));                            // the engine's: off, nothing
            Assert.AreEqual(SfxMutate.Bit(SfxParam.BaseFreq), SfxPresetPanel.Locked);
            Click(panel.Key("LOCK PAGE"));
            Assert.AreEqual(SfxUnits.PageMask(SfxUnits.PageOsc), SfxPresetPanel.Locked);
            Click(panel.Key("UNLOCK ALL"));
            Assert.AreEqual(0u, SfxPresetPanel.Locked);

            var tile = panel.AmountTile;                        // AMOUNT dragged 30 px up: ten steps of 3 px
            Vector2 text = new Vector2(tile.worldBound.xMin + 70f, tile.worldBound.center.y);
            Mouse(tile, EventType.MouseDown, text);
            Mouse(tile, EventType.MouseDrag, text + new Vector2(0f, -30f));
            Mouse(tile, EventType.MouseUp, text + new Vector2(0f, -30f));
            Assert.AreEqual(25, SfxPresetPanel.MutateAmount); Assert.AreEqual("25%", tile.ValueText);
            Mouse(tile, EventType.MouseDown, tile.Knob.worldBound.center, 2);
            Assert.AreEqual(15, SfxPresetPanel.MutateAmount, "a double-click on its ring: 15%");

            Click(panel.Key("AGAIN")); Click(panel.Key("BACK TO BASE"));         // off: nothing
            Assert.IsFalse(panel.HasGrid);
            Click(panel.Key("MORE LIKE THIS"));
            Assert.IsTrue(panel.HasGrid); Assert.IsTrue(panel.GridShown);
            uint seed = panel.GridSeed;
            Click(panel.Key("AGAIN"));
            Assert.AreNotEqual(seed, panel.GridSeed);
            Assert.IsTrue(toolsPreset.patch.Equals(SfxPatch.Default), "nothing of this changed the sound");
            SfxPatch was = toolsPreset.patch;
            Click(panel.Key("MUTATE"));
            Assert.IsFalse(toolsPreset.patch.Equals(was));
        }

        static void MouseOnTheVariants(VisualElement element)
        {
            var panel = (SfxPresetPanel)element;
            SfxPreset preset = toolsPreset;
            Assert.IsTrue(panel.GridShown);
            Assert.Greater(panel.VariantTileAt(1).worldBound.width, 100f);
            Assert.AreEqual(panel.VariantTileAt(1).worldBound.y, panel.VariantTileAt(4).worldBound.y, 0.5f, "640 px: four in a row");
            Assert.Greater(panel.VariantTileAt(5).worldBound.y, panel.VariantTileAt(1).worldBound.y + 20f);
            Click(panel.VariantTileAt(2).PlayKey);              // (plays, changes nothing)
            Assert.IsTrue(preset.patch.Equals(panel.GridBase));
            Click(panel.VariantTileAt(6).KeepKey);
            Assert.IsTrue(preset.patch.Equals(panel.GridVariant(6)), "KEEP on a tile");
            Assert.IsTrue(panel.VariantTileAt(6).KeepKey.Lit); Assert.IsFalse(panel.VariantTileAt(2).KeepKey.Lit);
            Click(panel.Key("BACK TO BASE"));
            Assert.IsTrue(preset.patch.Equals(panel.GridBase));
            Assert.IsFalse(panel.VariantTileAt(6).KeepKey.Lit);
        }

        /// <summary>The mouse on the bake window's view, in a real panel: its keys reach the model's actions, a tile's menu and the folder field too.</summary>
        [UnityTest]
        public IEnumerator TheMouseOnTheBakeWindow()
        {
            yield return new EnterPlayMode();
            bakeModel = SfxBatchBakeTests.Baked();
            bakeSaid = "";
            var m = bakeModel;
            m.bake = () => bakeSaid += "bake "; m.bakeSets = () => bakeSaid += "sets "; m.show = () => bakeSaid += "show ";
            m.setTo = to => { bakeModel.to = to; bakeSaid += "to:" + to + " "; };
            m.setFolder = folder => { bakeModel.folder = folder; bakeSaid += "folder:" + folder + " "; };
            m.optionsChanged = () => bakeSaid += "options ";
            yield return Shot.Take("mouse-bake", 640, () => SfxBakeWindow.Build(bakeModel), MouseOnTheBakeWindow);
            yield return new ExitPlayMode();
        }

        static void MouseOnTheBakeWindow(VisualElement element)
        {
            var view = (SfxBakeView)element;
            string Said() { string s = bakeSaid.Trim(); bakeSaid = ""; return s; }
            Click(view.Key("BAKE")); Click(view.Key("BAKE SETS")); Click(view.Key("SHOW"));
            Assert.AreEqual("bake sets show", Said());
            Click(view.ToKey(SfxBakeTo.NextToEachAsset));
            Assert.AreEqual("to:NextToEachAsset", Said());
            Click(view.ToKey(SfxBakeTo.OneFolder));
            Assert.AreEqual("to:OneFolder", Said());
            view.FolderField.value = "Assets/Elsewhere";
            Assert.AreEqual("folder:Assets/Elsewhere", Said());

            var tile = view.Tile(SfxBakeView.TilePitch);        // PITCH dragged 16 px up: four steps of 4 px
            Vector2 text = new Vector2(tile.worldBound.xMin + 70f, tile.worldBound.center.y);
            Mouse(tile, EventType.MouseDown, text);
            Mouse(tile, EventType.MouseDrag, text + new Vector2(0f, -16f));
            Mouse(tile, EventType.MouseUp, text + new Vector2(0f, -16f));
            Assert.AreEqual(4f, view.Model.options.semitones); Assert.AreEqual("+4st", tile.ValueText);
            StringAssert.Contains("options", Said());
            Mouse(tile, EventType.MouseDown, tile.Knob.worldBound.center, 2);
            Assert.AreEqual(0f, view.Model.options.semitones, "a double-click on the ring");

            bakeModel.files = 0; bakeModel.presets = 0; bakeModel.sets = 0; bakeModel.report = null;      // nothing selected: the keys are off
            view.Refresh();
            Said();
            Click(view.Key("BAKE")); Click(view.Key("BAKE SETS")); Click(view.Key("SHOW"));
            Assert.AreEqual("", Said());
        }

        static SfxForgeModel mouseModel;
        static string mouseSaid;

        /// <summary>The mouse on the window's view, in a real panel: a pad held and let go, the tabs, a patch's keys, the soft keys.</summary>
        [UnityTest]
        public IEnumerator TheMouseOnTheWindow()
        {
            yield return new EnterPlayMode();
            mouseModel = SfxForgeViewTests.Linked();
            mouseSaid = "";
            var m = mouseModel;
            m.playPad = p => mouseSaid += "play" + p + " "; m.releasePad = p => mouseSaid += "release" + p + " "; m.fetchPad = p => mouseSaid += "fetch" + p + " ";
            m.play = p => mouseSaid += "hear" + p.slot + " "; m.save = p => mouseSaid += "save" + p.slot + " "; m.show = p => mouseSaid += "show" + p.slot + " ";
            m.connect = () => mouseSaid += "connect "; m.disconnect = () => mouseSaid += "disconnect "; m.refreshPorts = () => mouseSaid += "ports ";
            m.fetchSong = () => mouseSaid += "song "; m.showSong = () => mouseSaid += "showsong "; m.sendText = t => mouseSaid += "text:" + t + " ";
            m.setFolder = f => mouseSaid += "folder:" + f + " ";
            m.setAutoSave = on => { mouseModel.autoSave = on; mouseSaid += "auto:" + on + " "; };
            yield return Shot.Take("mouse-window", 640, () => SfxForgeWindow.Build(mouseModel), MouseOnTheWindow);
            yield return new ExitPlayMode();
        }

        static void MouseOnTheWindow(VisualElement element)
        {
            var view = (SfxForgeView)element;
            var m = mouseModel;
            string Said() { string s = mouseSaid.Trim(); mouseSaid = ""; return s; }

            var pad = view.Pads.Tile(4);
            Mouse(pad, EventType.MouseDown, pad.worldBound.center);
            Assert.AreEqual("play4", Said(), "a pad pressed: it plays");
            Assert.AreEqual("5", view.HeaderNumber);
            Mouse(pad, EventType.MouseUp, pad.worldBound.center);
            Assert.AreEqual("release4", Said(), "and let go");

            // the mouse is taken away while a pad is down (another window comes to the front): the pad is let go all the same, once
            Mouse(pad, EventType.MouseDown, pad.worldBound.center);
            Assert.AreEqual("play4", Said());
            pad.ReleasePointer(PointerId.mousePointerId);
            Mouse(view, EventType.MouseMove, new Vector2(2f, 2f));
            Assert.AreEqual("release4", Said(), "the capture went without a button up");
            Mouse(pad, EventType.MouseUp, pad.worldBound.center);
            Assert.AreEqual("", Said(), "not twice");
            // ... and when the window closes with a pad down
            var other = view.Pads.Tile(6);
            Mouse(other, EventType.MouseDown, other.worldBound.center);
            Assert.AreEqual("play6", Said());
            view.LetGo();
            Assert.AreEqual("release6", Said(), "the window lets go of what it holds");
            Mouse(other, EventType.MouseUp, other.worldBound.center);
            Assert.AreEqual("", Said());

            Click(view.PadTabs.Query<TextElement>(className: "sfx-tab").AtIndex(1));
            Assert.AreEqual(1, m.padMode, "the second tab: pads fetch");
            Click(view.Pads.Tile(9));
            Assert.AreEqual("fetch9", Said(), "a pad in fetch mode asks for its patch, and plays nothing");

            Assert.Greater(view.PatchTileAt(0).worldBound.width, 100f);
            Click(view.PatchTileAt(0).PlayKey);
            Assert.AreEqual("hear" + m.received[0].slot, Said());
            Click(view.PatchTileAt(0).SaveKey);               // the newest one is saved already: SHOW
            Assert.AreEqual("show" + m.received[0].slot, Said());
            Click(view.PatchTileAt(1).SaveKey);
            Assert.AreEqual("save" + m.received[1].slot, Said());

            Click(view.Key("SAVE AS THEY ARRIVE"));
            Assert.AreEqual("auto:False", Said());
            Click(view.Key("SHOW ON DEVICE"));
            Assert.AreEqual("text:LEVEL 3", Said());
            view.TextField.value = "BOSS";                      // typed: the model has it, nothing is sent before the key
            Assert.AreEqual("BOSS", m.text);
            Assert.AreEqual("", Said());
            view.FolderField.value = "Assets/Sounds";
            Assert.AreEqual("folder:Assets/Sounds", Said());
            Click(view.Key("DISCONNECT"));
            Click(view.Key("REFRESH PORTS"));
            Click(view.Key("FETCH SONG"));
            Click(view.Key("SHOW SONG"));
            Assert.AreEqual("disconnect ports song showsong", Said());

            // the ports are fixed while the link is open: a click on a port tile opens no menu (and throws nothing)
            Click(view.InputTile);
            Assert.IsTrue(view.InputTile.Locked);
        }
    }
}
