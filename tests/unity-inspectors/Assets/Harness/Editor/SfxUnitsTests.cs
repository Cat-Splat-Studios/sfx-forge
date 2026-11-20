using System.Collections.Generic;
using System.IO;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEngine;

namespace Inspector.Harness
{
    /// <summary>
    /// SfxUnits against the firmware. The table: values read off the device's own pictures (out/*.png, docs/img/stage.png)
    /// with the raw values of the factory sounds they show (PRESETS in sfx-forge.c), and the ends of each range as
    /// docs/MANUAL.md "Every value" states them.
    /// </summary>
    public class SfxUnitsTests
    {
        static readonly object[] Table =
        {
            // the pictures
            new object[] { SfxParam.BaseFreq, 30, "94Hz" },            // sound.png: CRACKLE
            new object[] { SfxParam.PitchEnvDecay, 0, "5ms" },         // sound.png
            new object[] { SfxParam.PitchEnvAmount, 64, "0" },         // sound.png
            new object[] { SfxParam.AmpDecay, 60, "167ms" },           // env.png: ENGINE HUM
            new object[] { SfxParam.Release, 34, "30ms" },             // env.png
            new object[] { SfxParam.Attack, 0, "2ms" },                // env.png
            new object[] { SfxParam.Sustain, 127, "100%" },            // env.png
            new object[] { SfxParam.LfoRate, 87, "4.9Hz" },            // lfo.png: STRINGS
            new object[] { SfxParam.LfoPitch, 12, "9%" },              // lfo.png
            new object[] { SfxParam.LfoFilter, 0, "0%" },              // lfo.png
            new object[] { SfxParam.FilterCutoff, 96, "3.6kHz" },      // stage.png: FM BASS
            new object[] { SfxParam.FilterRes, 10, "8%" },             // stage.png
            new object[] { SfxParam.FilterEnv, 64, "0" },              // stage.png
            new object[] { SfxParam.FilterEnvDecay, 40, "36ms" },      // stage.png
            new object[] { SfxParam.Table, 0, "SAW-SQ" },              // wave.png
            // the manual: the ends of each range
            new object[] { SfxParam.BaseFreq, 0, "20Hz" },
            new object[] { SfxParam.BaseFreq, 127, "14.5kHz" },
            new object[] { SfxParam.FilterCutoff, 0, "40Hz" },
            new object[] { SfxParam.FilterCutoff, 127, "15.5kHz" },
            new object[] { SfxParam.PitchEnvDecay, 127, "2.6s" },
            new object[] { SfxParam.FilterEnvDecay, 0, "5ms" },
            new object[] { SfxParam.FilterEnvDecay, 127, "2.6s" },
            new object[] { SfxParam.AmpDecay, 0, "10ms" },
            new object[] { SfxParam.AmpDecay, 127, "3.9s" },
            new object[] { SfxParam.Release, 0, "5ms" },
            new object[] { SfxParam.Release, 127, "3.9s" },
            new object[] { SfxParam.Attack, 127, "4.1s" },             // (the manual rounds it to 4 s: 0.0015 * 2^11.4 = 4.05)
            new object[] { SfxParam.LfoRate, 0, "0.1Hz" },
            new object[] { SfxParam.LfoRate, 127, "29Hz" },
            new object[] { SfxParam.Sustain, 0, "OFF" },
            new object[] { SfxParam.Bitcrush, 0, "OFF" },
            new object[] { SfxParam.Bitcrush, 127, "100%" },
            new object[] { SfxParam.NoiseMix, 0, "0%" },
            new object[] { SfxParam.Reverb, 127, "100%" },
            new object[] { SfxParam.Level, 100, "100" },
            // the code, read: signs, plain numbers, names
            new object[] { SfxParam.PitchEnvAmount, 96, "+32" },
            new object[] { SfxParam.PitchEnvAmount, 44, "-20" },
            new object[] { SfxParam.PitchEnvAmount, 0, "-64" },
            new object[] { SfxParam.FilterEnv, 127, "+63" },
            new object[] { SfxParam.FmRatio, 42, "42" },
            new object[] { SfxParam.FmDepth, 127, "127" },
            new object[] { SfxParam.Spread, 35, "28%" },
            new object[] { SfxParam.Table, 5, "GLASS" },
            new object[] { SfxParam.Table, 3, "VOWEL" },
            // the two values where the SDK's own 2^x (vd_exp2, a polynomial) and the exact power differ in the text
            new object[] { SfxParam.BaseFreq, 26, "76Hz" },
            new object[] { SfxParam.Attack, 100, "755ms" },
        };

