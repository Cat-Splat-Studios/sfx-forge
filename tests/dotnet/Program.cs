// Tests of the Unity package's engine-independent code. Run after `sh build.sh` (it reads out/midi.json and out/audio.wav):
//   dotnet run -c Release --project tests/dotnet -- <repo root>
using System;
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

    static int Main(string[] args)
    {
        string root = args.Length > 0 ? args[0] : ".";

        // ---- the patch and the protocol
        var rng = new Random(7);
        bool round = true;
        for (int i = 0; i < 2000; i++)
        {
            var p = new SfxPatch { engine = (SfxEngine)rng.Next(4) };
            for (int k = 0; k < SfxPatch.ParamCount; k++) p[(SfxParam)k] = (byte)rng.Next(128);
            int slot = rng.Next(16);
            round &= SfxProtocol.TryParsePatch(SfxProtocol.BuildLoad(slot, p), out int s2, out var p2) && s2 == slot && p2.Equals(p);
        }
        Ok(round, "2000 random patches survive pack -> SysEx -> unpack");
        var bad = SfxProtocol.BuildLoad(3, SfxPatch.Default); bad[10] ^= 1;
        Ok(!SfxProtocol.TryParsePatch(bad, out _, out _), "a damaged patch is refused (checksum)");
        Ok(!SfxProtocol.TryParsePatch(new byte[] { 0xF0, 0x7D, 0x56, 0x4C, 1, 0, 0xF7 }, out _, out _), "another device's SysEx is ignored");
        var text = SfxProtocol.BuildText("Level 3 – Boss with a very long name here");
        Ok(text.Length == 6 + 24 && text.All(b => b == 0xF0 || b == 0xF7 || b < 128), "text is cut to 24 characters of 7-bit ASCII");
        var m = SfxPatch.Default; var hold = SfxPatch.Default; hold.ampDecay = SfxPatch.Hold;
        bool mut = true;
        for (int i = 0; i < 500; i++)
        {
            var a = m; a.Mutate(rng);
            for (int k = 0; k < SfxPatch.ParamCount; k++) mut &= Math.Abs(a[(SfxParam)k] - m[(SfxParam)k]) <= 19;
            mut &= a.engine == m.engine && a.ampDecay != SfxPatch.Hold;
            var h = hold; h.Mutate(rng); mut &= h.ampDecay == SfxPatch.Hold;
        }
        Ok(mut, "Mutate stays within 15 %, keeps the engine and HOLD");
        var mods = SfxMods.None;
        SfxProtocol.Apply(ref mods, SfxTelemetry.Pitch, 127); SfxProtocol.Apply(ref mods, SfxTelemetry.Cutoff, 64);
        Ok(mods.pitchSemitones == 24f && mods.cutoffOctaves == 0f && SfxProtocol.ToCc(0.5f) == 64 && SfxProtocol.ToCc(float.NaN) == 0, "telemetry maps as on the device");

        // ---- what the real firmware sent in the test performance
        string midiFile = Path.Combine(root, "out", "midi.json");
        if (File.Exists(midiFile))
        {
            var msgs = JsonSerializer.Deserialize<JsonElement>(File.ReadAllText(midiFile)).EnumerateArray()
                .Select(e => e.GetProperty("bytes").EnumerateArray().Select(b => (byte)b.GetInt32()).ToArray()).ToList();
            var sx = msgs.FirstOrDefault(b => b[0] == 0xF0);
            bool parsed = SfxProtocol.TryParsePatch(sx, out int slot, out var fromDevice);
            Ok(parsed, parsed ? $"the firmware's EXPORT parses here (pad {slot + 1}, {fromDevice.engine})" : "the firmware's EXPORT parses here");
            Ok(parsed && SfxProtocol.BuildDump(slot, fromDevice).SequenceEqual(sx), "and packs back to the same bytes");
        }
        else Ok(false, "out/midi.json missing: run sh build.sh first");

        // ---- the native engine
        Ok(SfxNative.Available, "sfxforge.dll loads");
        if (SfxNative.Available)
        {
            var thud = new SfxPatch { engine = SfxEngine.Impact, baseFreq = 24, pitchEnvAmount = 90, pitchEnvDecay = 35, noiseMix = 5, filterCutoff = 70, ampDecay = 70 };
            var laser = SfxPatch.Default;
            foreach (var (name, p) in new[] { ("thud", thud), ("laser", laser), ("hold", hold) })
            {
                var a = SfxNative.Bake(p, 48000, 0.9f, 0f, 0.5f);
                bool sane = a != null && a.Length > 2000 && a.All(v => !float.IsNaN(v) && Math.Abs(v) <= 1f) && a.Max(v => Math.Abs(v)) > 0.05f && Math.Abs(a[a.Length - 1]) < 1e-3f;
                Ok(sane, $"bake '{name}': {(a == null ? 0 : a.Length / 48000f):0.00} s, peak {(a == null ? 0 : a.Max(v => Math.Abs(v))):0.00}, ends in silence");
            }
            string wav = Path.Combine(root, "out", "audio.wav");
            if (File.Exists(wav))
            {
                // tests/tour.json plays pad 3 (this thud) at 1.4 s with velocity 0.9; the Vibe's output stage is tanh(x) at volume 0.8
                byte[] w = File.ReadAllBytes(wav);
                var dev = new float[(w.Length - 44) / 4];
                for (int i = 0; i < dev.Length; i++) dev[i] = BitConverter.ToInt16(w, 44 + i * 4) / 32768f;
                var mine = SfxNative.Bake(thud, 48000, 0.9f).Select(v => (float)Math.Tanh(v)).ToArray();
                int at = (int)(1.4 * 48000), n = 9600, bestOff = 0; double best = 0;
                for (int off = -64; off <= 512; off++)
                {
                    double ab = 0, aa = 0, bb = 0;
                    for (int i = 0; i < n; i++) { double x = mine[i], y = dev[at + off + i]; ab += x * y; aa += x * x; bb += y * y; }
                    double c = ab / Math.Sqrt(aa * bb + 1e-12);
                    if (c > best) { best = c; bestOff = off; }
                }
                Ok(best > 0.98, $"the DLL and the Vibe render the same sound (correlation {best:0.0000}, offset {bestOff} samples)");
            }
            // a voice as a game plays it: block by block, with telemetry
            IntPtr v = Marshal.AllocHGlobal(SfxNative.VoiceSize);
            for (int i = 0; i < SfxNative.VoiceSize; i++) Marshal.WriteByte(v, i, 0);
            var buf = new float[256]; var md = SfxMods.None; md.pitchSemitones = 12;
            bool started = SfxNative.Trigger(v, hold.Pack(), 1f, 0f, 44100f, 99), sounding = true; double energy = 0;
            for (int b = 0; b < 40; b++) { Array.Clear(buf, 0, 256); sounding &= SfxNative.Render(v, ref md, buf, 256); energy += buf.Sum(x => (double)x * x); }
            SfxNative.Release(v);
            int blocks = 0; while (SfxNative.Render(v, ref md, buf, 256) && blocks < 400) blocks++;
            Ok(started && sounding && energy > 1 && blocks > 0 && blocks < 400 && !SfxNative.Active(v), $"a held voice plays until released, then fades ({blocks} blocks)");
            Marshal.FreeHGlobal(v);
            SfxWav.Write(Path.Combine(Path.GetTempPath(), "sfxforge-test.wav"), SfxNative.Bake(laser), 48000);
        }

        // ---- music: the song the firmware sent (SEND on its SONG page), played here by the same player
        if (File.Exists(midiFile) && SfxNative.Available)
        {
            var msgs = JsonSerializer.Deserialize<JsonElement>(File.ReadAllText(midiFile)).EnumerateArray()
                .Select(e => e.GetProperty("bytes").EnumerateArray().Select(b => (byte)b.GetInt32()).ToArray()).ToList();
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
                var all = new float[blocks * 256]; var blk = new float[256]; var none = SfxMods.None;
                SfxNative.PlayerSetLive(pl, ref none, intensity);
                for (int b = 0; b < blocks; b++) { Array.Clear(blk, 0, 256); each?.Invoke(b); SfxNative.PlayerRender(pl, blk, 256); Array.Copy(blk, 0, all, b * 256, 256); }
                return all;
            }
            double Rms(float[] a, double t0, double t1) { double s2 = 0; int i0 = (int)(t0 * 48000), i1 = (int)(t1 * 48000); for (int i = i0; i < i1; i++) s2 += a[i] * a[i]; return Math.Sqrt(s2 / (i1 - i0)); }

            // at intensity 0: the same as the device played from 21.0 s of the test performance (one bar, with the step added there)
            SfxNative.PlayerStart(pl, 0);
            var low = Play(0f, 750);
            string wav2 = Path.Combine(root, "out", "audio.wav");
            if (File.Exists(wav2))
            {
                byte[] w = File.ReadAllBytes(wav2);
                var dev = new float[(w.Length - 44) / 4];
                for (int i = 0; i < dev.Length; i++) dev[i] = BitConverter.ToInt16(w, 44 + i * 4) / 32768f;
                int at = 21 * 48000, n = 48000 * 2; double best = 0; int bestOff = 0;
                for (int off = -64; off <= 512; off++)
                {
                    double ab = 0, aa = 0, bb = 0;
                    for (int i = 0; i < n; i += 2) { double x = Math.Tanh(low[i]), y = dev[at + off + i]; ab += x * y; aa += x * x; bb += y * y; }
                    double c = ab / Math.Sqrt(aa * bb + 1e-12);
                    if (c > best) { best = c; bestOff = off; }
                }
                Ok(best > 0.98, $"the native player and the Vibe play the song alike (correlation {best:0.0000} over 2 s, offset {bestOff} samples)");
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
            for (int i = 0; i < 40; i++) Play(1f, 10);                               // let everything ring out
            SfxNative.PlayerNoteOn(pl, 6, 1f, 69);
            var tone = Play(1f, 60);
            int cross = 0; for (int i = 2400; i < 9600; i++) if (tone[i - 1] < 0 && tone[i] >= 0) cross++;
            Ok(Math.Abs(cross / 0.15 - 440) < 12, $"a sound played as the note A4 is in tune ({cross / 0.15:0} Hz)");
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
