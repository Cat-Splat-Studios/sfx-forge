// Tests of the Unity package's engine-independent code. Run after `sh build.sh` (it reads out/midi.json and out/audio.wav):
//   dotnet run -c Release --project tests/dotnet -- <repo root>
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using CatSplat.SfxForge;

static class Tests
{
    static int failed;
    static void Ok(bool c, string t) { Console.WriteLine((c ? "  ok    " : "  FAIL  ") + t); if (!c) failed++; }
    static double Rms(float[] a, double t0, double t1, int sr = 48000) { double s = 0; int i0 = (int)(t0 * sr), i1 = Math.Min(a.Length, (int)(t1 * sr)); for (int i = i0; i < i1; i++) s += a[i] * a[i]; return i1 > i0 ? Math.Sqrt(s / (i1 - i0)) : 0; }
    static int Cycles(float[] a, double t0, double t1, int sr = 48000) { int c = 0; for (int i = (int)(t0 * sr) + 1; i < (int)(t1 * sr); i++) if (a[i - 1] < 0 && a[i] >= 0) c++; return c; }

    static List<byte[]> ReadMidi(string file) => JsonSerializer.Deserialize<JsonElement>(File.ReadAllText(file)).EnumerateArray()
        .Select(e => e.GetProperty("bytes").EnumerateArray().Select(b => (byte)b.GetInt32()).ToArray()).ToList();

    static float[] ReadWavLeft(string file)
    {
        byte[] w = File.ReadAllBytes(file);
        var x = new float[(w.Length - 44) / 4];
        for (int i = 0; i < x.Length; i++) x[i] = BitConverter.ToInt16(w, 44 + i * 4) / 32768f;
        return x;
    }

    static double Correlate(float[] mine, float[] device, int at, int n, out int bestOff)
    {
        double best = 0; bestOff = 0;
        for (int off = -64; off <= 512; off++)
        {
            double ab = 0, aa = 0, bb = 0;
            for (int i = 0; i < n; i += 2) { double x = Math.Tanh(mine[i]), y = device[at + off + i]; ab += x * y; aa += x * x; bb += y * y; }
            double c = ab / Math.Sqrt(aa * bb + 1e-12);
            if (c > best) { best = c; bestOff = off; }
        }
        return best;
    }

