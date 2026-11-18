// SPDX-License-Identifier: MIT
using System;
using System.Text;

namespace CatSplat.SfxForge
{
    /// <summary>
    /// The connection to SFX FORGE (the VIBE FM firmware in the browser, the plugin or on the hardware) over two MIDI ports.
    /// One for the editor and the game: SfxLink.Shared. Call Poll() regularly – the events fire from there, on your thread.
    /// </summary>
    public sealed class SfxLink : IDisposable
    {
        public static readonly SfxLink Shared = new SfxLink();

        MidiIn input;
        MidiOut output;
        readonly int[] lastCc = new int[8];

        public SfxLink() { Forget(); }

        /// <summary>The device sent a patch: EXPORT was pressed, or the answer to RequestPatch. (slot 0..15)</summary>
        public event Action<int, SfxPatch> PatchReceived;
        /// <summary>A pad was played on the device (slot 0..15, velocity 0..1).</summary>
        public event Action<int, float> PadDown;
        public event Action<int> PadUp;
        /// <summary>An encoder turned on the device's LINK page: index 0..5 (encoders 1-4, A, B), value 0..1.</summary>
        public event Action<int, float> EncoderChanged;
        /// <summary>Everything that comes in, as it is.</summary>
        public event Action<byte[]> MessageReceived;

        public string InputName => input != null ? input.Name : null;
        public string OutputName => output != null ? output.Name : null;
        public bool HasInput => input != null;
        public bool HasOutput => output != null;
        public bool IsOpen => input != null || output != null;

        /// <summary>The first port whose name contains the text (any case), -1 = none.</summary>
        public static int Find(string[] names, string contains)
        {
            if (string.IsNullOrEmpty(contains)) return -1;
            for (int i = 0; i < names.Length; i++) if (names[i].IndexOf(contains, StringComparison.OrdinalIgnoreCase) >= 0) return i;
            return -1;
        }

        /// <summary>Opens the ports whose names contain these texts (null or "": none). True if every port asked for is open.</summary>
        public bool Open(string inputContains, string outputContains)
        {
            Close();
            int i = Find(MidiPorts.Inputs(), inputContains), o = Find(MidiPorts.Outputs(), outputContains);
            if (i >= 0) input = MidiIn.TryOpen(i);
            if (o >= 0) output = MidiOut.TryOpen(o);
            return (string.IsNullOrEmpty(inputContains) || input != null) && (string.IsNullOrEmpty(outputContains) || output != null);
        }

        public void Close()
        {
            if (input != null) { input.Dispose(); input = null; }
            if (output != null) { output.Dispose(); output = null; }
            Forget();
        }

        public void Dispose() => Close();

        void Forget() { for (int i = 0; i < lastCc.Length; i++) lastCc[i] = -1; }

        /// <summary>Hands out what came in since the last call.</summary>
        public void Poll()
        {
            if (input == null) return;
            for (int guard = 0; guard < 256 && input.TryRead(out var m); guard++)
            {
                MessageReceived?.Invoke(m);
                if (m.Length == 0) continue;
                if (m[0] == 0xF0)
                {
                    if (SfxProtocol.TryParsePatch(m, out int slot, out var patch)) PatchReceived?.Invoke(slot, patch);
                    continue;
                }
                if (m.Length < 3) continue;
                int kind = m[0] & 0xF0, pad = m[1] - SfxProtocol.PadNoteBase, enc = m[1] - SfxProtocol.EncoderCcBase;
                if (kind == 0x90 && m[2] > 0 && pad >= 0 && pad < SfxProtocol.Slots) PadDown?.Invoke(pad, m[2] / 127f);
                else if ((kind == 0x80 || kind == 0x90) && pad >= 0 && pad < SfxProtocol.Slots) PadUp?.Invoke(pad);
                else if (kind == 0xB0 && enc >= 0 && enc < SfxProtocol.EncoderCount) EncoderChanged?.Invoke(enc, m[2] / 127f);
            }
        }

        // ---- to the device ------------------------------------------------------------------------------------------
        /// <summary>A game value 0..1. Sent only when its 7-bit value changes, so calling it every frame is fine.</summary>
        public bool SendTelemetry(SfxTelemetry channel, float value01)
        {
            int k = (int)channel;
            byte cc = SfxProtocol.ToCc(value01);
            if (output == null || k < 0 || k > 7 || lastCc[k] == cc) return false;
            lastCc[k] = cc;
            return output.Send(SfxProtocol.TelemetryStatus, (byte)(SfxProtocol.TelemetryCcBase + k), cc);
        }

        /// <summary>A patch into a pad of the device (it selects that pad).</summary>
        public bool SendPatch(int slot, SfxPatch patch) => output != null && output.SendSysEx(SfxProtocol.BuildLoad(slot, patch));

        /// <summary>Asks for a pad's patch; the answer comes as PatchReceived.</summary>
        public bool RequestPatch(int slot) => output != null && output.SendSysEx(SfxProtocol.BuildRequest(slot));

        /// <summary>Text for the device's top line (a level's name, a state): 24 ASCII characters.</summary>
        public bool SendText(string text) => output != null && output.SendSysEx(SfxProtocol.BuildText(text));

        public bool PlayPad(int slot, float velocity = 0.8f)
        {
            if (output == null || slot < 0 || slot >= SfxProtocol.Slots) return false;
            byte v = SfxProtocol.ToCc(velocity);
            return output.Send(0x90, (byte)(SfxProtocol.PadNoteBase + slot), v == 0 ? (byte)1 : v);
        }

        public bool ReleasePad(int slot)
            => output != null && slot >= 0 && slot < SfxProtocol.Slots && output.Send(0x80, (byte)(SfxProtocol.PadNoteBase + slot), 0);

        public static string Describe(byte[] m)
        {
            var s = new StringBuilder();
            for (int i = 0; i < m.Length && i < 24; i++) s.Append(m[i].ToString("X2")).Append(' ');
            if (m.Length > 24) s.Append('…');
            return s.ToString().TrimEnd();
        }
    }
}
