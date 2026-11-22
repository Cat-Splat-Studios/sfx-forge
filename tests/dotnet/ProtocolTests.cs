// The patch and the messages of the link: packing, the SysEx messages, telemetry, and what the real
// firmware sent in the test performance (out/midi.json).
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
    // the patch and the protocol
    static void PatchAndProtocol()
    {
        bool round = true;
        for (int i = 0; i < 2000; i++)
        {
            var p = new SfxPatch { engine = (SfxEngine)rng.Next(SfxPatch.EngineCount) };
            for (int k = 0; k < SfxPatch.ParamCount; k++)
            {
                p[(SfxParam)k] = (byte)rng.Next(128);
            }

            int slot = rng.Next(16);
            round &=
                SfxProtocol.TryParsePatch(SfxProtocol.BuildLoad(slot, p), out int s2, out var p2)
                && s2 == slot
                && p2.Equals(p);
        }

        Ok(round && SfxPatch.Bytes == 28, "2000 random patches survive pack -> SysEx -> unpack (28 bytes each)");
        var bad = SfxProtocol.BuildLoad(3, SfxPatch.Default);
        bad[10] ^= 1;
        Ok(!SfxProtocol.TryParsePatch(bad, out _, out _), "a damaged patch is refused (checksum)");
        Ok(
            !SfxProtocol.TryParsePatch(new byte[] { 0xF0, 0x7D, 0x56, 0x4C, 1, 0, 0xF7 }, out _, out _),
            "another device's SysEx is ignored"
        );
        var text = SfxProtocol.BuildText("Level 3 – Boss with a very long name here");
        Ok(
            text.Length == 6 + 24 && text.All(b => b == 0xF0 || b == 0xF7 || b < 128),
            "text is cut to 24 characters of 7-bit ASCII"
        );

        laser = SfxPatch.Default;
        hum = SfxPatch.Create(SfxEngine.SciFiFm, 22, 64, 0, 21, 55, 8, 78, 45, 60, 0, 0, 127); // sustained

        // pad 3 of the device
        thud = SfxPatch.Create(SfxEngine.Impact, 24, 90, 35, 0, 0, 5, 70, 0, 70, 0);

        // table 0 at its start: a saw, held
        saw = SfxPatch.Create(SfxEngine.Wavetable, 52, 64, 0, 0, 0, 0, 127, 0, 90, 0, 0, 110, 40);
        Ok(
            laser.level == 100 && !laser.IsSustained && hum.IsSustained && new SfxPatch().level == 0,
            "Create fills the defaults (level 100, a one-shot); sustain above 0 = held"
        );
        bool mut = true;
        for (int i = 0; i < 500; i++)
        {
            var a = laser;
            a.Mutate(rng);
            for (int k = 0; k < SfxPatch.ParamCount; k++)
            {
                mut &= Math.Abs(a[(SfxParam)k] - laser[(SfxParam)k]) <= 19;
            }

            mut &= a.engine == laser.engine && a.sustain == 0 && a.level == laser.level && a.table == laser.table;
            var h = hum;
            h.Mutate(rng);
            mut &= h.sustain > 0;
        }

        Ok(mut, "Mutate stays within 15 % and keeps the kind of sound: engine, table, level, one-shot or sustained");
        var mods = SfxMods.None;
        SfxProtocol.Apply(ref mods, SfxTelemetry.Pitch, 127);
        SfxProtocol.Apply(ref mods, SfxTelemetry.Cutoff, 64);
        Ok(
            mods.pitchSemitones == 24f
                && mods.cutoffOctaves == 0f
                && SfxProtocol.ToCc(0.5f) == 64
                && SfxProtocol.ToCc(float.NaN) == 0,
            "telemetry maps as on the device"
        );
    }

    // what the real firmware sent in the test performance
    static void FirmwareExport()
    {
        msgs = File.Exists(midiFile) ? ReadMidi(midiFile) : null;
        deviceAudioLeft = File.Exists(wavFile) ? ReadWavLeft(wavFile) : null;
        if (msgs != null)
        {
            var sx = msgs.FirstOrDefault(b => b[0] == 0xF0);
            bool parsed = SfxProtocol.TryParsePatch(sx, out int slot, out var fromDevice);
            Ok(
                parsed,
                parsed
                    ? $"the firmware's EXPORT parses here (pad {slot + 1}, {fromDevice.engine})"
                    : "the firmware's EXPORT parses here"
            );
            Ok(parsed && SfxProtocol.BuildDump(slot, fromDevice).SequenceEqual(sx), "and packs back to the same bytes");
        }
        else
        {
            Ok(false, "out/midi.json missing: run sh build.sh first");
        }
    }
}
