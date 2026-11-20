using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UIElements;

namespace Inspector.Harness
{
    /// <summary>
    /// Window > SFX Forge without a window and without a MIDI port: its view shows a model, and tells the model what was
    /// asked for. The states here are made up (Linked, NotConnected) – they are what the pictures show too.
    /// </summary>
    public class SfxForgeViewTests
    {
        static readonly string[] Ins = { "VIBE FM", "loopMIDI Port A", "USB MIDI Interface" };
        static readonly string[] Outs = { "Microsoft GS Wavetable Synth", "VIBE FM", "loopMIDI Port B" };

        /// <summary>A link that is open, six patches that came (the newest saved), six messages, the knobs somewhere, pad 3 held on the device.</summary>
        public static SfxForgeModel Linked()
        {
            var m = NotConnected();
            m.isOpen = true; m.hasOutput = true; m.inputName = "VIBE FM"; m.outputName = "VIBE FM";
            m.text = "LEVEL 3";
            m.hasSong = true;
            int[] knobs = { 69, 64, 101, 12, 127, 0 };
            for (int i = 0; i < knobs.Length; i++) m.TakeKnob(i, knobs[i] / 127f);
            (string preset, int pad, string time)[] came =
            {
                ("Woofer", 0, "14:02:11"), ("Wind", 12, "14:02:40"), ("TuningFork", 4, "14:03:05"), ("WT Sweep", 7, "14:03:31"), ("Chime", 9, "14:03:52"), ("Laser", 10, "14:05:27"),
            };
            foreach (var c in came) m.TakePatch(c.pad, Patch(c.preset), c.time);
            m.received[0].saved = Shots.Preset("Laser");
            m.TakePad(2, true);
            m.lastPad = 10;
            m.TakeMessage("B0 14 45");
            m.TakeMessage("90 26 64");
            m.TakeMessage("80 26 00");
            m.TakeMessage("F0 7D 53 46 01 0A 02 2E 40 00 0A 14 00 64 1E 50 00 05 5A 3C 5A 46 1E 00 …");
            m.TakeMessage("B0 16 65");
            m.TakeMessage("B0 18 7F");
            return m;
        }

        // (the wavetable is the harness's own: there is none among the copied presets)
        static SfxPatch Patch(string preset) => preset == "WT Sweep" ? Shots.Wavetable().patch : Shots.Preset(preset).patch;

        /// <summary>Ports there are and two of them chosen, nothing open, nothing came.</summary>
        public static SfxForgeModel NotConnected()
        {
            return new SfxForgeModel { inputs = Ins, outputs = Outs, inputPort = "VIBE FM", outputPort = "VIBE FM" };
        }

        static SfxForgeView View(SfxForgeModel m) => (SfxForgeView)SfxForgeWindow.Build(m);

        [Test]
        public void NotConnectedItSaysSoAndNothingThatNeedsTheDeviceClicks()
        {
            var view = View(NotConnected());
            StringAssert.StartsWith("NOT CONNECTED | ", view.StatusText);
            Assert.AreEqual("FROM DEVICE", view.InputTile.LabelText); Assert.AreEqual("VIBE FM", view.InputTile.ValueText);
            Assert.AreEqual("TO DEVICE", view.OutputTile.LabelText); Assert.AreEqual("VIBE FM", view.OutputTile.ValueText);
            Assert.IsFalse(view.InputTile.Locked, "the ports can be chosen while nothing is open");
            Assert.IsNotNull(view.Key("CONNECT")); Assert.IsNull(view.Key("DISCONNECT"));
            Assert.IsFalse(view.Key("CONNECT").Lit);
            Assert.IsTrue(view.Key("FETCH SONG").Off); Assert.IsTrue(view.Key("SHOW SONG").Off); Assert.IsTrue(view.Key("SHOW ON DEVICE").Off);
            Assert.IsFalse(view.Key("REFRESH PORTS").Off);
            Assert.IsTrue(view.Key("SAVE AS THEY ARRIVE").Lit, "saving on arrival is on unless switched off");
            Assert.AreEqual(0, view.PatchCount); Assert.IsTrue(view.PatchHintShown);
            for (int i = 0; i < view.Bars.Count; i++) Assert.AreEqual("-", view.Bars.ValueText(i));
            Assert.AreEqual("", view.MessageText(0));
            Assert.AreEqual(SfxForgeModel.DefaultFolder, view.FolderField.value);
        }

