// SPDX-License-Identifier: MIT
using System;

namespace CatSplat.SfxForge
{
    /// <summary>What a game sends the device while it plays (CC 10..17 on channel 2).</summary>
    public enum SfxTelemetry { Pitch, Cutoff, FmDepth, Level, Aux1, Aux2, Aux3, Aux4 }

    /// <summary>What moves a sounding voice: the device's "Mod" parameters, the same for a local SfxPlayer.</summary>
    public struct SfxMods
    {
        public float pitchSemitones, cutoffOctaves, fm, level;
        public static SfxMods None => new SfxMods { level = 1f };
    }

    /// <summary>The MIDI between SFX FORGE and a game engine (README.md, "The link").</summary>
    public static class SfxProtocol
    {
        public const int Slots = 16;
        public const int PadNoteBase = 36;          // pads <-> notes 36..51
        public const int TelemetryCcBase = 10;      // game -> device, channel 2
        public const int EncoderCcBase = 20;        // device -> game, channel 1: CC 20..25
        public const int EncoderCount = 6;
        public const byte TelemetryStatus = 0xB1;   // control change, channel 2
        public const int MaxText = 24;

        public const byte Dump = 0x01, Load = 0x02, Text = 0x03, Request = 0x04;
        static readonly byte[] Header = { 0xF0, 0x7D, 0x56, 0x42 };

        public static bool IsOurs(byte[] m)
        {
            if (m == null || m.Length < 6 || m[m.Length - 1] != 0xF7) return false;
            for (int i = 0; i < Header.Length; i++) if (m[i] != Header[i]) return false;
            return true;
        }

        /// <summary>F0 7D 56 42 02 slot [13] F7 – a patch into a pad of the device.</summary>
        public static byte[] BuildLoad(int slot, SfxPatch patch) => BuildPatch(Load, slot, patch);

        /// <summary>F0 7D 56 42 01 slot [13] F7 – what the device sends on EXPORT.</summary>
        public static byte[] BuildDump(int slot, SfxPatch patch) => BuildPatch(Dump, slot, patch);

        static byte[] BuildPatch(byte command, int slot, SfxPatch patch)
        {
            if (slot < 0 || slot >= Slots) throw new ArgumentOutOfRangeException(nameof(slot));
            var m = new byte[7 + SfxPatch.Bytes];
            Header.CopyTo(m, 0);
            m[4] = command; m[5] = (byte)slot;
            patch.Pack(m, 6);
            m[m.Length - 1] = 0xF7;
            return m;
        }

        public static byte[] BuildRequest(int slot)
        {
            if (slot < 0 || slot >= Slots) throw new ArgumentOutOfRangeException(nameof(slot));
            return new byte[] { 0xF0, 0x7D, 0x56, 0x42, Request, (byte)slot, 0xF7 };
        }

        /// <summary>Text for the device's top line: printable ASCII, 24 characters at most.</summary>
        public static byte[] BuildText(string text)
        {
            text = text ?? "";
            int n = Math.Min(text.Length, MaxText);
            var m = new byte[6 + n];
            Header.CopyTo(m, 0);
            m[4] = Text;
            for (int i = 0; i < n; i++) m[5 + i] = (byte)(text[i] >= 32 && text[i] < 127 ? text[i] : '?');
            m[m.Length - 1] = 0xF7;
            return m;
        }

        /// <summary>A dump or a load with a valid patch.</summary>
        public static bool TryParsePatch(byte[] m, out int slot, out SfxPatch patch)
        {
            slot = 0; patch = default;
            if (!IsOurs(m) || m.Length != 7 + SfxPatch.Bytes || (m[4] != Dump && m[4] != Load) || m[5] >= Slots) return false;
            slot = m[5];
            return SfxPatch.TryUnpack(m, 6, out patch);
        }

        /// <summary>A telemetry value 0..1 as its controller value.</summary>
        public static byte ToCc(float value01)
        {
            if (!(value01 > 0f)) return 0;                       // (NaN too)
            return value01 >= 1f ? (byte)127 : (byte)(value01 * 127f + 0.5f);
        }

        /// <summary>What a controller value does on the device – so a local player can do the same.</summary>
        public static void Apply(ref SfxMods mods, SfxTelemetry channel, byte cc)
        {
            switch (channel)
            {
                case SfxTelemetry.Pitch: mods.pitchSemitones = cc / 127f * 24f; break;
                case SfxTelemetry.Cutoff: mods.cutoffOctaves = Math.Max(-4f, Math.Min(4f, (cc - 64) / 16f)); break;
                case SfxTelemetry.FmDepth: mods.fm = cc / 127f; break;
                case SfxTelemetry.Level: mods.level = cc / 127f; break;
            }
        }
    }
}
