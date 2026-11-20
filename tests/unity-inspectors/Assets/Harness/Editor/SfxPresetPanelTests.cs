using System.Diagnostics;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;

namespace Inspector.Harness
{
    /// <summary>The sound's Inspector without a window: what its knobs and keys do to the asset, to Undo, to several sounds at once.</summary>
    public class SfxPresetPanelTests
    {
        SfxPreset a, b;

        static SfxPreset Make(string name, SfxPatch patch)
        {
            var p = ScriptableObject.CreateInstance<SfxPreset>();
            p.name = name; p.patch = patch;
            return p;
        }

        static SfxPresetPanel Panel(params Object[] presets) => (SfxPresetPanel)SfxPresetEditor.Build(new SerializedObject(presets));

        [SetUp]
        public void SetUp()
        {
            a = Make("One", SfxPatch.Default);
            b = Make("Two", SfxPatch.Create(SfxEngine.Foley, 30, 64, 0, 0, 0, 127));
        }

        [TearDown]
        public void TearDown()
        {
            SfxUI.PretendNativeMissing = false;
            Object.DestroyImmediate(a); Object.DestroyImmediate(b);
        }

        [Test]
        public void EveryPageShowsItsValuesInTheUnitsOfTheDevice()
        {
            var panel = Panel(a);
            for (int page = 0; page < SfxUnits.PageCount; page++)
            {
                if (page == SfxUnits.PageWave) continue;       // (a laser is no wavetable)
                panel.Page = page;
                Assert.AreEqual(page, panel.Page);
                for (int i = 0; i < 4; i++)
                {
                    int k = SfxUnits.PageParams[page][i];
                    if (k == SfxUnits.EngineKnob) { Assert.AreEqual("ENGINE", panel.Tile(i).LabelText); Assert.AreEqual("SCI-FI FM", panel.Tile(i).ValueText); continue; }
                    Assert.AreEqual(SfxUnits.ParamName((SfxParam)k, a.patch.engine), panel.Tile(i).LabelText);
                    Assert.AreEqual(SfxUnits.ParamText((SfxParam)k, a.patch[(SfxParam)k]), panel.Tile(i).ValueText);
                }
            }
        }

        [Test]
        public void WaveIsOnlyThereForAWavetable()
        {
            var panel = Panel(a);
            panel.Page = SfxUnits.PageWave;
            Assert.AreEqual(SfxUnits.PageOsc, panel.Page);

            a.patch = SfxPatch.Create(SfxEngine.Wavetable);
            panel = Panel(a);
            panel.Page = SfxUnits.PageWave;
            Assert.AreEqual(SfxUnits.PageWave, panel.Page);
            Assert.AreEqual("TABLE", panel.Tile(0).LabelText);
            Assert.AreEqual("SAW-SQ", panel.Tile(0).ValueText);
            Assert.AreEqual("SPREAD", panel.Tile(1).LabelText);
            panel.Nudge(0, -1);                                // the table goes round
            Assert.AreEqual(SfxPatch.TableCount - 1, a.patch.table);
        }

        [Test]
        public void AKnobChangesTheAssetMarksItDirtyAndUndoTakesItBack()
        {
            var panel = Panel(a);
            EditorUtility.ClearDirty(a);
            byte before = a.patch.baseFreq;
            panel.Nudge(1, 5);
            Assert.AreEqual(before + 5, a.patch.baseFreq);
            Assert.IsTrue(EditorUtility.IsDirty(a));
            Assert.AreEqual(SfxUnits.ParamText(SfxParam.BaseFreq, before + 5), panel.Tile(1).ValueText);
            Undo.PerformUndo();
            Assert.AreEqual(before, a.patch.baseFreq);
            panel.Refresh();                                   // (in an Inspector, Undo's own event does this: the panel is in no window here)
            Assert.AreEqual(SfxUnits.ParamText(SfxParam.BaseFreq, before), panel.Tile(1).ValueText);
            Undo.PerformRedo();
            Assert.AreEqual(before + 5, a.patch.baseFreq);
            panel.Nudge(1, 500);
            Assert.AreEqual(127, a.patch.baseFreq);
            panel.Nudge(1, -500);
            Assert.AreEqual(0, a.patch.baseFreq);
        }