        // Every value of the parameters that go through vd_exp2, against param_text computed in IEEE float32 step by step
        // as the C code does (units_table.py in the test project writes Assets/Data/units-float32.txt).
        [Test]
        public void EveryCurvedValueIsTheFirmwaresFloatArithmetic()
        {
            string[] lines = File.ReadAllLines(Path.Combine(Application.dataPath, "Data", "units-float32.txt"));
            int rows = 0, differ = 0;
            string first = "";
            foreach (string line in lines)
            {
                string[] w = line.Split(' ');
                if (w.Length != 3) continue;
                var k = (SfxParam)System.Enum.Parse(typeof(SfxParam), w[0]);
                string here = SfxUnits.ParamText(k, int.Parse(w[1]));
                rows++;
                if (here == w[2]) continue;
                differ++;
                if (first.Length == 0) first = line + " but here " + here;
            }
            Assert.AreEqual(8 * 128, rows);
            Assert.AreEqual(0, differ, first);
            Debug.Log($"UNITS {rows} texts of 8 curved parameters equal the firmware's float32 arithmetic");
        }

        [TestCaseSource(nameof(Table))]
        public void ValueAsTheDisplayWritesIt(SfxParam k, int v, string text) => Assert.AreEqual(text, SfxUnits.ParamText(k, v));

        [Test]
        public void EveryValueOfEveryParameterHasAText()
        {
            for (int k = 0; k < SfxPatch.ParamCount; k++)
                for (int v = 0; v <= 127; v++) Assert.IsNotEmpty(SfxUnits.ParamText((SfxParam)k, v));
        }

        [Test]
        public void NamesFollowTheEngine()
        {
            Assert.AreEqual("FM RATIO", SfxUnits.ParamName(SfxParam.FmRatio, SfxEngine.SciFiFm));
            Assert.AreEqual("FM RATIO", SfxUnits.ParamName(SfxParam.FmRatio, SfxEngine.Impact));
            Assert.AreEqual("ARP STEP", SfxUnits.ParamName(SfxParam.FmRatio, SfxEngine.UiBlip));
            Assert.AreEqual("ARP SPEED", SfxUnits.ParamName(SfxParam.FmDepth, SfxEngine.UiBlip));
            Assert.AreEqual("GRAIN RATE", SfxUnits.ParamName(SfxParam.FmRatio, SfxEngine.Foley));
            Assert.AreEqual("GRAIN AMT", SfxUnits.ParamName(SfxParam.FmDepth, SfxEngine.Foley));
            Assert.AreEqual("BAND", SfxUnits.ParamName(SfxParam.FilterCutoff, SfxEngine.Foley));
            Assert.AreEqual("CUTOFF", SfxUnits.ParamName(SfxParam.FilterCutoff, SfxEngine.Wavetable));
            Assert.AreEqual("POSITION", SfxUnits.ParamName(SfxParam.FmRatio, SfxEngine.Wavetable));
            Assert.AreEqual("WARP", SfxUnits.ParamName(SfxParam.FmDepth, SfxEngine.Wavetable));
            Assert.AreEqual("F.ENV TIME", SfxUnits.ParamName(SfxParam.FilterEnvDecay, SfxEngine.Impact));
            Assert.AreEqual("> TIMBRE", SfxUnits.ParamName(SfxParam.LfoTimbre, SfxEngine.Impact));
        }