        [Test]
        public void WithoutPortsAndWithoutMidiItSaysWhy()
        {
            StringAssert.StartsWith("NO MIDI PORTS | ", View(new SfxForgeModel()).StatusText);
            StringAssert.Contains("loopMIDI", View(new SfxForgeModel()).StatusText);
            StringAssert.StartsWith("NO MIDI HERE | ", View(new SfxForgeModel { midiSupported = false }).StatusText);
            var gone = View(new SfxForgeModel { inputs = Ins, outputs = Outs, inputPort = "Old Cable", outputPort = "" });
            StringAssert.Contains("NOT FOUND", gone.InputTile.LabelText); Assert.AreEqual("Old Cable", gone.InputTile.ValueText);
            Assert.AreEqual("(none)", gone.OutputTile.ValueText);
        }

        [Test]
        public void LinkedItShowsWhatCame()
        {
            var m = Linked();
            var view = View(m);
            Assert.AreEqual("LINKED | in: VIBE FM   out: VIBE FM", view.StatusText);
            Assert.IsNotNull(view.Key("DISCONNECT")); Assert.IsTrue(view.Key("DISCONNECT").Lit);
            Assert.IsTrue(view.InputTile.Locked); Assert.IsTrue(view.OutputTile.Locked);
            Assert.IsFalse(view.Key("FETCH SONG").Off); Assert.IsFalse(view.Key("SHOW SONG").Off); Assert.IsFalse(view.Key("SHOW ON DEVICE").Off);
            Assert.AreEqual("11", view.HeaderNumber);
            Assert.AreEqual("LEVEL 3", view.TextField.value);

            // the knobs of the device's LINK page
            Assert.AreEqual("FROM THE DEVICE", view.Bars.TitleText);
            int[] knobs = { 69, 64, 101, 12, 127, 0 };
            for (int i = 0; i < knobs.Length; i++) Assert.AreEqual(knobs[i].ToString(), view.Bars.ValueText(i), "knob " + i);

            // the patches, the newest first: pad, engine, time, SAVE or SHOW
            Assert.AreEqual(6, view.PatchCount); Assert.IsFalse(view.PatchHintShown);
            (int pad, string preset, string time)[] order = { (10, "Laser", "14:05:27"), (9, "Chime", "14:03:52"), (7, "WT Sweep", "14:03:31"), (4, "TuningFork", "14:03:05"), (12, "Wind", "14:02:40"), (0, "Woofer", "14:02:11") };
            for (int i = 0; i < order.Length; i++)
            {
                var tile = view.PatchTileAt(i);
                var engine = Patch(order[i].preset).engine;
                Assert.AreEqual((order[i].pad + 1).ToString(), tile.NumberText);
                Assert.AreEqual(SfxUnits.EngineNames[(int)engine], tile.EngineText);
                Assert.AreEqual(order[i].time, tile.TimeText);
                Assert.AreEqual(i == 0 ? "SHOW" : "SAVE", tile.SaveKey.text);
                Assert.IsTrue(tile.HasPicture, "the sound's shape, from the engine");
                Assert.IsFalse(tile.PlayKey.Off);
            }

            // the messages, the newest first
            Assert.AreEqual("B0 18 7F", view.MessageText(0));
            Assert.AreEqual("B0 14 45", view.MessageText(5));
        }