        [Test]
        public void TheUndoKeyTakesBackWhatWasDoneHere()
        {
            var panel = Panel(a);
            SfxPatch before = a.patch;
            panel.UndoOwn();                                   // nothing done here yet: nothing happens
            Assert.IsTrue(before.Equals(a.patch));
            panel.Mutate();
            Assert.IsFalse(before.Equals(a.patch));
            Assert.AreEqual(before.engine, a.patch.engine);
            panel.UndoOwn();
            Assert.IsTrue(before.Equals(a.patch));
        }

        // UNDO is Unity's Undo, and Unity's next step may be anything: the key takes back only what was done to this sound here
        [Test]
        public void TheUndoKeyLeavesAloneWhatWasNotDoneHere()
        {
            a.name = "UndoOne"; b.name = "UndoTwo";
            var panel = Panel(a);
            byte before = a.patch.baseFreq;
            float held = b.holdSeconds;
            panel.Refresh();
            Assert.IsFalse(panel.CanUndoOwn, "nothing done here yet");

            panel.Nudge(1, 5);
            Assert.IsTrue(panel.CanUndoOwn);
            Undo.IncrementCurrentGroup();                       // (Unity does this by itself after every event)
            panel.Refresh();
            Assert.IsTrue(panel.CanUndoOwn, "still the last thing done");

            Undo.RecordObject(b, "Something Else");             // something else is done in Unity
            b.holdSeconds = 9f;
            Undo.FlushUndoRecordObjects();
            Undo.IncrementCurrentGroup();
            panel.Refresh();                                    // (in an Inspector: the panel looks again every 400 ms)
            Assert.IsFalse(panel.CanUndoOwn, "the next undo is not ours: the key is dark");
            panel.UndoOwn();
            Assert.AreEqual(9f, b.holdSeconds, "the other thing stays done");
            Assert.AreEqual(before + 5, a.patch.baseFreq);

            Undo.PerformUndo();                                 // Ctrl+Z takes the other thing back
            Assert.AreEqual(held, b.holdSeconds);
            panel.Refresh();
            Assert.IsTrue(panel.CanUndoOwn, "ours is the next again");
            panel.UndoOwn();
            Assert.AreEqual(before, a.patch.baseFreq);
            panel.Refresh();
            Assert.IsFalse(panel.CanUndoOwn);

            // a second Inspector on another sound: what was done to the first is not its to take back - nor after a redo
            var other = Panel(b);
            panel.Nudge(1, 2);
            other.Refresh();
            Assert.IsFalse(other.CanUndoOwn);
            other.UndoOwn();
            Assert.AreEqual(before + 2, a.patch.baseFreq);
            Undo.PerformUndo(); Undo.PerformRedo();
            other.Refresh(); panel.Refresh();
            Assert.IsFalse(other.CanUndoOwn);
            Assert.IsTrue(panel.CanUndoOwn);
            panel.Mutate(); Assert.IsTrue(panel.CanUndoOwn);
            panel.Randomize(); Assert.IsTrue(panel.CanUndoOwn);
        }

        [Test]
        public void TheEngineGoesRoundAndTypingAndResetWork()
        {
            var panel = Panel(a);
            panel.Nudge(0, 1);
            Assert.AreEqual(SfxEngine.Foley, a.patch.engine);
            panel.Nudge(0, 2);
            Assert.AreEqual(SfxEngine.Impact, a.patch.engine);
            Assert.AreEqual("IMPACT", panel.Tile(0).ValueText);

            panel.Typed(1, "440hz");
            Assert.AreEqual("448Hz", panel.Tile(1).ValueText);  // the nearest there is
            panel.Typed(1, "nonsense");
            Assert.AreEqual("448Hz", panel.Tile(1).ValueText);
            panel.ResetValue(1);
            Assert.AreEqual(SfxPatch.Create(SfxEngine.Impact).baseFreq, a.patch.baseFreq);

            panel.Page = SfxUnits.PageEnv;
            panel.Typed(2, "off");
            Assert.AreEqual(0, a.patch.sustain);
            panel.Typed(1, "1.2s");
            Assert.AreEqual("1.2s", SfxUnits.ParamText(SfxParam.AmpDecay, a.patch.ampDecay));
        }

