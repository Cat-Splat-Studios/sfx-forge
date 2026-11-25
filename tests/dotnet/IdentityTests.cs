// The identity (firmware 1.4.0 on): what a device says it is, what SfxLink does with it, and what is
// sent to whom - the refusals of a patch or a song in a format the device does not speak.
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
    // the identity: what a device says it is (firmware 1.4.0 on), and what is done with it
    static void Identity()
    {
        var ask = SfxProtocol.BuildRequestIdentity();
        Ok(
            ask.SequenceEqual(new byte[] { 0xF0, 0x7D, 0x56, 0x42, 0x07, 0xF7 }),
            "REQUEST IDENTITY is F0 7D 56 42 07 F7, six bytes"
        );
        var fm = new SfxDeviceInfo
        {
            hardware = 1,
            major = 1,
            minor = 4,
            patch = 0,
            profile = SfxProfile.VibeFm15,
            flags = 15,
        };
        var id = SfxProtocol.BuildIdentity(fm);

        // csharpier-ignore
        var want = new byte[]
        {
            0xF0, 0x7D, 0x56, 0x42, 0x08, 1, 1, 4, 0, 1, 2, 5, 16, 2, 8, 16, 24, 32, 16, 5, 6, 25, 15, 0xF7,
        };
        Ok(
            id.SequenceEqual(want) && SfxProtocol.IdentityBytes == 18,
            "IDENTITY of firmware 1.4.0 on VIBE FM is F0 7D 56 42 08, these 18 bytes, F7: " + SfxLink.Describe(id)
        );
        bool back = SfxProtocol.TryParseIdentity(id, out var got);
        Ok(
            back
                && got.Equals(fm)
                && got.profile == SfxProfile.VibeFm15
                && got.HasPads
                && got.HasEncoders
                && got.TakesSongSounds
                && got.TakesTelemetry
                && got.IsAtLeast(1, 4, 0)
                && !got.IsAtLeast(1, 4, 1)
                && got.IsAtLeast(1, 3, 9),
            "an identity survives build -> parse: hardware, version, the profile firmware 1.4 and 1.5 said, "
                + "all four flags"
        );
        var fm16 = new SfxDeviceInfo
        {
            hardware = 1,
            major = 1,
            minor = 6,
            patch = 0,
            profile = SfxProfile.VibeFm1,
            flags = 15,
        };
        var id16 = SfxProtocol.BuildIdentity(fm16);

        // csharpier-ignore
        var want16 = new byte[]
        {
            0xF0, 0x7D, 0x56, 0x42, 0x08, 1, 1, 6, 0, 1, 2, 6, 16, 2, 8, 64, 24, 32, 16, 5, 6, 25, 15, 0xF7,
        };

        // firmware 1.7.2, the one built from this source today: the same answer but for the version's second and
        // third byte (tests/check-identity.mjs reads these bytes off the built firmware)
        var fm17 = fm16;
        fm17.minor = 7;
        fm17.patch = 2;
        var id17 = SfxProtocol.BuildIdentity(fm17);
        var want17 = (byte[])want16.Clone();
        want17[7] = 7;
        want17[8] = 2;
        Ok(
            id16.SequenceEqual(want16)
                && SfxProtocol.TryParseIdentity(id16, out var got16)
                && got16.Equals(fm16)
                && got16.profile == SfxProfile.VibeFm1
                && got16.profile.songFormat == 6
                && got16.profile.steps == 64
                && got16.IsAtLeast(1, 6, 0)
                && !got16.IsAtLeast(1, 7, 0)
                && id17.SequenceEqual(want17)
                && SfxProtocol.TryParseIdentity(id17, out var got17)
                && got17.Equals(fm17)
                && got17.profile == got16.profile
                && got17.flags == got16.flags
                && got17.IsAtLeast(1, 7, 2)
                && !got17.IsAtLeast(1, 7, 3)
                && got17.ToString() == "VIBE FM, firmware 1.7.2, profile 1",
            "IDENTITY of firmware 1.6.0 says song format 6 and 64 steps, and survives build -> parse; that of 1.7.2 "
                + "is the same but for the version: "
                + SfxLink.Describe(id17)
        );
        Ok(got.ToString() == "VIBE FM, firmware 1.4.0, profile 1", $"and reads \"{got}\"");
        bool everyField = true;
        for (int i = 0; i < 300; i++)
        {
            var d = new SfxDeviceInfo
            {
                hardware = rng.Next(128),
                major = rng.Next(128),
                minor = rng.Next(128),
                patch = rng.Next(128),
                flags = rng.Next(128),
            };
            d.profile = new SfxProfile
            {
                id = rng.Next(128),
                patchFormat = rng.Next(128),
                songFormat = rng.Next(128),
                sounds = rng.Next(128),
                kits = rng.Next(128),
                patterns = rng.Next(128),
                steps = rng.Next(128),
                lanes = rng.Next(128),
                bars = rng.Next(128),
                voices = rng.Next(128),
                engines = rng.Next(128),
                wavetables = rng.Next(128),
                patchValues = rng.Next(128),
            };
            everyField &= SfxProtocol.TryParseIdentity(SfxProtocol.BuildIdentity(d), out var d2) && d2.Equals(d);
        }

        Ok(everyField, "300 random identities survive build -> parse, field by field");
        var flagsOnly = fm;
        flagsOnly.flags = SfxDeviceInfo.FlagEncoders | SfxDeviceInfo.FlagTelemetry;
        Ok(
            !flagsOnly.HasPads && flagsOnly.HasEncoders && !flagsOnly.TakesSongSounds && flagsOnly.TakesTelemetry,
            "the flags are read bit by bit"
        );

        var longer = id.Take(id.Length - 1).Concat(new byte[] { 9, 99, 0, 127, 0xF7 }).ToArray();
        Ok(
            SfxProtocol.TryParseIdentity(longer, out var fromLater) && fromLater.Equals(fm),
            "an identity with more bytes at its end (a later firmware) is read: the first 18, the rest ignored"
        );
        var shorter = id.Take(5 + 17).Append((byte)0xF7).ToArray();
        Ok(shorter.Length == 6 + 17 && !SfxProtocol.TryParseIdentity(shorter, out _), "17 bytes are not an identity");
        bool over = true;
        for (int i = 5; i < id.Length - 1; i++)
        {
            var b2 = (byte[])id.Clone();
            b2[i] = 0x80;
            over &= !SfxProtocol.TryParseIdentity(b2, out _);
        }

        var overLate = (byte[])longer.Clone();
        overLate[overLate.Length - 3] = 200;
        Ok(
            over && !SfxProtocol.TryParseIdentity(overLate, out _),
            "a byte over 127 anywhere in it, also after the 18th, and it is not an identity"
        );

        // csharpier-ignore
        Ok(
            !SfxProtocol.TryParseIdentity(ask, out _)
                && !SfxProtocol.TryParseIdentity(SfxProtocol.BuildLoad(0, laser), out _)
                && !SfxProtocol.TryParseIdentity(null, out _)
                && !SfxProtocol.TryParseIdentity(
                    new byte[]
                    {
                        0xF0, 0x7D, 0x56, 0x4C, 8, 1, 1, 4, 0, 1, 2, 5, 16, 2, 8, 16, 24, 32, 16, 5, 6, 25, 15,
                        0xF7,
                    },
                    out _
                ),
            "a request, a patch, nothing, another device's SysEx: none is an identity"
        );

        // an identity as long as a patch message (35) and as long as a song's piece (208) is neither
        byte[] Padded(int length)
        {
            var m = new byte[length];
            Array.Copy(id, m, id.Length - 1);
            m[4] = SfxProtocol.Identity;
            m[length - 1] = 0xF7;
            return m;
        }

        var as35 = Padded(7 + SfxPatch.Bytes);
        var as208 = Padded(8 + SfxProtocol.OldSongPieceBytes);
        var as251 = Padded(11 + SfxProtocol.SongPieceBytes);

        // a valid patch's bytes under the identity's command
        var asPatch = SfxProtocol.BuildLoad(0, laser);
        asPatch[4] = SfxProtocol.Identity;
        var noSong = new SfxSongTransfer();
        bool notPatch =
            !SfxProtocol.TryParsePatch(as35, out _, out _)
            && !SfxProtocol.TryParsePatch(asPatch, out _, out _)
            && !SfxProtocol.TryParsePatch(id, out _, out _);
        bool notPiece =
            !noSong.Take(as208)
            && !noSong.Take(as251)
            && !noSong.Take(as35)
            && !noSong.Take(id)
            && noSong.Song == null
            && !SfxSongTransfer.IsPiece(as208)
            && !SfxSongTransfer.IsPiece(as251)
            && !SfxSongTransfer.IsPiece(as35)
            && !SfxSongTransfer.IsPiece(id);
        // (the reader of the old shape alone, kept for those who call it: it says the same)
#pragma warning disable 618
        var songBuf = new byte[SfxProtocol.SongBytesV5];
        ulong seenBits = 0;
        notPiece &=
            !SfxProtocol.TryTakeSongChunk(as208, songBuf, ref seenBits)
            && !SfxProtocol.TryTakeSongChunk(as35, songBuf, ref seenBits)
            && !SfxProtocol.TryTakeSongChunk(id, songBuf, ref seenBits)
            && seenBits == 0
            && songBuf.All(x => x == 0);
        notPiece &= SfxProtocol.SongBytes == 7699 && SfxProtocol.SongChunkBytes == 200 && SfxProtocol.SongChunks == 39;
#pragma warning restore 618
        Ok(
            as35.Length == 35
                && as208.Length == 208
                && as251.Length == 251
                && notPatch
                && notPiece
                && SfxProtocol.TryParseIdentity(as35, out _)
                && SfxProtocol.TryParseIdentity(as208, out _)
                && SfxProtocol.TryParseIdentity(as251, out _),
            "an identity is neither a patch nor a piece of a song, also when it is 35, 208 or 251 bytes long"
        );

        // the link: an identity in the middle of a song that is arriving
        using (var link = new SfxLink())
        {
            var theSong = new byte[SfxProtocol.SongBytesV5];
            theSong[0] = 5;
            for (int i = SfxProtocol.SongHead; i < theSong.Length; i++)
            {
                theSong[i] = (byte)(i * 7 % 128);
            }

            // (a head the player takes - 16 sixteenths, 120 BPM, a chain of 1 -, and rows of anything)
            theSong[1] = 16;
            theSong[3] = 120;
            theSong[6] = 1;
            var pieces = SfxProtocol.BuildSong(theSong);
            int songs = 0;
            int patches = 0;
            int identities = 0;
            byte[] arrived = null;
            SfxDeviceInfo said = default;
            link.SongReceived += (s, _) =>
            {
                songs++;
                arrived = s;
            };
            link.PatchReceived += (_, _) => patches++;
            link.IdentityReceived += d =>
            {
                identities++;
                said = d;
            };
            Ok(
                link.Device == null && !link.IdentityUnanswered && link.LastRefusal == null,
                "a link that asked nothing knows no device, and does not say one failed to answer"
            );
            for (int k = 0; k < pieces.Length; k++)
            {
                // (the emulator delivers everything twice, and hands back what was sent)
                if (k == 20)
                {
                    link.Take(id);
                    link.Take(id);
                    link.Take(as208);
                    link.Take(ask);
                }

                link.Take(pieces[k]);
            }

            Ok(
                songs == 1
                    && arrived != null
                    && arrived.SequenceEqual(theSong)
                    && patches == 0
                    && identities == 3
                    && said.Equals(fm)
                    && link.Device.HasValue
                    && link.Device.Value.Equals(fm),
                "identities (twice, and a long one) and a request come back in the middle of a song: the song "
                    + "arrives whole, once; no patch; the device is known"
            );
            Ok(
                link.SendPatch(0, laser) == false
                    && link.LastRefusal == null
                    && !link.SendSong(new SfxPatch[16], theSong)
                    && link.LastRefusal == null,
                "a device that speaks song format 5 is refused no patch and no format-5 song (without a port "
                    + "nothing is sent, and that is no refusal)"
            );

            var later = fm;
            later.major = 2;
            later.minor = 0;
            later.profile.patchFormat = 3;
            link.Take(SfxProtocol.BuildIdentity(later));
            bool refusedPatch = !link.SendPatch(0, laser);
            string whyPatch = link.LastRefusal;
            bool refusedSong = !link.SendSong(new SfxPatch[16], theSong);
            string whySong = link.LastRefusal;
            Ok(
                refusedPatch
                    && whyPatch != null
                    && whyPatch.Contains("patch format 3")
                    && whyPatch.Contains("update the package")
                    && refusedSong
                    && whySong != null,
                $"a device that says patch format 3 is sent no patch and no song: \"{whyPatch}\""
            );
            var olderSong = fm;
            olderSong.profile.songFormat = 4;
            link.Take(SfxProtocol.BuildIdentity(olderSong));
            bool patchGoes = !link.SendPatch(0, laser) && link.LastRefusal == null;
            refusedSong = !link.SendSong(new SfxPatch[16], theSong);
            whySong = link.LastRefusal;
            Ok(
                patchGoes
                    && refusedSong
                    && whySong != null
                    && whySong.Contains("song format 4")
                    && whySong.Contains("update the device's firmware"),
                $"a device that says song format 4 is sent patches but no song: \"{whySong}\""
            );

            // asked again: what the device said before is forgotten, and a second without an answer says so
            bool knownBefore = link.Device.HasValue && SfxLink.Refusal(link.Device, true) != null;
            link.Asked();
            bool forgotten =
                link.Device == null && !link.IdentityUnanswered && SfxLink.Refusal(link.Device, true) == null;
            System.Threading.Thread.Sleep((int)(SfxLink.IdentityAnswerSeconds * 1000) + 100);
            bool unanswered = link.IdentityUnanswered;
            link.Take(id);
            Ok(
                knownBefore && forgotten && unanswered && !link.IdentityUnanswered && link.Device.Value.Equals(fm),
                "asking again forgets the earlier answer (and its refusal); a second without one reads as "
                    + "unanswered; the next answer is taken"
            );

            // a patch and a song in a higher format come in: newer is said, once each, and nothing of them is taken
            var said2 = new List<string>();
            link.NewerReceived += said2.Add;
            var newerDump = SfxProtocol.BuildDump(3, laser);
            newerDump[6] = 3;
            var cutDump = newerDump.Take(20).Append((byte)0xF7).ToArray();
            int patchesBefore = patches;
            int songsBefore = songs;
            link.Take(newerDump);
            link.Take(newerDump);
            link.Take(cutDump);
            link.Take(SfxProtocol.BuildDump(3, laser));
            Ok(
                said2.Count == 1
                    && said2[0].Contains("a patch in format 3")
                    && said2[0].Contains("knows up to 2")
                    && patches == patchesBefore + 1,
                $"a dump in patch format 3, delivered twice, is said to be newer once and not taken; a cut-off one says nothing; this version's is taken: \"{(said2.Count > 0 ? said2[0] : "")}\""
            );
            var laterSong = new byte[600];
            laterSong[0] = 7;
            for (int i = 1; i < laterSong.Length; i++)
            {
                laterSong[i] = (byte)(i * 5 % 128);
            }

            foreach (var piece in SfxProtocol.BuildPieces(laterSong))
            {
                link.Take(piece);
                link.Take(piece);
            }

            // (and the same format in the message of before: said within the same two seconds, so not said again)
            var laterOld = (byte[])theSong.Clone();
            laterOld[0] = 7;
            foreach (var piece in SfxProtocol.BuildSong(laterOld))
            {
                link.Take(piece);
                link.Take(piece);
            }

            Ok(
                said2.Count == 2
                    && said2[1].Contains("a song in format 7")
                    && said2[1].Contains("knows up to 6")
                    && songs == songsBefore,
                $"a song in format 7 in the message of firmware 1.6, every piece twice - and once more in the message of before - is said to be newer once and does not arrive as a song: \"{(said2.Count > 1 ? said2[1] : "")}\""
            );
            foreach (var piece in pieces)
            {
                link.Take(piece);
            }

            Ok(
                songs == songsBefore + 1 && arrived.SequenceEqual(theSong) && said2.Count == 2,
                "and this version's song after it arrives whole"
            );

            link.Close();
            Ok(link.Device == null && link.LastRefusal == null, "closing the link forgets the device");
        }

        Ok(
            SfxLink.Refusal(null, true) == null
                && SfxLink.Refusal(null, false) == null
                && SfxLink.Refusal(SfxDeviceInfo.AssumedBefore14, true) == null,
            "a device that never answered is refused nothing: it is taken for a firmware before 1.4"
        );
        var assumed = SfxDeviceInfo.AssumedBefore14;
        Ok(
            assumed.ToString() == "VIBE FM, firmware before 1.4, profile 1"
                && assumed.profile == SfxProfile.VibeFm15
                && assumed.profile.songFormat == 5
                && assumed.profile.steps == 16
                && !assumed.IsAtLeast(1, 0, 0)
                && assumed.HasPads
                && assumed.HasEncoders
                && assumed.TakesTelemetry
                && !assumed.TakesSongSounds,
            $"and reads \"{assumed}\" (whether it takes a song's sounds is not known)"
        );

        // newer is not broken
        var newerPatch = laser.Pack();
        newerPatch[0] = 3;
        newerPatch[SfxPatch.Bytes - 1] = SfxPatch.Checksum(newerPatch, 0);
        var brokenPatch = laser.Pack();
        brokenPatch[5] ^= 1;
        var olderPatch = laser.Pack();
        olderPatch[0] = 1;
        var inMessage = SfxProtocol.BuildLoad(2, laser);
        inMessage[6] = 9;
        Ok(
            SfxPatch.IsNewer(newerPatch, 0)
                && !SfxPatch.TryUnpack(newerPatch, 0, out _)
                && !SfxPatch.IsNewer(laser.Pack(), 0)
                && !SfxPatch.IsNewer(brokenPatch, 0)
                && !SfxPatch.IsNewer(olderPatch, 0)
                && SfxPatch.IsNewer(inMessage, 6)
                && !SfxPatch.IsNewer(inMessage, 7)
                && !SfxPatch.IsNewer(null, 0)
                && !SfxPatch.IsNewer(newerPatch, 28)
                && !SfxPatch.IsNewer(newerPatch, -1)
                && !SfxPatch.IsNewer(new byte[] { 0xF0 }, 0),
            "a patch with a higher version byte is newer (and not unpacked); this version's, a damaged one, "
                + "an older one, a status byte are not"
        );
        var longerPatch = newerPatch.Concat(new byte[4]).ToArray();
        Ok(
            !SfxPatch.IsNewer(new byte[] { 9 }, 0)
                && !SfxPatch.IsNewer(newerPatch.Take(27).ToArray(), 0)
                && !SfxPatch.IsNewer(newerPatch, 1)
                && SfxPatch.IsNewer(longerPatch, 0)
                && SfxPatch.IsNewer(longerPatch, 4) == (longerPatch[4] > 2),
            "fewer than 28 bytes are a cut-off patch, not a newer one, whatever they begin with; more than "
                + "28 may be one"
        );
        var song7 = new byte[SfxProtocol.SongBytesMin];
        song7[0] = 7;
        var song6 = new byte[SfxProtocol.SongBytesV5];
        song6[0] = 6;
        var song5 = new byte[SfxProtocol.SongBytesV5];
        song5[0] = 5;
        Ok(
            SfxProtocol.IsNewerSong(song7)
                && SfxProtocol.IsNewerSong(song7.Concat(new byte[9000]).ToArray())
                && !SfxProtocol.IsNewerSong(song7.Take(210).ToArray())
                && !SfxProtocol.IsNewerSong(song6)
                && !SfxProtocol.IsNewerSong(song5)
                && !SfxProtocol.IsNewerSong(new byte[] { 0x40 })
                && !SfxProtocol.IsNewerSong(null)
                && !SfxProtocol.IsNewerSong(new byte[0])
                && SfxProtocol.SongVersion == 6
                && SfxProtocol.SongBytesMin == 211
                && SfxProtocol.NewerIn(SfxProtocol.BuildLoad(0, laser)) == null
                && SfxProtocol.NewerIn(SfxProtocol.BuildRequestIdentity()) == null
                && SfxProtocol.NewerIn(null) == null,
            "a packed song of a format above 6 and at least 211 bytes (the shortest song there is) is newer; "
                + "210 bytes, a 6, a 5, one byte are not"
        );
        // 130 pieces: piece 128 has a 0 where piece 0 has one
        var long7 = new byte[129 * 240 + 7];
        long7[0] = 7;
        var p7 = SfxProtocol.BuildPieces(long7);
        var p6 = SfxProtocol.BuildPieces(song6);
        var old7 = (byte[])song5.Clone();
        old7[0] = 7;
        var o7 = SfxProtocol.BuildSong(old7);
        var o5 = SfxProtocol.BuildSong(song5);
        Ok(
            p7.Length == 130
                && SfxProtocol.NewerIn(p7[0]) == "a song in format 7"
                && SfxProtocol.NewerIn(o7[0]) == "a song in format 7"
                && p7[128][5] == 0
                && p7[128][6] == 1
                && SfxProtocol.NewerIn(p7[128]) == null
                && p7.Skip(1).All(m => SfxProtocol.NewerIn(m) == null)
                && o7.Skip(1).All(m => SfxProtocol.NewerIn(m) == null)
                && p6.All(m => SfxProtocol.NewerIn(m) == null)
                && o5.All(m => SfxProtocol.NewerIn(m) == null),
            "the first piece of a song in format 7 says so, in the message of firmware 1.6 and in the one "
                + "before; no other piece does - not piece 129 of a long song either - and no piece of a "
                + "format-6 or format-5 song"
        );

        // the profile
        var p1 = SfxProfile.VibeFm1;
        Ok(
            p1.id == 1
                && p1.patchFormat == 2
                && p1.songFormat == 6
                && p1.sounds == 16
                && p1.kits == 2
                && p1.patterns == 8
                && p1.steps == 64
                && p1.lanes == 24
                && p1.bars == 32
                && p1.voices == 16
                && p1.engines == 5
                && p1.wavetables == 6
                && p1.patchValues == 25,
            "the profile of VIBE FM is made of the package's constants: " + p1
        );
        var p15 = SfxProfile.VibeFm15;
        var p15as1 = p15;
        p15as1.songFormat = 6;
        p15as1.steps = 64;
        Ok(
            p15.songFormat == 5
                && p15.steps == 16
                && p15 != p1
                && p15as1 == p1
                && SfxProtocol.Steps == 16
                && SfxProtocol.GridSteps == 64,
            "and what firmware 1.4 and 1.5 said is the same but for song format 5 and 16 steps: " + p15
        );
        bool allFit = true;
        for (int i = 0; i < 500; i++)
        {
            var p = new SfxPatch { engine = (SfxEngine)rng.Next(SfxPatch.EngineCount) };
            for (int k = 0; k < SfxPatch.ParamCount; k++)
            {
                p[(SfxParam)k] = (byte)rng.Next(128);
            }

            allFit &= p1.Fits(p);
        }

        var fewer = p1;
        fewer.engines = 4;
        var otherFormat = p1;
        otherFormat.patchFormat = 3;

        // (set past the indexer, which cuts to 127)
        var raw = laser;
        raw.filterCutoff = 200;
        Ok(
            allFit
                && p1.Fits(laser)
                && fewer.Fits(laser)
                && !fewer.Fits(SfxPatch.Create(SfxEngine.Wavetable))
                && !otherFormat.Fits(laser)
                && !p1.Fits(raw)
                && !p1.Fits(new SfxPatch { engine = (SfxEngine)7 }),
            "every patch of this version fits it; not one with an engine the device lacks, another format, "
                + "or a value over 127"
        );

        // (16 sixteenths, 120 BPM, a chain of 1, 32 bars: a song the player takes)
        var s5 = new byte[SfxProtocol.SongBytesV5];
        s5[0] = 5;
        s5[1] = 16;
        s5[3] = 120;
        s5[6] = 1;
        s5[66] = 32;
        var s7 = new byte[SfxProtocol.SongBytesMin];
        s7[0] = 7;
        s7[1] = 16;
        var tooLong = (byte[])s5.Clone();
        tooLong[1] = 17;
        var noBar = (byte[])s5.Clone();
        noBar[1] = 0;
        var tooManyBars = (byte[])s5.Clone();
        tooManyBars[66] = 33;
        var highByte = (byte[])s5.Clone();
        highByte[5000] = 128;
        var s4 = new byte[SfxProtocol.SongBytesV4];
        s4[0] = 4;
        s4[1] = 16;
        s4[3] = 120;
        s4[6] = 1;
        s4[82] = 8;
        var s1 = new byte[SfxProtocol.SongBytesV1];
        s1[0] = 1;
        s1[1] = 16;
        s1[3] = 120;
        s1[6] = 1;
        var wrongLength = new byte[SfxProtocol.SongBytesV3];
        wrongLength[0] = 5;
        var shortBars = p1;
        shortBars.bars = 4;
        var onlyOld = p1;
        onlyOld.songFormat = 4;
        Ok(
            p1.Fits(s5)
                && p1.Fits(s4)
                && p1.Fits(s1)
                && !p1.Fits(s7)
                && !p1.Fits(tooLong)
                && !p1.Fits(noBar)
                && !p1.Fits(tooManyBars)
                && !p1.Fits(highByte)
                && !p1.Fits(wrongLength)
                && !p1.Fits((byte[])null)
                && !p1.Fits(new byte[0])
                && !shortBars.Fits(s5)
                && !shortBars.Fits(s4)
                && shortBars.Fits(s1)
                && !onlyOld.Fits(s5)
                && onlyOld.Fits(s4)
                && p15.Fits(s5)
                && p15.Fits(s4),
            "a packed song fits it in every format up to its own; not a newer one, a wrong length, a byte "
                + "over 127, a bar of 0 or 17 sixteenths (a format-5 song, a profile of 64 steps), more bars than "
                + "the device holds"
        );

        // format 6: an empty song is its head and eight empty blocks; a block's first byte is its pattern's grid
        var s6 = new byte[SfxProtocol.SongBytesMin];
        s6[0] = 6;
        s6[1] = 16;
        s6[3] = 120;
        s6[6] = 1;

        // pattern D: 32 steps
        var s6fine = (byte[])s6.Clone();
        s6fine[SfxProtocol.SongHead + 3 * 8] = 1;

        // pattern H: 64
        var s6finest = (byte[])s6.Clone();
        s6finest[SfxProtocol.SongHead + 7 * 8] = 2;

        // (a device that speaks format 6 and holds 16 steps a pattern)
        var six16 = p1;
        six16.steps = 16;
        var six32 = p1;
        six32.steps = 32;
        var s6bar17 = (byte[])s6.Clone();
        s6bar17[1] = 17;
        var s6cut = s6.Take(210).ToArray();
        var s6bars = (byte[])s6.Clone();
        s6bars[66] = 33;
        Ok(
            p1.Fits(s6)
                && p1.Fits(s6fine)
                && p1.Fits(s6finest)
                && six16.Fits(s6)
                && !six16.Fits(s6fine)
                && !six16.Fits(s6finest)
                && six32.Fits(s6fine)
                && !six32.Fits(s6finest)
                && !p15.Fits(s6)
                && !p15.Fits(s6fine)
                && !p1.Fits(s6bar17)
                && !p1.Fits(s6cut)
                && !p1.Fits(s6bars)
                && !shortBars.Fits(s6bars)
                && shortBars.Fits(s6),
            "a format-6 song fits the profile of 1.6; a pattern of 32 or 64 steps does not fit a profile "
                + "that holds 16; no format-6 song fits what 1.4 and 1.5 said; a bar of 17, a cut-off one, 33 bars "
                + "do not fit"
        );
        Ok(
            SfxPackedSong.Format(s6) == 6
                && SfxPackedSong.Grid(s6, 0) == 16
                && SfxPackedSong.Grid(s6fine, 3) == 32
                && SfxPackedSong.Div(s6fine, 3) == 2
                && SfxPackedSong.Grid(s6finest, 7) == 64
                && SfxPackedSong.Grid(s6finest, 6) == 16
                && SfxPackedSong.Grid(s6, 8) == 0
                && SfxPackedSong.Grid(s6, -1) == 0
                && SfxPackedSong.Format(s6cut) == 0
                && SfxPackedSong.Grid(s6cut, 0) == 0
                && SfxPackedSong.Format(s5) == 5
                && SfxPackedSong.Grid(s5, 7) == 16
                && SfxPackedSong.Format(s7) == 0
                && SfxPackedSong.Format(null) == 0
                && !SfxPackedSong.StepOn(s6, 0, 0, 0)
                && SfxPackedSong.StepVelocity(s6, 0, 0, 0) == 5
                && SfxPackedSong.LanePoint(s6, 0, 0, 0) == -1
                && SfxPackedSong.StepNote(s6, 0, 0, 99) == 0
                && SfxPackedSong.StepLength(s6finest, 7, 15, 63) == 0,
            "SfxPackedSong: the format and each pattern's grid of a packed song; out of range, or not a "
                + "song: 0, false, velocity 5, no point"
        );

        // ---- what is sent to whom: the refusals, in words
        var dev15 = new SfxDeviceInfo
        {
            hardware = 1,
            major = 1,
            minor = 5,
            patch = 0,
            profile = SfxProfile.VibeFm15,
            flags = 15,
        };
        var dev16 = fm16;
        var dev17 = fm17;
        var dev9 = fm16;
        dev9.major = 9;
        dev9.profile.songFormat = 9;
        var garbage = new byte[300];
        garbage[0] = 3;
        string r15 = SfxLink.Refusal(dev15, s6);
        string rNone = SfxLink.Refusal(null, s6);
        string r7 = SfxLink.Refusal(dev16, s7);
        string rBad = SfxLink.Refusal(dev16, garbage);
        string r9 = SfxLink.Refusal(dev9, s6);
        string r4on15 = SfxLink.Refusal(dev15, s4);
        var old5 = SfxLink.SongMessages(dev15, s5);
        var none5 = SfxLink.SongMessages(null, s5);
        var new5 = SfxLink.SongMessages(dev16, s5);
        var new6 = SfxLink.SongMessages(dev16, s6fine);
        bool oldShape(byte[][] ms) => ms.Length == 39 && ms.All(m => m[6] == 39 && m.Length <= 208);
        bool newShape(byte[][] ms, byte[] song) =>
            ms.Length == (song.Length + 239) / 240
            && ms.Select(
                    (m, k) =>
                        (m[5] | m[6] << 7) == k && m[7] == song[0] && (m[8] | m[9] << 7) == ms.Length && m.Length <= 251
                )
                .All(x => x);
        Ok(
            SfxLink.Refusal(dev15, s5) == null && oldShape(old5) && SfxLink.Refusal(dev15, true) == null,
            "a device that said 1.5 (song format 5) is sent a format-5 song in 16 + 39 messages, the song's "
                + "in the shape it knows"
        );
        Ok(
            r15 != null
                && r15.Contains("speaks song format 5")
                && r15.Contains("the song is in format 6")
                && r15.Contains("update the device's firmware"),
            $"and is refused a format-6 song: \"{r15}\""
        );
        Ok(
            r4on15 != null && r4on15.Contains("speaks song format 5") && r4on15.Contains("fetch the song again"),
            $"and a song older than its own, which never travelled in pieces: \"{r4on15}\""
        );
        Ok(
            SfxLink.Refusal(dev16, s5) == null
                && SfxLink.Refusal(dev16, s6) == null
                && SfxLink.Refusal(dev16, s6fine) == null
                && SfxLink.Refusal(dev16, s4) == null
                && newShape(new5, s5)
                && new5.Length == 33
                && newShape(new6, s6fine)
                && new6.Length == 1
                && SfxLink.Refusal(dev17, s5) == null
                && SfxLink.Refusal(dev17, s6fine) == null
                && SfxLink.Refusal(dev17, true) == null
                && newShape(SfxLink.SongMessages(dev17, s5), s5)
                && newShape(SfxLink.SongMessages(dev17, s6fine), s6fine),
            "a device that said 1.6 or 1.7 (song format 6) is sent a song of any format this version reads, in "
                + "the message of 1.6: a format-5 song in 33 pieces, an empty format-6 one in 1"
        );
        Ok(
            SfxLink.Refusal(null, s5) == null
                && oldShape(none5)
                && rNone != null
                && rNone.Contains("did not say")
                && rNone.Contains("song format 6")
                && SfxLink.Refusal(SfxDeviceInfo.AssumedBefore14, s5) == null,
            $"a device that said nothing is sent a format-5 song in the shape of before, and no format-6 song - an older firmware would drop its pieces without a word: \"{rNone}\""
        );
        Ok(
            r7 != null
                && r7.Contains("the song is in format 7")
                && r7.Contains("knows up to 6")
                && r7.Contains("update the package")
                && SfxLink.Refusal(null, s7) == r7,
            $"a song in format 7 goes nowhere: \"{r7}\""
        );
        Ok(
            rBad == "this is not a packed song"
                && SfxLink.Refusal(null, garbage) == rBad
                && SfxLink.Refusal(dev16, (byte[])null) == rBad
                && SfxLink.Refusal(null, new byte[0]) == rBad,
            "bytes that are no song are refused in words, without an exception"
        );
        var swing101 = (byte[])s6.Clone();
        swing101[4] = 101;
        var chain0 = (byte[])s5.Clone();
        chain0[6] = 0;
        string rRange = SfxLink.Refusal(dev16, swing101);
        Ok(
            SfxPackedSong.IsPlayable(s6)
                && SfxPackedSong.IsPlayable(s5)
                && SfxPackedSong.IsPlayable(s4)
                && SfxPackedSong.IsPlayable(s1)
                && SfxPackedSong.IsValid(swing101)
                && rRange != null
                && rRange.Contains("out of range")
                && rRange.Contains("BAD SONG")
                && SfxLink.Refusal(dev15, chain0) == rRange
                && SfxLink.Refusal(null, chain0) == rRange
                && SfxLink.Refusal(null, swing101) == rRange,
            $"a song with a value the device would not take (a swing of 101, a chain of 0) goes to no device, whatever it speaks: \"{rRange}\""
        );
        Ok(
            r9 != null
                && r9.Contains("speaks song format 9")
                && r9.Contains("update the package")
                && SfxLink.Refusal(dev9, true) == r9,
            $"a device that speaks a song format above this version's is sent no song: \"{r9}\""
        );
        using (var link = new SfxLink())
        {
            link.Take(SfxProtocol.BuildIdentity(dev15));
            bool a = !link.SendSong(new SfxPatch[16], s6) && link.LastRefusal == r15;
            bool b = !link.SendSong(new SfxPatch[16], s5) && link.LastRefusal == null;
            bool c =
                !link.SendSong(new SfxPatch[16], garbage)
                && link.LastRefusal == "this is not a packed song"
                && !link.SendSong(new SfxPatch[16], null)
                && link.LastRefusal == "this is not a packed song";
            link.Asked();
            bool waiting =
                !link.SendSong(new SfxPatch[16], s6)
                && link.LastRefusal != null
                && link.LastRefusal.Contains("did not say");
            System.Threading.Thread.Sleep((int)(SfxLink.IdentityAnswerSeconds * 1000) + 100);
            bool silent =
                link.IdentityUnanswered
                && !link.SendSong(new SfxPatch[16], s6)
                && link.LastRefusal != null
                && link.LastRefusal.Contains("did not say")
                && !link.SendSong(new SfxPatch[16], s5)
                && link.LastRefusal == null;
            link.Take(SfxProtocol.BuildIdentity(dev16));
            bool now =
                !link.SendSong(new SfxPatch[16], s6)
                && link.LastRefusal == null
                && !link.SendSong(new SfxPatch[16], s5)
                && link.LastRefusal == null;
            Ok(
                a && b && c && waiting && silent && now,
                "SendSong says the same: LastRefusal for a 1.5 device and a format-6 song, for one that has "
                    + "not answered, for one that never will, for bytes that are no song; none for a "
                    + "format-5 song, and none once the device said 1.6"
            );
        }
    }
}