    static int Main(string[] args)
    {
        string root = args.Length > 0 ? args[0] : ".";
        string midiFile = Path.Combine(root, "out", "midi.json"), wavFile = Path.Combine(root, "out", "audio.wav");
        var rng = new Random(7);

        // ---- the patch and the protocol
        bool round = true;
        for (int i = 0; i < 2000; i++)
        {
            var p = new SfxPatch { engine = (SfxEngine)rng.Next(SfxPatch.EngineCount) };
            for (int k = 0; k < SfxPatch.ParamCount; k++) p[(SfxParam)k] = (byte)rng.Next(128);
            int slot = rng.Next(16);
            round &= SfxProtocol.TryParsePatch(SfxProtocol.BuildLoad(slot, p), out int s2, out var p2) && s2 == slot && p2.Equals(p);
        }
        Ok(round && SfxPatch.Bytes == 28, "2000 random patches survive pack -> SysEx -> unpack (28 bytes each)");
        var bad = SfxProtocol.BuildLoad(3, SfxPatch.Default); bad[10] ^= 1;
        Ok(!SfxProtocol.TryParsePatch(bad, out _, out _), "a damaged patch is refused (checksum)");
        Ok(!SfxProtocol.TryParsePatch(new byte[] { 0xF0, 0x7D, 0x56, 0x4C, 1, 0, 0xF7 }, out _, out _), "another device's SysEx is ignored");
        var text = SfxProtocol.BuildText("Level 3 – Boss with a very long name here");
        Ok(text.Length == 6 + 24 && text.All(b => b == 0xF0 || b == 0xF7 || b < 128), "text is cut to 24 characters of 7-bit ASCII");

        var laser = SfxPatch.Default;
        var hum = SfxPatch.Create(SfxEngine.SciFiFm, 22, 64, 0, 21, 55, 8, 78, 45, 60, 0, 0, 127);          // sustained
        var thud = SfxPatch.Create(SfxEngine.Impact, 24, 90, 35, 0, 0, 5, 70, 0, 70, 0);                     // pad 3 of the device
        var saw = SfxPatch.Create(SfxEngine.Wavetable, 52, 64, 0, 0, 0, 0, 127, 0, 90, 0, 0, 110, 40);       // table 0 at its start: a saw, held
        Ok(laser.level == 100 && !laser.IsSustained && hum.IsSustained && new SfxPatch().level == 0, "Create fills the defaults (level 100, a one-shot); sustain above 0 = held");
        bool mut = true;
        for (int i = 0; i < 500; i++)
        {
            var a = laser; a.Mutate(rng);
            for (int k = 0; k < SfxPatch.ParamCount; k++) mut &= Math.Abs(a[(SfxParam)k] - laser[(SfxParam)k]) <= 19;
            mut &= a.engine == laser.engine && a.sustain == 0 && a.level == laser.level && a.table == laser.table;
            var h = hum; h.Mutate(rng); mut &= h.sustain > 0;
        }
        Ok(mut, "Mutate stays within 15 % and keeps the kind of sound: engine, table, level, one-shot or sustained");
        var mods = SfxMods.None;
        SfxProtocol.Apply(ref mods, SfxTelemetry.Pitch, 127); SfxProtocol.Apply(ref mods, SfxTelemetry.Cutoff, 64);
        Ok(mods.pitchSemitones == 24f && mods.cutoffOctaves == 0f && SfxProtocol.ToCc(0.5f) == 64 && SfxProtocol.ToCc(float.NaN) == 0, "telemetry maps as on the device");

        // ---- what the real firmware sent in the test performance
        List<byte[]> msgs = File.Exists(midiFile) ? ReadMidi(midiFile) : null;
        float[] device = File.Exists(wavFile) ? ReadWavLeft(wavFile) : null;
        if (msgs != null)
        {
            var sx = msgs.FirstOrDefault(b => b[0] == 0xF0);
            bool parsed = SfxProtocol.TryParsePatch(sx, out int slot, out var fromDevice);
            Ok(parsed, parsed ? $"the firmware's EXPORT parses here (pad {slot + 1}, {fromDevice.engine})" : "the firmware's EXPORT parses here");
            Ok(parsed && SfxProtocol.BuildDump(slot, fromDevice).SequenceEqual(sx), "and packs back to the same bytes");
        }
        else Ok(false, "out/midi.json missing: run sh build.sh first");

        // ---- the native engine
        Ok(SfxNative.Available, "sfxforge.dll loads");
        if (!SfxNative.Available) { Console.WriteLine("FAILED"); return 1; }

        foreach (var (name, p) in new[] { ("thud", thud), ("laser", laser), ("hum (held 0.5 s)", hum) })
        {
            bool ok = SfxNative.Bake(p, out var l, out var r, 48000, 0.9f, 0, 0f, 0.5f);
            bool sane = ok && l.Length > 2000 && l.Length == r.Length && l.All(v => !float.IsNaN(v) && Math.Abs(v) <= 1f) && l.Max(v => Math.Abs(v)) > 0.05f && Math.Abs(l[l.Length - 1]) < 1e-3f;
            Ok(sane, $"bake '{name}': {(ok ? l.Length / 48000f : 0):0.00} s, peak {(ok ? l.Max(v => Math.Abs(v)) : 0):0.00}, ends in silence");
        }
        if (device != null)
        {
            // tests/tour.json plays pad 3 (this thud) at 1.4 s with velocity 0.9; the Vibe's output stage is tanh(x) at volume 0.8
            SfxNative.Bake(thud, out var mine, out _, 48000, 0.9f);
            double c = Correlate(mine, device, (int)(1.4 * 48000), 9600, out int off);
            Ok(c > 0.98, $"the DLL and the Vibe render the same sound (correlation {c:0.0000}, offset {off} samples)");
        }

        // a single dry voice, block by block, with telemetry
        IntPtr v = Marshal.AllocHGlobal(SfxNative.VoiceSize);
        for (int i = 0; i < SfxNative.VoiceSize; i++) Marshal.WriteByte(v, i, 0);
        var buf = new float[256]; var md = SfxMods.None; md.pitchSemitones = 12;
        bool started = SfxNative.Trigger(v, hum.Pack(), 1f, 0f, 44100f, 99), sounding = true; double energy = 0;
        for (int b = 0; b < 40; b++) { Array.Clear(buf, 0, 256); sounding &= SfxNative.Render(v, ref md, buf, 256); energy += buf.Sum(x => (double)x * x); }
        SfxNative.Release(v);
        int blocks = 0; while (SfxNative.Render(v, ref md, buf, 256) && blocks < 400) blocks++;
        Ok(started && sounding && energy > 1 && blocks > 0 && blocks < 400 && !SfxNative.Active(v), $"a sustained voice plays until released, then fades ({blocks} blocks)");
        Marshal.FreeHGlobal(v);

        // ---- envelopes, the wavetable, the effects
        {
            SfxNative.Bake(saw, out var a4, out _, 48000, 1f, 69, 0f, 1.0f);
            Ok(Math.Abs(Cycles(a4, 0.3, 0.8) / 0.5 - 440) < 4 && Rms(a4, 0.8, 0.95) > Rms(a4, 0.2, 0.4) * 0.6, $"a wavetable sound held as the note A4: {Cycles(a4, 0.3, 0.8) / 0.5:0} Hz, still sounding after 0.9 s");
            Ok(a4.Length < 48000 * 1.4, $"and gone soon after the key is let go ({a4.Length / 48000f:0.00} s for a note held 1 s)");

            var slow = saw; slow.attack = 100;
            SfxNative.Bake(slow, out var sl, out _, 48000, 1f, 69, 0f, 1.5f);
            Ok(Rms(sl, 0.0, 0.05) < Rms(a4, 0.0, 0.05) * 0.2 && Rms(sl, 1.2, 1.4) > 0.05, $"ATTACK fades it in ({Rms(a4, 0, 0.05):0.000} -> {Rms(sl, 0, 0.05):0.000} in the first 50 ms)");

            var end = saw; end.fmRatio = 127;                                              // the table's last frame: a square
            SfxNative.Bake(end, out var sq, out _, 48000, 1f, 69, 0f, 1.0f);
            double diff = 0, tot = 0; for (int i = 14400; i < 33600; i++) { diff += (a4[i] - sq[i]) * (a4[i] - sq[i]); tot += a4[i] * a4[i]; }
            Ok(diff / tot > 0.05 && Math.Abs(Cycles(sq, 0.3, 0.8) / 0.5 - 440) < 4, $"POSITION changes the wave, not the pitch (difference {diff / tot:0.00})");

            var tables = Enumerable.Range(0, SfxPatch.TableCount).Select(t => { var p = saw; p.table = (byte)t; p.fmRatio = 64; SfxNative.Bake(p, out var x, out _, 48000, 1f, 57, 0f, 0.5f); return x; }).ToArray();
            Ok(tables.All(x => x != null && x.Max(s => Math.Abs(s)) > 0.1f && x.All(s => !float.IsNaN(s))), $"all {tables.Length} wavetables sound");

            var wide = saw; wide.spread = 60;
            SfxNative.Bake(wide, out var un, out _, 48000, 1f, 57, 0f, 1.0f);
            double lo = double.MaxValue, hi = 0; for (double t = 0.2; t < 0.9; t += 0.05) { double e = Rms(un, t, t + 0.05); lo = Math.Min(lo, e); hi = Math.Max(hi, e); }
            Ok(hi > lo * 1.15, $"SPREAD adds detuned oscillators: the level beats ({lo:0.000} .. {hi:0.000})");

            var wet = thud; wet.reverb = 110;
            SfxNative.Bake(thud, out var dl, out var dr, 48000, 0.9f);
            SfxNative.Bake(wet, out var wl, out var wr, 48000, 0.9f);
            double side = 0; for (int i = 0; i < wl.Length; i++) side += (wl[i] - wr[i]) * (wl[i] - wr[i]);
            Ok(wl.Length > dl.Length + 12000 && side > 0.01 && dl.SequenceEqual(dr), $"REVERB: a longer tail ({dl.Length / 48000f:0.00} s -> {wl.Length / 48000f:0.00} s) and a stereo one; without it left = right");

            var echo = laser; echo.delay = 110;
            SfxNative.Bake(laser, out var e0, out _, 48000, 0.9f);
            SfxNative.Bake(echo, out var e1, out var e1r, 48000, 0.9f);
            Ok(e1.Length > e0.Length + 12000 && Rms(e1, 0.375, 0.5) > Rms(e0, 0.375, 0.5) * 3 + 0.005, "DELAY: the sound comes back three 16ths later (0.375 s at 120 BPM)");

            var thick = saw; thick.chorus = 110;
            SfxNative.Bake(thick, out var cl, out var cr, 48000, 1f, 57, 0f, 0.6f);
            double cside = 0; for (int i = 4800; i < 24000; i++) cside += (cl[i] - cr[i]) * (cl[i] - cr[i]);
            Ok(cside > 0.5, "CHORUS: left and right differ");
        }

        // ---- music: the song the firmware sent (SEND on its SONG page), played here by the same player
        if (msgs != null)
        {
            var song = new byte[SfxProtocol.SongBytes]; var sounds = new SfxPatch[16]; int next = -1, seen = 0; bool whole = false;
            foreach (var msg in msgs.Skip(msgs.FindLastIndex(b => b[0] == 0xF0 && b[4] == SfxProtocol.Dump && b[5] == 0)))
            {
                if (SfxProtocol.TryParsePatch(msg, out int slot, out var patch)) { sounds[slot] = patch; seen |= 1 << slot; }
                else if (SfxProtocol.TryTakeSongChunk(msg, song, ref next)) whole = true;
            }
            Ok(whole && seen == 0xFFFF, "the firmware's song arrives whole: 16 sounds and 13 pieces");
            Ok(whole && SfxProtocol.BuildSong(song).SelectMany(c => c).SequenceEqual(msgs.Where(b => b[0] == 0xF0 && b[4] == SfxProtocol.SongChunk).SelectMany(c => c)), "and packs back to the same messages");

            IntPtr pl = Marshal.AllocHGlobal(SfxNative.PlayerSize);
            SfxNative.PlayerInit(pl, 48000f);
            bool loaded = SfxNative.PlayerSetSong(pl, song);
            for (int i = 0; i < 16; i++) loaded &= SfxNative.PlayerSetPatch(pl, i, sounds[i].Pack());
            var wrong = (byte[])song.Clone(); wrong[0] = 9;
            Ok(loaded && !SfxNative.PlayerSetSong(pl, wrong), "the native player takes the song (and refuses a broken one)");

            float[] Play(float intensity, int blocks, Action<int> each = null)
            {
                var all = new float[blocks * 256]; var bl = new float[256]; var br = new float[256]; var none = SfxMods.None;
                SfxNative.PlayerSetLive(pl, ref none, intensity);
                for (int b = 0; b < blocks; b++) { Array.Clear(bl, 0, 256); Array.Clear(br, 0, 256); each?.Invoke(b); SfxNative.PlayerRender(pl, bl, br, 256); Array.Copy(bl, 0, all, b * 256, 256); }
                return all;
            }

            // at intensity 0: the same as the device played from 21.0 s of the test performance (one bar, with the step added there)
            SfxNative.PlayerStart(pl, 0);
            var low = Play(0f, 750);
            if (device != null)
            {
                double c = Correlate(low, device, 21 * 48000, 48000 * 2, out int off);
                Ok(c > 0.97, $"the native player and the Vibe play the song alike (correlation {c:0.0000} over 2 s, offset {off} samples)");
            }
            var st = new int[4]; SfxNative.PlayerState(pl, st);
            Ok(st[0] == 1 && st[3] == 1 && st[2] == 1, $"after 4 s at 120 BPM it is in bar 2, on pattern B of the chain (bar {st[3] + 1}, pattern {(char)('A' + st[2])})");

            // intensity brings the layers in; a cue changes the pattern on the bar line
            SfxNative.PlayerStart(pl, 0);
            var high = Play(1f, 375);
            Ok(Rms(high, 0.26, 0.49) > Rms(low, 0.26, 0.49) * 2, $"intensity 1 plays more layers than intensity 0 ({Rms(low, 0.26, 0.49):0.000} -> {Rms(high, 0.26, 0.49):0.000} between two kicks)");
            SfxNative.PlayerStart(pl, 0);
            int changedAt = -1;
            Play(1f, 600, b => { if (b == 100) SfxNative.PlayerCue(pl, 5); SfxNative.PlayerState(pl, st); if (st[2] == 5 && changedAt < 0) changedAt = b; });
            Ok(changedAt > 372 && changedAt <= 377, $"a cue at 0.5 s changes the pattern on the bar line at 2.0 s (block {changedAt} of 256 samples)");
            SfxNative.PlayerStop(pl);

            // a sound as a note: A4 is 440 Hz whatever the sound's own FREQ says
            for (int i = 0; i < 60; i++) Play(1f, 10);                               // let everything ring out
            SfxNative.PlayerNoteOn(pl, 6, 1, 1f, 69);
            var tone = Play(1f, 60);
            Ok(Math.Abs(Cycles(tone, 0.05, 0.2) / 0.15 - 440) < 12, $"a sound played as the note A4 is in tune ({Cycles(tone, 0.05, 0.2) / 0.15:0} Hz)");

            // a chord on the strings: 16 voices, held, then let go
            for (int i = 0; i < 60; i++) Play(1f, 10);
            int[] chord = { 45, 52, 57, 60, 64, 69, 72, 76 };
            foreach (int n in chord) SfxNative.PlayerNoteOn(pl, 15, 100 + n, 0.8f, n);
            var held = Play(1f, 300);
            foreach (int n in chord) SfxNative.PlayerNoteOff(pl, 100 + n);
            var after = Play(1f, 900);
            Ok(sounds[15].engine == SfxEngine.Wavetable && Rms(held, 1.2, 1.6) > 0.05 && held.All(x => !float.IsNaN(x)) && Rms(after, 4.0, 4.8) < 0.002,
               $"an 8-note chord on the strings holds ({Rms(held, 1.2, 1.6):0.000}) and dies away after the keys are let go ({Rms(after, 4.0, 4.8):0.0000})");
            Marshal.FreeHGlobal(pl);

            // the demo project's song: exactly what the firmware sent
            var demo = new byte[16 * SfxPatch.Bytes + SfxProtocol.SongBytes];
            for (int i = 0; i < 16; i++) sounds[i].Pack(demo, i * SfxPatch.Bytes);
            song.CopyTo(demo, 16 * SfxPatch.Bytes);
            File.WriteAllBytes(Path.Combine(root, "out", "demo-song.bytes"), demo);
        }

        // ---- MIDI: what can be tried without a device
        string[] ins = MidiPorts.Inputs(), outs = MidiPorts.Outputs();
        Console.WriteLine("  ports in:  " + (ins.Length > 0 ? string.Join(", ", ins) : "(none)"));
        Console.WriteLine("  ports out: " + (outs.Length > 0 ? string.Join(", ", outs) : "(none)"));
        if (SfxLink.Find(outs, "GS Wavetable") >= 0)
        {
            using var link = new SfxLink();
            bool open = link.Open(null, "GS Wavetable");
            bool sent = open && link.SendTelemetry(SfxTelemetry.Pitch, 0.5f) && !link.SendTelemetry(SfxTelemetry.Pitch, 0.5f) && link.SendText("TEST") && link.SendPatch(0, SfxPatch.Default);
            Ok(sent, "output: opens, sends CC (once per change) and SysEx");
        }
        else Console.WriteLine("  ----  no built-in synth port: output not tried");
        if (ins.Length > 0)
        {
            var port = MidiIn.TryOpen(0);
            Thread.Sleep(100);
            bool opened = port != null;
            if (port != null) port.Dispose();
            Ok(opened, $"input: '{ins[0]}' opens and closes cleanly");
        }
        Console.WriteLine("  ----  receiving MIDI is NOT covered: it needs a loopback port or a device");

        Console.WriteLine(failed > 0 ? $"FAILED ({failed})" : "PASSED");
        return failed > 0 ? 1 : 0;
    }
}