        [Test]
        public void RandomIsAOneShotInsideItsRanges()
        {
            var rng = new System.Random(7);
            for (int i = 0; i < 500; i++)
            {
                SfxPatch p = SfxPresetPanel.RandomPatch(rng);
                Assert.AreEqual(0, p.sustain);
                Assert.AreEqual(100, p.level);
                Assert.That(p.baseFreq, Is.InRange(30, 100));
                Assert.That(p.filterCutoff, Is.InRange(50, 127));
                Assert.That(p.table, Is.InRange(0, SfxPatch.TableCount - 1));
                Assert.That((int)p.engine, Is.InRange(0, SfxPatch.EngineCount - 1));
            }
            var panel = Panel(a);
            SfxPatch before = a.patch;
            panel.Randomize();
            Assert.AreEqual(0, a.patch.sustain);
            Undo.PerformUndo();
            Assert.IsTrue(before.Equals(a.patch));
        }

        [Test]
        public void HoldSeconds()
        {
            a.patch.sustain = 100; a.holdSeconds = 1.5f;
            var panel = Panel(a);
            panel.SetHold(h => h + 0.25f);
            Assert.AreEqual(1.75f, a.holdSeconds, 0.0001f);
            panel.SetHold(_ => -3f);
            Assert.AreEqual(0.05f, a.holdSeconds, 0.0001f);
            Undo.PerformUndo();
            Assert.AreEqual(1.75f, a.holdSeconds, 0.0001f);

            // typed: seconds however they are written, never more than a bake is long
            foreach (var (text, seconds) in new[] { ("2", 2f), ("2s", 2f), ("2sec", 2f), ("2 sec", 2f), ("1.5 seconds", 1.5f), ("200ms", 0.2f), ("200 ms", 0.2f), ("0,75", 0.75f), ("3 S", 3f) })
            {
                panel.TypedHold(text);
                Assert.AreEqual(seconds, a.holdSeconds, 0.0001f, text);
                Assert.AreEqual(SfxUnits.HoldText(seconds), panel.HoldTile.ValueText, text);
            }
            foreach (string text in new[] { "2k", "1khz", "50%", "soon", "" })
            {
                panel.TypedHold(text);
                Assert.AreEqual(3f, a.holdSeconds, 0.0001f, "not a time: " + text);
            }
            panel.TypedHold("2000");
            Assert.AreEqual(SfxPresetPanel.MaxHold, a.holdSeconds, 0.0001f);
            panel.SetHold(h => h + 100f);
            Assert.AreEqual(12f, a.holdSeconds, 0.0001f);
            Assert.AreEqual("12.00s", panel.HoldTile.ValueText);
        }

        // the picture is the device's: one dry voice, 1.5 s at most - whatever the patch sends to chorus, delay and reverb
        [Test]
        public void ThePictureIsTheDevicesDryVoice()
        {
            Assume.That(SfxNative.Available, "the native library is for Windows x64");
            SfxPatch wet = Shots.Wavetable().patch, dry = wet;
            Assert.Greater(wet.chorus + wet.delay + wet.reverb, 0);
            dry.chorus = 0; dry.delay = 0; dry.reverb = 0;
            float[] w = new float[SfxPicture.Max], d = new float[SfxPicture.Max];
            int wn = SfxPicture.Render(wet, w), dn = SfxPicture.Render(dry, d);
            Assert.AreEqual(dn, wn);
            Assert.AreEqual(d, w, "the sends are not in the picture");
            Assert.Greater(wn, 1199); Assert.LessOrEqual(wn, SfxPicture.Max);

            // a sustained sound is held 0.8 s, then let go: it sounds at 0.7 s, and the picture is longer than the hold
            float at = 0f;
            for (int i = (int)(0.7f * SfxPicture.Rate); i < (int)(0.75f * SfxPicture.Rate); i++) at = Mathf.Max(at, Mathf.Abs(w[i]));
            Assert.Greater(at, 0.01f);
            Assert.Greater(wn, (int)(SfxPicture.Hold * SfxPicture.Rate));

            // the device's own pictures say how long two factory sounds are: out/env.png ENGINE HUM "0.90s long", out/sound.png CRACKLE "1.19s long"
            SfxPatch hum = SfxPatch.Create(SfxEngine.SciFiFm, 22, 64, 0, 21, 55, 8, 78, 45, 60, 0, 0, 127, 34, 64, 40, 50, 0, 0, 0, 0, 0, 0, 0, 100, 0);
            SfxPatch crackle = SfxPatch.Create(SfxEngine.Foley, 30, 64, 0, 110, 127, 127, 105, 20, 100, 25, 0, 0, 34, 64, 40, 50, 0, 0, 0, 0, 0, 0, 0, 100, 0);
            string Long(SfxPatch p) => (SfxPicture.Render(p, d) / 24000f).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + "s long";
            Assert.AreEqual("0.90s long", Long(hum));
            Assert.AreEqual("1.19s long", Long(crackle));

            // the same again: always the same noise, a steady picture
            float[] again = new float[SfxPicture.Max];
            SfxPatch foley = SfxPatch.Create(SfxEngine.Foley, 30, 64, 0, 0, 0, 127);
            int n1 = SfxPicture.Render(foley, w), n2 = SfxPicture.Render(foley, again);
            Assert.AreEqual(n1, n2); Assert.AreEqual(w, again);

            a.patch = wet;
            var panel = Panel(a);
            panel.Page = SfxUnits.PageWave;
            Assert.IsTrue(panel.Wave.HasPicture);
            Assert.AreEqual((wn / 24000f).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + "s long", panel.LengthText);
            Assert.LessOrEqual(wn / 24000f, 1.5f);
            UnityEngine.Debug.Log($"PICTURE WT SWEEP {panel.LengthText} ({wn} samples; the device draws 1.50s at most)");
        }