        [Test]
        public void ThePagesHoldAll25ParametersOnceAndTheEngine()
        {
            var seen = new HashSet<int>();
            int engines = 0;
            Assert.AreEqual(SfxUnits.PageCount, SfxUnits.PageParams.Length);
            Assert.AreEqual(SfxUnits.PageCount, SfxUnits.PageNames.Length);
            Assert.AreEqual(SfxUnits.PageCount, SfxUnits.PageTitles.Length);
            foreach (var page in SfxUnits.PageParams)
            {
                Assert.AreEqual(4, page.Length);
                foreach (int k in page)
                {
                    if (k == SfxUnits.EngineKnob) engines++;
                    else if (k != SfxUnits.NoKnob) Assert.IsTrue(seen.Add(k), "twice: " + (SfxParam)k);
                }
            }
            Assert.AreEqual(1, engines);
            Assert.AreEqual(SfxPatch.ParamCount, seen.Count);
        }

        [Test]
        public void OtherTexts()
        {
            Assert.AreEqual("C", SfxUnits.PanText(64));
            Assert.AreEqual("L12", SfxUnits.PanText(52));
            Assert.AreEqual("R30", SfxUnits.PanText(94));
            Assert.AreEqual("--", SfxUnits.NoteText(0));
            Assert.AreEqual("A#3", SfxUnits.NoteText(58));
            Assert.AreEqual("C4", SfxUnits.NoteText(60));
            Assert.AreEqual("1/4", SfxUnits.DelayTimeText(3));
            Assert.AreEqual("28%", SfxUnits.Percent(36));
            Assert.AreEqual("100%", SfxUnits.Percent(127));
            Assert.AreEqual("120ms", SfxUnits.TimeText(0.12f));
            Assert.AreEqual("1.4s", SfxUnits.TimeText(1.4f));
            Assert.AreEqual("1.50s", SfxUnits.HoldText(1.5f));
            Assert.IsTrue(SfxUnits.TryParseSeconds("2sec", out float s)); Assert.AreEqual(2f, s);
            Assert.IsTrue(SfxUnits.TryParseSeconds("250 ms", out s)); Assert.AreEqual(0.25f, s);
            Assert.IsFalse(SfxUnits.TryParseSeconds("1k", out s));
            Assert.IsTrue(SfxUnits.Magnitude("3.6 kHz", out double m, out string unit)); Assert.AreEqual(3600.0, m); Assert.AreEqual("khz", unit);
        }

        [Test]
        public void TypedValues()
        {
            int v;
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.FilterCutoff, "3.6kHz", out v)); Assert.AreEqual("3.6kHz", SfxUnits.ParamText(SfxParam.FilterCutoff, v));
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.FilterCutoff, "3.6 khz", out v)); Assert.AreEqual("3.6kHz", SfxUnits.ParamText(SfxParam.FilterCutoff, v));
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.FilterCutoff, "200", out v)); Assert.AreEqual("197Hz", SfxUnits.ParamText(SfxParam.FilterCutoff, v));   // the nearest there is
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.AmpDecay, "1.2s", out v)); Assert.AreEqual("1.2s", SfxUnits.ParamText(SfxParam.AmpDecay, v));
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.AmpDecay, "167", out v)); Assert.AreEqual(60, v);
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.Sustain, "off", out v)); Assert.AreEqual(0, v);
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.Sustain, "100", out v)); Assert.AreEqual(127, v);
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.PitchEnvAmount, "+32", out v)); Assert.AreEqual(96, v);
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.PitchEnvAmount, "-20", out v)); Assert.AreEqual(44, v);
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.PitchEnvAmount, "0", out v)); Assert.AreEqual(64, v);
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.Reverb, "50%", out v)); Assert.AreEqual("50%", SfxUnits.ParamText(SfxParam.Reverb, v));
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.Level, "100", out v)); Assert.AreEqual(100, v);
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.Level, "999", out v)); Assert.AreEqual(127, v);
            Assert.IsTrue(SfxUnits.TryParse(SfxParam.Table, "glass", out v)); Assert.AreEqual(5, v);
            Assert.IsFalse(SfxUnits.TryParse(SfxParam.Level, "abc", out v));
            Assert.IsFalse(SfxUnits.TryParse(SfxParam.Level, "", out v));
        }
    }
}
