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
            var fm = new SfxDeviceInfo { hardware = 1, major = 1, minor = 4, patch = 0, profile = SfxProfile.VibeFm1, flags = 15 };
            var id = SfxProtocol.BuildIdentity(fm);
            var want = new byte[] { 0xF0, 0x7D, 0x56, 0x42, 0x08, 1, 1, 4, 0, 1, 2, 5, 16, 2, 8, 16, 24, 32, 16, 5, 6, 25, 15, 0xF7 };
            Ok(id.SequenceEqual(want) && SfxProtocol.IdentityBytes == 18, "IDENTITY of firmware 1.4.0 on VIBE FM is F0 7D 56 42 08, these 18 bytes, F7: " + SfxLink.Describe(id));
            bool back = SfxProtocol.TryParseIdentity(id, out var got);
            Ok(back && got.Equals(fm) && got.profile == SfxProfile.VibeFm1 && got.HasPads && got.HasEncoders && got.TakesSongSounds && got.TakesTelemetry && got.IsAtLeast(1, 4, 0) && !got.IsAtLeast(1, 4, 1) && got.IsAtLeast(1, 3, 9),
               "an identity survives build -> parse: hardware, version, the profile of this package, all four flags");
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
            var as35 = Padded(7 + SfxPatch.Bytes); var as208 = Padded(8 + SfxProtocol.SongChunkBytes);
            var asPatch = SfxProtocol.BuildLoad(0, laser); asPatch[4] = SfxProtocol.Identity;            // a valid patch's bytes under the identity's command
            var songBuf = new byte[SfxProtocol.SongBytes]; ulong seenBits = 0;
            bool notPatch = !SfxProtocol.TryParsePatch(as35, out _, out _) && !SfxProtocol.TryParsePatch(asPatch, out _, out _) && !SfxProtocol.TryParsePatch(id, out _, out _);
            bool notPiece = !SfxProtocol.TryTakeSongChunk(as208, songBuf, ref seenBits) && !SfxProtocol.TryTakeSongChunk(as35, songBuf, ref seenBits) && !SfxProtocol.TryTakeSongChunk(id, songBuf, ref seenBits) && seenBits == 0 && songBuf.All(x => x == 0);
            Ok(as35.Length == 35 && as208.Length == 208 && notPatch && notPiece && SfxProtocol.TryParseIdentity(as35, out _) && SfxProtocol.TryParseIdentity(as208, out _),
               "an identity is neither a patch nor a piece of a song, also when it is 35 or 208 bytes long");

            // the link: an identity in the middle of a song that is arriving
            using (var link = new SfxLink())
            {
                var theSong = new byte[SfxProtocol.SongBytes]; theSong[0] = 5; for (int i = 1; i < theSong.Length; i++) theSong[i] = (byte)(i * 7 % 128);
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
                   "a device of this package's formats is refused nothing (without a port nothing is sent, and that is no refusal)");

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
                var laterSong = (byte[])theSong.Clone(); laterSong[0] = 6;
                foreach (var piece in SfxProtocol.BuildSong(laterSong)) { link.Take(piece); link.Take(piece); }
                Ok(said2.Count == 2 && said2[1].Contains("a song in format 6") && said2[1].Contains("knows up to 5") && songs == songsBefore,
                   $"a song in format 6, every piece twice, is said to be newer once and does not arrive as a song: \"{(said2.Count > 1 ? said2[1] : "")}\"");
                foreach (var piece in pieces) link.Take(piece);
                Ok(songs == songsBefore + 1 && arrived.SequenceEqual(theSong) && said2.Count == 2, "and this version's song after it arrives whole");

                link.Close();
                Ok(link.Device == null && link.LastRefusal == null, "closing the link forgets the device");
            }
            Ok(SfxLink.Refusal(null, true) == null && SfxLink.Refusal(null, false) == null && SfxLink.Refusal(SfxDeviceInfo.AssumedBefore14, true) == null,
               "a device that never answered is refused nothing: it is taken for a firmware before 1.4");
            var assumed = SfxDeviceInfo.AssumedBefore14;
            Ok(assumed.ToString() == "VIBE FM, firmware before 1.4, profile 1" && assumed.profile == SfxProfile.VibeFm1 && !assumed.IsAtLeast(1, 0, 0) && assumed.HasPads && assumed.HasEncoders && assumed.TakesTelemetry && !assumed.TakesSongSounds,
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
            var song6 = new byte[SfxProtocol.SongBytes]; song6[0] = 6;
            var song5 = new byte[SfxProtocol.SongBytes]; song5[0] = 5;
            Ok(SfxProtocol.IsNewerSong(song6) && SfxProtocol.IsNewerSong(song6.Concat(new byte[300]).ToArray()) && !SfxProtocol.IsNewerSong(song5) && !SfxProtocol.IsNewerSong(new byte[] { 0x40 })
               && !SfxProtocol.IsNewerSong(song6.Take(SfxProtocol.SongBytes - 1).ToArray()) && !SfxProtocol.IsNewerSong(null) && !SfxProtocol.IsNewerSong(new byte[0])
               && SfxProtocol.NewerIn(SfxProtocol.BuildLoad(0, laser)) == null && SfxProtocol.NewerIn(SfxProtocol.BuildRequestIdentity()) == null && SfxProtocol.NewerIn(null) == null,
               "a packed song of a higher format and at least today's length is newer; one byte, a cut-off one, this version's are not");

            // the profile
            var p1 = SfxProfile.VibeFm1;
            Ok(p1.id == 1 && p1.patchFormat == 2 && p1.songFormat == 5 && p1.sounds == 16 && p1.kits == 2 && p1.patterns == 8 && p1.steps == 16 && p1.lanes == 24 && p1.bars == 32 && p1.voices == 16
               && p1.engines == 5 && p1.wavetables == 6 && p1.patchValues == 25, "the profile of VIBE FM is made of the package's constants: " + p1);
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
            var s5 = new byte[SfxProtocol.SongBytes]; s5[0] = 5; s5[1] = 16; s5[66] = 32;
            var s6 = (byte[])s5.Clone(); s6[0] = 6;
            var tooLong = (byte[])s5.Clone(); tooLong[1] = 17;
            var tooManyBars = (byte[])s5.Clone(); tooManyBars[66] = 33;
            var highByte = (byte[])s5.Clone(); highByte[5000] = 128;
            var s4 = new byte[SfxProtocol.SongBytesV4]; s4[0] = 4; s4[1] = 16; s4[82] = 8;
            var s1 = new byte[SfxProtocol.SongBytesV1]; s1[0] = 1; s1[1] = 16;
            var wrongLength = new byte[SfxProtocol.SongBytesV3]; wrongLength[0] = 5;
            var shortBars = p1; shortBars.bars = 4;
            var onlyOld = p1; onlyOld.songFormat = 4;
            Ok(p1.Fits(s5) && p1.Fits(s4) && p1.Fits(s1) && !p1.Fits(s6) && !p1.Fits(tooLong) && !p1.Fits(tooManyBars) && !p1.Fits(highByte) && !p1.Fits(wrongLength) && !p1.Fits((byte[])null) && !p1.Fits(new byte[0])
               && !shortBars.Fits(s5) && !shortBars.Fits(s4) && shortBars.Fits(s1) && !onlyOld.Fits(s5) && onlyOld.Fits(s4),
               "a packed song fits it in every format up to its own; not a newer one, a wrong length, a byte over 127, more steps or more bars than the device holds");
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
            var song = new byte[SfxProtocol.SongBytes]; var sounds = new SfxPatch[16]; ulong next = 0; int seen = 0; bool whole = false;
            foreach (var msg in msgs.Skip(msgs.FindLastIndex(b => b[0] == 0xF0 && b[4] == SfxProtocol.Dump && b[5] == 0)))
            {
                if (SfxProtocol.TryParsePatch(msg, out int slot, out var patch)) { sounds[slot] = patch; seen |= 1 << slot; }
                else if (SfxProtocol.TryTakeSongChunk(msg, song, ref next)) whole = true;
            }
            Ok(whole && seen == 0xFFFF, "the firmware's song arrives whole: 16 sounds and 39 pieces");
            {   // how a song's pieces are taken: in any order, twins left alone, another song's piece begins anew
                var pieces = SfxProtocol.BuildSong(song);
                var songTwo = (byte[])song.Clone();
                for (int k = 0; k < pieces.Length; k++) { int at = k * SfxProtocol.SongChunkBytes + 50; songTwo[at] = (byte)(songTwo[at] ^ 1); }   // (another song: every piece differs)
                var piecesTwo = SfxProtocol.BuildSong(songTwo);
                var order = Enumerable.Range(0, pieces.Length).ToArray(); (order[36], order[37]) = (order[37], order[36]); (order[9], order[10]) = (order[10], order[9]);
                byte[] Fresh() { var b = new byte[SfxProtocol.SongBytes]; for (int i = 0; i < b.Length; i++) b[i] = 0x55; return b; }

                var got = Fresh(); ulong mask = 0; int done = 0;
                foreach (int k in order) if (SfxProtocol.TryTakeSongChunk(pieces[k], got, ref mask)) done++;
                Ok(done == 1 && got.SequenceEqual(song), "a song whose pieces come out of order arrives whole all the same");

                got = Fresh(); mask = 0; done = 0;
                foreach (int k in order) { if (SfxProtocol.TryTakeSongChunk(pieces[k], got, ref mask)) done++; if (SfxProtocol.TryTakeSongChunk(pieces[k], got, ref mask)) done++; }
                Ok(done == 1 && got.SequenceEqual(song), "a song whose every piece comes twice arrives whole, once");
                foreach (int k in order) { if (SfxProtocol.TryTakeSongChunk(piecesTwo[k], got, ref mask)) done++; if (SfxProtocol.TryTakeSongChunk(piecesTwo[k], got, ref mask)) done++; }
                Ok(done == 2 && got.SequenceEqual(songTwo), "and the next song, straight after and doubled too, arrives whole and is the next song (no piece of the first in it)");

                got = Fresh(); mask = 0; done = 0;
                foreach (int k in Enumerable.Range(0, pieces.Length).Where(k => k != 5)) SfxProtocol.TryTakeSongChunk(pieces[k], got, ref mask);     // a song that lost a piece …
                bool early = false;
                foreach (int k in Enumerable.Range(0, pieces.Length)) { bool pieceMadeItWhole = SfxProtocol.TryTakeSongChunk(piecesTwo[k], got, ref mask); if (pieceMadeItWhole) { done++; early |= k != pieces.Length - 1; } }   // … and another, whole, after it
                Ok(done == 1 && !early && got.SequenceEqual(songTwo), "after a song that lost a piece, the next is whole only with its own last piece, and is the next song");

                got = Fresh(); mask = 0; done = 0;
                foreach (int k in order.Take(20)) SfxProtocol.TryTakeSongChunk(pieces[k], got, ref mask);
                foreach (int k in order.Reverse()) if (SfxProtocol.TryTakeSongChunk(piecesTwo[k], got, ref mask)) done++;
                Ok(done == 1 && got.SequenceEqual(songTwo), "after a transfer that broke off, another song sent last piece first arrives whole");

                var cut = pieces[3].Take(pieces[3].Length - 5).Append((byte)0xF7).ToArray(); mask = 0;
                Ok(!SfxProtocol.TryTakeSongChunk(cut, got, ref mask) && mask == 0, "a piece of the wrong length is not taken");
                var sound = SfxProtocol.BuildLoadSongSound(3, sounds[3]);
                Ok(sound[4] == SfxProtocol.Load && sound[5] == 19 && sound.Length == 7 + SfxPatch.Bytes && SfxPatch.TryUnpack(sound, 6, out var soundBack) && soundBack.Pack().SequenceEqual(sounds[3].Pack()),
                   "a song's sound goes out as LOAD for slot 16 + sound (the MUSIC kit's pad)");
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
            const int Head = 147, Track = 35, Lanes = Head - 48, Points = Head + 128 * Track;
            var v3 = new byte[SfxProtocol.SongBytesV3]; var v2 = new byte[SfxProtocol.SongBytesV2]; var old = new byte[SfxProtocol.SongBytesV1];
            Array.Copy(mixed, 0, v3, 0, 66); Array.Copy(mixed, 0, v2, 0, 66); Array.Copy(song, 0, old, 0, 23); v3[0] = 3; v2[0] = 2; old[0] = 1;
            for (int k = 0; k < 128; k++) { Array.Copy(song, Head + k * Track, v3, 66 + k * 35, 35); Array.Copy(song, Head + k * Track, v2, 66 + k * 19, 19); Array.Copy(song, Head + k * Track, old, 23 + k * 19, 19); }
            bool took3 = SfxNative.PlayerSetSong(pl, v3); var from3 = Hit(2);
            Ok(took3 && from3.l < full.l * 0.001 && Math.Abs(from3.r / full.l - 0.5) < 0.02, "a format 3 song still loads, with its mix");
            bool took2 = SfxNative.PlayerSetSong(pl, v2); var from2 = Hit(2);
            Ok(took2 && from2.l < full.l * 0.001 && Math.Abs(from2.r / full.l - 0.5) < 0.02, "a format 2 song still loads, with its mix");
            Ok(SfxNative.PlayerSetSong(pl, old) && Math.Abs(Hit(2).l - full.l) < full.l * 0.01, "a format 1 song still loads, with every fader as designed");

            // velocity and length: a step of the strings, soft and loud, held for 2 steps and for 8
            double[] Bar(int vel, int len, Action<byte[], int> edit = null)
            {
                var one = (byte[])song.Clone();
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
        Console.WriteLine("  ----  receiving MIDI is not covered here: it needs the device. See unity/Demo/Assets/Showcase/Tests/LinkTakes.cs");

        Console.WriteLine(failed > 0 ? $"FAILED ({failed})" : "PASSED");
        return failed > 0 ? 1 : 0;
    }
}
