// The sound-design tools that need no engine: the generators, MUTATE with an amount and locks, the
// order of a variation set, the arithmetic of levels, a patch as text and as a .syx file.
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
    // the tools: generators
    static void Generators()
    {
        bool fits = true;
        bool again = true;
        bool both = true;
        int fewest = int.MaxValue;
        for (int c = 0; c < SfxGenerator.Count; c++)
        {
            var cat = (SfxCategory)c;
            var engines = new HashSet<SfxEngine>();
            int differ = 0;
            SfxPatch before = default;
            for (uint seed = 1; seed <= 300; seed++)
            {
                SfxPatch p = SfxGenerator.Make(cat, seed);
                fits &=
                    SfxGenerator.Fits(cat, p)
                    && SfxPatch.TryUnpack(p.Pack(), 0, out var back)
                    && back.Equals(p)
                    && p.level == 100
                    && p.IsSustained == SfxGenerator.IsSustained(cat);
                again &= p.Equals(SfxGenerator.Make(cat, seed));
                if (seed > 1 && !p.Equals(before))
                {
                    differ++;
                }

                before = p;
                engines.Add(p.engine);
            }

            fewest = Math.Min(fewest, differ);
            both &= engines.Count == 2;
        }

        Ok(
            fits
                && SfxGenerator.Names.Length == SfxGenerator.Count
                && Enum.GetValues(typeof(SfxCategory)).Length == SfxGenerator.Count,
            "every category: 300 seeds give a valid patch inside its ranges"
        );
        Ok(
            again && fewest >= 290,
            $"a seed gives the same sound again, another seed another (at least {fewest} of 299 neighbours differ)"
        );
        Ok(both, "both recipes of every category come up");
        var one = new SfxRandom(0x3F2A91C4u);
        var two = new SfxRandom(0x3F2A91C4u);
        Ok(
            SfxGenerator.Make(SfxCategory.Zap, ref one).Equals(SfxGenerator.Make(SfxCategory.Zap, 0x3F2A91C4u))
                && !SfxGenerator.Fits(SfxCategory.Hum, SfxGenerator.Make(SfxCategory.Chime, ref two)),
            "a generator gives what its seed gives; a coin does not fit the hum's ranges"
        );
    }

    // the tools: mutate with an amount, locks, variants
    static void MutateWithAmountAndLocks()
    {
        bool inside = true;
        foreach (int step in new[] { 1, 19, 64, 127 })
        {
            var mr = new SfxRandom((uint)(1000 + step));
            for (int i = 0; i < 300; i++)
            {
                var a = laser;
                a.Mutate(ref mr, step);
                for (int k = 0; k < SfxPatch.ParamCount; k++)
                {
                    inside &= Math.Abs(a[(SfxParam)k] - laser[(SfxParam)k]) <= step;
                }

                inside &=
                    a.engine == laser.engine && a.sustain == 0 && a.level == laser.level && a.table == laser.table;
                var h = hum;
                h.Mutate(ref mr, step);
                inside &= h.sustain > 0 && h.level == hum.level;
            }
        }

        var zero = new SfxRandom(5);
        var still = laser;
        still.Mutate(ref zero, 0);
        Ok(inside && still.Equals(laser), "Mutate with an amount stays inside it and keeps the kind of sound");

        bool locks = true;
        for (uint seed = 1; seed <= 200; seed++)
        {
            var free = new SfxRandom(seed);
            var held = new SfxRandom(seed);
            var a = laser;
            a.Mutate(ref free, 40);
            var b = laser;
            b.Mutate(ref held, 40, SfxMutate.Bit(SfxParam.BaseFreq) | SfxMutate.Bit(SfxParam.AmpDecay));
            locks &= b.baseFreq == laser.baseFreq && b.ampDecay == laser.ampDecay;
            for (int k = 0; k < SfxPatch.ParamCount; k++)
            {
                if (k != (int)SfxParam.BaseFreq && k != (int)SfxParam.AmpDecay)
                {
                    locks &= a[(SfxParam)k] == b[(SfxParam)k];
                }
            }

            locks &= free.Next() == held.Next();
        }

        Ok(locks, "a locked value does not move, and locking it changes no other value");
        Ok(
            SfxMutate.Variant(laser, 77, 3, 100, SfxMutate.All).Equals(laser)
                && SfxMutate.All == 0x1FFFFFF
                && !SfxMutate.Variant(laser, 77, 3, 100).Equals(laser),
            "all values locked: the variant is the base"
        );
        SfxPatch third = SfxMutate.Variant(laser, 42, 3, 15);
        for (int n = 8; n >= 1; n--)
        {
            SfxMutate.Variant(laser, 42, n, 15);
        }

        Ok(
            SfxMutate.Variant(laser, 42, 0, 15).Equals(laser)
                && SfxMutate.Variant(laser, 42, 3, 15).Equals(third)
                && !SfxMutate.Variant(laser, 43, 3, 15).Equals(third)
                && !SfxMutate.Variant(laser, 42, 4, 15).Equals(third),
            "variant 0 is the base; variant n depends on seed and n only"
        );
        Ok(
            SfxPatch.StepOf(0) == 0
                && SfxPatch.StepOf(1) == 1
                && SfxPatch.StepOf(15) == 19
                && SfxPatch.StepOf(100) == 127
                && SfxPatch.StepOf(-3) == 0
                && SfxPatch.StepOf(500) == 127,
            "StepOf: 0 -> 0, 1 -> 1, 15 -> 19, 100 -> 127"
        );
        var dev = new SfxRandom(1);
        Ok(
            dev.Next() == 270369u
                && new SfxRandom(0).Next() == new SfxRandom(0x9E3779B9u).Next()
                && SfxRandom.Mix(0, 0) != 0
                && SfxRandom.Mix(1, 2) != SfxRandom.Mix(2, 1),
            "SfxRandom is the device's xorshift32 (seed 1 gives 270369), 0 is taken as its constant"
        );
    }

    // the tools: a variation set's order and stamp
    static void VariationOrderAndStamp()
    {
        bool never = true;
        bool all = true;
        bool same = true;
        bool single = true;
        foreach (int count in new[] { 1, 2, 5, 16 })
        {
            foreach (int noRepeat in new[] { 0, 1, 3, 20 })
            {
                var order = new SfxShuffle(count, noRepeat, 9);
                var twin = new SfxShuffle(count, noRepeat, 9);
                int avoid = Math.Min(noRepeat, count - 1);
                var history = new List<int>();
                var seen = new HashSet<int>();
                never &= order.Last == -1;
                for (int i = 0; i < 2000; i++)
                {
                    int at = order.Next();
                    same &= at == twin.Next() && order.Last == at;
                    never &= at >= 0 && at < count;
                    for (int k = 1; k <= avoid && k <= history.Count; k++)
                    {
                        never &= history[history.Count - k] != at;
                    }

                    history.Add(at);
                    seen.Add(at);
                    if (count == 1)
                    {
                        single &= at == 0;
                    }
                }

                all &= seen.Count == count;
            }
        }

        Ok(
            never && all && same && single,
            "shuffle: never one of the last N, every variant comes up, the same seed the same order"
        );
        var even = new SfxShuffle(6, 2, 1234);
        var hits = new int[6];
        for (int i = 0; i < 12000; i++)
        {
            hits[even.Next()]++;
        }

        float unit = even.NextUnit();
        Ok(
            hits.All(h => h >= 1700 && h <= 2300)
                && unit >= 0f
                && unit < 1f
                && even.NoRepeat == 2
                && new SfxShuffle(3, 9, 1).NoRepeat == 2
                && new SfxShuffle(3, -1, 1).NoRepeat == 0,
            "shuffle: uniform over what is allowed (" + string.Join(" ", hits) + ")"
        );

        byte[] packed = laser.Pack();
        byte[] other = hum.Pack();
        int stamp = SfxMutate.Stamp(packed, 150, 1, 6, 10, 0, false);
        bool changes =
            stamp != 0
            && stamp == SfxMutate.Stamp(laser.Pack(), 150, 1, 6, 10, 0, false)
            && stamp != SfxMutate.Stamp(other, 150, 1, 6, 10, 0, false)
            && stamp != SfxMutate.Stamp(packed, 151, 1, 6, 10, 0, false)
            && stamp != SfxMutate.Stamp(packed, 150, 2, 6, 10, 0, false)
            && stamp != SfxMutate.Stamp(packed, 150, 1, 7, 10, 0, false)
            && stamp != SfxMutate.Stamp(packed, 150, 1, 6, 11, 0, false)
            && stamp != SfxMutate.Stamp(packed, 150, 1, 6, 10, 1, false)
            && stamp != SfxMutate.Stamp(packed, 150, 1, 6, 10, 0, true);
        Ok(changes, "the stamp changes with every input and with nothing else");
        bool kept = true;
        var six = Enumerable.Range(0, 6).Select(n => SfxMutate.Variant(thud, 5, n, 10)).ToArray();
        var nine = Enumerable.Range(0, 9).Select(n => SfxMutate.Variant(thud, 5, n, 10)).ToArray();
        for (int n = 0; n < 6; n++)
        {
            kept &= six[n].Equals(nine[n]);
        }

        Ok(kept && nine.Distinct().Count() > 6, "raising the count keeps the earlier variants");
    }

    // the tools: levels
    static void Levels()
    {
        var sine = new float[48000];
        for (int i = 0; i < sine.Length; i++)
        {
            sine[i] = (float)Math.Sin(2 * Math.PI * 440 * i / 48000.0);
        }

        var full = SfxLevels.Measure(sine, null);
        var quiet = SfxLevels.Measure(new float[1000], new float[1000]);
        var over = (float[])sine.Clone();
        for (int i = 0; i < over.Length; i++)
        {
            over[i] *= 1.01f;
        }

        Ok(
            Math.Abs(full.PeakDb) < 0.01f
                && Math.Abs(full.RmsDb + 3.01f) < 0.01f
                && !full.Clips
                && !full.Silent
                && quiet.PeakDb == SfxLevels.Floor
                && quiet.RmsDb == SfxLevels.Floor
                && quiet.Silent
                && SfxLevels.Measure(over, over).Clips
                && Math.Abs(SfxLevels.Measure(sine, sine).RmsDb - full.RmsDb) < 0.001f
                && SfxLevels.FromDb(SfxLevels.ToDb(0.25f)) is float q
                && Math.Abs(q - 0.25f) < 1e-6f,
            $"levels: a full-scale sine reads peak 0 dB and RMS -3.01 dB ({full.PeakDb:0.00} / {full.RmsDb:0.00}); silence reads -120 and is Silent; 1.01 clips"
        );
        var padded = new float[sine.Length + 30000];
        Array.Copy(sine, 0, padded, 10000, sine.Length);
        var half = (float[])padded.Clone();
        for (int i = 0; i < half.Length; i++)
        {
            half[i] *= 0.5f;
        }

        Ok(
            Math.Abs(SfxLevels.Measure(padded, null).RmsDb - full.RmsDb) < 0.001f
                && Math.Abs(SfxLevels.Measure(half, null).RmsDb - (full.RmsDb - 6.0206f)) < 0.001f
                && SfxLevels.Measure(padded, null, 5000).Silent,
            "the body ignores the silence around a sound"
        );
        bool inverse =
            SfxLevels.LevelFor(100, SfxLevels.LevelDb(80)) == 80
            && SfxLevels.LevelFor(100, 40f) == 127
            && SfxLevels.LevelFor(100, -200f) == 1
            && SfxLevels.LevelFor(0, 12f) == 0
            && SfxLevels.LevelDb(100) == 0f
            && Math.Abs(SfxLevels.LevelDb(127) - 4.152f) < 0.01f
            && SfxLevels.LevelDb(0) == SfxLevels.Floor;
        for (int a = 1; a <= 127; a++)
        {
            for (int b = 1; b <= 127; b++)
            {
                inverse &= SfxLevels.LevelFor(a, SfxLevels.LevelDb(b) - SfxLevels.LevelDb(a)) == b;
            }
        }

        Ok(inverse, "LevelDb and LevelFor are each other's inverse");

        SfxLevelReading Reading(float peakDb, float rmsDb) =>
            new SfxLevelReading
            {
                peak = SfxLevels.FromDb(peakDb),
                rms = SfxLevels.FromDb(rmsDb),
                frames = 1000,
            };
        bool match = true;
        foreach (SfxLevelMeasure by in new[] { SfxLevelMeasure.Rms, SfxLevelMeasure.Peak })
        {
            int[] levels = { 100, 90, 127, 60, 100, 0 };
            var now = new int[6];
            SfxLevelReading[] reads =
            {
                Reading(-3f, -14f),
                Reading(-9f, -21f),
                Reading(-1f, -9f),
                Reading(-20f, -30f),
                new SfxLevelReading(),
                Reading(-6f, -12f),
            };
            float target = SfxLevels.Match(levels, reads, by, now);
            match &= now[4] == 100 && now[5] == 0 && !float.IsNaN(target);
            for (int i = 0; i < 4; i++)
            {
                float change = SfxLevels.LevelDb(now[i]) - SfxLevels.LevelDb(levels[i]);
                float reading = (by == SfxLevelMeasure.Rms ? reads[i].RmsDb : reads[i].PeakDb) + change;
                match &=
                    now[i] >= 1
                    && now[i] <= 127
                    && reads[i].PeakDb + change <= 0.0001f
                    && Math.Abs(reading - target) < 0.5f;
            }

            float wanted = SfxLevels.Match(levels, reads, by, now, -40f);
            match &=
                Math.Abs(wanted + 40f) < 0.001f
                && now[0] < 100
                && SfxLevels.Match(levels, reads, by, now, 30f) <= target + 0.001f;
        }

        Ok(
            match
                && float.IsNaN(
                    SfxLevels.Match(new[] { 0 }, new[] { Reading(-6f, -12f) }, SfxLevelMeasure.Rms, new int[1])
                ),
            "match: the target is reachable by all, nobody passes LEVEL 127 or 0 dBFS"
        );

        // a wanted level below what LEVEL 1 reaches: what comes back is what
        // the sounds get; a reading that is no number changes nothing
        int[] lowLevels = { 100, 3 };
        var lowNow = new int[2];
        SfxLevelReading[] lowReads = { Reading(-3f, -14f), Reading(-30f, -33f) };
        float got70 = SfxLevels.Match(lowLevels, lowReads, SfxLevelMeasure.Rms, lowNow, -70f);
        float readA = lowReads[0].RmsDb + SfxLevels.LevelDb(lowNow[0]) - SfxLevels.LevelDb(100);
        float readB = lowReads[1].RmsDb + SfxLevels.LevelDb(lowNow[1]) - SfxLevels.LevelDb(3);
        int[] nanNow = new int[3];
        float nanTarget = SfxLevels.Match(
            new[] { 100, 90, 80 },
            new[]
            {
                Reading(-3f, -14f),
                new SfxLevelReading { peak = 0.5f, rms = float.NaN },
                Reading(-9f, -20f),
            },
            SfxLevelMeasure.Rms,
            nanNow
        );
        Ok(
            got70 > -70f
                && lowNow[1] == 1
                && Math.Abs(readB - got70) < 0.01f
                && Math.Abs(readA - got70) < 1f
                && SfxLevels.LevelFor(100, float.NaN) == 100
                && SfxLevels.LevelFor(0, float.NaN) == 0
                && nanNow[1] == 90
                && !float.IsNaN(nanTarget)
                && nanNow[0] >= 1
                && nanNow[2] >= 1,
            $"match: a wanted level below what LEVEL 1 leaves comes back as what is reached ({got70:0.0} dB, not -70; LEVEL {lowNow[0]} and {lowNow[1]}); a reading that is no number changes nothing"
        );
    }

    // the tools: a patch as text, and as a .syx file
    static void PatchAsTextAndSyx()
    {
        bool survives = true;
        var tr = new Random(11);
        for (int i = 0; i < 2000; i++)
        {
            var p = new SfxPatch { engine = (SfxEngine)tr.Next(SfxPatch.EngineCount) };
            for (int k = 0; k < SfxPatch.ParamCount; k++)
            {
                p[(SfxParam)k] = (byte)tr.Next(128);
            }

            string t = SfxPatchText.ToText(p);
            survives &=
                t.Length == 62
                && t.Length == SfxPatchText.Length
                && SfxPatchText.Parse(t, out var back) == SfxTextResult.Ok
                && back.Equals(p)
                && SfxPatchText.TryParse(t, out back);
        }

        Ok(survives, "2000 random patches survive patch -> text -> patch; the text is 62 characters");
        string laserText = SfxPatchText.ToText(SfxPatch.Default);
        Ok(
            laserText == SfxPatchText.Prefix + string.Concat(SfxPatch.Default.Pack().Select(b => b.ToString("X2")))
                && laserText == "SFXF1:0202466E2D32460078143A0000002240283200000000000000640043",
            "the default laser's text is the one written in the manual: " + laserText
        );
        string messy = " `sfxf1:0202466e2d32 4600 78143A0000\r\n\t002240283200\u200B0000\u200D00000000\uFEFF640043` ";
        Ok(
            SfxPatchText.Parse(messy, out var read) == SfxTextResult.Ok
                && read.Equals(SfxPatch.Default)
                && SfxPatchText.TryParse("<\"" + laserText + "\">", out read)
                && SfxPatchText.TryParse("'" + laserText + "'", out read)
                && read.Equals(SfxPatch.Default),
            "text is read through spaces, line breaks, lower case, quotes and zero-width characters"
        );

        // as it comes out of a chat: a label in front that itself says "SFX Forge", brackets, typographic
        // quotes, a full stop behind it, a sentence that goes on, a mark of writing direction, a soft hyphen
        // and a word joiner inside
        char lq = (char)0x201C;
        char rq = (char)0x201D;
        char rtl = (char)0x200F;
        char shy = (char)0x00AD;
        char wj = (char)0x2060;
        string[] chatty =
        {
            "SFX Forge patch: " + laserText,
            "my sfxf sound (" + laserText + ").",
            lq + laserText + rq + ",",
            "**" + laserText + "**",
            rtl + laserText.Substring(0, 20) + shy + laserText.Substring(20, 20) + wj + laserText.Substring(40),
            "try " + laserText + " - it is the laser!",
            laserText + "\nsecond line",
            "[" + laserText + "]:",
        };
        bool chat = true;
        string notRead = "";
        foreach (string c in chatty)
        {
            if (SfxPatchText.Parse(c, out var got) != SfxTextResult.Ok || !got.Equals(SfxPatch.Default))
            {
                chat = false;
                notRead += " [" + c + "]";
            }
        }

        Ok(
            SfxPatchText.Parse(laserText + " for the boss, and 2 more", out read) == SfxTextResult.Ok
                && read.Equals(SfxPatch.Default)
                && SfxPatchText.Parse(laserText + "\ncrush it", out read) == SfxTextResult.Ok,
            "a word after the text may begin with a to f or a digit, when a space or a line break stands between"
        );
        Ok(
            chat
                && SfxPatchText.Parse("a newer one: SFXF2:" + laserText.Substring(6), out read)
                    == SfxTextResult.NewerText
                && SfxPatchText.Parse(laserText + "abc", out read) == SfxTextResult.BadLength
                && SfxPatchText.Parse("SFXF1:0202.", out read) == SfxTextResult.BadLength,
            "text is read out of a sentence: a label in front, brackets, typographic quotes, a full stop, "
                + "invisible characters"
                + notRead
        );
        string digits = laserText.Substring(6);
        string changed = "SFXF1:" + digits.Substring(0, 10) + (digits[10] == '0' ? '1' : '0') + digits.Substring(11);
        byte[] newerBytes = SfxPatch.Default.Pack();
        newerBytes[0] = 3;
        byte[] highBytes = SfxPatch.Default.Pack();
        highBytes[5] = 0x80;
        string HexOf(byte[] b) => "SFXF1:" + string.Concat(b.Select(x => x.ToString("X2")));
        var answers = new (string text, SfxTextResult want)[]
        {
            ("", SfxTextResult.Empty),
            (null, SfxTextResult.Empty),
            (" \r\n ` ", SfxTextResult.Empty),
            (digits, SfxTextResult.NotOurs),
            ("hello there", SfxTextResult.NotOurs),
            ("SFXF:" + digits, SfxTextResult.NotOurs),
            ("SFXF2:" + digits, SfxTextResult.NewerText),
            ("SFXF12:" + digits, SfxTextResult.NewerText),
            ("SFXF1:" + digits.Substring(1), SfxTextResult.BadLength),
            ("SFXF1:" + digits + "0", SfxTextResult.BadLength),
            ("SFXF1:" + digits.Substring(0, 20) + "G" + digits.Substring(21), SfxTextResult.BadCharacter),
            (changed, SfxTextResult.BadPatch),
            (HexOf(newerBytes), SfxTextResult.NewerPatch),
            (HexOf(highBytes), SfxTextResult.BadPatch),
        };
        bool each = true;
        string wrong = "";
        foreach (var (t, want) in answers)
        {
            SfxTextResult got = SfxPatchText.Parse(t, out var none);
            if (got != want || !none.Equals(default(SfxPatch)) || SfxPatchText.Message(got) == null)
            {
                each = false;
                wrong += $" [{t}: {got}, not {want}]";
            }
        }

        Ok(
            each
                && SfxPatchText.Message(SfxTextResult.Ok) == null
                && Enum.GetValues(typeof(SfxTextResult))
                    .Cast<SfxTextResult>()
                    .Where(r => r != SfxTextResult.Ok)
                    .Select(SfxPatchText.Message)
                    .Distinct()
                    .Count() == 9,
            "each damage has its own answer" + wrong
        );

        byte[] syx = SfxPatchText.ToSyx(thud, 4);
        bool files =
            syx.Length == 35
            && SfxPatchText.ParseSyx(syx, out var fromFile, out int pad) == SfxTextResult.Ok
            && pad == 4
            && fromFile.Equals(thud);
        files &=
            SfxPatchText.ParseSyx(SfxProtocol.BuildDump(9, hum), out fromFile, out pad) == SfxTextResult.Ok
            && pad == 9
            && fromFile.Equals(hum);
        files &=
            SfxPatchText.ParseSyx(SfxProtocol.BuildLoadSongSound(3, saw), out fromFile, out pad) == SfxTextResult.Ok
            && pad == 3
            && fromFile.Equals(saw);
        files &=
            SfxPatchText.ParseSyx(
                SfxProtocol.BuildLoad(2, laser).Concat(SfxProtocol.BuildLoad(7, hum)).ToArray(),
                out fromFile,
                out pad
            ) == SfxTextResult.Ok
            && pad == 2
            && fromFile.Equals(laser);
        byte[] foreign = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
        files &=
            SfxPatchText.ParseSyx(
                foreign.Concat(SfxProtocol.BuildText("HI")).Concat(SfxProtocol.BuildLoad(11, hum)).ToArray(),
                out fromFile,
                out pad
            ) == SfxTextResult.Ok
            && pad == 11
            && fromFile.Equals(hum);
        files &=
            SfxPatchText.ParseSyx(foreign, out fromFile, out pad) == SfxTextResult.NoPatchInFile
            && SfxPatchText.ParseSyx(new byte[0], out fromFile, out pad) == SfxTextResult.NoPatchInFile
            && SfxPatchText.ParseSyx(null, out fromFile, out pad) == SfxTextResult.NoPatchInFile
            && SfxPatchText.ParseSyx(new byte[70000], out fromFile, out pad) == SfxTextResult.TooLarge;
        byte[] damaged = SfxPatchText.ToSyx(thud, 4);
        damaged[10] ^= 1;
        byte[] later = SfxPatchText.ToSyx(thud, 4);
        later[6] = 3;
        files &=
            SfxPatchText.ParseSyx(later, out fromFile, out pad) == SfxTextResult.NewerPatch
            && SfxPatchText.ParseSyx(syx.Take(20).ToArray(), out fromFile, out pad) == SfxTextResult.NoPatchInFile;

        // a patch of ours whose checksum does not fit is damaged, not "no patch" - unless
        // a good one follows; realtime bytes inside a message are passed over
        byte[] clocked = syx.Take(12)
            .Concat(new byte[] { 0xF8 })
            .Concat(syx.Skip(12).Take(10))
            .Concat(new byte[] { 0xFE, 0xF8 })
            .Concat(syx.Skip(22))
            .ToArray();
        bool hurt =
            SfxPatchText.ParseSyx(damaged, out fromFile, out pad) == SfxTextResult.BadPatch
            && fromFile.Equals(default(SfxPatch))
            && pad == 0
            && SfxPatchText.ParseSyx(damaged.Concat(SfxProtocol.BuildLoad(5, hum)).ToArray(), out fromFile, out pad)
                == SfxTextResult.Ok
            && pad == 5
            && fromFile.Equals(hum)
            && SfxPatchText.ParseSyx(foreign.Concat(damaged).ToArray(), out fromFile, out pad) == SfxTextResult.BadPatch
            && SfxPatchText.ParseSyx(clocked, out fromFile, out pad) == SfxTextResult.Ok
            && pad == 4
            && fromFile.Equals(thud);
        Ok(
            hurt,
            "a .syx whose patch does not fit its checksum is damaged (BadPatch); realtime bytes inside a "
                + "message are passed over"
        );
        Ok(
            files,
            "a .syx of one patch round-trips with its pad; a dump, a load and a song-sound load are read; "
                + "two messages give the first; a foreign SysEx and an empty file give NoPatchInFile; 70000 bytes "
                + "give TooLarge"
        );
        Ok(
            SfxPatchText.ToSyx(laser, 6).SequenceEqual(SfxProtocol.BuildLoad(6, laser))
                && SfxPatchText.ToSyx(laser).SequenceEqual(SfxProtocol.BuildLoad(0, laser)),
            "ToSyx is the load message the link sends"
        );
    }
}
