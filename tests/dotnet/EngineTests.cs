// The native engine itself: bakes, a single voice with telemetry, the tools that need it (what the
// generators make, a bake with options, levels), envelopes, the wavetable and the effects.
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
    // three sounds baked; the thud against the device's own recording; a single dry voice with telemetry
    static void BakesAndASingleVoice()
    {
        foreach (var (name, p) in new[] { ("thud", thud), ("laser", laser), ("hum (held 0.5 s)", hum) })
        {
            bool ok = SfxNative.Bake(p, out var l, out var r, 48000, 0.9f, 0, 0f, 0.5f);
            bool sane =
                ok
                && l.Length > 2000
                && l.Length == r.Length
                && l.All(v => !float.IsNaN(v) && Math.Abs(v) <= 1f)
                && l.Max(v => Math.Abs(v)) > 0.05f
                && Math.Abs(l[l.Length - 1]) < 1e-3f;
            Ok(
                sane,
                $"bake '{name}': {(ok ? l.Length / 48000f : 0):0.00} s, peak {(ok ? l.Max(v => Math.Abs(v)) : 0):0.00}, ends in silence"
            );
        }

        if (deviceAudioLeft != null)
        {
            // tests/tour.json plays pad 3 (this thud) at 1.4 s with velocity
            // 0.9; the Vibe's output stage is tanh(x) at volume 0.8
            SfxNative.Bake(thud, out var mine, out _, 48000, 0.9f);
            double c = Correlate(mine, deviceAudioLeft, (int)(1.4 * 48000), 9600, out int off);
            Ok(c > 0.98, $"the DLL and the Vibe render the same sound (correlation {c:0.0000}, offset {off} samples)");
        }

        // a single dry voice, block by block, with telemetry
        IntPtr v = Marshal.AllocHGlobal(SfxNative.VoiceSize);
        for (int i = 0; i < SfxNative.VoiceSize; i++)
        {
            Marshal.WriteByte(v, i, 0);
        }

        var buf = new float[256];
        var md = SfxMods.None;
        md.pitchSemitones = 12;
        bool started = SfxNative.Trigger(v, hum.Pack(), 1f, 0f, 44100f, 99);
        bool sounding = true;
        double energy = 0;
        for (int b = 0; b < 40; b++)
        {
            Array.Clear(buf, 0, 256);
            sounding &= SfxNative.Render(v, ref md, buf, 256);
            energy += buf.Sum(x => (double)x * x);
        }

        SfxNative.Release(v);
        int blocks = 0;
        while (SfxNative.Render(v, ref md, buf, 256) && blocks < 400)
        {
            blocks++;
        }

        Ok(
            started && sounding && energy > 1 && blocks > 0 && blocks < 400 && !SfxNative.Active(v),
            $"a sustained voice plays until released, then fades ({blocks} blocks)"
        );
        Marshal.FreeHGlobal(v);
    }

    // the tools, with the engine: what the generators make, a bake with options, levels
    static void ToolsWithTheEngine()
    {
        bool audible = true;
        string worst = "";
        float low = 9f;
        float high = 0f;
        int longest = 0;
        for (int c = 0; c < SfxGenerator.Count; c++)
        {
            for (uint seed = 1; seed <= 100; seed++)
            {
                SfxPatch p = SfxGenerator.Make((SfxCategory)c, seed * 2654435761u);
                SfxBaked b = SfxBake.Render(p, 1f);
                bool good =
                    b != null
                    && b.level.peak > 0.02f
                    && b.level.peak <= 1f
                    && !b.cutAtLimit
                    && !b.samples.Any(float.IsNaN);
                if (b != null)
                {
                    low = Math.Min(low, b.level.peak);
                    high = Math.Max(high, b.level.peak);
                    longest = Math.Max(longest, b.frames);
                }

                if (!good && worst.Length < 400)
                {
                    worst +=
                        $" [{SfxGenerator.Names[c]} {seed}: {(b == null ? "null" : $"peak {b.level.peak:0.000} {b.Seconds:0.0}s")}]";
                }

                audible &= good;
            }
        }

        Ok(
            audible,
            $"every category: 100 seeds bake audible and unclipped (peaks {low:0.000} .. {high:0.000}, the longest {longest / 48000f:0.0} s)"
                + worst
        );

        SfxPatch wide = saw;
        wide.spread = 90;
        wide.chorus = 80;
        SfxNative.Bake(wide, out var wl, out var wr, 48000, 1f, 0, 0f, 0.5f);
        SfxBaked stereo = SfxBake.Render(wide, 0.5f);
        SfxBaked mono = SfxBake.Render(wide, 0.5f, new SfxBakeOptions { mono = true });
        bool mean =
            stereo.channels == 2
            && mono.channels == 1
            && stereo.frames == wl.Length
            && mono.frames == wl.Length
            && mono.samples.Length == wl.Length
            && stereo.samples.Length == 2 * wl.Length
            && stereo.sampleRate == 48000;
        for (int i = 0; mean && i < wl.Length; i++)
        {
            mean =
                stereo.samples[i * 2] == wl[i]
                && stereo.samples[i * 2 + 1] == wr[i]
                && Math.Abs(mono.samples[i] - (wl[i] + wr[i]) * 0.5f) < 1e-6f;
        }

        // The rate: the sound is rendered at 48 kHz and brought to the
        // file's rate - the same sound, as long, the same note.
        string rates = "";
        double note48 = Pitch(SfxBake.Render(saw, 0.6f, new SfxBakeOptions { mono = true }).samples, 0.2, 0.4);
        SfxBaked thudAt48 = SfxBake.Render(thud, 0.5f);
        float thud48 = thudAt48.Seconds;
        foreach (int rate in new[] { 8000, 22050, 24000, 32000, 44100 })
        {
            SfxBaked at = SfxBake.Render(thud, 0.5f, new SfxBakeOptions { sampleRate = rate });
            SfxBaked held = SfxBake.Render(saw, 0.6f, new SfxBakeOptions { sampleRate = rate, mono = true });
            double note = held == null ? 0 : Pitch(held.samples, 0.2, 0.4, rate);
            bool good =
                at != null
                && held != null
                && at.sampleRate == rate
                && at.samples.Length == at.frames * 2
                && Math.Abs(at.Seconds - thud48) < 0.001f
                && Math.Abs(at.frames - thudAt48.frames * (rate / 48000.0)) <= 1
                && Math.Abs(at.frames / (double)thudAt48.frames - rate / 48000.0) < 0.02
                && at.level.peak > 0.05f
                && at.level.peak <= 1f
                && (rate < 22050 || Math.Abs(at.level.RmsDb - thudAt48.level.RmsDb) < 0.5f)
                && !at.samples.Any(float.IsNaN)
                && Math.Abs(note / note48 - 1) < 0.02
                && Math.Abs(held.Seconds - SfxBake.Render(saw, 0.6f).Seconds) < 0.001f;
            mean &= good;
            rates +=
                $" {rate}: {(at == null ? 0 : at.Seconds):0.00} s, {note:0.0} Hz, rms {(at == null ? 0 : at.level.RmsDb - thudAt48.level.RmsDb):+0.00;-0.00} dB{(good ? "" : " WRONG")};";
        }

        Ok(
            mean,
            $"bake options: mono is the mean of left and right; a lower rate is the same sound, as long, the same note - 24000 has half the frames (48000: {thud48:0.00} s, {note48:0.0} Hz;{rates})"
        );

        // the resampler by itself: a tone under half the new rate comes through as it is, one above it is gone
        var tone1k = new float[48000];
        var tone15k = new float[48000];
        for (int i = 0; i < 48000; i++)
        {
            tone1k[i] = (float)Math.Sin(2 * Math.PI * 1000 * i / 48000.0) * 0.5f;
            tone15k[i] = (float)Math.Sin(2 * Math.PI * 15000 * i / 48000.0) * 0.5f;
        }

        float[] kept1k = SfxBake.Resample(tone1k, 48000, 48000, 24000);
        float[] gone15k = SfxBake.Resample(tone15k, 48000, 48000, 24000);
        float[] to441 = SfxBake.Resample(tone1k, 48000, 48000, 44100);
        float[] copy = SfxBake.Resample(tone1k, 100, 48000, 48000);
        float r1 = (float)Rms(kept1k.Skip(2000).Take(20000).ToArray(), 0, 20000 / 48000.0);
        float r15 = (float)Rms(gone15k.Skip(2000).Take(20000).ToArray(), 0, 20000 / 48000.0);
        Ok(
            kept1k.Length == 24000
                && to441.Length == 44100
                && copy.Length == 100
                && copy[50] == tone1k[50]
                && Math.Abs(SfxLevels.ToDb(r1) - SfxLevels.ToDb(0.5f / (float)Math.Sqrt(2))) < 0.05f
                && SfxLevels.ToDb(r15) < -60f
                && Math.Abs(Cycles(to441, 0.2, 0.4, 44100) - 200) <= 1
                && SfxBake.Resample(null, 0, 48000, 24000).Length == 0
                && SfxBake.Resample(tone1k, 0, 48000, 24000).Length == 0
                && kept1k.SequenceEqual(SfxBake.Resample(tone1k, 48000, 48000, 24000)),
            $"the resampler: 1 kHz stays as loud at 24000 ({SfxLevels.ToDb(r1):0.00} dB), 15 kHz is gone ({SfxLevels.ToDb(r15):0.0} dB), and it is the same every time"
        );

        // what a careless caller passes: a hold that is no number or
        // below 0 is no hold; a trim past the end of the array
        SfxBaked noHold = SfxBake.Render(laser, 0f);
        SfxBaked nanHold = SfxBake.Render(laser, float.NaN);
        SfxBaked minusHold = SfxBake.Render(hum, -3f);
        SfxBaked longHold = SfxBake.Render(hum, 99f);
        var shortArray = new float[40];
        for (int i = 0; i < 40; i++)
        {
            shortArray[i] = 0.5f;
        }

        Ok(
            nanHold != null
                && nanHold.frames == noHold.frames
                && nanHold.samples.SequenceEqual(noHold.samples)
                && minusHold != null
                && minusHold.frames > 0
                && longHold != null
                && longHold.cutAtLimit
                && SfxBake.TrimEnd(shortArray, null, 500, -36f, 10) == 40
                && SfxBake.TrimEnd(shortArray, new float[20], 500, -36f, 10) == 20
                && SfxBake.TrimEnd(null, null, 5, -36f, 10) == 0,
            "a hold that is no number or below 0 bakes as no hold, one above 12 s as 12 s; a trim never "
                + "reads past its arrays"
        );

        SfxBaked normal = SfxBake.Render(laser, 0f, new SfxBakeOptions { normaliseDb = -3f });
        SfxBaked plain = SfxBake.Render(laser, 0f);
        SfxPatch silentPatch = laser;
        silentPatch.level = 0;
        SfxBaked nothing = SfxBake.Render(silentPatch, 0f, new SfxBakeOptions { normaliseDb = -3f });
        Ok(
            Math.Abs(normal.level.PeakDb + 3f) < 0.01f
                && Math.Abs(normal.gainDb - (-3f - plain.level.PeakDb)) < 0.01f
                && plain.gainDb == 0f
                && nothing != null
                && nothing.level.Silent
                && nothing.gainDb == 0f
                && nothing.samples.All(s => s == 0f),
            $"normalise reaches its peak ({plain.level.PeakDb:0.00} dB by {normal.gainDb:+0.00;-0.00} dB to {normal.level.PeakDb:0.00} dB); silence stays silence"
        );

        SfxPatch roomy = laser;
        roomy.reverb = 110;
        SfxBaked whole = SfxBake.Render(roomy, 0f);
        SfxBaked cut = SfxBake.Render(roomy, 0f, new SfxBakeOptions { trimDb = -36f });
        float gate = SfxLevels.FromDb(-36f);
        int fade = 256;
        int lastLoud = cut.frames - fade - 1;
        bool trimmed =
            cut.frames < whole.frames - 4800
            && Math.Abs(cut.samples[cut.frames * 2 - 1]) < 0.001f
            && Math.Abs(cut.samples[cut.frames * 2 - 2]) < 0.001f
            && (Math.Abs(cut.samples[lastLoud * 2]) > gate || Math.Abs(cut.samples[lastLoud * 2 + 1]) > gate)
            && cut.samples.Length == cut.frames * 2;
        for (int i = 0; trimmed && i < lastLoud * 2; i++)
        {
            trimmed = cut.samples[i] == whole.samples[i];
        }

        var ramp = new float[1000];
        for (int i = 0; i < ramp.Length; i++)
        {
            ramp[i] = i < 500 ? 0.5f : 0.001f;
        }

        trimmed &=
            SfxBake.TrimEnd(ramp, null, 1000, -36f, 100) == 600
            && ramp[499] == 0.5f
            && ramp[599] == 0f
            && ramp[500] < 0.001f
            && SfxBake.TrimEnd(new float[50], null, 50, -36f, 100) == 50;
        Ok(
            trimmed,
            $"trim cuts the tail and ends in silence ({whole.Seconds:0.00} s to {cut.Seconds:0.00} s at -36 dB)"
        );

        SfxBaked low1 = SfxBake.Render(saw, 0.6f, new SfxBakeOptions { mono = true });
        SfxBaked up = SfxBake.Render(saw, 0.6f, new SfxBakeOptions { mono = true, semitones = 12f });
        double f0 = Pitch(low1.samples, 0.2, 0.4);
        double f1 = Pitch(up.samples, 0.2, 0.4);
        SfxBaked soft = SfxBake.Render(laser, 0f, new SfxBakeOptions { velocity = 0.5f });
        SfxBaked asNote = SfxBake.Render(saw, 0.6f, new SfxBakeOptions { mono = true, midiNote = 69 });
        double fa = Pitch(asNote.samples, 0.2, 0.4);
        Ok(
            Math.Abs(f1 / f0 - 2) < 0.02
                && Math.Abs(soft.level.peak / plain.level.peak - 0.5f) < 0.01f
                && Math.Abs(fa - 440) < 5,
            $"pitch +12 doubles the frequency ({f0:0.0} Hz to {f1:0.0} Hz); velocity 0.5 halves the peak; note 69 is {fa:0.0} Hz"
        );

        SfxBaked ringing = SfxBake.Render(hum, 12f);
        SfxBaked let = SfxBake.Render(hum, 1f);
        Ok(
            ringing != null
                && ringing.cutAtLimit
                && ringing.frames == (int)(SfxBake.MaxSeconds * 48000)
                && !let.cutAtLimit
                && let.frames < ringing.frames,
            "a 12 s hold is cut and says so"
        );

        var refused = new[]
        {
            new SfxBakeOptions { sampleRate = 96000 },
            new SfxBakeOptions { velocity = 0f },
            new SfxBakeOptions { normaliseDb = 1f },
            new SfxBakeOptions { trimDb = -5f },
            new SfxBakeOptions { semitones = 60f },
            new SfxBakeOptions { midiNote = 128 },
            new SfxBakeOptions { sampleRate = 4000 },
        };
        Ok(
            refused.All(o => o.Problem() != null && SfxBake.Render(laser, 0f, o) == null)
                && new SfxBakeOptions().Problem() == null
                && new SfxBakeOptions { sampleRate = 96000 }.Problem() == "sample rate 96000 is outside 8000..48000"
                && new SfxBakeOptions
                {
                    trimDb = -60f,
                    normaliseDb = -1f,
                    midiNote = 60,
                    semitones = -12f,
                    velocity = 0.3f,
                    sampleRate = 22050,
                    mono = true,
                }.Problem() == null,
            "options out of range are refused: " + string.Join("; ", refused.Select(o => o.Problem()))
        );

        SfxPatch noisy = SfxPatch.Create(SfxEngine.Foley, 30, 64, 0, 110, 127, 127, 105, 20, 100, 25);
        noisy.reverb = 60;
        noisy.delay = 40;
        var opts = new SfxBakeOptions
        {
            normaliseDb = -1f,
            trimDb = -48f,
            sampleRate = 44100,
        };
        SfxBaked first = SfxBake.Render(noisy, 0f, opts);
        SfxBaked second = SfxBake.Render(noisy, 0f, opts);
        Ok(
            first.frames == second.frames
                && first.samples.SequenceEqual(second.samples)
                && first.level.peak == second.level.peak,
            "two bakes are the same samples"
        );

        SfxPatch quieter = laser;
        quieter.level = 80;
        SfxBaked at100 = SfxBake.Render(laser, 0f);
        SfxBaked at80 = SfxBake.Render(quieter, 0f);
        float dPeak = at100.level.PeakDb - at80.level.PeakDb;
        float dRms = at100.level.RmsDb - at80.level.RmsDb;
        Ok(
            Math.Abs(dPeak - 3.876f) < 0.05f
                && Math.Abs(dRms - 3.876f) < 0.05f
                && Math.Abs(-SfxLevels.LevelDb(80) - 3.876f) < 0.01f,
            $"LEVEL scales the baked sound by 40 log10(level / 100): 100 to 80 is {dPeak:0.000} dB in peak, {dRms:0.000} dB in RMS"
        );

        bool matched = true;
        string spreads = "";
        foreach (SfxLevelMeasure by in new[] { SfxLevelMeasure.Rms, SfxLevelMeasure.Peak })
        {
            SfxPatch[] three = { thud, laser, hum };
            var reads = three.Select(p => SfxBake.Render(p, 1f).level).ToArray();
            int[] levels = three.Select(p => (int)p.level).ToArray();
            int[] now = new int[3];
            float target = SfxLevels.Match(levels, reads, by, now);
            float lowest = 99f;
            float highest = -99f;
            for (int i = 0; i < 3; i++)
            {
                three[i].level = (byte)now[i];
                SfxLevelReading after = SfxBake.Render(three[i], 1f).level;
                float reading = by == SfxLevelMeasure.Rms ? after.RmsDb : after.PeakDb;
                lowest = Math.Min(lowest, reading);
                highest = Math.Max(highest, reading);
                matched &= !after.Clips && Math.Abs(reading - target) < 0.25f;
            }

            matched &= highest - lowest <= 0.25f;
            spreads += $" {by}: {target:0.00} dB, spread {highest - lowest:0.00} dB, LEVEL {string.Join(" ", now)};";
        }

        Ok(matched, "after a match three sounds read the same RMS, and the same peak:" + spreads);
    }

    // envelopes, the wavetable, the effects
    static void EnvelopesWavetableEffects()
    {
        SfxNative.Bake(saw, out var a4, out _, 48000, 1f, 69, 0f, 1.0f);
        Ok(
            Math.Abs(Cycles(a4, 0.3, 0.8) / 0.5 - 440) < 4 && Rms(a4, 0.8, 0.95) > Rms(a4, 0.2, 0.4) * 0.6,
            $"a wavetable sound held as the note A4: {Cycles(a4, 0.3, 0.8) / 0.5:0} Hz, still sounding after 0.9 s"
        );
        Ok(
            a4.Length < 48000 * 1.4,
            $"and gone soon after the key is let go ({a4.Length / 48000f:0.00} s for a note held 1 s)"
        );

        var slow = saw;
        slow.attack = 100;
        SfxNative.Bake(slow, out var sl, out _, 48000, 1f, 69, 0f, 1.5f);
        Ok(
            Rms(sl, 0.0, 0.05) < Rms(a4, 0.0, 0.05) * 0.2 && Rms(sl, 1.2, 1.4) > 0.05,
            $"ATTACK fades it in ({Rms(a4, 0, 0.05):0.000} -> {Rms(sl, 0, 0.05):0.000} in the first 50 ms)"
        );

        // the table's last frame: a square
        var end = saw;
        end.fmRatio = 127;
        SfxNative.Bake(end, out var sq, out _, 48000, 1f, 69, 0f, 1.0f);
        double diff = 0;
        double tot = 0;
        for (int i = 14400; i < 33600; i++)
        {
            diff += (a4[i] - sq[i]) * (a4[i] - sq[i]);
            tot += a4[i] * a4[i];
        }

        Ok(
            diff / tot > 0.05 && Math.Abs(Cycles(sq, 0.3, 0.8) / 0.5 - 440) < 4,
            $"POSITION changes the wave, not the pitch (difference {diff / tot:0.00})"
        );

        var tables = Enumerable
            .Range(0, SfxPatch.TableCount)
            .Select(t =>
            {
                var p = saw;
                p.table = (byte)t;
                p.fmRatio = 64;
                SfxNative.Bake(p, out var x, out _, 48000, 1f, 57, 0f, 0.5f);
                return x;
            })
            .ToArray();
        Ok(
            tables.All(x => x != null && x.Max(s => Math.Abs(s)) > 0.1f && x.All(s => !float.IsNaN(s))),
            $"all {tables.Length} wavetables sound"
        );

        var wide = saw;
        wide.spread = 60;
        SfxNative.Bake(wide, out var un, out _, 48000, 1f, 57, 0f, 1.0f);
        double lo = double.MaxValue;
        double hi = 0;
        for (double t = 0.2; t < 0.9; t += 0.05)
        {
            double e = Rms(un, t, t + 0.05);
            lo = Math.Min(lo, e);
            hi = Math.Max(hi, e);
        }

        Ok(hi > lo * 1.15, $"SPREAD adds detuned oscillators: the level beats ({lo:0.000} .. {hi:0.000})");

        var wet = thud;
        wet.reverb = 110;
        SfxNative.Bake(thud, out var dl, out var dr, 48000, 0.9f);
        SfxNative.Bake(wet, out var wl, out var wr, 48000, 0.9f);
        double side = 0;
        for (int i = 0; i < wl.Length; i++)
        {
            side += (wl[i] - wr[i]) * (wl[i] - wr[i]);
        }

        Ok(
            wl.Length > dl.Length + 12000 && side > 0.01 && dl.SequenceEqual(dr),
            $"REVERB: a longer tail ({dl.Length / 48000f:0.00} s -> {wl.Length / 48000f:0.00} s) and a stereo one; without it left = right"
        );

        var echo = laser;
        echo.delay = 110;
        SfxNative.Bake(laser, out var e0, out _, 48000, 0.9f);
        SfxNative.Bake(echo, out var e1, out var e1r, 48000, 0.9f);
        Ok(
            e1.Length > e0.Length + 12000 && Rms(e1, 0.375, 0.5) > Rms(e0, 0.375, 0.5) * 3 + 0.005,
            "DELAY: the sound comes back three 16ths later (0.375 s at 120 BPM)"
        );

        var thick = saw;
        thick.chorus = 110;
        SfxNative.Bake(thick, out var cl, out var cr, 48000, 1f, 57, 0f, 0.6f);
        double cside = 0;
        for (int i = 4800; i < 24000; i++)
        {
            cside += (cl[i] - cr[i]) * (cl[i] - cr[i]);
        }

        Ok(cside > 0.5, "CHORUS: left and right differ");
    }
}
