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
    // the pitch of a stretch of sound, by its autocorrelation (FM and wavetables cross zero more than once a cycle)
    static double Pitch(float[] a, double t0, double t1, int sr = 48000)
    {
        int i0 = (int)(t0 * sr), n = Math.Min(a.Length, (int)(t1 * sr)) - i0, best = 0; double top = 0, r0 = 0; bool dipped = false;
        for (int i = 0; i < n; i++) r0 += a[i0 + i] * a[i0 + i];
        r0 /= Math.Max(1, n);
        for (int lag = sr / 2000; lag < sr / 30 && lag < n / 2; lag++)                // the first peak after the first dip
        {
            double c = 0; for (int i = 0; i < n - lag; i++) c += a[i0 + i] * a[i0 + i + lag];
            c /= n - lag;
            if (c < r0 * 0.3) dipped = true;
            if (dipped && c > top * 1.02) { top = c; best = lag; }
        }
        return best > 0 ? (double)sr / best : 0;
    }
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

    // ---- a song rendered by the native player, as one number: a hash over the float bits of both channels
    static ulong Fnv(ulong h, float[] a, int n) { for (int i = 0; i < n; i++) { uint b = BitConverter.SingleToUInt32Bits(a[i]); for (int k = 0; k < 4; k++) { h ^= (b >> (k * 8)) & 0xFF; h *= 0x100000001B3UL; } } return h; }
    static ulong RenderHash(byte[] sounds, byte[] song, int pattern, bool loop, float intensity, int bars, Action<IntPtr> before = null)
    {
        IntPtr pl = Marshal.AllocHGlobal(SfxNative.PlayerSize);
        try
        {
            SfxNative.PlayerInit(pl, 48000f);
            if (!SfxNative.PlayerSetSong(pl, song)) return 0;
            for (int i = 0; i < 16; i++) if (!SfxNative.PlayerSetPatch(pl, i, sounds.Skip(i * SfxPatch.Bytes).Take(SfxPatch.Bytes).ToArray())) return 0;
            var none = SfxMods.None; SfxNative.PlayerSetLive(pl, ref none, intensity);
            if (loop) SfxNative.PlayerSetLoop(pl, true);
            before?.Invoke(pl);
            SfxNative.PlayerStart(pl, pattern);
            int bpm = song[2] << 7 | song[3], total = (int)Math.Ceiling(bars * song[1] * 60.0 / bpm / 4.0 * 48000.0);
            var l = new float[256]; var r = new float[256]; ulong h = 0xCBF29CE484222325UL;
            for (int done = 0; done < total; done += 256) { Array.Clear(l, 0, 256); Array.Clear(r, 0, 256); SfxNative.PlayerRender(pl, l, r, 256); h = Fnv(Fnv(h, l, 256), r, 256); }
            return h;
        }
        finally { Marshal.FreeHGlobal(pl); }
    }
    // (a) 16 bars from the top at intensity 1, (b) pattern F looped 4 bars, (c) 8 bars at intensity 0.4
    static ulong[] ThreeHashes(byte[] sounds, byte[] song) => new[] { RenderHash(sounds, song, 0, false, 1f, 16), RenderHash(sounds, song, 5, true, 1f, 4), RenderHash(sounds, song, 0, false, 0.4f, 8) };
    static string Hex(ulong[] h) => string.Join(" ", h.Select(x => x.ToString("X16")));

    // ---- a packed song of format 6 written here, by the layout in core/sfx_song.h (not by the C packer: a second pair of eyes).
    // head: 147 bytes of any format-5 or format-6 song. grids: 0, 1, 2 for each of the 8 patterns. A step: pattern, sound,
    // step, note, velocity, length. A point: pattern, lane, step, value.
    static byte[] Pack6(byte[] head, int[] grids, IEnumerable<(int p, int s, int i, int note, int vel, int len)> steps, IEnumerable<(int p, int l, int i, int v)> points = null)
    {
        var o = new List<byte>(head.Take(147)); o[0] = 6;
        for (int p = 0; p < 8; p++)
        {
            int g = 16 << grids[p], nb = (g + 6) / 7;
            var mine = steps.Where(x => x.p == p).ToList(); var pts = (points ?? new (int, int, int, int)[0]).Where(x => x.p == p).ToList();
            int rows = mine.Aggregate(0, (a, x) => a | 1 << x.s), lanes = pts.Aggregate(0, (a, x) => a | 1 << x.l);
            o.AddRange(new[] { (byte)grids[p], (byte)(rows & 127), (byte)(rows >> 7 & 127), (byte)(rows >> 14), (byte)(lanes & 127), (byte)(lanes >> 7 & 127), (byte)(lanes >> 14 & 127), (byte)(lanes >> 21) });
            for (int s = 0; s < 16; s++)
            {
                if ((rows >> s & 1) == 0) continue;
                var row = new byte[nb + 3 * g];
                for (int i = 0; i < g; i++) row[nb + g + i] = 5;
                foreach (var x in mine.Where(x => x.s == s)) { row[x.i / 7] |= (byte)(1 << x.i % 7); row[nb + x.i] = (byte)x.note; row[nb + g + x.i] = (byte)x.vel; row[nb + 2 * g + x.i] = (byte)x.len; }
                o.AddRange(row);
            }
            for (int l = 0; l < 24; l++)
            {
                if ((lanes >> l & 1) == 0) continue;
                var row = new byte[g];
                foreach (var x in pts.Where(x => x.l == l)) row[x.i] = (byte)(x.v + 1);
                o.AddRange(row);
            }
        }
        return o.ToArray();
    }
    // a head to build test songs on: the given song's, with this tempo and swing, no layers, no arrangement, a chain of `chain` patterns
    static byte[] PlainHead(byte[] song, int bpm, int swing, int chain)
    {
        var h = song.Take(147).ToArray();
        h[2] = (byte)(bpm >> 7); h[3] = (byte)(bpm & 127); h[4] = (byte)swing; h[6] = (byte)chain; h[66] = 0;
        for (int t = 0; t < 16; t++) h[7 + t] = 0;
        return h;
    }
    // the left channel of a song played from its pattern A, and (out) for every block of 256 samples the sounds that played in it
    static float[] RenderSong(byte[] sounds, byte[] song, double seconds, out uint[] playing, Action<IntPtr, int> eachBlock = null)
    {
        IntPtr pl = Marshal.AllocHGlobal(SfxNative.PlayerSize);
        try
        {
            SfxNative.PlayerInit(pl, 48000f);
            if (!SfxNative.PlayerSetSong(pl, song)) throw new Exception("the test's song was refused");
            for (int i = 0; i < 16; i++) SfxNative.PlayerSetPatch(pl, i, sounds.Skip(i * SfxPatch.Bytes).Take(SfxPatch.Bytes).ToArray());
            var none = SfxMods.None; SfxNative.PlayerSetLive(pl, ref none, 1f);
            SfxNative.PlayerStart(pl, 0);
            int blocks = (int)Math.Ceiling(seconds * 48000 / 256);
            var all = new float[blocks * 256]; playing = new uint[blocks]; var l = new float[256]; var r = new float[256];
            for (int b = 0; b < blocks; b++) { Array.Clear(l, 0, 256); Array.Clear(r, 0, 256); playing[b] = SfxNative.PlayerRender(pl, l, r, 256); Array.Copy(l, 0, all, b * 256, 256); eachBlock?.Invoke(pl, b); }
            return all;
        }
        finally { Marshal.FreeHGlobal(pl); }
    }
    // where sounds begin: much more in 2 ms than in the 9 ms before them (in seconds)
    static List<double> Onsets(float[] a, double from, double to)
    {
        var found = new List<double>();
        double E(int i0, int i1) { double s = 0; for (int i = Math.Max(0, i0); i < Math.Min(a.Length, i1); i++) s += a[i] * a[i]; return Math.Sqrt(s / Math.Max(1, i1 - i0)); }
        for (int i = (int)(from * 48000); i < (int)(to * 48000); i += 8)
        {
            double now = E(i, i + 96), before = E(i - 480, i - 48);
            if (now > 0.01 && now > before * 4) { found.Add(i / 48000.0); i += 960; }
        }
        return found;
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

        // ---- the identity: what a device says it is (firmware 1.4.0 on), and what is done with it
        {
            var ask = SfxProtocol.BuildRequestIdentity();
            Ok(ask.SequenceEqual(new byte[] { 0xF0, 0x7D, 0x56, 0x42, 0x07, 0xF7 }), "REQUEST IDENTITY is F0 7D 56 42 07 F7, six bytes");
            var fm = new SfxDeviceInfo { hardware = 1, major = 1, minor = 4, patch = 0, profile = SfxProfile.VibeFm15, flags = 15 };
            var id = SfxProtocol.BuildIdentity(fm);
            var want = new byte[] { 0xF0, 0x7D, 0x56, 0x42, 0x08, 1, 1, 4, 0, 1, 2, 5, 16, 2, 8, 16, 24, 32, 16, 5, 6, 25, 15, 0xF7 };
            Ok(id.SequenceEqual(want) && SfxProtocol.IdentityBytes == 18, "IDENTITY of firmware 1.4.0 on VIBE FM is F0 7D 56 42 08, these 18 bytes, F7: " + SfxLink.Describe(id));
            bool back = SfxProtocol.TryParseIdentity(id, out var got);
            Ok(back && got.Equals(fm) && got.profile == SfxProfile.VibeFm15 && got.HasPads && got.HasEncoders && got.TakesSongSounds && got.TakesTelemetry && got.IsAtLeast(1, 4, 0) && !got.IsAtLeast(1, 4, 1) && got.IsAtLeast(1, 3, 9),
               "an identity survives build -> parse: hardware, version, the profile firmware 1.4 and 1.5 said, all four flags");
            var fm16 = new SfxDeviceInfo { hardware = 1, major = 1, minor = 6, patch = 0, profile = SfxProfile.VibeFm1, flags = 15 };
            var id16 = SfxProtocol.BuildIdentity(fm16);
            var want16 = new byte[] { 0xF0, 0x7D, 0x56, 0x42, 0x08, 1, 1, 6, 0, 1, 2, 6, 16, 2, 8, 64, 24, 32, 16, 5, 6, 25, 15, 0xF7 };
            Ok(id16.SequenceEqual(want16) && SfxProtocol.TryParseIdentity(id16, out var got16) && got16.Equals(fm16) && got16.profile == SfxProfile.VibeFm1 && got16.profile.songFormat == 6 && got16.profile.steps == 64 && got16.IsAtLeast(1, 6, 0),
               "IDENTITY of firmware 1.6.0 says song format 6 and 64 steps, and survives build -> parse: " + SfxLink.Describe(id16));
            Ok(got.ToString() == "VIBE FM, firmware 1.4.0, profile 1", $"and reads \"{got}\"");
            bool everyField = true;
            for (int i = 0; i < 300; i++)
            {
                var d = new SfxDeviceInfo { hardware = rng.Next(128), major = rng.Next(128), minor = rng.Next(128), patch = rng.Next(128), flags = rng.Next(128) };
                d.profile = new SfxProfile { id = rng.Next(128), patchFormat = rng.Next(128), songFormat = rng.Next(128), sounds = rng.Next(128), kits = rng.Next(128), patterns = rng.Next(128), steps = rng.Next(128),
                                             lanes = rng.Next(128), bars = rng.Next(128), voices = rng.Next(128), engines = rng.Next(128), wavetables = rng.Next(128), patchValues = rng.Next(128) };
                everyField &= SfxProtocol.TryParseIdentity(SfxProtocol.BuildIdentity(d), out var d2) && d2.Equals(d);
            }
            Ok(everyField, "300 random identities survive build -> parse, field by field");
            var flagsOnly = fm; flagsOnly.flags = SfxDeviceInfo.FlagEncoders | SfxDeviceInfo.FlagTelemetry;
            Ok(!flagsOnly.HasPads && flagsOnly.HasEncoders && !flagsOnly.TakesSongSounds && flagsOnly.TakesTelemetry, "the flags are read bit by bit");

            var longer = id.Take(id.Length - 1).Concat(new byte[] { 9, 99, 0, 127, 0xF7 }).ToArray();
            Ok(SfxProtocol.TryParseIdentity(longer, out var fromLater) && fromLater.Equals(fm), "an identity with more bytes at its end (a later firmware) is read: the first 18, the rest ignored");
            var shorter = id.Take(5 + 17).Append((byte)0xF7).ToArray();
            Ok(shorter.Length == 6 + 17 && !SfxProtocol.TryParseIdentity(shorter, out _), "17 bytes are not an identity");
            bool over = true;
            for (int i = 5; i < id.Length - 1; i++) { var b2 = (byte[])id.Clone(); b2[i] = 0x80; over &= !SfxProtocol.TryParseIdentity(b2, out _); }
            var overLate = (byte[])longer.Clone(); overLate[overLate.Length - 3] = 200;
            Ok(over && !SfxProtocol.TryParseIdentity(overLate, out _), "a byte over 127 anywhere in it, also after the 18th, and it is not an identity");
            Ok(!SfxProtocol.TryParseIdentity(ask, out _) && !SfxProtocol.TryParseIdentity(SfxProtocol.BuildLoad(0, laser), out _) && !SfxProtocol.TryParseIdentity(null, out _)
               && !SfxProtocol.TryParseIdentity(new byte[] { 0xF0, 0x7D, 0x56, 0x4C, 8, 1, 1, 4, 0, 1, 2, 5, 16, 2, 8, 16, 24, 32, 16, 5, 6, 25, 15, 0xF7 }, out _),
               "a request, a patch, nothing, another device's SysEx: none is an identity");

            // an identity as long as a patch message (35) and as long as a song's piece (208) is neither
            byte[] Padded(int length) { var m = new byte[length]; Array.Copy(id, m, id.Length - 1); m[4] = SfxProtocol.Identity; m[length - 1] = 0xF7; return m; }
            var as35 = Padded(7 + SfxPatch.Bytes); var as208 = Padded(8 + SfxProtocol.OldSongPieceBytes); var as251 = Padded(11 + SfxProtocol.SongPieceBytes);
            var asPatch = SfxProtocol.BuildLoad(0, laser); asPatch[4] = SfxProtocol.Identity;            // a valid patch's bytes under the identity's command
            var noSong = new SfxSongTransfer();
            bool notPatch = !SfxProtocol.TryParsePatch(as35, out _, out _) && !SfxProtocol.TryParsePatch(asPatch, out _, out _) && !SfxProtocol.TryParsePatch(id, out _, out _);
            bool notPiece = !noSong.Take(as208) && !noSong.Take(as251) && !noSong.Take(as35) && !noSong.Take(id) && noSong.Song == null
                && !SfxSongTransfer.IsPiece(as208) && !SfxSongTransfer.IsPiece(as251) && !SfxSongTransfer.IsPiece(as35) && !SfxSongTransfer.IsPiece(id);
#pragma warning disable 618     // (the reader of the old shape alone, kept for those who call it: it says the same)
            var songBuf = new byte[SfxProtocol.SongBytesV5]; ulong seenBits = 0;
            notPiece &= !SfxProtocol.TryTakeSongChunk(as208, songBuf, ref seenBits) && !SfxProtocol.TryTakeSongChunk(as35, songBuf, ref seenBits) && !SfxProtocol.TryTakeSongChunk(id, songBuf, ref seenBits) && seenBits == 0 && songBuf.All(x => x == 0);
            notPiece &= SfxProtocol.SongBytes == 7699 && SfxProtocol.SongChunkBytes == 200 && SfxProtocol.SongChunks == 39;
#pragma warning restore 618
            Ok(as35.Length == 35 && as208.Length == 208 && as251.Length == 251 && notPatch && notPiece && SfxProtocol.TryParseIdentity(as35, out _) && SfxProtocol.TryParseIdentity(as208, out _) && SfxProtocol.TryParseIdentity(as251, out _),
               "an identity is neither a patch nor a piece of a song, also when it is 35, 208 or 251 bytes long");

            // the link: an identity in the middle of a song that is arriving
            using (var link = new SfxLink())
            {
                var theSong = new byte[SfxProtocol.SongBytesV5]; theSong[0] = 5; for (int i = SfxProtocol.SongHead; i < theSong.Length; i++) theSong[i] = (byte)(i * 7 % 128);
                theSong[1] = 16; theSong[3] = 120; theSong[6] = 1;                              // (a head the player takes - 16 sixteenths, 120 BPM, a chain of 1 -, and rows of anything)
                var pieces = SfxProtocol.BuildSong(theSong);
                int songs = 0, patches = 0, identities = 0; byte[] arrived = null; SfxDeviceInfo said = default;
                link.SongReceived += (s, _) => { songs++; arrived = s; };
                link.PatchReceived += (_, _) => patches++;
                link.IdentityReceived += d => { identities++; said = d; };
                Ok(link.Device == null && !link.IdentityUnanswered && link.LastRefusal == null, "a link that asked nothing knows no device, and does not say one failed to answer");
                for (int k = 0; k < pieces.Length; k++)
                {
                    if (k == 20) { link.Take(id); link.Take(id); link.Take(as208); link.Take(ask); }       // (the emulator delivers everything twice, and hands back what was sent)
                    link.Take(pieces[k]);
                }
                Ok(songs == 1 && arrived != null && arrived.SequenceEqual(theSong) && patches == 0 && identities == 3 && said.Equals(fm) && link.Device.HasValue && link.Device.Value.Equals(fm),
                   "identities (twice, and a long one) and a request come back in the middle of a song: the song arrives whole, once; no patch; the device is known");
                Ok(link.SendPatch(0, laser) == false && link.LastRefusal == null && !link.SendSong(new SfxPatch[16], theSong) && link.LastRefusal == null,
                   "a device that speaks song format 5 is refused no patch and no format-5 song (without a port nothing is sent, and that is no refusal)");

                var later = fm; later.major = 2; later.minor = 0; later.profile.patchFormat = 3;
                link.Take(SfxProtocol.BuildIdentity(later));
                bool refusedPatch = !link.SendPatch(0, laser); string whyPatch = link.LastRefusal;
                bool refusedSong = !link.SendSong(new SfxPatch[16], theSong); string whySong = link.LastRefusal;
                Ok(refusedPatch && whyPatch != null && whyPatch.Contains("patch format 3") && whyPatch.Contains("update the package") && refusedSong && whySong != null,
                   $"a device that says patch format 3 is sent no patch and no song: \"{whyPatch}\"");
                var olderSong = fm; olderSong.profile.songFormat = 4;
                link.Take(SfxProtocol.BuildIdentity(olderSong));
                bool patchGoes = !link.SendPatch(0, laser) && link.LastRefusal == null;
                refusedSong = !link.SendSong(new SfxPatch[16], theSong); whySong = link.LastRefusal;
                Ok(patchGoes && refusedSong && whySong != null && whySong.Contains("song format 4") && whySong.Contains("update the device's firmware"),
                   $"a device that says song format 4 is sent patches but no song: \"{whySong}\"");

                // asked again: what the device said before is forgotten, and a second without an answer says so
                bool knownBefore = link.Device.HasValue && SfxLink.Refusal(link.Device, true) != null;
                link.Asked();
                bool forgotten = link.Device == null && !link.IdentityUnanswered && SfxLink.Refusal(link.Device, true) == null;
                System.Threading.Thread.Sleep((int)(SfxLink.IdentityAnswerSeconds * 1000) + 100);
                bool unanswered = link.IdentityUnanswered;
                link.Take(id);
                Ok(knownBefore && forgotten && unanswered && !link.IdentityUnanswered && link.Device.Value.Equals(fm),
                   "asking again forgets the earlier answer (and its refusal); a second without one reads as unanswered; the next answer is taken");

                // a patch and a song in a higher format come in: newer is said, once each, and nothing of them is taken
                var said2 = new List<string>();
                link.NewerReceived += said2.Add;
                var newerDump = SfxProtocol.BuildDump(3, laser); newerDump[6] = 3;
                var cutDump = newerDump.Take(20).Append((byte)0xF7).ToArray();
                int patchesBefore = patches, songsBefore = songs;
                link.Take(newerDump); link.Take(newerDump); link.Take(cutDump); link.Take(SfxProtocol.BuildDump(3, laser));
                Ok(said2.Count == 1 && said2[0].Contains("a patch in format 3") && said2[0].Contains("knows up to 2") && patches == patchesBefore + 1,
                   $"a dump in patch format 3, delivered twice, is said to be newer once and not taken; a cut-off one says nothing; this version's is taken: \"{(said2.Count > 0 ? said2[0] : "")}\"");
                var laterSong = new byte[600]; laterSong[0] = 7; for (int i = 1; i < laterSong.Length; i++) laterSong[i] = (byte)(i * 5 % 128);
                foreach (var piece in SfxProtocol.BuildPieces(laterSong)) { link.Take(piece); link.Take(piece); }
                var laterOld = (byte[])theSong.Clone(); laterOld[0] = 7;                      // (and the same format in the message of before: said within the same two seconds, so not said again)
                foreach (var piece in SfxProtocol.BuildSong(laterOld)) { link.Take(piece); link.Take(piece); }
                Ok(said2.Count == 2 && said2[1].Contains("a song in format 7") && said2[1].Contains("knows up to 6") && songs == songsBefore,
                   $"a song in format 7 in the message of firmware 1.6, every piece twice - and once more in the message of before - is said to be newer once and does not arrive as a song: \"{(said2.Count > 1 ? said2[1] : "")}\"");
                foreach (var piece in pieces) link.Take(piece);
                Ok(songs == songsBefore + 1 && arrived.SequenceEqual(theSong) && said2.Count == 2, "and this version's song after it arrives whole");

                link.Close();
                Ok(link.Device == null && link.LastRefusal == null, "closing the link forgets the device");
            }
            Ok(SfxLink.Refusal(null, true) == null && SfxLink.Refusal(null, false) == null && SfxLink.Refusal(SfxDeviceInfo.AssumedBefore14, true) == null,
               "a device that never answered is refused nothing: it is taken for a firmware before 1.4");
            var assumed = SfxDeviceInfo.AssumedBefore14;
            Ok(assumed.ToString() == "VIBE FM, firmware before 1.4, profile 1" && assumed.profile == SfxProfile.VibeFm15 && assumed.profile.songFormat == 5 && assumed.profile.steps == 16 && !assumed.IsAtLeast(1, 0, 0) && assumed.HasPads && assumed.HasEncoders && assumed.TakesTelemetry && !assumed.TakesSongSounds,
               $"and reads \"{assumed}\" (whether it takes a song's sounds is not known)");

            // newer is not broken
            var newerPatch = laser.Pack(); newerPatch[0] = 3; newerPatch[SfxPatch.Bytes - 1] = SfxPatch.Checksum(newerPatch, 0);
            var brokenPatch = laser.Pack(); brokenPatch[5] ^= 1;
            var olderPatch = laser.Pack(); olderPatch[0] = 1;
            var inMessage = SfxProtocol.BuildLoad(2, laser); inMessage[6] = 9;
            Ok(SfxPatch.IsNewer(newerPatch, 0) && !SfxPatch.TryUnpack(newerPatch, 0, out _) && !SfxPatch.IsNewer(laser.Pack(), 0) && !SfxPatch.IsNewer(brokenPatch, 0) && !SfxPatch.IsNewer(olderPatch, 0)
               && SfxPatch.IsNewer(inMessage, 6) && !SfxPatch.IsNewer(inMessage, 7) && !SfxPatch.IsNewer(null, 0) && !SfxPatch.IsNewer(newerPatch, 28) && !SfxPatch.IsNewer(newerPatch, -1) && !SfxPatch.IsNewer(new byte[] { 0xF0 }, 0),
               "a patch with a higher version byte is newer (and not unpacked); this version's, a damaged one, an older one, a status byte are not");
            var longerPatch = newerPatch.Concat(new byte[4]).ToArray();
            Ok(!SfxPatch.IsNewer(new byte[] { 9 }, 0) && !SfxPatch.IsNewer(newerPatch.Take(27).ToArray(), 0) && !SfxPatch.IsNewer(newerPatch, 1) && SfxPatch.IsNewer(longerPatch, 0) && SfxPatch.IsNewer(longerPatch, 4) == (longerPatch[4] > 2),
               "fewer than 28 bytes are a cut-off patch, not a newer one, whatever they begin with; more than 28 may be one");
            var song7 = new byte[SfxProtocol.SongBytesMin]; song7[0] = 7;
            var song6 = new byte[SfxProtocol.SongBytesV5]; song6[0] = 6;
            var song5 = new byte[SfxProtocol.SongBytesV5]; song5[0] = 5;
            Ok(SfxProtocol.IsNewerSong(song7) && SfxProtocol.IsNewerSong(song7.Concat(new byte[9000]).ToArray()) && !SfxProtocol.IsNewerSong(song7.Take(210).ToArray()) && !SfxProtocol.IsNewerSong(song6) && !SfxProtocol.IsNewerSong(song5)
               && !SfxProtocol.IsNewerSong(new byte[] { 0x40 }) && !SfxProtocol.IsNewerSong(null) && !SfxProtocol.IsNewerSong(new byte[0]) && SfxProtocol.SongVersion == 6 && SfxProtocol.SongBytesMin == 211
               && SfxProtocol.NewerIn(SfxProtocol.BuildLoad(0, laser)) == null && SfxProtocol.NewerIn(SfxProtocol.BuildRequestIdentity()) == null && SfxProtocol.NewerIn(null) == null,
               "a packed song of a format above 6 and at least 211 bytes (the shortest song there is) is newer; 210 bytes, a 6, a 5, one byte are not");
            {
                var long7 = new byte[129 * 240 + 7]; long7[0] = 7;                             // 130 pieces: piece 128 has a 0 where piece 0 has one
                var p7 = SfxProtocol.BuildPieces(long7); var p6 = SfxProtocol.BuildPieces(song6);
                var old7 = (byte[])song5.Clone(); old7[0] = 7; var o7 = SfxProtocol.BuildSong(old7); var o5 = SfxProtocol.BuildSong(song5);
                Ok(p7.Length == 130 && SfxProtocol.NewerIn(p7[0]) == "a song in format 7" && SfxProtocol.NewerIn(o7[0]) == "a song in format 7" && p7[128][5] == 0 && p7[128][6] == 1 && SfxProtocol.NewerIn(p7[128]) == null
                   && p7.Skip(1).All(m => SfxProtocol.NewerIn(m) == null) && o7.Skip(1).All(m => SfxProtocol.NewerIn(m) == null) && p6.All(m => SfxProtocol.NewerIn(m) == null) && o5.All(m => SfxProtocol.NewerIn(m) == null),
                   "the first piece of a song in format 7 says so, in the message of firmware 1.6 and in the one before; no other piece does - not piece 129 of a long song either - and no piece of a format-6 or format-5 song");
            }

            // the profile
            var p1 = SfxProfile.VibeFm1;
            Ok(p1.id == 1 && p1.patchFormat == 2 && p1.songFormat == 6 && p1.sounds == 16 && p1.kits == 2 && p1.patterns == 8 && p1.steps == 64 && p1.lanes == 24 && p1.bars == 32 && p1.voices == 16
               && p1.engines == 5 && p1.wavetables == 6 && p1.patchValues == 25, "the profile of VIBE FM is made of the package's constants: " + p1);
            var p15 = SfxProfile.VibeFm15; var p15as1 = p15; p15as1.songFormat = 6; p15as1.steps = 64;
            Ok(p15.songFormat == 5 && p15.steps == 16 && p15 != p1 && p15as1 == p1 && SfxProtocol.Steps == 16 && SfxProtocol.GridSteps == 64,
               "and what firmware 1.4 and 1.5 said is the same but for song format 5 and 16 steps: " + p15);
            bool allFit = true;
            for (int i = 0; i < 500; i++)
            {
                var p = new SfxPatch { engine = (SfxEngine)rng.Next(SfxPatch.EngineCount) };
                for (int k = 0; k < SfxPatch.ParamCount; k++) p[(SfxParam)k] = (byte)rng.Next(128);
                allFit &= p1.Fits(p);
            }
            var fewer = p1; fewer.engines = 4;
            var otherFormat = p1; otherFormat.patchFormat = 3;
            var raw = laser; raw.filterCutoff = 200;                                             // (set past the indexer, which cuts to 127)
            Ok(allFit && p1.Fits(laser) && fewer.Fits(laser) && !fewer.Fits(SfxPatch.Create(SfxEngine.Wavetable)) && !otherFormat.Fits(laser) && !p1.Fits(raw) && !p1.Fits(new SfxPatch { engine = (SfxEngine)7 }),
               "every patch of this version fits it; not one with an engine the device lacks, another format, or a value over 127");
            var s5 = new byte[SfxProtocol.SongBytesV5]; s5[0] = 5; s5[1] = 16; s5[3] = 120; s5[6] = 1; s5[66] = 32;      // (16 sixteenths, 120 BPM, a chain of 1, 32 bars: a song the player takes)
            var s7 = new byte[SfxProtocol.SongBytesMin]; s7[0] = 7; s7[1] = 16;
            var tooLong = (byte[])s5.Clone(); tooLong[1] = 17;
            var noBar = (byte[])s5.Clone(); noBar[1] = 0;
            var tooManyBars = (byte[])s5.Clone(); tooManyBars[66] = 33;
            var highByte = (byte[])s5.Clone(); highByte[5000] = 128;
            var s4 = new byte[SfxProtocol.SongBytesV4]; s4[0] = 4; s4[1] = 16; s4[3] = 120; s4[6] = 1; s4[82] = 8;
            var s1 = new byte[SfxProtocol.SongBytesV1]; s1[0] = 1; s1[1] = 16; s1[3] = 120; s1[6] = 1;
            var wrongLength = new byte[SfxProtocol.SongBytesV3]; wrongLength[0] = 5;
            var shortBars = p1; shortBars.bars = 4;
            var onlyOld = p1; onlyOld.songFormat = 4;
            Ok(p1.Fits(s5) && p1.Fits(s4) && p1.Fits(s1) && !p1.Fits(s7) && !p1.Fits(tooLong) && !p1.Fits(noBar) && !p1.Fits(tooManyBars) && !p1.Fits(highByte) && !p1.Fits(wrongLength) && !p1.Fits((byte[])null) && !p1.Fits(new byte[0])
               && !shortBars.Fits(s5) && !shortBars.Fits(s4) && shortBars.Fits(s1) && !onlyOld.Fits(s5) && onlyOld.Fits(s4) && p15.Fits(s5) && p15.Fits(s4),
               "a packed song fits it in every format up to its own; not a newer one, a wrong length, a byte over 127, a bar of 0 or 17 sixteenths (a format-5 song, a profile of 64 steps), more bars than the device holds");
            // format 6: an empty song is its head and eight empty blocks; a block's first byte is its pattern's grid
            var s6 = new byte[SfxProtocol.SongBytesMin]; s6[0] = 6; s6[1] = 16; s6[3] = 120; s6[6] = 1;
            var s6fine = (byte[])s6.Clone(); s6fine[SfxProtocol.SongHead + 3 * 8] = 1;                        // pattern D: 32 steps
            var s6finest = (byte[])s6.Clone(); s6finest[SfxProtocol.SongHead + 7 * 8] = 2;                    // pattern H: 64
            var six16 = p1; six16.steps = 16;                                                               // (a device that speaks format 6 and holds 16 steps a pattern)
            var six32 = p1; six32.steps = 32;
            var s6bar17 = (byte[])s6.Clone(); s6bar17[1] = 17;
            var s6cut = s6.Take(210).ToArray(); var s6bars = (byte[])s6.Clone(); s6bars[66] = 33;
            Ok(p1.Fits(s6) && p1.Fits(s6fine) && p1.Fits(s6finest) && six16.Fits(s6) && !six16.Fits(s6fine) && !six16.Fits(s6finest) && six32.Fits(s6fine) && !six32.Fits(s6finest)
               && !p15.Fits(s6) && !p15.Fits(s6fine) && !p1.Fits(s6bar17) && !p1.Fits(s6cut) && !p1.Fits(s6bars) && !shortBars.Fits(s6bars) && shortBars.Fits(s6),
               "a format-6 song fits the profile of 1.6; a pattern of 32 or 64 steps does not fit a profile that holds 16; no format-6 song fits what 1.4 and 1.5 said; a bar of 17, a cut-off one, 33 bars do not fit");
            Ok(SfxPackedSong.Format(s6) == 6 && SfxPackedSong.Grid(s6, 0) == 16 && SfxPackedSong.Grid(s6fine, 3) == 32 && SfxPackedSong.Div(s6fine, 3) == 2 && SfxPackedSong.Grid(s6finest, 7) == 64 && SfxPackedSong.Grid(s6finest, 6) == 16
               && SfxPackedSong.Grid(s6, 8) == 0 && SfxPackedSong.Grid(s6, -1) == 0 && SfxPackedSong.Format(s6cut) == 0 && SfxPackedSong.Grid(s6cut, 0) == 0 && SfxPackedSong.Format(s5) == 5 && SfxPackedSong.Grid(s5, 7) == 16
               && SfxPackedSong.Format(s7) == 0 && SfxPackedSong.Format(null) == 0 && !SfxPackedSong.StepOn(s6, 0, 0, 0) && SfxPackedSong.StepVelocity(s6, 0, 0, 0) == 5 && SfxPackedSong.LanePoint(s6, 0, 0, 0) == -1
               && SfxPackedSong.StepNote(s6, 0, 0, 99) == 0 && SfxPackedSong.StepLength(s6finest, 7, 15, 63) == 0,
               "SfxPackedSong: the format and each pattern's grid of a packed song; out of range, or not a song: 0, false, velocity 5, no point");

            // ---- what is sent to whom: the refusals, in words
            {
                var dev15 = new SfxDeviceInfo { hardware = 1, major = 1, minor = 5, patch = 0, profile = SfxProfile.VibeFm15, flags = 15 };
                var dev16 = fm16;
                var dev9 = fm16; dev9.major = 9; dev9.profile.songFormat = 9;
                var garbage = new byte[300]; garbage[0] = 3;
                string r15 = SfxLink.Refusal(dev15, s6), rNone = SfxLink.Refusal(null, s6), r7 = SfxLink.Refusal(dev16, s7), rBad = SfxLink.Refusal(dev16, garbage), r9 = SfxLink.Refusal(dev9, s6), r4on15 = SfxLink.Refusal(dev15, s4);
                var old5 = SfxLink.SongMessages(dev15, s5); var none5 = SfxLink.SongMessages(null, s5); var new5 = SfxLink.SongMessages(dev16, s5); var new6 = SfxLink.SongMessages(dev16, s6fine);
                bool oldShape(byte[][] ms) => ms.Length == 39 && ms.All(m => m[6] == 39 && m.Length <= 208);
                bool newShape(byte[][] ms, byte[] song) => ms.Length == (song.Length + 239) / 240 && ms.Select((m, k) => (m[5] | m[6] << 7) == k && m[7] == song[0] && (m[8] | m[9] << 7) == ms.Length && m.Length <= 251).All(x => x);
                Ok(SfxLink.Refusal(dev15, s5) == null && oldShape(old5) && SfxLink.Refusal(dev15, true) == null,
                   "a device that said 1.5 (song format 5) is sent a format-5 song in 16 + 39 messages, the song's in the shape it knows");
                Ok(r15 != null && r15.Contains("speaks song format 5") && r15.Contains("the song is in format 6") && r15.Contains("update the device's firmware"), $"and is refused a format-6 song: \"{r15}\"");
                Ok(r4on15 != null && r4on15.Contains("speaks song format 5") && r4on15.Contains("fetch the song again"), $"and a song older than its own, which never travelled in pieces: \"{r4on15}\"");
                Ok(SfxLink.Refusal(dev16, s5) == null && SfxLink.Refusal(dev16, s6) == null && SfxLink.Refusal(dev16, s6fine) == null && SfxLink.Refusal(dev16, s4) == null && newShape(new5, s5) && new5.Length == 33 && newShape(new6, s6fine) && new6.Length == 1,
                   "a device that said 1.6 (song format 6) is sent a song of any format this version reads, in the message of 1.6: a format-5 song in 33 pieces, an empty format-6 one in 1");
                Ok(SfxLink.Refusal(null, s5) == null && oldShape(none5) && rNone != null && rNone.Contains("did not say") && rNone.Contains("song format 6") && SfxLink.Refusal(SfxDeviceInfo.AssumedBefore14, s5) == null,
                   $"a device that said nothing is sent a format-5 song in the shape of before, and no format-6 song - an older firmware would drop its pieces without a word: \"{rNone}\"");
                Ok(r7 != null && r7.Contains("the song is in format 7") && r7.Contains("knows up to 6") && r7.Contains("update the package") && SfxLink.Refusal(null, s7) == r7, $"a song in format 7 goes nowhere: \"{r7}\"");
                Ok(rBad == "this is not a packed song" && SfxLink.Refusal(null, garbage) == rBad && SfxLink.Refusal(dev16, (byte[])null) == rBad && SfxLink.Refusal(null, new byte[0]) == rBad, "bytes that are no song are refused in words, without an exception");
                var swing101 = (byte[])s6.Clone(); swing101[4] = 101; var chain0 = (byte[])s5.Clone(); chain0[6] = 0;
                string rRange = SfxLink.Refusal(dev16, swing101);
                Ok(SfxPackedSong.IsPlayable(s6) && SfxPackedSong.IsPlayable(s5) && SfxPackedSong.IsPlayable(s4) && SfxPackedSong.IsPlayable(s1) && SfxPackedSong.IsValid(swing101) && rRange != null && rRange.Contains("out of range") && rRange.Contains("BAD SONG")
                   && SfxLink.Refusal(dev15, chain0) == rRange && SfxLink.Refusal(null, chain0) == rRange && SfxLink.Refusal(null, swing101) == rRange,
                   $"a song with a value the device would not take (a swing of 101, a chain of 0) goes to no device, whatever it speaks: \"{rRange}\"");
                Ok(r9 != null && r9.Contains("speaks song format 9") && r9.Contains("update the package") && SfxLink.Refusal(dev9, true) == r9, $"a device that speaks a song format above this version's is sent no song: \"{r9}\"");
                using (var link = new SfxLink())
                {
                    link.Take(SfxProtocol.BuildIdentity(dev15));
                    bool a = !link.SendSong(new SfxPatch[16], s6) && link.LastRefusal == r15, b = !link.SendSong(new SfxPatch[16], s5) && link.LastRefusal == null;
                    bool c = !link.SendSong(new SfxPatch[16], garbage) && link.LastRefusal == "this is not a packed song" && !link.SendSong(new SfxPatch[16], null) && link.LastRefusal == "this is not a packed song";
                    link.Asked();
                    bool waiting = !link.SendSong(new SfxPatch[16], s6) && link.LastRefusal != null && link.LastRefusal.Contains("did not say");
                    System.Threading.Thread.Sleep((int)(SfxLink.IdentityAnswerSeconds * 1000) + 100);
                    bool silent = link.IdentityUnanswered && !link.SendSong(new SfxPatch[16], s6) && link.LastRefusal != null && link.LastRefusal.Contains("did not say") && !link.SendSong(new SfxPatch[16], s5) && link.LastRefusal == null;
                    link.Take(SfxProtocol.BuildIdentity(dev16));
                    bool now = !link.SendSong(new SfxPatch[16], s6) && link.LastRefusal == null && !link.SendSong(new SfxPatch[16], s5) && link.LastRefusal == null;
                    Ok(a && b && c && waiting && silent && now, "SendSong says the same: LastRefusal for a 1.5 device and a format-6 song, for one that has not answered, for one that never will, for bytes that are no song; none for a format-5 song, and none once the device said 1.6");
                }
            }
        }

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

        // ---- the tools: generators
        {
            bool fits = true, again = true, both = true; int fewest = int.MaxValue;
            for (int c = 0; c < SfxGenerator.Count; c++)
            {
                var cat = (SfxCategory)c; var engines = new HashSet<SfxEngine>(); int differ = 0; SfxPatch before = default;
                for (uint seed = 1; seed <= 300; seed++)
                {
                    SfxPatch p = SfxGenerator.Make(cat, seed);
                    fits &= SfxGenerator.Fits(cat, p) && SfxPatch.TryUnpack(p.Pack(), 0, out var back) && back.Equals(p) && p.level == 100 && p.IsSustained == SfxGenerator.IsSustained(cat);
                    again &= p.Equals(SfxGenerator.Make(cat, seed));
                    if (seed > 1 && !p.Equals(before)) differ++;
                    before = p; engines.Add(p.engine);
                }
                fewest = Math.Min(fewest, differ); both &= engines.Count == 2;
            }
            Ok(fits && SfxGenerator.Names.Length == SfxGenerator.Count && Enum.GetValues(typeof(SfxCategory)).Length == SfxGenerator.Count, "every category: 300 seeds give a valid patch inside its ranges");
            Ok(again && fewest >= 290, $"a seed gives the same sound again, another seed another (at least {fewest} of 299 neighbours differ)");
            Ok(both, "both recipes of every category come up");
            var one = new SfxRandom(0x3F2A91C4u); var two = new SfxRandom(0x3F2A91C4u);
            Ok(SfxGenerator.Make(SfxCategory.Zap, ref one).Equals(SfxGenerator.Make(SfxCategory.Zap, 0x3F2A91C4u)) && !SfxGenerator.Fits(SfxCategory.Hum, SfxGenerator.Make(SfxCategory.Chime, ref two)),
               "a generator gives what its seed gives; a coin does not fit the hum's ranges");
        }

        // ---- the tools: mutate with an amount, locks, variants
        {
            bool inside = true;
            foreach (int step in new[] { 1, 19, 64, 127 })
            {
                var mr = new SfxRandom((uint)(1000 + step));
                for (int i = 0; i < 300; i++)
                {
                    var a = laser; a.Mutate(ref mr, step);
                    for (int k = 0; k < SfxPatch.ParamCount; k++) inside &= Math.Abs(a[(SfxParam)k] - laser[(SfxParam)k]) <= step;
                    inside &= a.engine == laser.engine && a.sustain == 0 && a.level == laser.level && a.table == laser.table;
                    var h = hum; h.Mutate(ref mr, step); inside &= h.sustain > 0 && h.level == hum.level;
                }
            }
            var zero = new SfxRandom(5); var still = laser; still.Mutate(ref zero, 0);
            Ok(inside && still.Equals(laser), "Mutate with an amount stays inside it and keeps the kind of sound");

            bool locks = true;
            for (uint seed = 1; seed <= 200; seed++)
            {
                var free = new SfxRandom(seed); var held = new SfxRandom(seed);
                var a = laser; a.Mutate(ref free, 40);
                var b = laser; b.Mutate(ref held, 40, SfxMutate.Bit(SfxParam.BaseFreq) | SfxMutate.Bit(SfxParam.AmpDecay));
                locks &= b.baseFreq == laser.baseFreq && b.ampDecay == laser.ampDecay;
                for (int k = 0; k < SfxPatch.ParamCount; k++) if (k != (int)SfxParam.BaseFreq && k != (int)SfxParam.AmpDecay) locks &= a[(SfxParam)k] == b[(SfxParam)k];
                locks &= free.Next() == held.Next();
            }
            Ok(locks, "a locked value does not move, and locking it changes no other value");
            Ok(SfxMutate.Variant(laser, 77, 3, 100, SfxMutate.All).Equals(laser) && SfxMutate.All == 0x1FFFFFF && !SfxMutate.Variant(laser, 77, 3, 100).Equals(laser), "all values locked: the variant is the base");
            SfxPatch third = SfxMutate.Variant(laser, 42, 3, 15);
            for (int n = 8; n >= 1; n--) SfxMutate.Variant(laser, 42, n, 15);
            Ok(SfxMutate.Variant(laser, 42, 0, 15).Equals(laser) && SfxMutate.Variant(laser, 42, 3, 15).Equals(third) && !SfxMutate.Variant(laser, 43, 3, 15).Equals(third) && !SfxMutate.Variant(laser, 42, 4, 15).Equals(third),
               "variant 0 is the base; variant n depends on seed and n only");
            Ok(SfxPatch.StepOf(0) == 0 && SfxPatch.StepOf(1) == 1 && SfxPatch.StepOf(15) == 19 && SfxPatch.StepOf(100) == 127 && SfxPatch.StepOf(-3) == 0 && SfxPatch.StepOf(500) == 127, "StepOf: 0 -> 0, 1 -> 1, 15 -> 19, 100 -> 127");
            var dev = new SfxRandom(1);
            Ok(dev.Next() == 270369u && new SfxRandom(0).Next() == new SfxRandom(0x9E3779B9u).Next() && SfxRandom.Mix(0, 0) != 0 && SfxRandom.Mix(1, 2) != SfxRandom.Mix(2, 1),
               "SfxRandom is the device's xorshift32 (seed 1 gives 270369), 0 is taken as its constant");
        }

        // ---- the tools: a variation set's order and stamp
        {
            bool never = true, all = true, same = true, single = true;
            foreach (int count in new[] { 1, 2, 5, 16 })
                foreach (int noRepeat in new[] { 0, 1, 3, 20 })
                {
                    var order = new SfxShuffle(count, noRepeat, 9); var twin = new SfxShuffle(count, noRepeat, 9);
                    int avoid = Math.Min(noRepeat, count - 1); var history = new List<int>(); var seen = new HashSet<int>();
                    never &= order.Last == -1;
                    for (int i = 0; i < 2000; i++)
                    {
                        int at = order.Next();
                        same &= at == twin.Next() && order.Last == at;
                        never &= at >= 0 && at < count;
                        for (int k = 1; k <= avoid && k <= history.Count; k++) never &= history[history.Count - k] != at;
                        history.Add(at); seen.Add(at);
                        if (count == 1) single &= at == 0;
                    }
                    all &= seen.Count == count;
                }
            Ok(never && all && same && single, "shuffle: never one of the last N, every variant comes up, the same seed the same order");
            var even = new SfxShuffle(6, 2, 1234); var hits = new int[6];
            for (int i = 0; i < 12000; i++) hits[even.Next()]++;
            float unit = even.NextUnit();
            Ok(hits.All(h => h >= 1700 && h <= 2300) && unit >= 0f && unit < 1f && even.NoRepeat == 2 && new SfxShuffle(3, 9, 1).NoRepeat == 2 && new SfxShuffle(3, -1, 1).NoRepeat == 0, "shuffle: uniform over what is allowed (" + string.Join(" ", hits) + ")");

            byte[] packed = laser.Pack(); byte[] other = hum.Pack();
            int stamp = SfxMutate.Stamp(packed, 150, 1, 6, 10, 0, false);
            bool changes = stamp != 0 && stamp == SfxMutate.Stamp(laser.Pack(), 150, 1, 6, 10, 0, false)
                && stamp != SfxMutate.Stamp(other, 150, 1, 6, 10, 0, false) && stamp != SfxMutate.Stamp(packed, 151, 1, 6, 10, 0, false) && stamp != SfxMutate.Stamp(packed, 150, 2, 6, 10, 0, false)
                && stamp != SfxMutate.Stamp(packed, 150, 1, 7, 10, 0, false) && stamp != SfxMutate.Stamp(packed, 150, 1, 6, 11, 0, false) && stamp != SfxMutate.Stamp(packed, 150, 1, 6, 10, 1, false)
                && stamp != SfxMutate.Stamp(packed, 150, 1, 6, 10, 0, true);
            Ok(changes, "the stamp changes with every input and with nothing else");
            bool kept = true;
            var six = Enumerable.Range(0, 6).Select(n => SfxMutate.Variant(thud, 5, n, 10)).ToArray();
            var nine = Enumerable.Range(0, 9).Select(n => SfxMutate.Variant(thud, 5, n, 10)).ToArray();
            for (int n = 0; n < 6; n++) kept &= six[n].Equals(nine[n]);
            Ok(kept && nine.Distinct().Count() > 6, "raising the count keeps the earlier variants");
        }

        // ---- the tools: levels
        {
            var sine = new float[48000]; for (int i = 0; i < sine.Length; i++) sine[i] = (float)Math.Sin(2 * Math.PI * 440 * i / 48000.0);
            var full = SfxLevels.Measure(sine, null); var quiet = SfxLevels.Measure(new float[1000], new float[1000]);
            var over = (float[])sine.Clone(); for (int i = 0; i < over.Length; i++) over[i] *= 1.01f;
            Ok(Math.Abs(full.PeakDb) < 0.01f && Math.Abs(full.RmsDb + 3.01f) < 0.01f && !full.Clips && !full.Silent && quiet.PeakDb == SfxLevels.Floor && quiet.RmsDb == SfxLevels.Floor && quiet.Silent
               && SfxLevels.Measure(over, over).Clips && Math.Abs(SfxLevels.Measure(sine, sine).RmsDb - full.RmsDb) < 0.001f && SfxLevels.FromDb(SfxLevels.ToDb(0.25f)) is float q && Math.Abs(q - 0.25f) < 1e-6f,
               $"levels: a full-scale sine reads peak 0 dB and RMS -3.01 dB ({full.PeakDb:0.00} / {full.RmsDb:0.00}); silence reads -120 and is Silent; 1.01 clips");
            var padded = new float[sine.Length + 30000]; Array.Copy(sine, 0, padded, 10000, sine.Length);
            var half = (float[])padded.Clone(); for (int i = 0; i < half.Length; i++) half[i] *= 0.5f;
            Ok(Math.Abs(SfxLevels.Measure(padded, null).RmsDb - full.RmsDb) < 0.001f && Math.Abs(SfxLevels.Measure(half, null).RmsDb - (full.RmsDb - 6.0206f)) < 0.001f && SfxLevels.Measure(padded, null, 5000).Silent,
               "the body ignores the silence around a sound");
            bool inverse = SfxLevels.LevelFor(100, SfxLevels.LevelDb(80)) == 80 && SfxLevels.LevelFor(100, 40f) == 127 && SfxLevels.LevelFor(100, -200f) == 1 && SfxLevels.LevelFor(0, 12f) == 0
                && SfxLevels.LevelDb(100) == 0f && Math.Abs(SfxLevels.LevelDb(127) - 4.152f) < 0.01f && SfxLevels.LevelDb(0) == SfxLevels.Floor;
            for (int a = 1; a <= 127; a++) for (int b = 1; b <= 127; b++) inverse &= SfxLevels.LevelFor(a, SfxLevels.LevelDb(b) - SfxLevels.LevelDb(a)) == b;
            Ok(inverse, "LevelDb and LevelFor are each other's inverse");

            SfxLevelReading Reading(float peakDb, float rmsDb) => new SfxLevelReading { peak = SfxLevels.FromDb(peakDb), rms = SfxLevels.FromDb(rmsDb), frames = 1000 };
            bool match = true;
            foreach (SfxLevelMeasure by in new[] { SfxLevelMeasure.Rms, SfxLevelMeasure.Peak })
            {
                int[] levels = { 100, 90, 127, 60, 100, 0 }; var now = new int[6];
                SfxLevelReading[] reads = { Reading(-3f, -14f), Reading(-9f, -21f), Reading(-1f, -9f), Reading(-20f, -30f), new SfxLevelReading(), Reading(-6f, -12f) };
                float target = SfxLevels.Match(levels, reads, by, now);
                match &= now[4] == 100 && now[5] == 0 && !float.IsNaN(target);
                for (int i = 0; i < 4; i++)
                {
                    float change = SfxLevels.LevelDb(now[i]) - SfxLevels.LevelDb(levels[i]);
                    float reading = (by == SfxLevelMeasure.Rms ? reads[i].RmsDb : reads[i].PeakDb) + change;
                    match &= now[i] >= 1 && now[i] <= 127 && reads[i].PeakDb + change <= 0.0001f && Math.Abs(reading - target) < 0.5f;
                }
                float wanted = SfxLevels.Match(levels, reads, by, now, -40f);
                match &= Math.Abs(wanted + 40f) < 0.001f && now[0] < 100 && SfxLevels.Match(levels, reads, by, now, 30f) <= target + 0.001f;
            }
            Ok(match && float.IsNaN(SfxLevels.Match(new[] { 0 }, new[] { Reading(-6f, -12f) }, SfxLevelMeasure.Rms, new int[1])), "match: the target is reachable by all, nobody passes LEVEL 127 or 0 dBFS");

            // a wanted level below what LEVEL 1 reaches: what comes back is what the sounds get; a reading that is no number changes nothing
            int[] lowLevels = { 100, 3 }; var lowNow = new int[2];
            SfxLevelReading[] lowReads = { Reading(-3f, -14f), Reading(-30f, -33f) };
            float got70 = SfxLevels.Match(lowLevels, lowReads, SfxLevelMeasure.Rms, lowNow, -70f);
            float readA = lowReads[0].RmsDb + SfxLevels.LevelDb(lowNow[0]) - SfxLevels.LevelDb(100), readB = lowReads[1].RmsDb + SfxLevels.LevelDb(lowNow[1]) - SfxLevels.LevelDb(3);
            int[] nanNow = new int[3];
            float nanTarget = SfxLevels.Match(new[] { 100, 90, 80 }, new[] { Reading(-3f, -14f), new SfxLevelReading { peak = 0.5f, rms = float.NaN }, Reading(-9f, -20f) }, SfxLevelMeasure.Rms, nanNow);
            Ok(got70 > -70f && lowNow[1] == 1 && Math.Abs(readB - got70) < 0.01f && Math.Abs(readA - got70) < 1f && SfxLevels.LevelFor(100, float.NaN) == 100 && SfxLevels.LevelFor(0, float.NaN) == 0
               && nanNow[1] == 90 && !float.IsNaN(nanTarget) && nanNow[0] >= 1 && nanNow[2] >= 1,
               $"match: a wanted level below what LEVEL 1 leaves comes back as what is reached ({got70:0.0} dB, not -70; LEVEL {lowNow[0]} and {lowNow[1]}); a reading that is no number changes nothing");
        }

        // ---- the tools: a patch as text, and as a .syx file
        {
            bool survives = true; var tr = new Random(11);
            for (int i = 0; i < 2000; i++)
            {
                var p = new SfxPatch { engine = (SfxEngine)tr.Next(SfxPatch.EngineCount) };
                for (int k = 0; k < SfxPatch.ParamCount; k++) p[(SfxParam)k] = (byte)tr.Next(128);
                string t = SfxPatchText.ToText(p);
                survives &= t.Length == 62 && t.Length == SfxPatchText.Length && SfxPatchText.Parse(t, out var back) == SfxTextResult.Ok && back.Equals(p) && SfxPatchText.TryParse(t, out back);
            }
            Ok(survives, "2000 random patches survive patch -> text -> patch; the text is 62 characters");
            string laserText = SfxPatchText.ToText(SfxPatch.Default);
            Ok(laserText == SfxPatchText.Prefix + string.Concat(SfxPatch.Default.Pack().Select(b => b.ToString("X2"))) && laserText == "SFXF1:0202466E2D32460078143A0000002240283200000000000000640043",
               "the default laser's text is the one written in the manual: " + laserText);
            string messy = " `sfxf1:0202466e2d32 4600 78143A0000\r\n\t002240283200\u200B0000\u200D00000000\uFEFF640043` ";
            Ok(SfxPatchText.Parse(messy, out var read) == SfxTextResult.Ok && read.Equals(SfxPatch.Default) && SfxPatchText.TryParse("<\"" + laserText + "\">", out read) && SfxPatchText.TryParse("'" + laserText + "'", out read) && read.Equals(SfxPatch.Default),
               "text is read through spaces, line breaks, lower case, quotes and zero-width characters");
            // as it comes out of a chat: a label in front that itself says "SFX Forge", brackets, typographic quotes, a full stop
            // behind it, a sentence that goes on, a mark of writing direction, a soft hyphen and a word joiner inside
            char lq = (char)0x201C, rq = (char)0x201D, rtl = (char)0x200F, shy = (char)0x00AD, wj = (char)0x2060;
            string[] chatty =
            {
                "SFX Forge patch: " + laserText, "my sfxf sound (" + laserText + ").", lq + laserText + rq + ",", "**" + laserText + "**", rtl + laserText.Substring(0, 20) + shy + laserText.Substring(20, 20) + wj + laserText.Substring(40),
                "try " + laserText + " - it is the laser!", laserText + "\nsecond line", "[" + laserText + "]:",
            };
            bool chat = true; string notRead = "";
            foreach (string c in chatty) if (SfxPatchText.Parse(c, out var got) != SfxTextResult.Ok || !got.Equals(SfxPatch.Default)) { chat = false; notRead += " [" + c + "]"; }
            Ok(SfxPatchText.Parse(laserText + " for the boss, and 2 more", out read) == SfxTextResult.Ok && read.Equals(SfxPatch.Default) && SfxPatchText.Parse(laserText + "\ncrush it", out read) == SfxTextResult.Ok,
               "a word after the text may begin with a to f or a digit, when a space or a line break stands between");
            Ok(chat && SfxPatchText.Parse("a newer one: SFXF2:" + laserText.Substring(6), out read) == SfxTextResult.NewerText && SfxPatchText.Parse(laserText + "abc", out read) == SfxTextResult.BadLength
               && SfxPatchText.Parse("SFXF1:0202.", out read) == SfxTextResult.BadLength,
               "text is read out of a sentence: a label in front, brackets, typographic quotes, a full stop, invisible characters" + notRead);
            string digits = laserText.Substring(6);
            string changed = "SFXF1:" + digits.Substring(0, 10) + (digits[10] == '0' ? '1' : '0') + digits.Substring(11);
            byte[] newerBytes = SfxPatch.Default.Pack(); newerBytes[0] = 3;
            byte[] highBytes = SfxPatch.Default.Pack(); highBytes[5] = 0x80;
            string HexOf(byte[] b) => "SFXF1:" + string.Concat(b.Select(x => x.ToString("X2")));
            var answers = new (string text, SfxTextResult want)[]
            {
                ("", SfxTextResult.Empty), (null, SfxTextResult.Empty), (" \r\n ` ", SfxTextResult.Empty), (digits, SfxTextResult.NotOurs), ("hello there", SfxTextResult.NotOurs), ("SFXF:" + digits, SfxTextResult.NotOurs),
                ("SFXF2:" + digits, SfxTextResult.NewerText), ("SFXF12:" + digits, SfxTextResult.NewerText), ("SFXF1:" + digits.Substring(1), SfxTextResult.BadLength), ("SFXF1:" + digits + "0", SfxTextResult.BadLength),
                ("SFXF1:" + digits.Substring(0, 20) + "G" + digits.Substring(21), SfxTextResult.BadCharacter), (changed, SfxTextResult.BadPatch), (HexOf(newerBytes), SfxTextResult.NewerPatch), (HexOf(highBytes), SfxTextResult.BadPatch),
            };
            bool each = true; string wrong = "";
            foreach (var (t, want) in answers)
            {
                SfxTextResult got = SfxPatchText.Parse(t, out var none);
                if (got != want || !none.Equals(default(SfxPatch)) || SfxPatchText.Message(got) == null) { each = false; wrong += $" [{t}: {got}, not {want}]"; }
            }
            Ok(each && SfxPatchText.Message(SfxTextResult.Ok) == null && Enum.GetValues(typeof(SfxTextResult)).Cast<SfxTextResult>().Where(r => r != SfxTextResult.Ok).Select(SfxPatchText.Message).Distinct().Count() == 9,
               "each damage has its own answer" + wrong);

            byte[] syx = SfxPatchText.ToSyx(thud, 4);
            bool files = syx.Length == 35 && SfxPatchText.ParseSyx(syx, out var fromFile, out int pad) == SfxTextResult.Ok && pad == 4 && fromFile.Equals(thud);
            files &= SfxPatchText.ParseSyx(SfxProtocol.BuildDump(9, hum), out fromFile, out pad) == SfxTextResult.Ok && pad == 9 && fromFile.Equals(hum);
            files &= SfxPatchText.ParseSyx(SfxProtocol.BuildLoadSongSound(3, saw), out fromFile, out pad) == SfxTextResult.Ok && pad == 3 && fromFile.Equals(saw);
            files &= SfxPatchText.ParseSyx(SfxProtocol.BuildLoad(2, laser).Concat(SfxProtocol.BuildLoad(7, hum)).ToArray(), out fromFile, out pad) == SfxTextResult.Ok && pad == 2 && fromFile.Equals(laser);
            byte[] foreign = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
            files &= SfxPatchText.ParseSyx(foreign.Concat(SfxProtocol.BuildText("HI")).Concat(SfxProtocol.BuildLoad(11, hum)).ToArray(), out fromFile, out pad) == SfxTextResult.Ok && pad == 11 && fromFile.Equals(hum);
            files &= SfxPatchText.ParseSyx(foreign, out fromFile, out pad) == SfxTextResult.NoPatchInFile && SfxPatchText.ParseSyx(new byte[0], out fromFile, out pad) == SfxTextResult.NoPatchInFile
                  && SfxPatchText.ParseSyx(null, out fromFile, out pad) == SfxTextResult.NoPatchInFile && SfxPatchText.ParseSyx(new byte[70000], out fromFile, out pad) == SfxTextResult.TooLarge;
            byte[] damaged = SfxPatchText.ToSyx(thud, 4); damaged[10] ^= 1;
            byte[] later = SfxPatchText.ToSyx(thud, 4); later[6] = 3;
            files &= SfxPatchText.ParseSyx(later, out fromFile, out pad) == SfxTextResult.NewerPatch && SfxPatchText.ParseSyx(syx.Take(20).ToArray(), out fromFile, out pad) == SfxTextResult.NoPatchInFile;
            // a patch of ours whose checksum does not fit is damaged, not "no patch" - unless a good one follows; realtime bytes inside a message are passed over
            byte[] clocked = syx.Take(12).Concat(new byte[] { 0xF8 }).Concat(syx.Skip(12).Take(10)).Concat(new byte[] { 0xFE, 0xF8 }).Concat(syx.Skip(22)).ToArray();
            bool hurt = SfxPatchText.ParseSyx(damaged, out fromFile, out pad) == SfxTextResult.BadPatch && fromFile.Equals(default(SfxPatch)) && pad == 0
                  && SfxPatchText.ParseSyx(damaged.Concat(SfxProtocol.BuildLoad(5, hum)).ToArray(), out fromFile, out pad) == SfxTextResult.Ok && pad == 5 && fromFile.Equals(hum)
                  && SfxPatchText.ParseSyx(foreign.Concat(damaged).ToArray(), out fromFile, out pad) == SfxTextResult.BadPatch
                  && SfxPatchText.ParseSyx(clocked, out fromFile, out pad) == SfxTextResult.Ok && pad == 4 && fromFile.Equals(thud);
            Ok(hurt, "a .syx whose patch does not fit its checksum is damaged (BadPatch); realtime bytes inside a message are passed over");
            Ok(files, "a .syx of one patch round-trips with its pad; a dump, a load and a song-sound load are read; two messages give the first; a foreign SysEx and an empty file give NoPatchInFile; 70000 bytes give TooLarge");
            Ok(SfxPatchText.ToSyx(laser, 6).SequenceEqual(SfxProtocol.BuildLoad(6, laser)) && SfxPatchText.ToSyx(laser).SequenceEqual(SfxProtocol.BuildLoad(0, laser)), "ToSyx is the load message the link sends");
        }

        // ---- the native engine
        Ok(SfxNative.Available, "sfxforge.dll loads");
        if (!SfxNative.Available) { Console.WriteLine("FAILED"); return 1; }

        // ---- the format-5 song of firmware 1.5.0 (tests/data/DemoSong-format5.bytes): the same samples as before the finer grid
        byte[] fixture = File.ReadAllBytes(Path.Combine(root, "tests", "data", "DemoSong-format5.bytes"));
        byte[] fixSounds = fixture.Take(16 * SfxPatch.Bytes).ToArray(), fix5 = fixture.Skip(16 * SfxPatch.Bytes).ToArray();
        ulong[] wantHash = { 0x663B73D61E165BFCUL, 0xAC323BECC54C7335UL, 0x7D5A2816477BE223UL };      // recorded with the library of firmware 1.5.0
        {
            ulong[] got = ThreeHashes(fixSounds, fix5);
            Ok(fixture.Length == 8147 && fix5[0] == 5 && got.SequenceEqual(wantHash), "a format-5 song renders bit for bit as it did before the finer grid (16 bars, pattern F looped, intensity 0.4): " + Hex(got));
        }

        // ---- every format this version reads plays, and its format-6 form plays the same
        byte[] fix6 = SfxNative.SongConvert(fix5);
        var olderFormats = new List<byte[]>();                                     // the fixture in the formats 1 to 4 (made just below)
        {
            const int Head = 147, Row = 35, Points = Head + 128 * Row;
            var v4 = new byte[SfxProtocol.SongBytesV4]; var v3 = new byte[SfxProtocol.SongBytesV3]; var v2 = new byte[SfxProtocol.SongBytesV2]; var v1 = new byte[SfxProtocol.SongBytesV1];
            Array.Copy(fix5, 0, v3, 0, 66); Array.Copy(fix5, 0, v2, 0, 66); Array.Copy(fix5, 0, v1, 0, 23); Array.Copy(fix5, 0, v4, 0, 66); Array.Copy(fix5, 66, v4, 82, 33);
            v4[0] = 4; v3[0] = 3; v2[0] = 2; v1[0] = 1;
            for (int k = 0; k < 128; k++) { Array.Copy(fix5, Head + k * Row, v4, 115 + k * 51, 35); Array.Copy(fix5, Head + k * Row, v3, 66 + k * 35, 35); Array.Copy(fix5, Head + k * Row, v2, 66 + k * 19, 19); Array.Copy(fix5, Head + k * Row, v1, 23 + k * 19, 19); }
            // (format 4 had one lane a sound: the fixture's two lanes - lane 0 moves a knob of sound 7, lane 1 one of sound 16 - become those sounds' own)
            for (int l = 0; l < 2; l++) { int s = fix5[99 + l * 2] - 1; v4[66 + s] = fix5[100 + l * 2]; for (int p = 0; p < 8; p++) for (int i = 0; i < 16; i++) v4[115 + (p * 16 + s) * 51 + 35 + i] = fix5[Points + (p * 24 + l) * 16 + i]; }
            bool all = true; string said = "";
            olderFormats.AddRange(new[] { v1, v2, v3, v4 });
            foreach (var old in new[] { v1, v2, v3, v4, fix5 })
            {
                byte[] six = SfxNative.SongConvert(old);
                ulong a = RenderHash(fixSounds, old, 0, false, 1f, 4), b = six == null ? 1 : RenderHash(fixSounds, six, 0, false, 1f, 4);
                bool good = six != null && SfxPackedSong.Format(old) == old[0] && SfxPackedSong.Format(six) == 6 && a != 0 && a == b && SfxNative.SongConvert(six).SequenceEqual(six);
                all &= good; said += $" {old[0]}: {old.Length} -> {(six == null ? 0 : six.Length)} bytes{(good ? "" : " WRONG")};";
            }
            Ok(all, "formats 1 to 5 play; each as format 6 plays the same samples (4 bars), and converting that again changes no byte:" + said);
            Ok(RenderHash(fixSounds, v4, 0, false, 1f, 16) == wantHash[0], "the fixture as format 4 (its two lanes as the lanes of their sounds) plays the samples of the format-5 one");
        }

        // ---- format 6
        byte[] demo6 = File.ReadAllBytes(Path.Combine(root, "unity", "Demo", "Assets", "SfxDemo", "DemoSong.bytes"));
        {
            Ok(fix6 != null && fix6.Length == 2487 && fix6[0] == 6 && demo6.Length == 448 + 2487 && demo6.Skip(448).SequenceEqual(fix6) && demo6.Take(448).SequenceEqual(fixSounds),
               $"the fixture as format 6 is 2487 bytes ({(fix6 == null ? 0 : fix6.Length)}): byte for byte the song of the demo's DemoSong.bytes, which firmware 1.6.0 sends");
            bool same = true; int on = 0, pts = 0;
            for (int p = 0; p < 8; p++)
            {
                same &= SfxPackedSong.Grid(fix6, p) == 16 && SfxPackedSong.Grid(fix5, p) == 16;
                for (int i = -1; i <= 16; i++)
                {
                    for (int s = 0; s < 16; s++)
                    {
                        same &= SfxPackedSong.StepOn(fix6, p, s, i) == SfxPackedSong.StepOn(fix5, p, s, i) && SfxPackedSong.StepNote(fix6, p, s, i) == SfxPackedSong.StepNote(fix5, p, s, i)
                             && SfxPackedSong.StepVelocity(fix6, p, s, i) == SfxPackedSong.StepVelocity(fix5, p, s, i) && SfxPackedSong.StepLength(fix6, p, s, i) == SfxPackedSong.StepLength(fix5, p, s, i);
                        if (SfxPackedSong.StepOn(fix6, p, s, i)) on++;
                    }
                    for (int l = 0; l < 24; l++) { same &= SfxPackedSong.LanePoint(fix6, p, l, i) == SfxPackedSong.LanePoint(fix5, p, l, i); if (SfxPackedSong.LanePoint(fix6, p, l, i) >= 0) pts++; }
                }
            }
            Ok(same && on > 200 && pts == 24 && fix6.Take(147).Skip(1).SequenceEqual(fix5.Take(147).Skip(1)), $"SfxPackedSong reads every step, note, velocity, length and point of it as from the format-5 bytes ({on} steps, {pts} points), and the head is the same");
            // written here by the layout, from what SfxPackedSong reads of the format-5 song: the C packer's bytes
            var steps = new List<(int, int, int, int, int, int)>(); var points = new List<(int, int, int, int)>();
            for (int p = 0; p < 8; p++) for (int i = 0; i < 16; i++)
            {
                for (int s = 0; s < 16; s++) if (SfxPackedSong.StepOn(fix5, p, s, i)) steps.Add((p, s, i, SfxPackedSong.StepNote(fix5, p, s, i), SfxPackedSong.StepVelocity(fix5, p, s, i), SfxPackedSong.StepLength(fix5, p, s, i)));
                for (int l = 0; l < 24; l++) if (SfxPackedSong.LanePoint(fix5, p, l, i) >= 0) points.Add((p, l, i, SfxPackedSong.LanePoint(fix5, p, l, i)));
            }
            Ok(Pack6(fix5, new int[8], steps, points).SequenceEqual(fix6), "the layout written out by hand in this test gives the same 2487 bytes");

            var empty5 = (byte[])fix5.Clone();
            for (int i = 147; i < empty5.Length; i++) empty5[i] = 0;
            for (int k = 0; k < 128; k++) for (int i = 0; i < 16; i++) empty5[147 + k * 35 + 19 + i] = 5;
            byte[] empty6 = SfxNative.SongConvert(empty5);
            Ok(empty6 != null && empty6.Length == 211 && empty6.Skip(147).All(x => x == 0) && SfxPackedSong.Format(empty6) == 6, $"an empty song is 211 bytes: the head and eight empty blocks ({(empty6 == null ? 0 : empty6.Length)})");

            // the longest song there is: every sound and every lane in all eight patterns at 64 steps
            var big = new List<byte>(fix6.Take(147));
            for (int p = 0; p < 8; p++)
            {
                big.AddRange(new byte[] { 2, 0x7F, 0x7F, 3, 0x7F, 0x7F, 0x7F, 7 });
                for (int t = 0; t < 16; t++)
                {
                    for (int b = 0; b < 10; b++) big.Add((byte)(b < 9 ? (p * 31 + t * 7 + b * 13) % 128 : (p + t) & 1));
                    for (int i = 0; i < 64; i++) big.Add((byte)(36 + (p * 5 + t * 3 + i) % 60));
                    for (int i = 0; i < 64; i++) big.Add((byte)((p + t + i) % 8));
                    for (int i = 0; i < 64; i++) big.Add((byte)((p * 11 + t * 5 + i * 3) % 64));
                }
                for (int l = 0; l < 24; l++) for (int i = 0; i < 64; i++) big.Add((byte)(1 + (p * 17 + l * 9 + i * 5) % 127));
            }
            byte[] longest = big.ToArray(); byte[] again = SfxNative.SongConvert(longest);
            Ok(longest.Length == 38355 && longest.Length == SfxProtocol.SongBytesMax && again != null && again.SequenceEqual(longest) && SfxPackedSong.Format(longest) == 6 && SfxPackedSong.Grid(longest, 7) == 64
               && SfxPackedSong.StepNote(longest, 7, 15, 63) == 36 + (35 + 45 + 63) % 60 && SfxPackedSong.LanePoint(longest, 7, 23, 63) == (7 * 17 + 23 * 9 + 63 * 5) % 127 && RenderHash(fixSounds, longest, 0, false, 1f, 1) != 0,
               "everything at 64 steps is 38 355 bytes, and is taken: it comes back from the packer byte for byte, and plays");

            // what the player refuses - and it keeps the song it had
            int rowA = 147 + 8;                                                    // the first row of pattern A: 3 bytes of steps, 16 notes, 16 velocities, 16 lengths
            byte[] With(int at, int value) { var c = (byte[])fix6.Clone(); c[at] = (byte)value; return c; }
            var damaged = new List<(string what, byte[] song)>
            {
                ("one byte short", fix6.Take(fix6.Length - 1).ToArray()), ("one byte over", fix6.Append((byte)0).ToArray()), ("a grid of 3", With(147, 3)), ("a velocity of 8", With(rowA + 3 + 16, 8)),
                ("a length of 16 at 16 steps", With(rowA + 3 + 32, 16)), ("a step bit from 16 on", With(rowA + 2, fix6[rowA + 2] | 4)), ("a spare bit in the sounds' mask", With(147 + 3, fix6[147 + 3] | 4)),
                ("a spare bit in the lanes' mask", With(147 + 7, 8)), ("a byte of 128", With(1000, 128)), ("a bar of 17", With(1, 17)), ("a bar of 0", With(1, 0)), ("six bytes", fix6.Take(6).ToArray()),
            };
            for (int p = 0; p <= 8; p++)
            {
                int at = 147 + p * 4776;                                           // where block p of the longest song begins (8: where the song ends)
                damaged.Add(($"the longest song cut one byte before block {p}", longest.Take(at - 1).ToArray()));
                if (p < 8) damaged.Add(($"the longest song cut at block {p}", longest.Take(at).ToArray()));
                damaged.Add(($"the longest song cut one byte after the start of block {p}", p < 8 ? longest.Take(at + 1).ToArray() : longest.Append((byte)0).ToArray()));
            }
            string taken = "";
            ulong kept = RenderHash(fixSounds, fix6, 0, false, 1f, 2, pl => { foreach (var (what, song) in damaged) if (SfxNative.PlayerSetSong(pl, song)) taken += " [" + what + "]"; });
            bool unreadable = damaged.Where(x => !x.what.StartsWith("a spare bit") && x.what != "a velocity of 8" && x.what != "a length of 16 at 16 steps" && x.what != "a step bit from 16 on" && x.what != "a byte of 128" && !x.what.StartsWith("a bar")).All(x => SfxPackedSong.Format(x.song) == 0);
            Ok(taken == "" && kept == RenderHash(fixSounds, fix6, 0, false, 1f, 2) && damaged.All(x => SfxNative.SongConvert(x.song) == null) && unreadable,
               $"the player refuses {damaged.Count} damaged songs and keeps the one it had: a byte short or over, a grid of 3, a velocity of 8, a length of a whole bar, a step bit past the grid, spare mask bits, a byte of 128, a bar of 0 or 17, the longest song cut at every block and a byte either side" + taken);
        }

        // ---- SfxPackedSong.IsPlayable says what the player says: the damaged songs above, and thousands of songs with a byte or three changed
        {
            byte[] fine = SfxNative.SongRegrid(SfxNative.SongRegrid(fix6, 5, 2), 4, 1);          // (pattern F at 64 steps, E at 32)
            var bases = new List<byte[]> { fix6, fine, fix5 }; bases.AddRange(olderFormats);
            bool wholeOnes = bases.All(SfxPackedSong.IsPlayable);
            var bad8 = (byte[])fix6.Clone(); bad8[147 + 8 + 3 + 16] = 9;                         // a velocity of 9 in the first row of pattern A
            var rnd = new Random(20261121); int tried = 0, refused = 0, apart = 0; string first = "";
            RenderHash(fixSounds, fix6, 0, false, 1f, 1, pl =>
            {
                foreach (var b in bases)
                    for (int n = 0; n < 4000; n++)
                    {
                        var c = (byte[])b.Clone(); int changes = 1 + rnd.Next(3); string what = "";
                        for (int k = 0; k < changes; k++)
                        {
                            int at = rnd.Next(4) == 0 ? rnd.Next(Math.Min(c.Length, 180)) : rnd.Next(c.Length);            // (a quarter of them in the head and the first block's masks)
                            int v = rnd.Next(5) == 0 ? rnd.Next(256) : rnd.Next(3) == 0 ? rnd.Next(128) : rnd.Next(20);
                            c[at] = (byte)v; what += $" [{at}]={v}";
                        }
                        bool says = SfxPackedSong.IsPlayable(c), takes = SfxNative.PlayerSetSong(pl, c);
                        tried++; if (!takes) refused++;
                        if (says != takes) { apart++; if (first == "") first = $" - first: format {b[0]}, {b.Length} bytes,{what}: IsPlayable {says}, the player {takes}"; }
                    }
            });
            Ok(wholeOnes && SfxPackedSong.IsValid(bad8) && !SfxPackedSong.IsPlayable(bad8) && !SfxPackedSong.IsPlayable(null) && !SfxPackedSong.IsPlayable(new byte[300]),
               "IsPlayable: the fixture in every format (1 to 6, and with patterns at 32 and 64 steps) is; a song with a velocity of 9 is valid by its layout and not playable");
            Ok(apart == 0 && refused > tried / 10 && refused < tried * 9 / 10, $"IsPlayable and the native player agree on {tried} songs with one to three bytes changed ({refused} refused by both){first}");
        }

        // ---- raising changes no sample
        {
            byte[] All(byte[] s, int grid) { for (int p = 0; p < 8; p++) s = SfxNative.SongRegrid(s, p, grid); return s; }
            byte[] at32 = All(fix6, 1), at64 = All(fix6, 2);
            ulong[] h32 = ThreeHashes(fixSounds, at32), h64 = ThreeHashes(fixSounds, at64);
            Ok(SfxPackedSong.Grid(at32, 0) == 32 && SfxPackedSong.Grid(at64, 7) == 64 && h32.SequenceEqual(wantHash) && h64.SequenceEqual(wantHash),
               $"every pattern raised to 32 steps, and to 64: the same samples as the format-5 song, bit for bit ({at32.Length} and {at64.Length} bytes; {Hex(h64)})");
            bool sweep = true; string off = ""; int tried = 0;
            foreach (int swing in new[] { 0, 1, 20, 50, 99, 100 })
                foreach (int bpm in new[] { 40, 97, 120, 173, 240 })
                {
                    var a = (byte[])fix6.Clone(); a[4] = (byte)swing; a[2] = (byte)(bpm >> 7); a[3] = (byte)(bpm & 127);
                    var b = (byte[])at64.Clone(); b[4] = a[4]; b[2] = a[2]; b[3] = a[3];
                    var c = (byte[])at32.Clone(); c[4] = a[4]; c[2] = a[2]; c[3] = a[3];
                    ulong ha = RenderHash(fixSounds, a, 5, true, 1f, 2);
                    bool good = ha != 0 && ha == RenderHash(fixSounds, b, 5, true, 1f, 2) && ha == RenderHash(fixSounds, c, 5, true, 1f, 2);
                    tried++; sweep &= good; if (!good) off += $" [swing {swing} at {bpm}]";
                }
            Ok(sweep, $"swing 0, 1, 20, 50, 99 and 100 at 40, 97, 120, 173 and 240 BPM, two bars of pattern F: at 16, at 32 and at 64 steps the same samples ({tried} pairs)" + off);
            bool back = true; byte[] down = at64;
            for (int p = 0; p < 8; p++) { back &= SfxNative.SongRegridLoss(down, p, 1) == 0; down = SfxNative.SongRegrid(down, p, 1); }
            back &= down.SequenceEqual(at32);
            for (int p = 0; p < 8; p++) { back &= SfxNative.SongRegridLoss(down, p, 0) == 0; down = SfxNative.SongRegrid(down, p, 0); }
            Ok(back && down.SequenceEqual(fix6) && SfxNative.SongRegridLoss(fix6, 0, 2) == 0 && SfxNative.SongRegridLoss(fix6, 8, 0) == -1 && SfxNative.SongRegridLoss(fix6, 0, 3) == -1 && SfxNative.SongRegrid(fix6, 0, 3) == null
               && SfxNative.SongRegrid(new byte[300], 0, 1) == null && SfxNative.SongRegridLoss(null, 0, 1) == -1,
               "64 back to 32 and to 16 loses nothing each time and gives the first bytes again; no such pattern, no such grid, no song: -1 and null");
            bool spread = SfxPackedSong.Grid(at64, 0) == 64;
            for (int s = 0; s < 16; s++) for (int i = 0; i < 16; i++)
            {
                spread &= SfxPackedSong.StepOn(at64, 0, s, i * 4) == SfxPackedSong.StepOn(fix6, 0, s, i) && SfxPackedSong.StepNote(at64, 0, s, i * 4) == SfxPackedSong.StepNote(fix6, 0, s, i)
                       && SfxPackedSong.StepLength(at64, 0, s, i * 4) == SfxPackedSong.StepLength(fix6, 0, s, i) * 4 && !SfxPackedSong.StepOn(at64, 0, s, i * 4 + 1) && !SfxPackedSong.StepOn(at64, 0, s, i * 4 + 2) && !SfxPackedSong.StepOn(at64, 0, s, i * 4 + 3);
            }
            Ok(spread, "raised, a step stands on the step that begins at the same moment, with its note; its length is four times as many steps");
        }

        // ---- a finer grid sounds finer
        {
            const int Hat = 12, Organ = 10, Strings = 15;
            int[] G(int a, int b = 0) { var g = new int[8]; g[0] = a; g[1] = b; return g; }
            // the hat on steps 0 and 3 of 64 at 120 BPM: a step is 31.25 ms
            var two = Pack6(PlainHead(fix6, 120, 0, 1), G(2), new[] { (0, Hat, 0, 0, 5, 0), (0, Hat, 3, 0, 5, 0) });
            var on = Onsets(RenderSong(fixSounds, two, 1.0, out _), 0, 1.0);
            double gap = on.Count == 2 ? (on[1] - on[0]) * 1000 : -1;
            Ok(on.Count == 2 && Math.Abs(gap - 93.75) < 256000.0 / 48000, $"a sound on steps 1 and 4 of 64 at 120 BPM: the second starts {gap:0.00} ms after the first (three steps are 93.75 ms)");
            // swing 100: the odd sixteenth begins half a sixteenth late and is half as long - step 4 of 32 is its second half
            var swung = Pack6(PlainHead(fix6, 120, 100, 1), G(1), new[] { (0, Hat, 0, 0, 5, 0), (0, Hat, 3, 0, 5, 0) });
            on = Onsets(RenderSong(fixSounds, swung, 1.0, out _), 0, 1.0);
            gap = on.Count == 2 ? (on[1] - on[0]) * 1000 : -1;
            Ok(on.Count == 2 && Math.Abs(gap - 1.75 * 125) < 256000.0 / 48000, $"swing 100, a step on step 4 of 32: it starts {gap / 125:0.000} sixteenths into the bar (1.75: the second half of a sixteenth that begins at 1.5 and is half as long)");
            // a length counts in the pattern's own steps
            double Held(int len) { var s = Pack6(PlainHead(fix6, 120, 0, 1), G(2), new[] { (0, Strings, 0, 57, 7, len) }); return Rms(RenderSong(fixSounds, s, 1.0, out _), 0.45, 0.75); }
            double l2 = Held(2), l8 = Held(8), l32 = Held(32);
            Ok(l2 < l8 * 0.6 && l8 < l32 * 0.6 && l32 > 0.02, $"LENGTH at 64 steps counts sixty-fourths: 2 is over before 8, 8 before 32 ({l2:0.000}, {l8:0.000}, {l32:0.000} between 0.45 and 0.75 s)");
            // a note of length 3 on the last step of a 64 pattern, the next bar a pattern of 16: it ends on that pattern's second step
            int Ends(int len, int gridB, out int state1, out int grid0, out int grid1, out bool hangs)
            {
                var s = Pack6(PlainHead(fix6, 120, 0, 2), G(2, gridB), new[] { (0, Organ, 63, 57, 7, len) });       // (the organ: at its full level at once, so its fading is as long whenever it is let go)
                int st = -9, g0 = -9, g1 = -9; var q = new int[4]; var g = new int[2];        // (5.9 s: the two bars, and the first again up to just before its note comes round)
                RenderSong(fixSounds, s, 5.9, out uint[] playing, (pl, b) => { if (b == 192) { SfxNative.PlayerState(pl, q); SfxNative.PlayerGrid(pl, g); st = q[1]; g0 = g[0]; g1 = g[1]; } });   // (block 192 ends 1.024 s into bar 1)
                int last = Array.FindLastIndex(playing, m => (m >> Organ & 1) != 0);
                state1 = st; grid0 = g0; grid1 = g1; hangs = last >= playing.Length - 1;
                return last;
            }
            int byItself = Ends(0, 0, out _, out _, out _, out bool h0), three = Ends(3, 0, out int sixteenth, out int step, out int per, out bool h3), threeFine = Ends(3, 2, out _, out _, out _, out _), long16 = Ends(63, 0, out _, out _, out _, out bool h63);
            double later = (three - byItself) * 256 / 48.0, laterFine = (threeFine - byItself) * 256 / 48.0;
            Ok(!h0 && !h3 && !h63 && later > 125 - 12 && later < 125 + 12 && laterFine > 62.5 - 12 && laterFine < 62.5 + 12 && long16 > three,
               $"a note of length 3 on the last step of a 64 pattern, the next bar a pattern of 16: it is let go {later:0.0} ms after the bar line - on that pattern's second step (125 ms), not later; into a pattern of 64 after {laterFine:0.0} ms (two more steps: 62.5); no note hangs");
            Ok(sixteenth == 8 && step / 4 == 8 && per == 4 && step >= 32 && step <= 33, $"1.024 s into a 64 pattern at 120 BPM the player's state says sixteenth 9 ({sixteenth + 1}), its grid step {step + 1} of 4 a sixteenth ({per})");
        }

        // ---- lowering drops what stands between, and says how much
        {
            int[] g64 = { 2, 0, 0, 0, 0, 0, 0, 0 }, g32 = { 1, 0, 0, 0, 0, 0, 0, 0 }, g16 = new int[8];
            var head = PlainHead(fix6, 120, 0, 1); head[99] = 7; head[100] = 26;                             // lane 0: sound 7's fader
            var fine = Pack6(head, g64, new[] { (0, 2, 0, 0, 5, 0), (0, 2, 5, 0, 5, 0), (0, 6, 6, 40, 7, 5), (0, 6, 8, 43, 3, 63) }, new[] { (0, 0, 3, 90), (0, 0, 8, 20), (0, 0, 62, 5) });
            var half = Pack6(head, g32, new[] { (0, 2, 0, 0, 5, 0), (0, 6, 3, 40, 7, 3), (0, 6, 4, 43, 3, 31) }, new[] { (0, 0, 4, 20), (0, 0, 31, 5) });
            var coarse = Pack6(head, g16, new[] { (0, 2, 0, 0, 5, 0), (0, 6, 2, 43, 3, 15) }, new[] { (0, 0, 2, 20) });
            byte[] to32 = SfxNative.SongRegrid(fine, 0, 1), to16 = SfxNative.SongRegrid(fine, 0, 0), stepwise = to32 == null ? null : SfxNative.SongRegrid(to32, 0, 0);
            Ok(SfxNative.SongRegridLoss(fine, 0, 1) == 2 && SfxNative.SongRegridLoss(fine, 0, 0) == 4 && SfxNative.SongRegridLoss(fine, 0, 2) == 0 && SfxNative.SongRegridLoss(half, 0, 0) == 2,
               $"what lowering would drop is counted, steps and points: 64 to 32 drops 2 (a step on 6, a point on 4), 64 to 16 drops 4 (two steps, two points), 32 to 16 two more ({SfxNative.SongRegridLoss(fine, 0, 1)}, {SfxNative.SongRegridLoss(fine, 0, 0)}, {SfxNative.SongRegridLoss(half, 0, 0)})");
            Ok(to32 != null && to32.SequenceEqual(half) && SfxPackedSong.StepLength(to32, 0, 6, 3) == 3 && SfxPackedSong.StepLength(to32, 0, 6, 4) == 31,
               "64 to 32: what stands on an even step stays, a length of 5 becomes 3 (rounded up), one of 63 the 31 a bar of 32 can hold; the rest is gone - the bytes are those of the song written at 32");
            Ok(to16 != null && to16.SequenceEqual(coarse) && stepwise != null && stepwise.SequenceEqual(coarse), "64 to 16 at once, or by way of 32: the same song, byte for byte, as written at 16");
            byte[] up = SfxNative.SongRegrid(to16, 0, 2);
            bool above = up != null;
            for (int i = 0; above && i < 64; i++) above = SfxPackedSong.StepOn(up, 0, 6, i) == (i == 8) && SfxPackedSong.StepOn(up, 0, 2, i) == (i == 0) && (SfxPackedSong.LanePoint(up, 0, 0, i) >= 0) == (i == 8);
            Ok(above && SfxPackedSong.StepLength(up, 0, 6, 8) == 60, "and raised again nothing comes back from the cells above: lowering emptied them");
        }

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

        // ---- the tools, with the engine: what the generators make, a bake with options, levels
        {
            bool audible = true; string worst = ""; float low = 9f, high = 0f; int longest = 0;
            for (int c = 0; c < SfxGenerator.Count; c++)
                for (uint seed = 1; seed <= 100; seed++)
                {
                    SfxPatch p = SfxGenerator.Make((SfxCategory)c, seed * 2654435761u);
                    SfxBaked b = SfxBake.Render(p, 1f);
                    bool good = b != null && b.level.peak > 0.02f && b.level.peak <= 1f && !b.cutAtLimit && !b.samples.Any(float.IsNaN);
                    if (b != null) { low = Math.Min(low, b.level.peak); high = Math.Max(high, b.level.peak); longest = Math.Max(longest, b.frames); }
                    if (!good && worst.Length < 400) worst += $" [{SfxGenerator.Names[c]} {seed}: {(b == null ? "null" : $"peak {b.level.peak:0.000} {b.Seconds:0.0}s")}]";
                    audible &= good;
                }
            Ok(audible, $"every category: 100 seeds bake audible and unclipped (peaks {low:0.000} .. {high:0.000}, the longest {longest / 48000f:0.0} s)" + worst);

            SfxPatch wide = saw; wide.spread = 90; wide.chorus = 80;
            SfxNative.Bake(wide, out var wl, out var wr, 48000, 1f, 0, 0f, 0.5f);
            SfxBaked stereo = SfxBake.Render(wide, 0.5f), mono = SfxBake.Render(wide, 0.5f, new SfxBakeOptions { mono = true });
            bool mean = stereo.channels == 2 && mono.channels == 1 && stereo.frames == wl.Length && mono.frames == wl.Length && mono.samples.Length == wl.Length && stereo.samples.Length == 2 * wl.Length && stereo.sampleRate == 48000;
            for (int i = 0; mean && i < wl.Length; i++) mean = stereo.samples[i * 2] == wl[i] && stereo.samples[i * 2 + 1] == wr[i] && Math.Abs(mono.samples[i] - (wl[i] + wr[i]) * 0.5f) < 1e-6f;
            // The rate: the sound is rendered at 48 kHz and brought to the file's rate - the same sound, as long, the same note.
            string rates = "";
            double note48 = Pitch(SfxBake.Render(saw, 0.6f, new SfxBakeOptions { mono = true }).samples, 0.2, 0.4);
            SfxBaked thudAt48 = SfxBake.Render(thud, 0.5f);
            float thud48 = thudAt48.Seconds;
            foreach (int rate in new[] { 8000, 22050, 24000, 32000, 44100 })
            {
                SfxBaked at = SfxBake.Render(thud, 0.5f, new SfxBakeOptions { sampleRate = rate }), held = SfxBake.Render(saw, 0.6f, new SfxBakeOptions { sampleRate = rate, mono = true });
                double note = held == null ? 0 : Pitch(held.samples, 0.2, 0.4, rate);
                bool good = at != null && held != null && at.sampleRate == rate && at.samples.Length == at.frames * 2 && Math.Abs(at.Seconds - thud48) < 0.001f
                     && Math.Abs(at.frames - thudAt48.frames * (rate / 48000.0)) <= 1 && Math.Abs(at.frames / (double)thudAt48.frames - rate / 48000.0) < 0.02
                     && at.level.peak > 0.05f && at.level.peak <= 1f && (rate < 22050 || Math.Abs(at.level.RmsDb - thudAt48.level.RmsDb) < 0.5f)
                     && !at.samples.Any(float.IsNaN) && Math.Abs(note / note48 - 1) < 0.02 && Math.Abs(held.Seconds - SfxBake.Render(saw, 0.6f).Seconds) < 0.001f;
                mean &= good;
                rates += $" {rate}: {(at == null ? 0 : at.Seconds):0.00} s, {note:0.0} Hz, rms {(at == null ? 0 : at.level.RmsDb - thudAt48.level.RmsDb):+0.00;-0.00} dB{(good ? "" : " WRONG")};";
            }
            Ok(mean, $"bake options: mono is the mean of left and right; a lower rate is the same sound, as long, the same note - 24000 has half the frames (48000: {thud48:0.00} s, {note48:0.0} Hz;{rates})");

            // the resampler by itself: a tone under half the new rate comes through as it is, one above it is gone
            var tone1k = new float[48000]; var tone15k = new float[48000];
            for (int i = 0; i < 48000; i++) { tone1k[i] = (float)Math.Sin(2 * Math.PI * 1000 * i / 48000.0) * 0.5f; tone15k[i] = (float)Math.Sin(2 * Math.PI * 15000 * i / 48000.0) * 0.5f; }
            float[] kept1k = SfxBake.Resample(tone1k, 48000, 48000, 24000), gone15k = SfxBake.Resample(tone15k, 48000, 48000, 24000), to441 = SfxBake.Resample(tone1k, 48000, 48000, 44100);
            float[] copy = SfxBake.Resample(tone1k, 100, 48000, 48000);
            float r1 = (float)Rms(kept1k.Skip(2000).Take(20000).ToArray(), 0, 20000 / 48000.0), r15 = (float)Rms(gone15k.Skip(2000).Take(20000).ToArray(), 0, 20000 / 48000.0);
            Ok(kept1k.Length == 24000 && to441.Length == 44100 && copy.Length == 100 && copy[50] == tone1k[50] && Math.Abs(SfxLevels.ToDb(r1) - SfxLevels.ToDb(0.5f / (float)Math.Sqrt(2))) < 0.05f && SfxLevels.ToDb(r15) < -60f
               && Math.Abs(Cycles(to441, 0.2, 0.4, 44100) - 200) <= 1 && SfxBake.Resample(null, 0, 48000, 24000).Length == 0 && SfxBake.Resample(tone1k, 0, 48000, 24000).Length == 0
               && kept1k.SequenceEqual(SfxBake.Resample(tone1k, 48000, 48000, 24000)),
               $"the resampler: 1 kHz stays as loud at 24000 ({SfxLevels.ToDb(r1):0.00} dB), 15 kHz is gone ({SfxLevels.ToDb(r15):0.0} dB), and it is the same every time");

            // what a careless caller passes: a hold that is no number or below 0 is no hold; a trim past the end of the array
            SfxBaked noHold = SfxBake.Render(laser, 0f), nanHold = SfxBake.Render(laser, float.NaN), minusHold = SfxBake.Render(hum, -3f), longHold = SfxBake.Render(hum, 99f);
            var shortArray = new float[40]; for (int i = 0; i < 40; i++) shortArray[i] = 0.5f;
            Ok(nanHold != null && nanHold.frames == noHold.frames && nanHold.samples.SequenceEqual(noHold.samples) && minusHold != null && minusHold.frames > 0 && longHold != null && longHold.cutAtLimit
               && SfxBake.TrimEnd(shortArray, null, 500, -36f, 10) == 40 && SfxBake.TrimEnd(shortArray, new float[20], 500, -36f, 10) == 20 && SfxBake.TrimEnd(null, null, 5, -36f, 10) == 0,
               "a hold that is no number or below 0 bakes as no hold, one above 12 s as 12 s; a trim never reads past its arrays");

            SfxBaked normal = SfxBake.Render(laser, 0f, new SfxBakeOptions { normaliseDb = -3f }), plain = SfxBake.Render(laser, 0f);
            SfxPatch silentPatch = laser; silentPatch.level = 0;
            SfxBaked nothing = SfxBake.Render(silentPatch, 0f, new SfxBakeOptions { normaliseDb = -3f });
            Ok(Math.Abs(normal.level.PeakDb + 3f) < 0.01f && Math.Abs(normal.gainDb - (-3f - plain.level.PeakDb)) < 0.01f && plain.gainDb == 0f && nothing != null && nothing.level.Silent && nothing.gainDb == 0f && nothing.samples.All(s => s == 0f),
               $"normalise reaches its peak ({plain.level.PeakDb:0.00} dB by {normal.gainDb:+0.00;-0.00} dB to {normal.level.PeakDb:0.00} dB); silence stays silence");

            SfxPatch roomy = laser; roomy.reverb = 110;
            SfxBaked whole = SfxBake.Render(roomy, 0f), cut = SfxBake.Render(roomy, 0f, new SfxBakeOptions { trimDb = -36f });
            float gate = SfxLevels.FromDb(-36f); int fade = 256, lastLoud = cut.frames - fade - 1;
            bool trimmed = cut.frames < whole.frames - 4800 && Math.Abs(cut.samples[cut.frames * 2 - 1]) < 0.001f && Math.Abs(cut.samples[cut.frames * 2 - 2]) < 0.001f
                && (Math.Abs(cut.samples[lastLoud * 2]) > gate || Math.Abs(cut.samples[lastLoud * 2 + 1]) > gate) && cut.samples.Length == cut.frames * 2;
            for (int i = 0; trimmed && i < lastLoud * 2; i++) trimmed = cut.samples[i] == whole.samples[i];
            var ramp = new float[1000]; for (int i = 0; i < ramp.Length; i++) ramp[i] = i < 500 ? 0.5f : 0.001f;
            trimmed &= SfxBake.TrimEnd(ramp, null, 1000, -36f, 100) == 600 && ramp[499] == 0.5f && ramp[599] == 0f && ramp[500] < 0.001f && SfxBake.TrimEnd(new float[50], null, 50, -36f, 100) == 50;
            Ok(trimmed, $"trim cuts the tail and ends in silence ({whole.Seconds:0.00} s to {cut.Seconds:0.00} s at -36 dB)");

            SfxBaked low1 = SfxBake.Render(saw, 0.6f, new SfxBakeOptions { mono = true }), up = SfxBake.Render(saw, 0.6f, new SfxBakeOptions { mono = true, semitones = 12f });
            double f0 = Pitch(low1.samples, 0.2, 0.4), f1 = Pitch(up.samples, 0.2, 0.4);
            SfxBaked soft = SfxBake.Render(laser, 0f, new SfxBakeOptions { velocity = 0.5f });
            SfxBaked asNote = SfxBake.Render(saw, 0.6f, new SfxBakeOptions { mono = true, midiNote = 69 });
            double fa = Pitch(asNote.samples, 0.2, 0.4);
            Ok(Math.Abs(f1 / f0 - 2) < 0.02 && Math.Abs(soft.level.peak / plain.level.peak - 0.5f) < 0.01f && Math.Abs(fa - 440) < 5,
               $"pitch +12 doubles the frequency ({f0:0.0} Hz to {f1:0.0} Hz); velocity 0.5 halves the peak; note 69 is {fa:0.0} Hz");

            SfxBaked ringing = SfxBake.Render(hum, 12f), let = SfxBake.Render(hum, 1f);
            Ok(ringing != null && ringing.cutAtLimit && ringing.frames == (int)(SfxBake.MaxSeconds * 48000) && !let.cutAtLimit && let.frames < ringing.frames, "a 12 s hold is cut and says so");

            var refused = new[] { new SfxBakeOptions { sampleRate = 96000 }, new SfxBakeOptions { velocity = 0f }, new SfxBakeOptions { normaliseDb = 1f }, new SfxBakeOptions { trimDb = -5f }, new SfxBakeOptions { semitones = 60f }, new SfxBakeOptions { midiNote = 128 }, new SfxBakeOptions { sampleRate = 4000 } };
            Ok(refused.All(o => o.Problem() != null && SfxBake.Render(laser, 0f, o) == null) && new SfxBakeOptions().Problem() == null && new SfxBakeOptions { sampleRate = 96000 }.Problem() == "sample rate 96000 is outside 8000..48000"
               && new SfxBakeOptions { trimDb = -60f, normaliseDb = -1f, midiNote = 60, semitones = -12f, velocity = 0.3f, sampleRate = 22050, mono = true }.Problem() == null,
               "options out of range are refused: " + string.Join("; ", refused.Select(o => o.Problem())));

            SfxPatch noisy = SfxPatch.Create(SfxEngine.Foley, 30, 64, 0, 110, 127, 127, 105, 20, 100, 25); noisy.reverb = 60; noisy.delay = 40;
            var opts = new SfxBakeOptions { normaliseDb = -1f, trimDb = -48f, sampleRate = 44100 };
            SfxBaked first = SfxBake.Render(noisy, 0f, opts), second = SfxBake.Render(noisy, 0f, opts);
            Ok(first.frames == second.frames && first.samples.SequenceEqual(second.samples) && first.level.peak == second.level.peak, "two bakes are the same samples");

            SfxPatch quieter = laser; quieter.level = 80;
            SfxBaked at100 = SfxBake.Render(laser, 0f), at80 = SfxBake.Render(quieter, 0f);
            float dPeak = at100.level.PeakDb - at80.level.PeakDb, dRms = at100.level.RmsDb - at80.level.RmsDb;
            Ok(Math.Abs(dPeak - 3.876f) < 0.05f && Math.Abs(dRms - 3.876f) < 0.05f && Math.Abs(-SfxLevels.LevelDb(80) - 3.876f) < 0.01f, $"LEVEL scales the baked sound by 40 log10(level / 100): 100 to 80 is {dPeak:0.000} dB in peak, {dRms:0.000} dB in RMS");

            bool matched = true; string spreads = "";
            foreach (SfxLevelMeasure by in new[] { SfxLevelMeasure.Rms, SfxLevelMeasure.Peak })
            {
                SfxPatch[] three = { thud, laser, hum };
                var reads = three.Select(p => SfxBake.Render(p, 1f).level).ToArray();
                int[] levels = three.Select(p => (int)p.level).ToArray(), now = new int[3];
                float target = SfxLevels.Match(levels, reads, by, now);
                float lowest = 99f, highest = -99f;
                for (int i = 0; i < 3; i++)
                {
                    three[i].level = (byte)now[i];
                    SfxLevelReading after = SfxBake.Render(three[i], 1f).level;
                    float reading = by == SfxLevelMeasure.Rms ? after.RmsDb : after.PeakDb;
                    lowest = Math.Min(lowest, reading); highest = Math.Max(highest, reading);
                    matched &= !after.Clips && Math.Abs(reading - target) < 0.25f;
                }
                matched &= highest - lowest <= 0.25f;
                spreads += $" {by}: {target:0.00} dB, spread {highest - lowest:0.00} dB, LEVEL {string.Join(" ", now)};";
            }
            Ok(matched, "after a match three sounds read the same RMS, and the same peak:" + spreads);
        }

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
            var sounds = new SfxPatch[16]; var arriving = new SfxSongTransfer(); int seen = 0, songPieces = 0; bool whole = false;
            foreach (var msg in msgs.Skip(msgs.FindLastIndex(b => b[0] == 0xF0 && b[4] == SfxProtocol.Dump && b[5] == 0)))
            {
                if (SfxProtocol.TryParsePatch(msg, out int slot, out var patch)) { sounds[slot] = patch; seen |= 1 << slot; }
                else if (SfxSongTransfer.IsPiece(msg)) { songPieces++; if (arriving.Take(msg)) whole = true; }
            }
            byte[] song = arriving.Song ?? new byte[0];
            Ok(whole && seen == 0xFFFF && song.Length > 211 && song[0] == 6 && songPieces == (song.Length + 239) / 240 && SfxPackedSong.Format(song) == 6,
               $"the firmware's song arrives whole: 16 sounds and {songPieces} pieces of up to 240 bytes - {song.Length} bytes of format 6");
            // how a song's pieces are taken: in any order, twins left alone, another song's piece begins anew - in the message of
            // firmware 1.6 (the song the firmware just sent) and in the one before (the format-5 fixture: 39 pieces of 200)
            foreach (var (shape, first, build, size) in new (string, byte[], Func<byte[], byte[][]>, int)[] { ("39 pieces of 200", fix5, SfxProtocol.BuildSong, 200), ("pieces of 240", song, SfxProtocol.BuildSongPieces, 240) })
            {
                var pieces = build(first);
                var songTwo = (byte[])first.Clone();
                for (int k = 0; k < pieces.Length; k++) { int at = Math.Min(k * size + 50, first.Length - 1); songTwo[at] = (byte)(songTwo[at] ^ 1); }   // (another song: every piece differs)
                var piecesTwo = SfxProtocol.BuildPieces(songTwo); if (size == 200) piecesTwo = SfxProtocol.BuildSong(songTwo);
                int n = pieces.Length;
                var order = Enumerable.Range(0, n).ToArray(); (order[n - 3], order[n - 2]) = (order[n - 2], order[n - 3]); (order[2], order[3]) = (order[3], order[2]);
                Ok(pieces.Length == (first.Length + size - 1) / size && pieces.All(m => m.Length <= (size == 200 ? 208 : 251)) && (size != 200 || (pieces.Length == 39 && pieces[38].Length == 8 + 99)),
                   $"{shape}: a song of {first.Length} bytes is {pieces.Length} messages");

                var t = new SfxSongTransfer(); int done = 0;
                foreach (int k in order) if (t.Take(pieces[k])) done++;
                Ok(done == 1 && t.Song != null && t.Song.SequenceEqual(first), $"{shape}: a song whose pieces come out of order arrives whole all the same");

                t = new SfxSongTransfer(); done = 0;
                foreach (int k in order.Reverse()) { if (t.Take(pieces[k])) done++; if (t.Take(pieces[k])) done++; }
                Ok(done == 1 && t.Song.SequenceEqual(first), $"{shape}: last piece first and every piece twice, it arrives whole, once");
                foreach (int k in order) { if (t.Take(piecesTwo[k])) done++; if (t.Take(piecesTwo[k])) done++; }
                Ok(done == 2 && t.Song.SequenceEqual(songTwo), $"{shape}: and the next song, straight after and doubled too, arrives whole and is the next song (no piece of the first in it)");

                t = new SfxSongTransfer(); done = 0;
                foreach (int k in Enumerable.Range(0, n).Where(k => k != 5)) t.Take(pieces[k]);              // a song that lost a piece …
                bool early = false, nothingYet = t.Song == null;
                foreach (int k in Enumerable.Range(0, n)) { bool pieceMadeItWhole = t.Take(piecesTwo[k]); if (pieceMadeItWhole) { done++; early |= k != n - 1; } }   // … and another, whole, after it
                Ok(nothingYet && done == 1 && !early && t.Song.SequenceEqual(songTwo), $"{shape}: after a song that lost a piece, the next is whole only with its own last piece, and is the next song");

                t = new SfxSongTransfer(); done = 0;
                foreach (int k in order.Take(n / 2)) t.Take(pieces[k]);
                t.Reset();                                                                 // (what the link does after two seconds without a piece: without it the halves of two songs would count as one)
                foreach (int k in order.Reverse()) if (t.Take(piecesTwo[k])) done++;
                Ok(done == 1 && t.Song != null && t.Song.SequenceEqual(songTwo), $"{shape}: after a transfer that broke off and was forgotten, another song sent last piece first arrives whole");

                // a second song soon after the first (no Reset between) that differs from it in its last piece only - then in its first only
                var lateOnly = (byte[])first.Clone(); lateOnly[first.Length - 1] = (byte)(lateOnly[first.Length - 1] ^ 1);
                var earlyOnly = (byte[])lateOnly.Clone(); earlyOnly[3] = (byte)(earlyOnly[3] ^ 1);
                var piecesLate = size == 200 ? SfxProtocol.BuildSong(lateOnly) : SfxProtocol.BuildPieces(lateOnly);
                var piecesEarly = size == 200 ? SfxProtocol.BuildSong(earlyOnly) : SfxProtocol.BuildPieces(earlyOnly);
                foreach (bool twice in new[] { false, true })
                {
                    t = new SfxSongTransfer(); done = 0; string got = "";
                    void Send(byte[][] ps) { foreach (var m in ps) { if (t.Take(m)) { done++; got += t.Song.SequenceEqual(first) ? "1" : t.Song.SequenceEqual(lateOnly) ? "2" : t.Song.SequenceEqual(earlyOnly) ? "3" : "?"; } if (twice && t.Take(m)) { done++; got += "!"; } } }
                    Send(pieces); Send(piecesLate); Send(piecesLate); Send(piecesEarly); Send(pieces.Take(n / 2).ToArray()); Send(piecesLate);
                    Ok(got == "1232" && t.Song.SequenceEqual(lateOnly),
                       $"{shape}{(twice ? ", every piece twice" : "")}: a song, one that differs in its last piece only, that one again, one that differs in its first piece only, half of the very first and the second once more: each is whole once, and never a mix (whole: {got})");
                }

                var cut = pieces[3].Take(pieces[3].Length - 5).Append((byte)0xF7).ToArray(); t = new SfxSongTransfer();
                Ok(!t.Take(cut) && !SfxSongTransfer.IsPiece(cut) && t.Song == null, $"{shape}: a piece of the wrong length is not taken");
                t.Take(pieces[0]); t.Reset(); done = 0;
                foreach (int k in order.Skip(1)) if (t.Take(pieces[k])) done++;
                Ok(done == 0 && t.Song == null, $"{shape}: Reset forgets what was collected");
            }
            {   // the two shapes into the same class, the longest song, a single piece, a last piece of one byte, a count that cannot be, another format mid-way
                var t = new SfxSongTransfer(); int done = 0;
                foreach (var m in SfxProtocol.BuildSongPieces(song).Take(5)) t.Take(m);
                foreach (var m in SfxProtocol.BuildSong(fix5)) if (t.Take(m)) done++;
                bool oldAfterNew = done == 1 && t.Song.SequenceEqual(fix5);
                foreach (var m in SfxProtocol.BuildSongPieces(song)) if (t.Take(m)) done++;
                Ok(oldAfterNew && done == 2 && t.Song.SequenceEqual(song), "the message of before into the same collector, in the middle of one in the new shape: the format-5 song arrives whole; then the new one does");

                var longest = new byte[SfxProtocol.SongBytesMax]; longest[0] = 6; for (int i = 1; i < longest.Length; i++) longest[i] = (byte)(i * 11 % 128);
                var many = SfxProtocol.BuildPieces(longest);
                t = new SfxSongTransfer(); done = 0;
                foreach (var m in many.Reverse()) if (t.Take(m)) done++;
                Ok(many.Length == 160 && many.Length == SfxProtocol.SongPiecesMax && many.All(m => m.Length <= 251) && many[128][5] == 0 && many[128][6] == 1 && done == 1 && t.Song.SequenceEqual(longest), "38 355 bytes are 160 pieces, none over 251 bytes; in reverse they arrive whole");

                var tiny = new byte[211]; tiny[0] = 6; var one = SfxProtocol.BuildPieces(tiny);
                var oneOver = new byte[241]; oneOver[0] = 6; oneOver[240] = 77; var twoPieces = SfxProtocol.BuildPieces(oneOver);
                t = new SfxSongTransfer();
                bool single = one.Length == 1 && t.Take(one[0]) && t.Song.SequenceEqual(tiny) && !t.Take(one[0]);
                bool oneByte = twoPieces.Length == 2 && twoPieces[1].Length == 12 && !t.Take(twoPieces[1]) && t.Song == null && t.Take(twoPieces[0]) && t.Song.SequenceEqual(oneOver);
                Ok(single && oneByte, "a song in a single piece arrives with it (its twin is left alone); a last piece of one byte is a piece");

                byte[] Piece(int k, int K, int format, int bytes) { var m = new byte[11 + bytes]; new byte[] { 0xF0, 0x7D, 0x56, 0x42, 5, (byte)(k & 127), (byte)(k >> 7), (byte)format, (byte)(K & 127), (byte)(K >> 7) }.CopyTo(m, 0); if (k == 0 && bytes > 0) m[10] = (byte)format; m[m.Length - 1] = 0xF7; return m; }   // (a first piece begins with the song's format)
                t = new SfxSongTransfer();
                long spentBefore = GC.GetAllocatedBytesForCurrentThread();
                bool none = !t.Take(Piece(0, 161, 6, 240)) && !t.Take(Piece(0, 16383, 6, 240)) && !t.Take(Piece(160, 16383, 6, 240)) && !t.Take(Piece(0, 0, 6, 240)) && !t.Take(Piece(3, 3, 6, 240)) && !t.Take(Piece(0, 2, 6, 100))
                         && !t.Take(Piece(0, 1, 6, 241)) && !t.Take(Piece(0, 1, 6, 0)) && !t.Take(Piece(0, 2, 0, 240)) && !t.Take(Piece(0, 2, 7, 240)) && t.Song == null;
                long spent = GC.GetAllocatedBytesForCurrentThread() - spentBefore;
                Ok(none && spent < 16000 && !SfxSongTransfer.IsPiece(Piece(0, 161, 6, 240)) && SfxSongTransfer.IsPiece(Piece(0, 160, 6, 240)) && SfxSongTransfer.IsPiece(Piece(159, 160, 6, 1)),
                   $"161 pieces, 16 383 pieces, none, a piece past the count, a short piece that is not the last, one too long, an empty one, format 0, format 7: not pieces, and nothing is set aside for them ({spent} bytes for the ten messages themselves)");

                var a6 = SfxProtocol.BuildSongPieces(song); var as5 = (byte[])song.Clone(); as5[0] = 5; var a5 = SfxProtocol.BuildPieces(as5);
                t = new SfxSongTransfer(); done = 0;
                foreach (var m in a6.Take(a6.Length - 1)) t.Take(m);
                if (t.Take(a5[a5.Length - 1])) done++;                                        // the last piece, but it names another format: a new transfer
                bool anew = done == 0 && t.Song == null;
                foreach (var m in a5.Take(a5.Length - 1)) if (t.Take(m)) done++;
                Ok(anew && done == 1 && t.Song.SequenceEqual(as5), "a piece that names another format begins a new transfer: the pieces before it are not mixed into it");

                var lying = SfxProtocol.BuildSongPieces(song).Select(m => { var c = (byte[])m.Clone(); c[7] = 5; return c; }).ToArray();   // the pieces name format 5, the song begins with 6
                t = new SfxSongTransfer(); done = 0;
                foreach (var m in lying) if (t.Take(m)) done++;
                Ok(done == 0 && t.Song == null && !SfxSongTransfer.IsPiece(lying[0]), "a first piece whose bytes begin with another format than its pieces name is no piece: the song does not arrive");

                var sound = SfxProtocol.BuildLoadSongSound(3, sounds[3]);
                Ok(sound[4] == SfxProtocol.Load && sound[5] == 19 && sound.Length == 7 + SfxPatch.Bytes && SfxPatch.TryUnpack(sound, 6, out var soundBack) && soundBack.Pack().SequenceEqual(sounds[3].Pack()),
                   "a song's sound goes out as LOAD for slot 16 + sound (the MUSIC kit's pad)");
#pragma warning disable 618     // the reader of the old shape alone still reads it
                var got = new byte[SfxProtocol.SongBytesV5]; ulong mask = 0; done = 0;
                foreach (var m in SfxProtocol.BuildSong(fix5).Reverse()) { if (SfxProtocol.TryTakeSongChunk(m, got, ref mask)) done++; if (SfxProtocol.TryTakeSongChunk(m, got, ref mask)) done++; }
                Ok(done == 1 && got.SequenceEqual(fix5) && !SfxProtocol.TryTakeSongChunk(a6[0], got, ref mask), "TryTakeSongChunk (kept for those who call it) still takes the 39 pieces of before, in reverse and doubled, and none of the new shape");
#pragma warning restore 618
            }
            {   // through the link: a song in the message of firmware 1.6 arrives with its own length
                using var link = new SfxLink();
                byte[] came = null; int times = 0;
                link.SongReceived += (s, _) => { came = s; times++; };
                foreach (var m in SfxProtocol.BuildSongPieces(song).Reverse()) { link.Take(m); link.Take(m); }
                Ok(times == 1 && came != null && came.SequenceEqual(song), $"the link hands a song in the new shape on, whole and once, with its own length ({(came == null ? 0 : came.Length)} bytes)");
            }

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

            // the loop flag: the pattern repeats until a cue; a cued one starts on the bar line and stays; off, the arrangement goes on
            Ok(SfxNative.PlayerSetLoop(pl, true), "the library has the loop flag (sfxn_player_set_loop)");
            SfxNative.PlayerStart(pl, 0);
            Play(0f, 750);
            SfxNative.PlayerState(pl, st);
            Ok(st[0] == 1 && st[3] == 1 && st[2] == 0, $"loop on: after 4 s it is in bar 2 and still on pattern A (bar {st[3] + 1}, pattern {(char)('A' + st[2])})");
            SfxNative.PlayerStart(pl, 0);
            int loopChangedAt = -1, loopLeft = -1, atBlock1200 = -1, afterOff = -1, barOff = -1;
            Play(1f, 1700, b =>
            {
                if (b == 100) SfxNative.PlayerCue(pl, 5);
                SfxNative.PlayerState(pl, st);
                if (st[2] == 5 && loopChangedAt < 0) loopChangedAt = b;
                if (loopChangedAt >= 0 && b < 1200 && st[2] != 5 && loopLeft < 0) loopLeft = b;
                if (b == 1200) { atBlock1200 = st[2]; barOff = st[3]; SfxNative.PlayerSetLoop(pl, false); }
                if (b > 1200 && st[3] != barOff && afterOff < 0) afterOff = st[2];       // the pattern of the first bar after it was switched off
            });
            Ok(loopChangedAt >= 373 && loopChangedAt <= 377, $"loop on: a cue at 0.5 s changes the pattern on the bar line at 2.0 s (block {loopChangedAt})");
            Ok(loopLeft < 0 && atBlock1200 == 5, $"and that pattern stays, bar after bar: still F three bars later (pattern {(char)('A' + atBlock1200)})");
            Ok(afterOff == 0, $"loop off while it plays: on in the arrangement from F's bar - its last, so the top comes next (pattern {(char)('A' + afterOff)})");
            var before = new int[4]; SfxNative.PlayerState(pl, before);
            SfxNative.PlayerSetLoop(pl, false);
            SfxNative.PlayerState(pl, st);
            Ok(st.SequenceEqual(before), "switching it off a second time changes nothing");
            SfxNative.PlayerStop(pl);

            // a sound as a note: A4 is 440 Hz whatever the sound's own FREQ says
            for (int i = 0; i < 60; i++) Play(1f, 10);                               // let everything ring out
            SfxNative.PlayerNoteOn(pl, 6, 1, 1f, 69);
            var tone = Play(1f, 60);
            Ok(Math.Abs(Pitch(tone, 0.05, 0.2) - 440) < 6, $"a sound played as the note A4 is in tune ({Pitch(tone, 0.05, 0.2):0} Hz)");

            // the band of the MUSIC kit, measured (nobody here can listen): the drums are drums, the FM instruments are in tune
            float[] One(int slot, int note = 0, float hold = 0.3f) { SfxNative.Bake(sounds[slot], out var x, out _, 48000, 1f, note, 0f, hold); return x; }
            double Bright(float[] x, double t0, double t1) => Cycles(x, t0, t1) / (t1 - t0);                     // zero crossings a second
            var kick = One(2); var snare = One(1); var hat = One(12); var bass = One(6, 45); var keys = One(7, 69);
            Ok(Pitch(kick, 0.12, 0.25) < 110 && Rms(kick, 0.0, 0.05) > 0.15 && kick.Length < 48000, $"KICK: a low drum ({Pitch(kick, 0.12, 0.25):0} Hz after its fall, {kick.Length / 48000f:0.00} s)");
            Ok(Bright(snare, 0.0, 0.1) > 1500 && Rms(snare, 0.4, 0.5) < Rms(snare, 0.0, 0.1) * 0.1, $"SNARE: noise over a tone ({Bright(snare, 0.0, 0.1):0} crossings a second, {snare.Length / 48000f:0.00} s)");
            Ok(Bright(hat, 0.0, 0.03) > 6000 && hat.Length < 48000 * 0.35, $"HI-HAT: a short, bright tick ({Bright(hat, 0.0, 0.03):0} crossings a second, {hat.Length / 48000f:0.00} s)");
            Ok(Math.Abs(Pitch(bass, 0.05, 0.25) - 110) < 2 && Rms(bass, 0.0, 0.1) > 0.1, $"FM BASS: the note A2 is {Pitch(bass, 0.05, 0.25):0.0} Hz, level {Rms(bass, 0.0, 0.1):0.00}");
            Ok(Math.Abs(Pitch(keys, 0.05, 0.25) - 440) < 6 && Rms(keys, 0.0, 0.1) > 0.1, $"FM KEYS: the note A4 is {Pitch(keys, 0.05, 0.25):0} Hz, level {Rms(keys, 0.0, 0.1):0.00}");
            Ok(Rms(bass, 0.0, 0.03) > Rms(bass, 0.2, 0.3) * 1.5 && Rms(keys, 0.0, 0.05) > Rms(keys, 0.5, 0.6) * 1.5, "both are plucked: loudest at the start, then they fall");

            // a chord on the strings: 16 voices, held, then let go
            for (int i = 0; i < 60; i++) Play(1f, 10);
            int[] chord = { 45, 52, 57, 60, 64, 69, 72, 76 };
            foreach (int n in chord) SfxNative.PlayerNoteOn(pl, 15, 100 + n, 0.8f, n);
            var held = Play(1f, 300);
            foreach (int n in chord) SfxNative.PlayerNoteOff(pl, 100 + n);
            var after = Play(1f, 900);
            Ok(sounds[15].engine == SfxEngine.Wavetable && Rms(held, 1.2, 1.6) > 0.05 && held.All(x => !float.IsNaN(x)) && Rms(after, 4.0, 4.8) < 0.002,
               $"an 8-note chord on the strings holds ({Rms(held, 1.2, 1.6):0.000}) and dies away after the keys are let go ({Rms(after, 4.0, 4.8):0.0000})");
            // ---- the mixer: it is part of the song, and a game may move it
            for (int i = 0; i < 60; i++) Play(1f, 10);
            (double l, double r) Hit(int slot)
            {
                var bl = new float[256]; var br = new float[256]; double sl = 0, sr = 0;
                SfxNative.PlayerNoteOn(pl, slot, 1, 1f);
                for (int b = 0; b < 60; b++) { Array.Clear(bl, 0, 256); Array.Clear(br, 0, 256); SfxNative.PlayerRender(pl, bl, br, 256); for (int i = 0; i < 256; i++) { sl += bl[i] * bl[i]; sr += br[i] * br[i]; } }
                SfxNative.PlayerNoteOff(pl, 1);
                for (int b = 0; b < 400; b++) SfxNative.PlayerRender(pl, bl, br, 256);
                return (Math.Sqrt(sl / 15360), Math.Sqrt(sr / 15360));
            }
            SfxNative.PlayerSetTrack(pl, 2, 100, 64, 0);
            var full = Hit(2);
            SfxNative.PlayerSetTrack(pl, 2, 50, -1, -1);
            var half = Hit(2);
            Ok(Math.Abs(half.l / full.l - 0.5) < 0.02 && Math.Abs(full.l - full.r) < full.l * 0.001, $"MIX: a fader at 50 halves a sound ({full.l:0.000} -> {half.l:0.000}), the middle is the same on both sides");
            SfxNative.PlayerSetTrack(pl, 2, 100, 0, -1);
            var leftOnly = Hit(2);
            SfxNative.PlayerSetTrack(pl, 2, -1, 127, -1);
            var rightOnly = Hit(2);
            Ok(leftOnly.r < full.l * 0.001 && Math.Abs(leftOnly.l - full.l) < full.l * 0.01 && rightOnly.l < full.l * 0.001 && Math.Abs(rightOnly.r - full.l) < full.l * 0.01, $"MIX: pan puts a sound left ({leftOnly.l:0.000} / {leftOnly.r:0.000}) or right ({rightOnly.l:0.000} / {rightOnly.r:0.000})");
            SfxNative.PlayerSetTrack(pl, 2, -1, 64, 1);
            var muted = Hit(2);
            SfxNative.PlayerSetTrack(pl, 2, -1, -1, 0);
            var back = Hit(2);
            Ok(muted.l < 1e-6 && muted.r < 1e-6 && back.l > full.l * 0.99, $"MIX: a muted sound is silent ({muted.l:0.0000}), and back when the mute goes ({back.l:0.000})");
            SfxNative.PlayerSetMaster(pl, SfxMaster.Drive, 127);
            var driven = Hit(2);
            SfxNative.PlayerSetMaster(pl, SfxMaster.Drive, 0);
            Ok(Math.Abs(driven.l - full.l) > full.l * 0.05, $"MASTER: DRIVE changes the sum ({full.l:0.000} -> {driven.l:0.000})");
            var mixed = (byte[])song.Clone();
            mixed[7 + 16 + 2] = 50; mixed[7 + 32 + 2] = 127;                           // sound 3: fader 50, hard right
            Ok(SfxNative.PlayerSetSong(pl, mixed), "a song with another mix is taken");
            var fromSong = Hit(2);
            Ok(fromSong.l < full.l * 0.001 && Math.Abs(fromSong.r / full.l - 0.5) < 0.02, $"the mix is part of the song: fader and pan come with it ({fromSong.l:0.000} / {fromSong.r:0.000})");
            // songs from before: format 4 (one lane a sound), 3 (no automation, no arrangement), 2 (no velocity, no length) and 1 (no mixer either).
            // The head is 147 bytes here (66, the arrangement's 33, 24 lanes of 2), 115 in format 4, 66 in formats 2 and 3, 23 in format 1; a track is
            // 3 + 32 bytes here and in format 3, 3 + 48 in format 4, 3 + 16 before. The lanes' points follow the tracks: 16 for each lane of each pattern.
            // (made of the format-5 fixture: the song the firmware sends is format 6 now, with only the rows that are used)
            const int Head = 147, Track = 35, Lanes = Head - 48, Points = Head + 128 * Track;
            var v3 = new byte[SfxProtocol.SongBytesV3]; var v2 = new byte[SfxProtocol.SongBytesV2]; var old = new byte[SfxProtocol.SongBytesV1];
            Array.Copy(mixed, 0, v3, 0, 66); Array.Copy(mixed, 0, v2, 0, 66); Array.Copy(fix5, 0, old, 0, 23); v3[0] = 3; v2[0] = 2; old[0] = 1;
            for (int k = 0; k < 128; k++) { Array.Copy(fix5, Head + k * Track, v3, 66 + k * 35, 35); Array.Copy(fix5, Head + k * Track, v2, 66 + k * 19, 19); Array.Copy(fix5, Head + k * Track, old, 23 + k * 19, 19); }
            bool took3 = SfxNative.PlayerSetSong(pl, v3); var from3 = Hit(2);
            Ok(took3 && from3.l < full.l * 0.001 && Math.Abs(from3.r / full.l - 0.5) < 0.02, "a format 3 song still loads, with its mix");
            bool took2 = SfxNative.PlayerSetSong(pl, v2); var from2 = Hit(2);
            Ok(took2 && from2.l < full.l * 0.001 && Math.Abs(from2.r / full.l - 0.5) < 0.02, "a format 2 song still loads, with its mix");
            Ok(SfxNative.PlayerSetSong(pl, old) && Math.Abs(Hit(2).l - full.l) < full.l * 0.01, "a format 1 song still loads, with every fader as designed");

            // velocity and length: a step of the strings, soft and loud, held for 2 steps and for 8
            double[] Bar(int vel, int len, Action<byte[], int> edit = null)
            {
                var one = (byte[])fix5.Clone();                                        // (a format-5 song: its rows stand at fixed places)
                for (int i = 66; i < one.Length; i++) one[i] = 0;                      // no lanes, no arrangement, no steps at all …
                for (int k = 0; k < 128; k++) for (int i = 0; i < 16; i++) one[Head + k * Track + 19 + i] = 5;
                int at = Head + 15 * Track;                                            // … but pattern A, sound 16, step 1: the note A3
                one[at] = 1; one[at + 3] = 57; one[at + 19] = (byte)(len << 3 | vel); one[6] = 1;
                edit?.Invoke(one, at);
                if (!SfxNative.PlayerSetSong(pl, one)) throw new Exception("the test's song was refused");
                for (int i = 0; i < 40; i++) Play(1f, 10);
                SfxNative.PlayerStart(pl, 0);
                var a = Play(1f, 375);                                                 // one bar: 2 s
                SfxNative.PlayerStop(pl);
                return new[] { Rms(a, 0.15, 0.24), Rms(a, 0.6, 0.9), Rms(a, 1.75, 1.95) };
            }
            var soft = Bar(1, 8); var loud = Bar(7, 8); var shortNote = Bar(7, 2); var auto = Bar(7, 0);
            Ok(loud[0] > soft[0] * 2.5, $"VELOCITY: a hard step is louder than a soft one ({soft[0]:0.000} -> {loud[0]:0.000})");
            Ok(shortNote[1] < loud[1] * 0.25 && loud[1] > 0.02, $"LENGTH: 2 steps is over where 8 steps still sounds ({shortNote[1]:0.000} against {loud[1]:0.000} at 0.6 s)");
            Ok(auto[2] > loud[2] * 3, $"LENGTH AUTO holds to the bar line, 8 steps ends before it ({auto[2]:0.000} against {loud[2]:0.000} at 1.8 s)");

            // automation: a lane is a sound (+ 1) and a knob in the head, and 16 points (value + 1) for each pattern after the tracks
            var faded = Bar(7, 0, (b, at) => { b[Lanes] = 16; b[Lanes + 1] = 26; b[Points] = 101; b[Points + 8] = 21; });      // the strings' fader: 100, from step 9 on 20
            // (a held chord of detuned oscillators is not the same twice: compare how the second half stands to the first)
            double R(double[] x) => x[2] / x[1]; double plain = R(auto);
            Ok(Math.Abs(R(faded) / plain - 0.2) < 0.06, $"AUTOMATION of a fader: a point at step 9 takes a held note from {faded[1]:0.000} to {faded[2]:0.000} ({R(faded) / plain:0.00} of what it would be)");
            var dulled = Bar(7, 0, (b, at) => { b[Lanes] = 16; b[Lanes + 1] = 1 + 6; b[Points + 8] = 11; });                  // the sound's own CUTOFF: 10 from step 9 on
            Ok(R(dulled) < plain * 0.5, $"AUTOMATION of a sound's own knob: CUTOFF closes on a held note ({dulled[1]:0.000} -> {dulled[2]:0.000})");
            var both = Bar(7, 0, (b, at) => { b[Lanes] = 16; b[Lanes + 1] = 26; b[Points] = 101; b[Points + 8] = 21;          // two lanes on one sound: its fader …
                                              b[Lanes + 2] = 16; b[Lanes + 3] = 1 + 6; b[Points + 16 + 8] = 11; });              // … and its CUTOFF, together
            Ok(R(both) < R(faded) * 0.5 && R(both) < R(dulled) * 1.1,   // (what is left at the end is the reverb's tail)
                $"AUTOMATION of two knobs of one sound at once: quieter and duller than either alone ({R(faded):0.00}, {R(dulled):0.00}, both {R(both):0.00})");
            var other = Bar(7, 0, (b, at) => { b[Lanes] = 4; b[Lanes + 1] = 28 + 4; b[Points + 8] = 127; });                   // a lane of sound 4 moves the master's DRIVE
            Ok(R(other) > plain * 1.4, $"AUTOMATION of the master from any lane: DRIVE comes in at step 9 ({other[1]:0.000} -> {other[2]:0.000})");
            Bar(7, 0, (b, at) => { b[Lanes] = 3; b[Lanes + 1] = 1 + 6; b[Points] = 6; });                                       // a lane closes sound 3's CUTOFF …
            for (int i = 0; i < 80; i++) Play(1f, 10);                                                              // (the strings ring out)
            SfxNative.PlayerSetTrack(pl, 2, 100, 64, 0);                                                            // (its fader as when it was measured)
            var untouched = Hit(2);
            Ok(Math.Abs(untouched.l / full.l - 1) < 0.01, $"and it writes nothing: sound 3 by hand is as it was made ({full.l:0.000} / {untouched.l:0.000})");

            // the arrangement: A C B, a bar each, then from the top – and a cue
            var arranged = (byte[])song.Clone();
            arranged[66] = 3; arranged[67] = 0; arranged[68] = 2; arranged[69] = 1;
            SfxNative.PlayerSetSong(pl, arranged);
            SfxNative.PlayerStart(pl, 0);
            var seen2 = new System.Collections.Generic.List<int>();
            Play(0f, 2250, b => { if (b % 375 == 180) { SfxNative.PlayerState(pl, st); seen2.Add(st[2]); } if (b == 1600) SfxNative.PlayerCue(pl, 7); });
            Ok(seen2.Take(4).SequenceEqual(new[] { 0, 2, 1, 0 }), $"the ARRANGEMENT plays its bars in order, then again from the top ({string.Join(" ", seen2.Take(4).Select(x => (char)('A' + x)))})");
            Ok(seen2[4] == 2 && seen2[5] == 7, $"a cued pattern that is not in the arrangement takes over on the bar line ({(char)('A' + seen2[4])} then {(char)('A' + seen2[5])})");
            seen2.Clear();
            Play(0f, 1125, b => { if (b % 375 == 180) { SfxNative.PlayerState(pl, st); seen2.Add(st[2]); } if (b == 400) SfxNative.PlayerCue(pl, 1); });
            Ok(seen2.SequenceEqual(new[] { 7, 7, 1 }), $"and stays until the next cue; a cue to a pattern of the arrangement goes on from that bar ({string.Join(" ", seen2.Select(x => (char)('A' + x)))})");
            SfxNative.PlayerStop(pl);

            // a format 4 song: one lane a sound, its target in the head after the mix, its points at the end of each track (3 + 48 bytes)
            var v4 = new byte[SfxProtocol.SongBytesV4];
            Array.Copy(song, 0, v4, 0, 66); Array.Copy(song, 66, v4, 82, 33); v4[0] = 4; v4[82] = 0; v4[6] = 1;
            for (int i = 115; i < v4.Length; i++) v4[i] = 0;
            for (int k = 0; k < 128; k++) for (int i = 0; i < 16; i++) v4[115 + k * 51 + 19 + i] = 5;
            v4[66 + 15] = 26;                                                          // the strings' lane moves their fader …
            { int at = 115 + 15 * 51; v4[at] = 1; v4[at + 3] = 57; v4[at + 19] = 7; v4[at + 35] = 101; v4[at + 35 + 8] = 21; }   // … 100, then 20 from step 9
            bool took4 = SfxNative.PlayerSetSong(pl, v4);
            for (int i = 0; i < 40; i++) Play(1f, 10);
            SfxNative.PlayerStart(pl, 0);
            var four = Play(1f, 375);
            SfxNative.PlayerStop(pl);
            double r4 = Rms(four, 1.75, 1.95) / Rms(four, 0.6, 0.9);
            Ok(took4 && r4 / plain < 0.45, $"a format 4 song still loads, and its one lane a sound still moves its knob ({r4 / plain:0.00} of the level)");
            Marshal.FreeHGlobal(pl);

            // the demo project's song: exactly what the firmware sent
            var demo = new byte[16 * SfxPatch.Bytes + song.Length];
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
        Console.WriteLine("  ----  receiving MIDI is not covered here: it needs the device. See unity/Demo/Assets/Showcase/Tests/LinkTakes.cs");

        Console.WriteLine(failed > 0 ? $"FAILED ({failed})" : "PASSED");
        return failed > 0 ? 1 : 0;
    }
}