        // ENV of a held sound: a thin line where the pad is let go (scene() in the firmware) - none for a one-shot
        [Test]
        public void TheEnvelopeMarksWhereAHeldSoundIsLetGo()
        {
            Assume.That(SfxNative.Available, "the native library is for Windows x64");
            a.patch = SfxPatch.Default;
            a.patch.sustain = 0;
            var panel = Panel(a);
            panel.Page = SfxUnits.PageEnv;
            Assert.Less(panel.Wave.Mark, 0f, "a one-shot is never let go");
            a.patch.sustain = 100; a.patch.attack = 0; a.patch.ampDecay = 0;
            panel = Panel(a);
            panel.Page = SfxUnits.PageEnv;
            Assert.AreEqual(16f / 368f + 0.16f, panel.Wave.Mark, 1e-4f, "the end of the sustain: 6 + 10 px of 368, and 16 % of the width");
            Assert.AreEqual(panel.Wave.Overlay[3].x, panel.Wave.Mark, 1e-6f);
            panel.Page = SfxUnits.PageOsc;
            Assert.Less(panel.Wave.Mark, 0f, "on the envelope's page only");
        }

        [Test]
        public void SeveralSoundsAtOnce()
        {
            b.patch.pitchEnvAmount = a.patch.pitchEnvAmount;
            var panel = Panel(a, b);
            Assert.AreEqual("-", panel.Tile(0).ValueText, "two engines");
            Assert.AreEqual("-", panel.Tile(1).ValueText, "two frequencies");
            Assert.AreEqual("+46", panel.Tile(2).ValueText, "the same pitch envelope: shown");
            byte fa = a.patch.baseFreq, fb = b.patch.baseFreq;
            panel.Nudge(1, 3);                                 // each from where it stands
            Assert.AreEqual(fa + 3, a.patch.baseFreq);
            Assert.AreEqual(fb + 3, b.patch.baseFreq);
            Undo.PerformUndo();
            Assert.AreEqual(fa, a.patch.baseFreq);
            Assert.AreEqual(fb, b.patch.baseFreq);
            panel.Typed(1, "440");                             // typed: the same for both
            Assert.AreEqual(a.patch.baseFreq, b.patch.baseFreq);
            Assert.AreEqual("448Hz", panel.Tile(1).ValueText);
            for (int page = 0; page < SfxUnits.PageCount; page++) { panel.Page = page; for (int i = 0; i < 4; i++) { panel.Nudge(i, 1); panel.ResetValue(i); panel.Typed(i, "10"); } }
            panel.Mutate(); panel.Randomize(); panel.SetHold(h => h + 1f); panel.UndoOwn();
        }