        [Test]
        public void TheModelKeepsTheLastThirtyTwoPatchesAndSixMessages()
        {
            var m = new SfxForgeModel();
            for (int i = 0; i < 40; i++) m.TakePatch(i % 16, SfxPatch.Create((SfxEngine)(i % 5)), "t" + i);
            Assert.AreEqual(SfxForgeModel.MaxPatches, m.received.Count);
            Assert.AreEqual("t39", m.received[0].time); Assert.AreEqual("t8", m.received[31].time);
            Assert.AreEqual(39 % 16, m.lastPad);
            Assert.AreEqual("t35", m.PatchOfPad(3).time, "the newest patch of a pad");
            for (int i = 0; i < 10; i++) m.TakeMessage("m" + i);
            Assert.AreEqual(SfxForgeModel.MaxMessages, m.messages.Count);
            Assert.AreEqual("m9", m.messages[0]); Assert.AreEqual("m4", m.messages[5]);
            m.TakeKnob(2, 1f); m.TakeKnob(3, 0.5f); m.TakeKnob(9, 1f);
            Assert.AreEqual(127, m.knobs[2]); Assert.AreEqual(64, m.knobs[3]); Assert.AreEqual(-1, m.knobs[0]);
            m.TakePad(5, true); Assert.IsTrue(m.padDown[5]); Assert.AreEqual(5, m.lastPad);
            m.TakePad(5, false); Assert.IsFalse(m.padDown[5]);
            m.TakePad(99, true);

            var view = View(m);
            Assert.AreEqual(32, view.PatchCount);
            var first = view.PatchTileAt(0);
            m.TakePatch(1, SfxPatch.Create(SfxEngine.Foley), "new");
            view.Refresh();
            Assert.AreEqual(32, view.PatchCount);
            Assert.AreEqual("new", view.PatchTileAt(0).TimeText);
            Assert.AreSame(first, view.PatchTileAt(1), "a patch keeps its tile (and its picture) while it is in the list");
        }

        [Test]
        public void WhenTheModelChangesRefreshShowsIt()
        {
            var m = Linked();
            var view = View(m);

            // a saved patch reads SHOW, the link closing turns the keys
            m.received[1].saved = Shots.Preset("Chime");
            m.isOpen = false; m.hasOutput = false; m.inputName = m.outputName = null;
            view.Refresh();
            Assert.AreEqual("SHOW", view.PatchTileAt(1).SaveKey.text);
            StringAssert.StartsWith("NOT CONNECTED", view.StatusText);
            Assert.IsNotNull(view.Key("CONNECT")); Assert.IsTrue(view.Key("FETCH SONG").Off);
            Assert.IsFalse(view.InputTile.Locked);
            Assert.AreEqual(6, view.PatchCount, "what came stays");
        }

        [Test]
        public void WithoutTheNativeLibraryThePatchesHaveNoPictureAndNoPlay()
        {
            SfxUI.PretendNativeMissing = true;
            try
            {
                var view = View(Linked());
                Assert.AreEqual(6, view.PatchCount);
                Assert.IsFalse(view.PatchTileAt(0).HasPicture);
                Assert.IsTrue(view.PatchTileAt(0).PlayKey.Off);
                Assert.AreEqual("SAVE", view.PatchTileAt(1).SaveKey.text, "saving needs no engine");
            }
            finally { SfxUI.PretendNativeMissing = false; }
        }

        [Test]
        public void SoftKeysWrapIntoRowsOfTheSameNumber()
        {
            Assert.AreEqual(6, SoftKeyRow.PerRow(6, 632f, 84f), "the six keys, wide");
            Assert.AreEqual(3, SoftKeyRow.PerRow(6, 332f, 84f), "three and three");
            Assert.AreEqual(4, SoftKeyRow.PerRow(4, 692f, 120f));
            Assert.AreEqual(2, SoftKeyRow.PerRow(4, 412f, 120f), "two and two, not three and one");
            Assert.AreEqual(2, SoftKeyRow.PerRow(4, 332f, 120f));
            Assert.AreEqual(3, SoftKeyRow.PerRow(3, 332f, 98f));
            Assert.AreEqual(3, SoftKeyRow.PerRow(3, 288f, 88f), "the three small keys at 300 px: one row");
            Assert.AreEqual(3, SoftKeyRow.PerRow(5, 300f, 84f), "five: three and two");
            Assert.AreEqual(1, SoftKeyRow.PerRow(4, 60f, 120f));
        }

        [Test]
        public void APictureOfAPatchEndsWhereTheSoundHasFaded()
        {
            float[] mono = PatchTile.Picture(Shots.Preset("Jump").patch, out int count);
            Assert.IsNotNull(mono);
            Assert.AreEqual(count, mono.Length);
            Assert.Greater(count, 1199); Assert.Less(count, PatchTile.PictureRate * 2, "a short blip is not drawn with seconds of silence");
            Assert.LessOrEqual(count, SfxPicture.Max);
            float peak = 0f;
            foreach (float s in mono) peak = Mathf.Max(peak, Mathf.Abs(s));
            Assert.Greater(peak, 0.01f);
        }
    }
}