        [Test]
        public void ThePictureAndTheStateWithoutTheNativeLibrary()
        {
            Assume.That(SfxNative.Available, "the native library is for Windows x64");
            var panel = Panel(a);
            Assert.IsTrue(panel.Wave.HasPicture);
            SfxUI.PretendNativeMissing = true;
            panel = Panel(a);
            Assert.IsFalse(panel.Wave.HasPicture);
            for (int page = 0; page < SfxUnits.PageCount; page++) { panel.Page = page; panel.Nudge(1, 1); }
            panel.Mutate();
        }

        /// <summary>Makes an asset one that a later asset format wrote (the number is not public: through its serialized field).</summary>
        public static T FromFormat<T>(T asset, int format) where T : Object
        {
            var so = new SerializedObject(asset);
            so.FindProperty("format").intValue = format;
            so.ApplyModifiedPropertiesWithoutUndo();
            return asset;
        }

        [Test]
        public void ASoundFromANewerVersionSaysSoOverItsPages()
        {
            var panel = Panel(a);
            Assert.AreEqual(1, a.Format); Assert.IsFalse(a.IsFromNewerVersion);
            Assert.IsFalse(panel.NewerBanner.Shown, "a sound of this version has no banner");

            FromFormat(a, 2);
            Assert.AreEqual(2, a.Format); Assert.IsTrue(a.IsFromNewerVersion);
            panel = Panel(a);
            Assert.IsTrue(panel.NewerBanner.Shown);
            Assert.AreEqual("FROM A NEWER SFX FORGE", panel.NewerBanner.Bold);
            StringAssert.Contains("asset format 2, this version knows 1", panel.NewerBanner.Small);
            StringAssert.Contains("Saving it here loses", panel.NewerBanner.Small);
            Assert.AreEqual(SfxUnits.ParamText(SfxParam.BaseFreq, a.patch.baseFreq), panel.Tile(1).ValueText, "what is read of it shows as ever");

            panel = Panel(a, b);
            Assert.IsTrue(panel.NewerBanner.Shown);
            Assert.AreEqual("ONE OF THESE 2 SOUNDS IS FROM A NEWER SFX FORGE", panel.NewerBanner.Bold);
            FromFormat(b, 3);
            panel = Panel(a, b);
            Assert.AreEqual("2 OF THESE 2 SOUNDS ARE FROM A NEWER SFX FORGE", panel.NewerBanner.Bold);
            StringAssert.Contains("asset format 3", panel.NewerBanner.Small, "the highest of them");

            FromFormat(a, 1); FromFormat(b, 1);
            Assert.IsFalse(Panel(a, b).NewerBanner.Shown);
        }

        [Test]
        public void NotConnectedTheLinkLineSaysSo()
        {
            Assume.That(!SfxLink.Shared.HasOutput, "no MIDI port is open in this editor");
            StringAssert.StartsWith("NOT CONNECTED | ", Panel(a).LinkText);
        }

        [Test]
        public void HowLongAPictureTakes()
        {
            Assume.That(SfxNative.Available, "the native library is for Windows x64");
            SfxPatch held = SfxPatch.Create(SfxEngine.Wavetable, 46, 64, 0, 10, 20, 0, 100, 30, 80, 0, 5, 90, 60, 90, 70, 30, 0, 20, 80, 35, 50, 30, 40, 100, 0);
            var into = new float[SfxPicture.Max];
            SfxPicture.Render(held, into);
            var watch = Stopwatch.StartNew();
            const int n = 20;
            for (int i = 0; i < n; i++) SfxPicture.Render(i % 2 == 0 ? held : SfxPatch.Default, into);
            watch.Stop();
            UnityEngine.Debug.Log($"PICTURE RENDER {watch.Elapsed.TotalMilliseconds / n:0.00} ms each (24 kHz, one dry voice as the device, a held wavetable and a laser in turn)");
            var panelWatch = Stopwatch.StartNew();
            var panel = Panel(a);
            panelWatch.Stop();
            UnityEngine.Debug.Log($"PANEL BUILD {panelWatch.Elapsed.TotalMilliseconds:0.00} ms (with its first picture)");
            Assert.Less(watch.Elapsed.TotalMilliseconds / n, 50.0);
        }
    }
}
