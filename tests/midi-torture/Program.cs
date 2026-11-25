// SPDX-License-Identifier: GPL-3.0-or-later
// midi-torture – heavy and hostile MIDI to a port the VIBE FM plugin listens on, while it plays SFX FORGE.
//
// WHAT IT SHOWS. The other tests call the firmware from a host of our own. This one goes through the real plugin
// (standalone or in a DAW): its MIDI input, its SysEx collector, its queue into the firmware, its audio thread. You
// start the plugin with SFX FORGE, start this tool on the MIDI port the plugin listens on, and watch the plugin. A
// crash, a hang, a silence that stays or a plugin that no longer answers is what it looks for.
//
// WHAT IT CANNOT SEE. It sends; it does not hear. It cannot tell a crash, a NaN or wrong audio: you watch and listen.
// The one thing it can check by itself needs --in: the port the plugin SENDS on. Then it asks for the identity before
// the run and after every phase, and a plugin that stopped answering makes it FAIL. Without --in its PASSED says no
// more than "every message was handed to the port". A virtual cable or a driver may drop or hold back what it does not
// like (a SysEx without its end, a status byte MIDI has not defined): what the plugin gets of the hostile phase
// depends on the cable. The tool counts what the port refused, not what the cable lost.
//
// HOW TO RUN IT (Windows; .NET SDK 8 or later):
//   dotnet run -c Release --project tests/midi-torture -- --list
//   dotnet run -c Release --project tests/midi-torture -- --port "name" [--in "name"] [--minutes 5] [--seed 1]
//                                                         [--level 1..3] [--phase 1..5] [--step n]
//                                                         [--seconds n] [--dry] [--dump file]
//     --list      the MIDI outputs and inputs of this computer, and no more
//     --port      the output to send to: a part of its name will do, if only one port has it
//     --in        the input to count on (what the plugin sends back), and to ask the identity through
//     --minutes   the length of the run, cut in five equal phases (5). --seconds: the same in seconds
//     --seed      the same seed, level and length send the same bytes in phases 1 to 4 (1)
//     --level     1 = about what a MIDI cable carries (3000 bytes a second), 2 = ten times, 3 = a hundred times
//     --phase     one phase only, for its whole length; --step: one step of it only (the number after the dot)
//     --dry       opens no port: counts what it would send, on a clock of its own (no waiting)
//     --dump      writes every message as a line of hex to a file ("M f0 7d ..."; "#" lines say where it is)
// The last line is "midi-torture PASSED" or "midi-torture FAILED" (exit code 0 or 1; 2: the arguments).
//
// THE PHASES. Each is announced with the time, and so is every step in it: if the plugin dies, the last line says what
// was going out. "--phase 4 --step 7" with the same seed and level sends that step again, alone.
//   1  notes        chords on the pads (36-51) and on the whole range, notes never released, note-offs without a
//                   note, velocity 0 and 127, all 16 channels
//   2  controllers  CC 10-17 on channel 2 (the telemetry) in sweeps and jumps, every other CC, pitch bend, aftertouch,
//                   program change, all notes off, the system messages
//   3  SysEx, valid identity and pad requests, the song request, text, valid patches to every pad, valid songs in
//                   pieces (format 6 from 211 to 38355 bytes, and a format-5 song in the shape of before 1.6.0)
//   4  SysEx, hostile  wrong checksums, cut messages, pieces out of order and repeated, counts that lie, nothing and
//                   too much, other makers' ids, a SysEx that never ends and notes after it
//   5  all at once  every step of 1 to 4, mixed by chance, as fast as the port takes it (at every level; how much
//                   goes out here depends on the computer, so this phase alone is not the same from run to run)
// At the end: every note off on every channel, the telemetry back where it rests, an empty song with a new song's
// mixer, one plain sound on every pad, and pad 1 played five times - which you should hear.
//
// THE PROTOCOL is written out here from sfx-forge/fw_protocol.h, fw_link.h and core/sfx_song.h ("A song as bytes"):
// nothing of the Unity package or of the firmware is compiled in. A number changed there must change here.
//
// WHAT THIS FILE HOLDS, in its order: Rng (the seeded numbers) · Mm (winmm.dll) · Sender (the output, its clock and
// its counts) · Listener (the input) · Wire (the messages, a patch, a song) · the steps of phases 1 to 4 · Program
// (the arguments, the instructions, the run, the end).
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace MidiTorture
{
    // ---- the seeded numbers: the same on every .NET, which System.Random does not promise --------------------------
    sealed class Rng
    {
        ulong state;

        public Rng(params long[] parts)
        {
            ulong x = 0x9E3779B97F4A7C15UL;
            foreach (long p in parts)
            {
                x += (ulong)p + 0x9E3779B97F4A7C15UL;
                x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9UL;
                x = (x ^ (x >> 27)) * 0x94D049BB133111EBUL;
                x ^= x >> 31;
            }
            state = x == 0 ? 1 : x;
        }

        ulong Next()
        {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            return state * 2685821657736338717UL;
        }

        /// <summary>0 .. n - 1</summary>
        public int To(int n) => (int)((Next() >> 33) % (ulong)n);

        /// <summary>lo .. hi, both included</summary>
        public int In(int lo, int hi) => lo + To(hi - lo + 1);

        public bool Chance(int percent) => To(100) < percent;

        public int Pick(params int[] a) => a[To(a.Length)];

        public byte[] Bytes(int n, int below = 128)
        {
            var b = new byte[n];
            for (int i = 0; i < n; i++)
            {
                b[i] = (byte)To(below);
            }
            return b;
        }
    }

    // ---- winmm.dll: the calls and the two structures they take -----------------------------------------------------
    static class Mm
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct OutCaps
        {
            public ushort Maker;
            public ushort Product;
            public uint Version;

            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string Name;
            public ushort Technology;
            public ushort Voices;
            public ushort Notes;
            public ushort ChannelMask;
            public uint Support;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct InCaps
        {
            public ushort Maker;
            public ushort Product;
            public uint Version;

            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string Name;
            public uint Support;
        }

        // MIDIHDR: the buffer of a long message
        [StructLayout(LayoutKind.Sequential)]
        public struct Header
        {
            public IntPtr Data;
            public uint BufferLength;
            public uint BytesRecorded;
            public IntPtr User;
            public uint Flags;
            public IntPtr Next;
            public IntPtr Reserved;
            public uint Offset;
            public IntPtr R0;
            public IntPtr R1;
            public IntPtr R2;
            public IntPtr R3;
            public IntPtr R4;
            public IntPtr R5;
            public IntPtr R6;
            public IntPtr R7;
        }

        public static readonly uint HeaderSize = (uint)Marshal.SizeOf(typeof(Header));
        public static readonly int FlagsAt = (int)Marshal.OffsetOf(typeof(Header), "Flags");
        public static readonly int RecordedAt = (int)Marshal.OffsetOf(typeof(Header), "BytesRecorded");

        public const uint HeaderDone = 1;
        public const uint CallbackFunction = 0x30000;
        public const uint InData = 0x3C3;
        public const uint InLongData = 0x3C4;
        public const uint InLongError = 0x3C6;
        public const uint NotReady = 67;
        public const uint StillPlaying = 65;

        public delegate void InProc(IntPtr handle, uint message, IntPtr instance, IntPtr p1, IntPtr p2);

        const string W = "winmm.dll";

        [DllImport(W)]
        public static extern uint midiOutGetNumDevs();

        [DllImport(W, CharSet = CharSet.Unicode)]
        public static extern uint midiOutGetDevCapsW(UIntPtr id, out OutCaps caps, uint size);

        [DllImport(W)]
        public static extern uint midiOutOpen(out IntPtr handle, uint id, IntPtr callback, IntPtr instance, uint flags);

        [DllImport(W)]
        public static extern uint midiOutShortMsg(IntPtr handle, uint message);

        [DllImport(W)]
        public static extern uint midiOutPrepareHeader(IntPtr handle, IntPtr header, uint size);

        [DllImport(W)]
        public static extern uint midiOutLongMsg(IntPtr handle, IntPtr header, uint size);

        [DllImport(W)]
        public static extern uint midiOutUnprepareHeader(IntPtr handle, IntPtr header, uint size);

        [DllImport(W)]
        public static extern uint midiOutReset(IntPtr handle);

        [DllImport(W)]
        public static extern uint midiOutClose(IntPtr handle);

        [DllImport(W)]
        public static extern uint midiInGetNumDevs();

        [DllImport(W, CharSet = CharSet.Unicode)]
        public static extern uint midiInGetDevCapsW(UIntPtr id, out InCaps caps, uint size);

        [DllImport(W)]
        public static extern uint midiInOpen(out IntPtr handle, uint id, InProc callback, IntPtr instance, uint flags);

        [DllImport(W)]
        public static extern uint midiInPrepareHeader(IntPtr handle, IntPtr header, uint size);

        [DllImport(W)]
        public static extern uint midiInUnprepareHeader(IntPtr handle, IntPtr header, uint size);

        [DllImport(W)]
        public static extern uint midiInAddBuffer(IntPtr handle, IntPtr header, uint size);

        [DllImport(W)]
        public static extern uint midiInStart(IntPtr handle);

        [DllImport(W)]
        public static extern uint midiInStop(IntPtr handle);

        [DllImport(W)]
        public static extern uint midiInReset(IntPtr handle);

        [DllImport(W)]
        public static extern uint midiInClose(IntPtr handle);

        public static List<string> Outputs()
        {
            var list = new List<string>();
            uint n = midiOutGetNumDevs();
            for (uint i = 0; i < n; i++)
            {
                list.Add(
                    midiOutGetDevCapsW((UIntPtr)i, out var c, (uint)Marshal.SizeOf(typeof(OutCaps))) == 0 ? c.Name : "?"
                );
            }
            return list;
        }

        public static List<string> Inputs()
        {
            var list = new List<string>();
            uint n = midiInGetNumDevs();
            for (uint i = 0; i < n; i++)
            {
                list.Add(
                    midiInGetDevCapsW((UIntPtr)i, out var c, (uint)Marshal.SizeOf(typeof(InCaps))) == 0 ? c.Name : "?"
                );
            }
            return list;
        }
    }

    // ---- the output: every message goes through here, is counted, and moves the clock ------------------------------
    // The clock (V, in seconds) is not the wall's: a message moves it by its bytes / Rate. Phases and steps end by it,
    // so the same seed sends the same bytes whatever the computer does. A real run waits until the wall's clock has
    // caught up with it. Rate 0 (phase 5): no waiting, and V follows the wall (a dry run: a million bytes a second).
    sealed class Sender : IDisposable
    {
        const int Capacity = 70000;

        IntPtr handle;
        bool open;
        IntPtr header;
        IntPtr buffer;
        readonly bool dry;
        readonly StreamWriter dump;
        readonly Stopwatch wall = Stopwatch.StartNew();
        double lag;

        public double Rate;
        public double V;
        public double SliceEnd = double.MaxValue;
        public volatile bool Cancel;

        /// <summary>the phase is long enough for the largest messages and songs</summary>
        public bool Roomy;
        public int Level = 1;

        public long Shorts;
        public long Longs;
        public long Bytes;
        public long Refused;
        public long Errors;
        public string FirstError;

        public Sender(bool dry, string dumpFile)
        {
            this.dry = dry;
            if (dumpFile != null)
            {
                dump = new StreamWriter(dumpFile, false, new UTF8Encoding(false)) { NewLine = "\n" };
            }
        }

        public bool Dry => dry;
        public bool Over => Cancel || V >= SliceEnd;

        public string Open(uint id)
        {
            uint r = Mm.midiOutOpen(out handle, id, IntPtr.Zero, IntPtr.Zero, 0);
            if (r != 0)
            {
                return "midiOutOpen says " + r + " (4: the port is in use by another program)";
            }
            open = true;
            header = Marshal.AllocHGlobal((int)Mm.HeaderSize);
            buffer = Marshal.AllocHGlobal(Capacity);
            return null;
        }

        /// <summary>from here on the wall's clock and V run together again (after a wait, at a phase's start)</summary>
        public void Sync()
        {
            lag = wall.Elapsed.TotalSeconds - V;
        }

        /// <summary>a line in the dump that is no message</summary>
        public void Note(string text)
        {
            dump?.WriteLine("# " + text);
        }

        void Error(string call, uint code)
        {
            Errors++;
            if (FirstError == null)
            {
                FirstError = call + " says " + code + " after " + (Shorts + Longs) + " messages";
            }
        }

        void Pace(int n)
        {
            if (Rate > 0)
            {
                V += n / Rate;
                if (dry)
                {
                    return;
                }
                // wait for the wall: sleep while it is far, spin for the last two milliseconds
                for (; ; )
                {
                    double ahead = V - (wall.Elapsed.TotalSeconds - lag);
                    if (ahead <= 0 || Cancel)
                    {
                        break;
                    }
                    if (ahead > 0.002)
                    {
                        Thread.Sleep(1);
                    }
                    else
                    {
                        Thread.SpinWait(50);
                    }
                }
            }
            else if (dry)
            {
                V += n / 1e6;
            }
            else
            {
                V = Math.Max(V, wall.Elapsed.TotalSeconds - lag);
            }
        }

        // the bytes a status byte brings with it (itself included)
        static int Length(int status)
        {
            if (status >= 0xF8)
            {
                return 1;
            }
            if (status >= 0xF0)
            {
                return status == 0xF2 ? 3
                    : status == 0xF1 || status == 0xF3 ? 2
                    : 1;
            }
            return (status & 0xE0) == 0xC0 ? 2 : 3;
        }

        /// <summary>a message of one to three bytes</summary>
        public void Short(int status, int d1 = 0, int d2 = 0)
        {
            int n = Length(status);
            Shorts++;
            Bytes += n;
            if (dump != null)
            {
                dump.WriteLine(
                    n == 1 ? "M " + status.ToString("x2")
                    : n == 2 ? "M " + status.ToString("x2") + " " + d1.ToString("x2")
                    : "M " + status.ToString("x2") + " " + d1.ToString("x2") + " " + d2.ToString("x2")
                );
            }
            if (open)
            {
                uint word = (uint)(status | d1 << 8 | d2 << 16);
                uint r = Mm.midiOutShortMsg(handle, word);
                // "not ready": the port is busy. Tried again for a tenth of a second, then counted as refused
                for (
                    long until = wall.ElapsedMilliseconds + 100;
                    r == Mm.NotReady && wall.ElapsedMilliseconds < until;

                )
                {
                    Thread.Yield();
                    r = Mm.midiOutShortMsg(handle, word);
                }
                if (r == Mm.NotReady)
                {
                    Refused++;
                }
                else if (r != 0)
                {
                    Error("midiOutShortMsg", r);
                }
            }
            Pace(n);
        }

        public void On(int channel, int note, int velocity) => Short(0x90 | channel, note, velocity);

        public void Off(int channel, int note, int velocity = 0) => Short(0x80 | channel, note, velocity);

        public void Cc(int channel, int number, int value) => Short(0xB0 | channel, number, value);

        /// <summary>a long message: these bytes as they are - a SysEx whole, or a part of one</summary>
        public void Raw(byte[] b)
        {
            int n = Math.Min(b.Length, Capacity);
            Longs++;
            Bytes += n;
            if (dump != null)
            {
                var text = new StringBuilder("M", 2 + n * 3);
                for (int i = 0; i < n; i++)
                {
                    text.Append(' ').Append(b[i].ToString("x2"));
                }
                dump.WriteLine(text.ToString());
            }
            if (open && n > 0)
            {
                Marshal.Copy(b, 0, buffer, n);
                var h = new Mm.Header
                {
                    Data = buffer,
                    BufferLength = (uint)n,
                    BytesRecorded = (uint)n,
                };
                Marshal.StructureToPtr(h, header, false);
                uint r = Mm.midiOutPrepareHeader(handle, header, Mm.HeaderSize);
                if (r != 0)
                {
                    Error("midiOutPrepareHeader", r);
                }
                else
                {
                    r = Mm.midiOutLongMsg(handle, header, Mm.HeaderSize);
                    long until = wall.ElapsedMilliseconds + 100;
                    while (r == Mm.NotReady && wall.ElapsedMilliseconds < until)
                    {
                        Thread.Yield();
                        r = Mm.midiOutLongMsg(handle, header, Mm.HeaderSize);
                    }
                    if (r == Mm.NotReady)
                    {
                        Refused++;
                    }
                    else if (r != 0)
                    {
                        Error("midiOutLongMsg", r);
                    }
                    else
                    {
                        // the driver has it when the header says "done": two seconds for that, then the port is reset
                        until = wall.ElapsedMilliseconds + 2000;
                        while ((Marshal.ReadInt32(header, Mm.FlagsAt) & Mm.HeaderDone) == 0)
                        {
                            if (wall.ElapsedMilliseconds > until)
                            {
                                Error("midiOutLongMsg (never done)", 0);
                                Mm.midiOutReset(handle);
                                break;
                            }
                            Thread.Yield();
                        }
                    }
                    until = wall.ElapsedMilliseconds + 2000;
                    while (
                        Mm.midiOutUnprepareHeader(handle, header, Mm.HeaderSize) == Mm.StillPlaying
                        && wall.ElapsedMilliseconds < until
                    )
                    {
                        Thread.Yield();
                    }
                }
            }
            Pace(n);
        }

        /// <summary>waits (not in a dry run), and the clocks run together again after it</summary>
        public void Rest(int milliseconds)
        {
            if (!dry)
            {
                Thread.Sleep(milliseconds);
            }
            Sync();
        }

        public void Dispose()
        {
            dump?.Dispose();
            if (open)
            {
                open = false;
                Mm.midiOutClose(handle);
                Marshal.FreeHGlobal(header);
                Marshal.FreeHGlobal(buffer);
            }
        }
    }

    // ---- the input: counts what comes back, and knows an identity when it sees one ---------------------------------
    // The callback of winmm does as little as it may: it counts a short message, and hands a long one's buffer to a
    // thread of ours, which reads it and gives the buffer back to the port.
    sealed class Listener : IDisposable
    {
        const int Buffers = 8;
        const int BufferBytes = 4096;

        IntPtr handle;
        readonly List<IntPtr> headers = new List<IntPtr>();
        readonly BlockingCollection<IntPtr> done = new BlockingCollection<IntPtr>();
        readonly List<byte> message = new List<byte>();
        Mm.InProc callback;
        Thread reader;
        volatile bool closing;

        public long Shorts;
        public long SysEx;
        public long Identities;
        public long Dumps;
        public long Pieces;
        public long Others;

        /// <summary>the firmware's version in the last identity: "1.7.1"</summary>
        public volatile string Version = "?";

        public string Open(uint id)
        {
            callback = OnMidi;
            uint r = Mm.midiInOpen(out handle, id, callback, IntPtr.Zero, Mm.CallbackFunction);
            if (r != 0)
            {
                return "midiInOpen says " + r + " (4: the port is in use by another program)";
            }
            for (int i = 0; i < Buffers; i++)
            {
                IntPtr h = Marshal.AllocHGlobal((int)Mm.HeaderSize + BufferBytes);
                var head = new Mm.Header { Data = h + (int)Mm.HeaderSize, BufferLength = BufferBytes };
                Marshal.StructureToPtr(head, h, false);
                Mm.midiInPrepareHeader(handle, h, Mm.HeaderSize);
                Mm.midiInAddBuffer(handle, h, Mm.HeaderSize);
                headers.Add(h);
            }
            reader = new Thread(Read) { IsBackground = true, Name = "midi in" };
            reader.Start();
            Mm.midiInStart(handle);
            return null;
        }

        void OnMidi(IntPtr h, uint what, IntPtr instance, IntPtr p1, IntPtr p2)
        {
            if (what == Mm.InData)
            {
                Interlocked.Increment(ref Shorts);
            }
            else if ((what == Mm.InLongData || what == Mm.InLongError) && !closing)
            {
                try
                {
                    done.Add(p1);
                }
                catch (InvalidOperationException)
                {
                    // (the port is closing: nobody reads any more)
                }
            }
        }

        void Read()
        {
            foreach (IntPtr h in done.GetConsumingEnumerable())
            {
                int n = Marshal.ReadInt32(h, Mm.RecordedAt);
                for (int i = 0; i < n; i++)
                {
                    byte b = Marshal.ReadByte(h, (int)Mm.HeaderSize + i);
                    if (b == 0xF0)
                    {
                        message.Clear();
                    }
                    message.Add(b);
                    if (b == 0xF7)
                    {
                        Take();
                        message.Clear();
                    }
                }
                if (!closing)
                {
                    Mm.midiInAddBuffer(handle, h, Mm.HeaderSize);
                }
            }
        }

        // one whole SysEx message
        void Take()
        {
            Interlocked.Increment(ref SysEx);
            var m = message;
            if (m.Count < 6 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 0x56 || m[3] != 0x42)
            {
                Interlocked.Increment(ref Others);
            }
            else if (m[4] == 0x08 && m.Count >= 6 + 18)
            {
                Version = m[6] + "." + m[7] + "." + m[8];
                Interlocked.Increment(ref Identities);
            }
            else if (m[4] == 0x01)
            {
                Interlocked.Increment(ref Dumps);
            }
            else if (m[4] == 0x05)
            {
                Interlocked.Increment(ref Pieces);
            }
            else
            {
                Interlocked.Increment(ref Others);
            }
        }

        public void Dispose()
        {
            if (handle == IntPtr.Zero)
            {
                return;
            }
            closing = true;
            Mm.midiInStop(handle);
            Mm.midiInReset(handle);
            done.CompleteAdding();
            reader?.Join(1000);
            foreach (IntPtr h in headers)
            {
                Mm.midiInUnprepareHeader(handle, h, Mm.HeaderSize);
                Marshal.FreeHGlobal(h);
            }
            Mm.midiInClose(handle);
            handle = IntPtr.Zero;
            GC.KeepAlive(callback);
        }
    }

    // ---- the messages of SFX FORGE, a patch and a song as bytes ----------------------------------------------------
    static class Wire
    {
        public const int Dump = 1;
        public const int Load = 2;
        public const int Text = 3;
        public const int Request = 4;
        public const int Song = 5;
        public const int RequestSong = 6;
        public const int RequestIdentity = 7;
        public const int Identity = 8;

        public const int PatchBytes = 28;
        public const int Piece = 240;
        public const int PiecesMost = 160;
        public const int SongHead = 147;
        public const int SongMost = 38355;

        public static byte[] Join(params byte[][] parts)
        {
            int n = 0;
            foreach (var p in parts)
            {
                n += p.Length;
            }
            var b = new byte[n];
            n = 0;
            foreach (var p in parts)
            {
                p.CopyTo(b, n);
                n += p.Length;
            }
            return b;
        }

        public static byte[] B(params int[] values)
        {
            var b = new byte[values.Length];
            for (int i = 0; i < b.Length; i++)
            {
                b[i] = (byte)values[i];
            }
            return b;
        }

        /// <summary>F0 7D 56 42 message body F7</summary>
        public static byte[] Msg(int message, params byte[] body)
        {
            return Join(B(0xF0, 0x7D, 0x56, 0x42, message), body, B(0xF7));
        }

        public static void Checksum(byte[] patch)
        {
            int sum = 0;
            for (int i = 0; i < PatchBytes - 1; i++)
            {
                sum += patch[i];
            }
            patch[PatchBytes - 1] = (byte)(sum & 0x7F);
        }

        /// <summary>A valid patch: version 2, an engine 0..4, 25 values 0..127, the checksum.
        /// kind 0: any values. 1: all 0. 2: all 127. 3: the plain sound of the end.</summary>
        public static byte[] Patch(Rng r, int kind)
        {
            var p = new byte[PatchBytes];
            p[0] = 2;
            if (kind == 3)
            {
                // a square blip: no envelope tricks, no noise, the filter open, a short decay, level 100
                // csharpier-ignore
                byte[] plain = { 1, 80, 64, 40, 40, 40, 0, 110, 10, 70, 0, 0, 0, 20, 64, 40, 0, 0, 0, 0, 0, 0, 0, 20, 100, 0 };
                plain.CopyTo(p, 1);
            }
            else
            {
                p[1] = (byte)r.To(5);
                for (int i = 2; i < PatchBytes - 1; i++)
                {
                    p[i] = (byte)(
                        kind == 1 ? 0
                        : kind == 2 ? 127
                        : r.To(128)
                    );
                }
            }
            Checksum(p);
            return p;
        }

        public static byte[] PatchMsg(int message, int slot, byte[] patch)
        {
            return Msg(message, Join(B(slot), patch));
        }

        // the head of a song, formats 5 and 6: 147 bytes, every value in its range
        static void Head(List<byte> o, Rng r, int format)
        {
            int bpm = r.In(40, 240);
            o.Add((byte)format);
            o.Add((byte)r.In(1, 16)); // the bar in sixteenths
            o.Add((byte)(bpm >> 7));
            o.Add((byte)(bpm & 127));
            o.Add((byte)r.In(0, 100)); // swing
            o.Add((byte)r.To(4)); // scale
            o.Add((byte)r.In(1, 8)); // chain
            o.AddRange(r.Bytes(16)); // the layers
            o.AddRange(r.Bytes(16)); // the faders
            o.AddRange(r.Bytes(16)); // the pans
            o.Add((byte)r.To(128)); // mute, 7 + 7 + 2 bits
            o.Add((byte)r.To(128));
            o.Add((byte)r.To(4));
            o.Add((byte)r.To(6)); // the master: the delay's time, then seven values
            o.AddRange(r.Bytes(7));
            o.Add((byte)r.In(0, 32)); // the arrangement's length and its 32 bars
            o.AddRange(r.Bytes(32, 8));
            for (int l = 0; l < 24; l++)
            {
                // a lane: whose knob (0, or a sound + 1) and which (0 = free, up to 32)
                int knob = r.Chance(50) ? r.In(1, 32) : 0;
                o.Add((byte)(knob == 0 ? 0 : r.In(0, 16)));
                o.Add((byte)knob);
            }
        }

        /// <summary>A valid format-6 song. fill 0: no row anywhere (211 bytes). 1: some rows and lanes.
        /// 2: every row and every lane at 64 steps (38355 bytes, the most there is).
        /// Every row it writes holds something, so the firmware's own packer writes the same bytes back.</summary>
        public static byte[] Song6(Rng r, int fill)
        {
            var o = new List<byte>(fill == 2 ? SongMost : 8192);
            Head(o, r, 6);
            for (int p = 0; p < 8; p++)
            {
                int grid = fill == 2 ? 2 : r.To(3);
                int steps = 16 << grid;
                int bitBytes = (steps + 6) / 7;
                uint rows =
                    fill == 2 ? 0xFFFFu
                    : fill == 0 ? 0
                    : (uint)(r.To(65536) & r.To(65536) & r.To(65536));
                uint lanes =
                    fill == 2 ? 0xFFFFFFu
                    : fill == 0 ? 0
                    : (uint)(r.To(1 << 24) & r.To(1 << 24) & r.To(1 << 24));
                o.Add((byte)grid);
                o.Add((byte)(rows & 127));
                o.Add((byte)(rows >> 7 & 127));
                o.Add((byte)(rows >> 14 & 3));
                o.Add((byte)(lanes & 127));
                o.Add((byte)(lanes >> 7 & 127));
                o.Add((byte)(lanes >> 14 & 127));
                o.Add((byte)(lanes >> 21 & 7));
                for (int t = 0; t < 16; t++)
                {
                    if ((rows >> t & 1) == 0)
                    {
                        continue;
                    }
                    // the steps that are on (7 a byte; the last byte holds what is left), with step 0 always on
                    byte[] bits = r.Bytes(bitBytes);
                    bits[0] |= 1;
                    bits[bitBytes - 1] &= (byte)((1 << (steps - (bitBytes - 1) * 7)) - 1);
                    o.AddRange(bits);
                    o.AddRange(r.Bytes(steps)); // notes
                    o.AddRange(r.Bytes(steps, 8)); // velocities
                    o.AddRange(r.Bytes(steps, steps)); // lengths
                }
                for (int l = 0; l < 24; l++)
                {
                    if ((lanes >> l & 1) != 0)
                    {
                        byte[] points = r.Bytes(steps);
                        points[0] |= 1;
                        o.AddRange(points);
                    }
                }
            }
            return o.ToArray();
        }

        /// <summary>A valid format-5 song: the head, 8 x 16 rows of 16 steps (step bits, notes, accents), then 8 x 24
        /// lanes of 16 points. 7699 bytes.</summary>
        public static byte[] Song5(Rng r)
        {
            var o = new List<byte>(7699);
            Head(o, r, 5);
            for (int row = 0; row < 8 * 16; row++)
            {
                o.Add((byte)r.To(128));
                o.Add((byte)r.To(128));
                o.Add((byte)r.To(4));
                o.AddRange(r.Bytes(32));
            }
            o.AddRange(r.Bytes(8 * 24 * 16));
            return o.ToArray();
        }

        /// <summary>the empty song with a new song's mixer: what the run leaves behind</summary>
        public static byte[] SongAtRest()
        {
            var o = new List<byte>(211);
            o.AddRange(B(6, 16, 0, 120, 0, 0, 1));
            for (int i = 0; i < 16; i++)
            {
                o.Add(0);
            }
            for (int i = 0; i < 16; i++)
            {
                o.Add(100);
            }
            for (int i = 0; i < 16; i++)
            {
                o.Add(64);
            }
            o.AddRange(B(0, 0, 0, 2, 64, 91, 40, 0, 0, 0, 0));
            o.AddRange(new byte[1 + 32 + 48 + 8 * 8]);
            return o.ToArray();
        }

        /// <summary>one piece, every field as given: F0 7D 56 42 05 k-lo k-hi format K-lo K-hi data F7</summary>
        public static byte[] PieceRaw(int kLow, int kHigh, int format, int countLow, int countHigh, byte[] data)
        {
            return Msg(Song, Join(B(kLow, kHigh, format, countLow, countHigh), data));
        }

        public static byte[] Part(byte[] b, int from, int n)
        {
            n = Math.Max(0, Math.Min(n, b.Length - from));
            var part = new byte[n];
            Array.Copy(b, from, part, 0, n);
            return part;
        }

        /// <summary>a song as its pieces of 240 (count: what the pieces say how many they are; 0 = the truth)</summary>
        public static List<byte[]> Pieces(byte[] song, int count = 0, int format = -1)
        {
            int K = (song.Length + Piece - 1) / Piece;
            int says = count > 0 ? count : K;
            var list = new List<byte[]>();
            for (int k = 0; k < K; k++)
            {
                list.Add(
                    PieceRaw(
                        k & 127,
                        k >> 7,
                        format < 0 ? song[0] : format,
                        says & 127,
                        says >> 7,
                        Part(song, k * Piece, Piece)
                    )
                );
            }
            return list;
        }

        /// <summary>a format-5 song in the shape of before 1.6.0: F0 7D 56 42 05 k 39 data F7, 39 pieces of 200</summary>
        public static List<byte[]> OldPieces(byte[] song)
        {
            var list = new List<byte[]>();
            for (int k = 0; k < 39; k++)
            {
                list.Add(Msg(Song, Join(B(k, 39), Part(song, k * 200, 200))));
            }
            return list;
        }

        public static void Shuffle<T>(Rng r, List<T> list)
        {
            for (int i = list.Count - 1; i > 0; i--)
            {
                int j = r.To(i + 1);
                (list[i], list[j]) = (list[j], list[i]);
            }
        }
    }

    sealed class Step
    {
        public readonly string Name;
        public readonly Action<Rng, Sender> Run;

        public Step(string name, Action<Rng, Sender> run)
        {
            Name = name;
            Run = run;
        }
    }

    // ---- the steps. Each call of a step sends one short burst; the run calls it again until its time is over. -------
    static class Steps
    {
        const int Pad0 = 36;

        // ---- phase 1: notes ----
        public static readonly Step[] Notes = { new Step("all 16 pads down and up, channel 1", (r, s) =>
            {
                for (int n = 0; n < 16; n++)
                {
                    s.On(0, Pad0 + n, r.In(1, 127));
                }
                for (int n = 0; n < 16; n++)
                {
                    s.Off(0, Pad0 + n);
                }
            }), new Step("the 16 pads as a chord on all 16 channels", (r, s) =>
            {
                for (int c = 0; c < 16 && !s.Over; c++)
                {
                    for (int n = 0; n < 16; n++)
                    {
                        s.On(c, Pad0 + n, r.In(1, 127));
                    }
                }
                // let go half with a note-off, half with a note-on of velocity 0
                for (int c = 0; c < 16; c++)
                {
                    for (int n = 0; n < 16; n++)
                    {
                        if ((c & 1) == 0)
                        {
                            s.Off(c, Pad0 + n, r.To(128));
                        }
                        else
                        {
                            s.On(c, Pad0 + n, 0);
                        }
                    }
                }
            }), new Step("all 128 notes down, then up", (r, s) =>
            {
                int c = r.To(16);
                for (int n = 0; n < 128; n++)
                {
                    s.On(c, n, r.In(1, 127));
                }
                for (int n = 127; n >= 0; n--)
                {
                    s.Off(c, n);
                }
            }), new Step("stuck notes: note-ons that are never released", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    s.On(r.To(16), r.Chance(50) ? Pad0 + r.To(16) : r.To(128), r.In(1, 127));
                }
            }), new Step("note-offs without a note-on", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    if (r.Chance(50))
                    {
                        s.Off(r.To(16), r.To(128), r.To(128));
                    }
                    else
                    {
                        s.On(r.To(16), r.To(128), 0);
                    }
                }
            }), new Step("velocity 0 and 127 only", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    s.On(r.To(16), r.Chance(50) ? Pad0 + r.To(16) : r.To(128), r.Chance(50) ? 0 : 127);
                }
            }), new Step("one pad hit again and again, never let go", (r, s) =>
            {
                int n = Pad0 + r.To(16);
                for (int i = 0; i < 64; i++)
                {
                    s.On(0, n, r.Pick(1, 64, 127));
                }
            }), new Step("one key down and up as fast as it goes", (r, s) =>
            {
                int n = r.To(128);
                int c = r.To(16);
                for (int i = 0; i < 32; i++)
                {
                    s.On(c, n, 127);
                    s.Off(c, n);
                }
            }), new Step("the edges: notes 0, 35, 36, 51, 52, 127", (r, s) =>
            {
                for (int i = 0; i < 32; i++)
                {
                    int n = r.Pick(0, 35, 36, 51, 52, 127, 95, 96, 100, 115);
                    s.On(r.To(16), n, r.Pick(1, 127));
                    if (r.Chance(70))
                    {
                        s.Off(r.To(16), n);
                    }
                }
            }), new Step("any note, any channel, down or up", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    if (r.Chance(55))
                    {
                        s.On(r.To(16), r.To(128), r.To(128));
                    }
                    else
                    {
                        s.Off(r.To(16), r.To(128), r.To(128));
                    }
                }
            }) };

        // ---- phase 2: controllers and the rest of the short messages ----
        public static readonly Step[] Controllers =
        {
            new Step("telemetry CC 10-17 on channel 2: sweeps up and down", (r, s) =>
            {
                int cc = r.In(10, 17);
                for (int v = 0; v < 128; v++)
                {
                    s.Cc(1, cc, v);
                }
                for (int v = 127; v >= 0; v--)
                {
                    s.Cc(1, cc, v);
                }
            }),
            new Step("telemetry CC 10-17: jumps between the ends", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    s.Cc(1, r.In(10, 17), r.Pick(0, 127, 0, 127, 1, 126, 63, 64, 65));
                }
            }),
            new Step("telemetry while pads and keys sound", (r, s) =>
            {
                for (int i = 0; i < 16; i++)
                {
                    int n = r.Chance(60) ? Pad0 + r.To(16) : r.In(24, 96);
                    s.On(0, n, r.In(40, 127));
                    for (int k = 0; k < 6; k++)
                    {
                        s.Cc(1, r.In(10, 14), r.To(128));
                    }
                    if (r.Chance(80))
                    {
                        s.Off(0, n);
                    }
                }
            }),
            new Step("CC 15 cues another pattern with every message", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    s.Cc(1, 15, r.To(8) * 16 + r.To(16));
                }
            }),
            new Step("CC 10-17 on the 15 channels that are not the telemetry's", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    int c = r.To(15);
                    s.Cc(c >= 1 ? c + 1 : c, r.In(10, 17), r.To(128));
                }
            }),
            new Step("every CC 0-127 on every channel", (r, s) =>
            {
                int c = r.To(16);
                for (int cc = 0; cc < 128; cc++)
                {
                    s.Cc(c, cc, r.Pick(0, 127, r.To(128)));
                }
            }),
            new Step("pitch bend: sweeps and jumps", (r, s) =>
            {
                int c = r.To(16);
                for (int v = 0; v < 128; v += 2)
                {
                    s.Short(0xE0 | c, r.To(128), v);
                }
                for (int i = 0; i < 32; i++)
                {
                    s.Short(0xE0 | r.To(16), r.Pick(0, 127), r.Pick(0, 127, 64));
                }
            }),
            new Step("aftertouch, of the channel and of single keys", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    if (r.Chance(50))
                    {
                        s.Short(0xD0 | r.To(16), r.To(128));
                    }
                    else
                    {
                        s.Short(0xA0 | r.To(16), r.Chance(50) ? Pad0 + r.To(16) : r.To(128), r.To(128));
                    }
                }
            }),
            new Step("program changes", (r, s) =>
            {
                for (int i = 0; i < 64; i++)
                {
                    s.Short(0xC0 | r.To(16), r.To(128));
                }
            }),
            new Step(
                "all notes off, all sound off, reset controllers, the mode messages - over sounding notes",
                (r, s) =>
                {
                    for (int i = 0; i < 16; i++)
                    {
                        s.On(r.To(16), Pad0 + r.To(16), 100);
                        s.On(r.To(16), r.To(128), 100);
                        s.Cc(r.To(16), r.In(120, 127), r.Pick(0, 127, r.To(128)));
                        s.Cc(r.To(16), 64, r.Pick(0, 127));
                    }
                }
            ),
            new Step(
                "system messages: clock, start, continue, stop, active sensing, song position and select, tune, reset",
                (r, s) =>
                {
                    for (int i = 0; i < 64; i++)
                    {
                        int status = r.Pick(0xF8, 0xF8, 0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF, 0xF1, 0xF2, 0xF3, 0xF6);
                        s.Short(status, r.To(128), r.To(128));
                    }
                }
            ),
            new Step("status bytes MIDI has not defined: F4, F5, F9, FD", (r, s) =>
            {
                for (int i = 0; i < 16; i++)
                {
                    s.Short(r.Pick(0xF4, 0xF5, 0xF9, 0xFD));
                    s.On(0, Pad0 + r.To(16), 100);
                }
            }),
        };

        // a transfer of a valid song, said so in the dump (a check that replays the dump can ask for the song after it)
        static void ValidSong(Sender s, byte[] song, List<byte[]> pieces)
        {
            s.Note("valid-song begins");
            foreach (var p in pieces)
            {
                s.Raw(p);
            }
            s.Note("valid-song ends " + Convert.ToHexString(song));
        }

        // ---- phase 3: SysEx as it should be ----
        public static readonly Step[] Valid =
        {
            new Step("the identity request", (r, s) => s.Raw(Wire.Msg(Wire.RequestIdentity))),
            new Step("a request for each of the 16 pads", (r, s) =>
            {
                for (int slot = 0; slot < 16; slot++)
                {
                    s.Raw(Wire.Msg(Wire.Request, (byte)slot));
                }
            }),
            new Step("the song request (16 sounds and the song come back), pads played meanwhile", (r, s) =>
            {
                s.Raw(Wire.Msg(Wire.RequestSong));
                for (int i = 0; i < 32; i++)
                {
                    int n = Pad0 + r.To(16);
                    s.On(0, n, 100);
                    s.Off(0, n);
                }
            }),
            new Step("text for the LINK page, 0 to 24 characters", (r, s) =>
            {
                var text = new byte[r.In(0, 24)];
                for (int i = 0; i < text.Length; i++)
                {
                    text[i] = (byte)r.In(32, 126);
                }
                s.Raw(Wire.Msg(Wire.Text, text));
            }),
            new Step("a valid patch to every pad (LOAD 0-15), each played as a pad and as a key", (r, s) =>
            {
                for (int slot = 0; slot < 16 && !s.Over; slot++)
                {
                    byte[] patch = Wire.Patch(r, 0);
                    s.Raw(Wire.PatchMsg(Wire.Load, slot, patch));
                    s.Note("valid-patch " + slot + " " + Convert.ToHexString(patch));
                    int key = r.In(0, 127);
                    s.On(0, Pad0 + slot, r.In(1, 127));
                    s.On(0, key, r.In(1, 127));
                    s.Off(0, Pad0 + slot);
                    s.Off(0, key);
                }
            }),
            new Step("a valid patch to every pad of the MUSIC kit (LOAD 16-31)", (r, s) =>
            {
                for (int slot = 16; slot < 32; slot++)
                {
                    s.Raw(Wire.PatchMsg(Wire.Load, slot, Wire.Patch(r, 0)));
                }
            }),
            new Step("a valid patch as a DUMP (01), as the device itself sends one", (r, s) =>
            {
                for (int i = 0; i < 16; i++)
                {
                    int slot = r.To(16);
                    byte[] patch = Wire.Patch(r, 0);
                    s.Raw(Wire.PatchMsg(Wire.Dump, slot, patch));
                    s.Note("valid-patch " + slot + " " + Convert.ToHexString(patch));
                }
            }),
            new Step(
                "the patches at their ends (every value 0, every value 127, each engine), played low, high and as chords",
                (r, s) =>
                {
                    int slot = r.To(16);
                    byte[] patch = Wire.Patch(r, r.In(1, 2));
                    patch[1] = (byte)r.To(5);
                    // some values back to anything, so that the ends meet everything else
                    for (int i = r.To(4); i > 0; i--)
                    {
                        patch[r.In(2, 26)] = (byte)r.To(128);
                    }
                    Wire.Checksum(patch);
                    s.Raw(Wire.PatchMsg(Wire.Load, slot, patch));
                    s.Note("valid-patch " + slot + " " + Convert.ToHexString(patch));
                    s.On(0, Pad0 + slot, 127);
                    foreach (int key in new[] { 0, 12, 127, 60, 64, 67, 108, 24 })
                    {
                        s.On(0, key, r.Pick(1, 127));
                    }
                    for (int k = 0; k < 8; k++)
                    {
                        s.Cc(1, r.In(10, 13), r.Pick(0, 127, r.To(128)));
                    }
                    s.Off(0, Pad0 + slot);
                    foreach (int key in new[] { 0, 12, 127, 60, 64, 67, 108, 24 })
                    {
                        s.Off(0, key);
                    }
                }
            ),
            new Step("a valid song (format 6), its pieces in order", (r, s) =>
            {
                byte[] song = Wire.Song6(r, 1);
                ValidSong(s, song, Wire.Pieces(song));
            }),
            new Step("a valid song, its pieces in any order", (r, s) =>
            {
                byte[] song = Wire.Song6(r, 1);
                var pieces = Wire.Pieces(song);
                Wire.Shuffle(r, pieces);
                ValidSong(s, song, pieces);
            }),
            new Step("a valid song, every piece twice", (r, s) =>
            {
                byte[] song = Wire.Song6(r, 1);
                var pieces = new List<byte[]>();
                foreach (var p in Wire.Pieces(song))
                {
                    pieces.Add(p);
                    pieces.Add(p);
                }
                ValidSong(s, song, pieces);
            }),
            new Step("the smallest valid song (211 bytes, one piece)", (r, s) =>
            {
                byte[] song = Wire.Song6(r, 0);
                ValidSong(s, song, Wire.Pieces(song));
            }),
            new Step("a valid format-5 song in the shape of before 1.6.0 (39 pieces of 200)", (r, s) =>
            {
                byte[] song = Wire.Song5(r);
                foreach (var p in Wire.OldPieces(song))
                {
                    s.Raw(p);
                }
            }),
            new Step("the largest valid song (38355 bytes, 160 pieces) - in a short phase a middle one", (r, s) =>
            {
                byte[] song = Wire.Song6(r, s.Roomy ? 2 : 1);
                ValidSong(s, song, Wire.Pieces(song));
            }),
            new Step("a valid song, then CC 15 through its patterns and CC 14 through its layers", (r, s) =>
            {
                byte[] song = Wire.Song6(r, 1);
                ValidSong(s, song, Wire.Pieces(song));
                for (int i = 0; i < 32; i++)
                {
                    s.Cc(1, 15, r.To(128));
                    s.Cc(1, 14, r.To(128));
                }
            }),
        };

        // a valid message of some kind, to be broken by the caller
        static byte[] AnyValid(Rng r)
        {
            switch (r.To(5))
            {
                case 0:
                    return Wire.PatchMsg(Wire.Load, r.To(32), Wire.Patch(r, 0));
                case 1:
                    return Wire.PatchMsg(Wire.Dump, r.To(16), Wire.Patch(r, 0));
                case 2:
                    return Wire.Msg(Wire.Request, (byte)r.To(16));
                case 3:
                    return Wire.Msg(Wire.Text, r.Bytes(r.In(0, 24)));
                default:
                    var pieces = Wire.Pieces(Wire.Song6(r, 1));
                    return pieces[r.To(pieces.Count)];
            }
        }

        // a body of n bytes, each below 128
        static byte[] Sized(Rng r, int total, params int[] start)
        {
            var b = new byte[total];
            for (int i = 0; i < total; i++)
            {
                b[i] = i < start.Length ? (byte)start[i] : (byte)r.To(128);
            }
            b[0] = 0xF0;
            b[total - 1] = 0xF7;
            return b;
        }

        // ---- phase 4: SysEx as it should not be ----
        public static readonly Step[] Hostile =
        {
            new Step(
                "patches that are wrong: checksum, version, engine, a byte too few or too many, a slot there is not",
                (r, s) =>
                {
                    for (int i = 0; i < 8; i++)
                    {
                        byte[] patch = Wire.Patch(r, 0);
                        int slot = r.To(16);
                        byte[] body = patch;
                        switch (r.To(6))
                        {
                            case 0:
                                patch[27] ^= (byte)r.In(1, 127);
                                break;
                            case 1:
                                patch[0] = (byte)r.Pick(0, 1, 3, 127);
                                Wire.Checksum(patch);
                                break;
                            case 2:
                                patch[1] = (byte)r.In(5, 127);
                                Wire.Checksum(patch);
                                break;
                            case 3:
                                body = Wire.Part(patch, 0, r.In(0, 27));
                                break;
                            case 4:
                                body = Wire.Join(patch, r.Bytes(r.In(1, 40)));
                                break;
                            default:
                                slot = r.In(32, 127);
                                break;
                        }
                        s.Raw(Wire.PatchMsg(r.Pick(Wire.Load, Wire.Dump), slot, body));
                    }
                }
            ),
            new Step("valid messages cut short at any byte", (r, s) =>
            {
                for (int i = 0; i < 8; i++)
                {
                    byte[] m = AnyValid(r);
                    s.Raw(Wire.Join(Wire.Part(m, 0, r.In(1, m.Length - 2)), Wire.B(0xF7)));
                }
            }),
            new Step("nothing: F0 F7, the head alone, each message number with no body", (r, s) =>
            {
                s.Raw(Wire.B(0xF0, 0xF7));
                s.Raw(Wire.B(0xF0, 0x7D, 0xF7));
                s.Raw(Wire.B(0xF0, 0x7D, 0x56, 0xF7));
                s.Raw(Wire.B(0xF0, 0x7D, 0x56, 0x42, 0xF7));
                for (int message = 0; message < 128 && !s.Over; message++)
                {
                    // (06 and 07 with no body are the two requests: valid, and answered)
                    s.Raw(Wire.Msg(message));
                }
            }),
            new Step("message numbers there are not, and an identity sent TO the device", (r, s) =>
            {
                for (int i = 0; i < 8; i++)
                {
                    s.Raw(
                        Wire.Msg(
                            r.Pick(0, 8, 8, 9, 10, 64, 126, 127, r.In(9, 127)),
                            r.Bytes(r.Pick(0, 1, 18, 28, 29, r.To(200)))
                        )
                    );
                }
                s.Raw(Wire.Msg(Wire.RequestIdentity, r.Bytes(r.In(1, 20))));
            }),
            new Step("other makers' ids before our messages, and the universal identity request", (r, s) =>
            {
                s.Raw(Wire.B(0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7));
                for (int i = 0; i < 8; i++)
                {
                    byte[] m = AnyValid(r);
                    switch (r.To(5))
                    {
                        case 0:
                            m[1] = (byte)r.Pick(0x41, 0x43, 0x47, 0x7E, 0x7F, 0x00);
                            break;
                        case 1:
                            m[2] = (byte)r.To(128);
                            break;
                        case 2:
                            m[3] = (byte)r.Pick(0x43, 0x41, 0x00, 0x7F);
                            break;
                        case 3:
                            (m[2], m[3]) = (m[3], m[2]);
                            break;
                        default:
                            // a three-byte id (00 20 29) pushed in before the rest
                            m = Wire.Join(Wire.B(0xF0, 0x00, 0x20, 0x29), Wire.Part(m, 1, m.Length));
                            break;
                    }
                    s.Raw(m);
                }
            }),
            new Step("pieces of two songs mixed, and one piece again and again with other bytes", (r, s) =>
            {
                byte[] a = Wire.Song6(r, 1);
                byte[] b = Wire.Song6(r, 1);
                Array.Resize(ref b, a.Length);
                var pa = Wire.Pieces(a);
                var pb = Wire.Pieces(b);
                for (int k = 0; k < pa.Count; k++)
                {
                    s.Raw(r.Chance(50) ? pa[k] : pb[k]);
                    if (r.Chance(30))
                    {
                        int again = r.To(pa.Count);
                        s.Raw(r.Chance(50) ? pa[again] : pb[again]);
                    }
                }
                int count = pa.Count;
                for (int i = 0; i < 8; i++)
                {
                    s.Raw(
                        Wire.PieceRaw(
                            0,
                            0,
                            6,
                            count & 127,
                            count >> 7,
                            Wire.Join(Wire.B(6), r.Bytes(count > 1 ? 239 : a.Length - 1))
                        )
                    );
                }
            }),
            new Step(
                "a count of pieces that lies: 0, 1, one off, 161, 16383, another with every piece, a piece past the count",
                (r, s) =>
                {
                    byte[] song = Wire.Song6(r, 1);
                    int K = (song.Length + 239) / 240;
                    int says = r.Pick(0, 1, K - 1, K + 1, 160, 161, 16383, 128, 127, -1);
                    foreach (var p in Wire.Pieces(song))
                    {
                        int count = says < 0 ? r.To(200) : says;
                        p[8] = (byte)(count & 127);
                        p[9] = (byte)(count >> 7);
                        s.Raw(p);
                    }
                    for (int i = 0; i < 8; i++)
                    {
                        // the piece's number past the count, or with high bits no song has
                        int k = r.Pick(K, K + 1, 159, 160, 161, 16383, 255, 256);
                        s.Raw(
                            Wire.PieceRaw(
                                k & 127,
                                r.Chance(30) ? r.In(2, 127) : k >> 7,
                                6,
                                K & 127,
                                r.Chance(30) ? r.In(2, 127) : K >> 7,
                                r.Bytes(240)
                            )
                        );
                    }
                }
            ),
            new Step(
                "pieces of the wrong length: a short one in the middle, a last one of 0 or 241 bytes, a message of 9 to 12 bytes",
                (r, s) =>
                {
                    byte[] song = Wire.Song6(r, 1);
                    var pieces = Wire.Pieces(song);
                    int K = pieces.Count;
                    int bad = r.To(K);
                    for (int k = 0; k < K; k++)
                    {
                        byte[] p = pieces[k];
                        if (k == bad)
                        {
                            int len = r.Pick(0, 1, 239, 241, 250, 400, r.To(240));
                            p = Wire.PieceRaw(k & 127, k >> 7, 6, K & 127, K >> 7, r.Bytes(len));
                        }
                        s.Raw(p);
                    }
                    for (int n = 6; n <= 12; n++)
                    {
                        s.Raw(
                            Sized(
                                r,
                                n,
                                0xF0,
                                0x7D,
                                0x56,
                                0x42,
                                Wire.Song,
                                r.To(4),
                                r.Pick(0, 1, 39),
                                r.Pick(5, 6),
                                r.To(4),
                                r.To(2)
                            )
                        );
                    }
                }
            ),
            new Step("a format that lies: 0, 7, 127, and a first piece that begins with another byte", (r, s) =>
            {
                byte[] song = Wire.Song6(r, 1);
                int format = r.Pick(0, 1, 5, 7, 127, 6);
                if (format == 6)
                {
                    song[0] = (byte)r.Pick(0, 5, 7, 127);
                }
                foreach (var p in Wire.Pieces(song, 0, format))
                {
                    s.Raw(p);
                }
            }),
            new Step(
                "a whole transfer of a song that is broken: one value out of range, a byte too few or too many, noise",
                (r, s) =>
                {
                    byte[] song = Wire.Song6(r, r.Chance(20) ? 0 : 1);
                    switch (r.To(6))
                    {
                        case 0:
                            // a head value out of its range: the bar, the tempo, swing, scale, chain, the delay's time
                            int at = r.Pick(1, 2, 3, 4, 5, 6, 58, 66, 67, 98, 99, 100, 146);
                            song[at] = (byte)r.Pick(0, 17, 101, 127, 6, 33, 8, 9);
                            break;
                        case 1:
                            // the first pattern's block: a grid there is not, bits past the masks
                            song[147 + r.Pick(0, 3, 7)] = (byte)r.Pick(3, 4, 8, 127);
                            break;
                        case 2:
                            Array.Resize(ref song, song.Length - r.In(1, 300 < song.Length - 7 ? 300 : 1));
                            break;
                        case 3:
                            song = Wire.Join(song, r.Bytes(r.In(1, 300)));
                            break;
                        case 4:
                            // any byte past the head set to its most: a step bit, a velocity, a length out of range
                            for (int i = r.In(1, 6); i > 0; i--)
                            {
                                song[r.In(147, song.Length - 1)] = 127;
                            }
                            break;
                        default:
                            song = Wire.Join(Wire.B(6), r.Bytes(r.In(6, 3000)));
                            break;
                    }
                    if (song.Length > Wire.Piece * Wire.PiecesMost)
                    {
                        Array.Resize(ref song, Wire.Piece * Wire.PiecesMost);
                    }
                    var pieces = Wire.Pieces(song);
                    if (r.Chance(50))
                    {
                        Wire.Shuffle(r, pieces);
                    }
                    foreach (var p in pieces)
                    {
                        s.Raw(p);
                    }
                }
            ),
            new Step(
                "the old shape (piece, 39): noise for a song, a piece past the last, pieces of the wrong length",
                (r, s) =>
                {
                    byte[] song = Wire.Song5(r);
                    int how = r.To(4);
                    if (how == 0)
                    {
                        song = Wire.Join(Wire.B(r.Pick(5, 5, 4, 6, 0)), r.Bytes(7698));
                    }
                    else if (how == 1)
                    {
                        song[r.Pick(1, 2, 4, 5, 6, 58, 66, 67)] = 127;
                    }
                    var pieces = Wire.OldPieces(song);
                    if (how == 2)
                    {
                        Wire.Shuffle(r, pieces);
                        pieces.RemoveAt(r.To(pieces.Count));
                    }
                    foreach (var p in pieces)
                    {
                        s.Raw(p);
                    }
                    for (int i = 0; i < 6; i++)
                    {
                        s.Raw(
                            Wire.Msg(
                                Wire.Song,
                                Wire.Join(
                                    Wire.B(r.Pick(38, 39, 40, 127, r.To(39)), 39),
                                    r.Bytes(r.Pick(0, 1, 98, 99, 100, 199, 200, 201, 240))
                                )
                            )
                        );
                    }
                }
            ),
            new Step("transfers that break off, and the next one begun at once", (r, s) =>
            {
                for (int i = 0; i < 3; i++)
                {
                    var pieces = Wire.Pieces(Wire.Song6(r, 1));
                    int stop = r.To(pieces.Count);
                    for (int k = 0; k < stop; k++)
                    {
                        s.Raw(pieces[k]);
                    }
                    s.Raw(Wire.Msg(r.Pick(Wire.RequestSong, Wire.RequestIdentity)));
                }
            }),
            new Step(
                "the longest: messages of 252, 4095, 4096 (the most the host takes), 4097 bytes and more",
                (r, s) =>
                {
                    int total =
                        !s.Roomy ? r.Pick(252, 1024, 4095, 4096, 4097)
                        : s.Level == 1 ? r.Pick(252, 1024, 4095, 4096, 4097, 8192)
                        : s.Level == 2 ? r.Pick(252, 4095, 4096, 4097, 8192, 16384)
                        : r.Pick(252, 4095, 4096, 4097, 16384, 65535);
                    switch (r.To(5))
                    {
                        case 0:
                            s.Raw(Sized(r, total, 0xF0, 0x7D, 0x56, 0x42, Wire.Text));
                            break;
                        case 1:
                            // a song's piece that says the right things and then does not end
                            s.Raw(
                                Sized(
                                    r,
                                    total,
                                    0xF0,
                                    0x7D,
                                    0x56,
                                    0x42,
                                    Wire.Song,
                                    r.To(128),
                                    r.To(2),
                                    6,
                                    160 & 127,
                                    160 >> 7,
                                    6
                                )
                            );
                            break;
                        case 2:
                            s.Raw(Sized(r, total, 0xF0, 0x7D, 0x56, 0x42, Wire.Load, r.To(32), 2, r.To(5)));
                            break;
                        case 3:
                            s.Raw(Sized(r, total, 0xF0, 0x7D, 0x56, 0x42, r.To(128)));
                            break;
                        default:
                            s.Raw(Sized(r, total, 0xF0, r.To(128)));
                            break;
                    }
                    s.Raw(Wire.Msg(Wire.RequestIdentity));
                }
            ),
            new Step("text: none, far too much, what cannot be printed", (r, s) =>
            {
                s.Raw(Wire.Msg(Wire.Text));
                s.Raw(Wire.Msg(Wire.Text, r.Bytes(r.Pick(24, 25, 26, 100, 245), 32)));
                s.Raw(Wire.Msg(Wire.Text, r.Bytes(r.In(1, 60))));
                s.Raw(Wire.Msg(Wire.Text, Wire.B(0, 0, 0, 127, 127, 37, 115, 37, 110, 37, 100, 92, 0)));
            }),
            new Step("requests that lie (a pad 16-127, bytes after it) and floods of both requests", (r, s) =>
            {
                for (int i = 0; i < 8; i++)
                {
                    s.Raw(Wire.Msg(Wire.Request, (byte)r.In(16, 127)));
                    s.Raw(Wire.Msg(Wire.Request, Wire.Join(Wire.B(r.To(16)), r.Bytes(r.In(1, 30)))));
                    s.Raw(Wire.Msg(Wire.Request));
                }
                for (int i = 0; i < 24; i++)
                {
                    s.Raw(Wire.Msg(r.Pick(Wire.RequestSong, Wire.RequestIdentity, Wire.RequestIdentity)));
                    s.Raw(Wire.Msg(Wire.Request, (byte)r.To(16)));
                }
                s.Raw(Wire.Msg(Wire.RequestSong, r.Bytes(r.In(1, 30))));
            }),
            new Step("a SysEx that never ends, then notes and controllers", (r, s) =>
            {
                byte[] m = Wire.PatchMsg(Wire.Load, r.To(16), Wire.Patch(r, 0));
                int cut = r.In(1, m.Length - 2);
                s.Raw(Wire.Part(m, 0, cut));
                for (int i = 0; i < 16; i++)
                {
                    int n = Pad0 + r.To(16);
                    s.On(0, n, 100);
                    s.Cc(1, r.In(10, 14), r.To(128));
                    s.Off(0, n);
                }
                // then: the rest of it after all, an end alone, or nothing (the next message's F0 must do)
                int then = r.To(3);
                if (then == 0)
                {
                    s.Raw(Wire.Part(m, cut, m.Length));
                }
                else if (then == 1)
                {
                    s.Raw(Wire.B(0xF7));
                }
                s.Raw(Wire.Msg(Wire.RequestIdentity));
            }),
            new Step("a SysEx in several parts, with clock bytes and active sensing between them", (r, s) =>
            {
                byte[] m = AnyValid(r);
                int at = 0;
                while (at < m.Length)
                {
                    int n = r.In(1, Math.Max(1, m.Length / 3));
                    s.Raw(Wire.Part(m, at, n));
                    at += n;
                    if (at < m.Length)
                    {
                        s.Short(r.Pick(0xF8, 0xFE));
                    }
                }
            }),
            new Step("bytes above 127 inside a message, an F0 inside a message, an F7 alone", (r, s) =>
            {
                for (int i = 0; i < 4; i++)
                {
                    byte[] m = AnyValid(r);
                    m[r.In(1, m.Length - 2)] = (byte)r.Pick(0x80, 0x90, 0xB1, 0xF0, 0xF7, 0xFF, 0xF8, r.In(128, 255));
                    s.Raw(m);
                }
                s.Raw(Wire.B(0xF7));
                s.Raw(Wire.B(0xF0));
                s.Raw(Wire.Msg(Wire.RequestIdentity));
            }),
        };
    }

    static class Program
    {
        static Sender sender;
        static Listener listener;
        static readonly Stopwatch run = Stopwatch.StartNew();
        static int failed;
        static long seed = 1;
        static int level = 1;
        static int onlyPhase;
        static int onlyStep;

        static string Stamp()
        {
            var t = run.Elapsed;
            return "["
                + DateTime.Now.ToString("HH:mm:ss", CultureInfo.InvariantCulture)
                + " +"
                + ((int)t.TotalMinutes).ToString("00")
                + ":"
                + t.Seconds.ToString("00")
                + "]";
        }

        static void Say(string text)
        {
            Console.WriteLine(Stamp() + " " + text);
            sender?.Note(text);
        }

        static void Ok(bool good, string what)
        {
            Console.WriteLine((good ? "  ok    " : "  FAIL  ") + what);
            if (!good)
            {
                failed++;
            }
        }

        static int Usage(string wrong)
        {
            if (wrong != null)
            {
                Console.WriteLine(wrong);
            }
            Console.WriteLine("  dotnet run -c Release --project tests/midi-torture -- --list");
            Console.WriteLine(
                "  dotnet run -c Release --project tests/midi-torture -- --port \"name\" [--in \"name\"] [--minutes 5] [--seed 1]"
            );
            Console.WriteLine("        [--level 1..3] [--phase 1..5] [--step n] [--seconds n] [--dry] [--dump file]");
            return 2;
        }

        static void List()
        {
            var outs = Mm.Outputs();
            var ins = Mm.Inputs();
            Console.WriteLine("MIDI outputs (--port): " + (outs.Count == 0 ? "none" : ""));
            for (int i = 0; i < outs.Count; i++)
            {
                Console.WriteLine("  " + i + "  " + outs[i]);
            }
            Console.WriteLine("MIDI inputs (--in): " + (ins.Count == 0 ? "none" : ""));
            for (int i = 0; i < ins.Count; i++)
            {
                Console.WriteLine("  " + i + "  " + ins[i]);
            }
        }

        // the port whose name is `name`, or the only one that has `name` in it; -1 and a line that says why not
        static int Find(List<string> ports, string name, string kind)
        {
            int found = -1;
            int count = 0;
            for (int i = 0; i < ports.Count; i++)
            {
                if (string.Equals(ports[i], name, StringComparison.OrdinalIgnoreCase))
                {
                    return i;
                }
                if (ports[i].IndexOf(name, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    found = i;
                    count++;
                }
            }
            if (count == 1)
            {
                return found;
            }
            Console.WriteLine(
                count == 0
                    ? "No MIDI " + kind + " has \"" + name + "\" in its name."
                    : count + " MIDI " + kind + "s have \"" + name + "\" in their names: say more of it."
            );
            return -1;
        }

        static void Instructions(string port, string input, double seconds)
        {
            Console.WriteLine("midi-torture: heavy and hostile MIDI for SFX FORGE in the VIBE FM plugin.");
            Console.WriteLine();
            Console.WriteLine("BEFORE IT STARTS");
            Console.WriteLine(
                "  1. The plugin (standalone or in a DAW) runs SFX FORGE and its MIDI INPUT is \"" + port + "\"."
            );
            Console.WriteLine(
                "     Plugin and this tool on the same computer: the port must be a virtual cable (loopMIDI or the like)."
            );
            Console.WriteLine(
                "     One cable for this direction; a second one, from the plugin's MIDI OUTPUT, if you use --in."
            );
            Console.WriteLine("     Never the same cable for both: what the plugin answers would come back to it.");
            Console.WriteLine(
                "  2. It OVERWRITES the sounds on the pads (both kits) and the song. Use an instance you do not need,"
            );
            Console.WriteLine(
                "     or save first. Turn the volume down: some of the sounds it loads are loud and harsh."
            );
            Console.WriteLine("  3. Other programs listening on this port get all of it too. Close what should not.");
            Console.WriteLine();
            Console.WriteLine("WHAT TO WATCH");
            Console.WriteLine(
                "  - the plugin is still there: no crash, no frozen window, the DAW's audio engine has not stopped"
            );
            Console.WriteLine("  - no silence that stays: at the end pad 1 is played five times, and you hear it");
            Console.WriteLine(
                "  - the display still draws, the keys and pads still work, the LINK page still shows what comes in"
            );
            Console.WriteLine(
                "  - if it dies: the last line here says which phase and step was going out. Keep these lines."
            );
            Console.WriteLine(
                "  - loopMIDI shows a port in red when it has shut it for a feedback loop: then the plugin got nothing."
            );
            Console.WriteLine();
            Console.WriteLine("THIS RUN");
            Console.WriteLine("  to      " + port + (sender.Dry ? "  (dry: no port is opened)" : ""));
            Console.WriteLine(
                "  from    " + (input ?? "nothing (no --in): the tool cannot see the plugin, only you can")
            );
            Console.WriteLine(
                "  seed "
                    + seed
                    + ", level "
                    + level
                    + ", "
                    + seconds.ToString("0.#", CultureInfo.InvariantCulture)
                    + " seconds"
                    + (onlyPhase > 0 ? ", phase " + onlyPhase + " only" : " in five phases")
                    + (onlyStep > 0 ? ", step " + onlyStep + " only" : "")
            );
            Console.WriteLine("  Ctrl+C stops it and still sends the end (notes off, the plain sounds).");
            Console.WriteLine();
        }

        // Asks for the identity and waits for it (with --in only). Three tries of a second each.
        static void Probe(string when)
        {
            if (listener == null || sender.Dry)
            {
                return;
            }
            sender.Rest(300);
            long before = Interlocked.Read(ref listener.Identities);
            bool got = false;
            for (int attempt = 0; attempt < 3 && !got; attempt++)
            {
                sender.Raw(Wire.Msg(Wire.RequestIdentity));
                for (int i = 0; i < 100 && !got; i++)
                {
                    Thread.Sleep(10);
                    got = Interlocked.Read(ref listener.Identities) > before;
                }
            }
            sender.Sync();
            Ok(
                got,
                got
                    ? "the device answers the identity request " + when + " (firmware " + listener.Version + ")"
                    : "NO identity within three seconds " + when
            );
        }

        static void Phase(int phase, string title, Step[] steps, double seconds, double rate)
        {
            if (onlyPhase > 0 && onlyPhase != phase)
            {
                return;
            }
            sender.Rate = rate;
            sender.Roomy = seconds * rate >= 150000;
            sender.Sync();
            Say(
                "PHASE "
                    + phase
                    + " of 5: "
                    + title
                    + "  ("
                    + seconds.ToString("0.#", CultureInfo.InvariantCulture)
                    + " s, "
                    + rate.ToString("0", CultureInfo.InvariantCulture)
                    + " bytes a second)"
            );
            // Every step has its slice of the phase, one after the other, and the rounds go on until the phase is over.
            // A step whose slice is over before it began (the ones before it sent something long) still sends one
            // burst in the first round: so every step is reached, and a short phase runs a little over for it.
            double start = sender.V;
            double end = start + seconds;
            double slice = Math.Max(0.25, Math.Min(10, seconds / steps.Length));
            int slices = 0;
            for (int round = 0; (round == 0 || sender.V < end) && !sender.Cancel; round++)
            {
                for (int i = 0; i < steps.Length && (round == 0 || sender.V < end) && !sender.Cancel; i++)
                {
                    if (onlyStep > 0 && onlyStep != i + 1)
                    {
                        continue;
                    }
                    slices++;
                    sender.SliceEnd = onlyStep > 0 ? end : Math.Min(end, Math.Max(sender.V, start + slices * slice));
                    var r = new Rng(seed, phase, i, round);
                    Say("  " + phase + "." + (i + 1) + "  " + steps[i].Name);
                    do
                    {
                        steps[i].Run(r, sender);
                    } while (!sender.Over);
                }
            }
            sender.SliceEnd = double.MaxValue;
            Probe("after phase " + phase);
        }

        // phase 5: every step of the four phases, picked by chance, with no waiting
        static void AllAtOnce(double seconds)
        {
            if (onlyPhase > 0 && onlyPhase != 5)
            {
                return;
            }
            var all = new List<Step>();
            all.AddRange(Steps.Notes);
            all.AddRange(Steps.Controllers);
            all.AddRange(Steps.Valid);
            all.AddRange(Steps.Hostile);
            sender.Rate = 0;
            sender.Roomy = seconds >= 20;
            sender.Sync();
            Say(
                "PHASE 5 of 5: all at once, as fast as the port takes it  ("
                    + seconds.ToString("0.#", CultureInfo.InvariantCulture)
                    + " s)"
            );
            double end = sender.V + seconds;
            double next = sender.V + 5;
            sender.SliceEnd = end;
            var r = new Rng(seed, 5);
            long bytes = sender.Bytes;
            while (!sender.Over)
            {
                // notes and controllers as often as both kinds of SysEx
                int kind = r.To(4);
                Step[] from =
                    kind == 0 ? Steps.Notes
                    : kind == 1 ? Steps.Controllers
                    : kind == 2 ? Steps.Valid
                    : Steps.Hostile;
                from[r.To(from.Length)].Run(r, sender);
                if (sender.V >= next)
                {
                    Say(
                        "  5  "
                            + (sender.Shorts + sender.Longs)
                            + " messages so far, "
                            + ((sender.Bytes - bytes) / 5 / 1000)
                            + " kB a second now"
                    );
                    bytes = sender.Bytes;
                    next = sender.V + 5;
                }
            }
            sender.SliceEnd = double.MaxValue;
            Probe("after phase 5");
        }

        // the end: everything let go, the telemetry where it rests, an empty song, a plain sound on every pad, pad 1
        static void End()
        {
            sender.Cancel = false;
            sender.Rate = 6000;
            sender.Sync();
            Say(
                "THE END: notes off on every channel, the telemetry at rest, an empty song, a plain sound on every pad"
            );
            // (an end for a SysEx that may still be open)
            sender.Raw(Wire.B(0xF7));
            for (int c = 0; c < 16; c++)
            {
                sender.Cc(c, 64, 0);
                sender.Cc(c, 123, 0);
                sender.Cc(c, 120, 0);
                sender.Short(0xE0 | c, 0, 64);
            }
            // a host may not know "all notes off": every note of every channel by itself as well
            for (int c = 0; c < 16; c++)
            {
                for (int n = 0; n < 128; n++)
                {
                    sender.Off(c, n);
                }
            }
            // the telemetry: no pitch, the cutoff in the middle, no timbre, full level, full intensity, pattern A
            sender.Cc(1, 10, 0);
            sender.Cc(1, 11, 64);
            sender.Cc(1, 12, 0);
            sender.Cc(1, 13, 127);
            sender.Cc(1, 14, 127);
            sender.Cc(1, 15, 0);
            // (a transfer that broke off is forgotten after two seconds without a piece)
            sender.Rest(2200);
            foreach (var p in Wire.Pieces(Wire.SongAtRest()))
            {
                sender.Raw(p);
            }
            byte[] plain = Wire.Patch(null, 3);
            for (int slot = 31; slot >= 0; slot--)
            {
                sender.Raw(Wire.PatchMsg(Wire.Load, slot, plain));
            }
            sender.Raw(Wire.Msg(Wire.Text, Encoding.ASCII.GetBytes("TORTURE DONE")));
            sender.Rest(500);
            Say("  pad 1 (note 36), five times, a second apart: YOU SHOULD HEAR IT");
            for (int i = 0; i < 5; i++)
            {
                sender.On(0, 36, 110);
                sender.Rest(150);
                sender.Off(0, 36);
                sender.Rest(850);
            }
            Probe("at the end");
        }

        static int Main(string[] args)
        {
            string port = null;
            string input = null;
            string dumpFile = null;
            double seconds = 300;
            bool list = false;
            bool dry = false;
            try
            {
                for (int i = 0; i < args.Length; i++)
                {
                    string a = args[i];
                    string Value() => i + 1 < args.Length ? args[++i] : throw new FormatException(a + " needs a value");
                    if (a == "--list")
                    {
                        list = true;
                    }
                    else if (a == "--dry")
                    {
                        dry = true;
                    }
                    else if (a == "--port")
                    {
                        port = Value();
                    }
                    else if (a == "--in")
                    {
                        input = Value();
                    }
                    else if (a == "--dump")
                    {
                        dumpFile = Value();
                    }
                    else if (a == "--minutes")
                    {
                        seconds = 60 * double.Parse(Value(), CultureInfo.InvariantCulture);
                    }
                    else if (a == "--seconds")
                    {
                        seconds = double.Parse(Value(), CultureInfo.InvariantCulture);
                    }
                    else if (a == "--seed")
                    {
                        seed = long.Parse(Value(), CultureInfo.InvariantCulture);
                    }
                    else if (a == "--level")
                    {
                        level = int.Parse(Value(), CultureInfo.InvariantCulture);
                    }
                    else if (a == "--phase")
                    {
                        onlyPhase = int.Parse(Value(), CultureInfo.InvariantCulture);
                    }
                    else if (a == "--step")
                    {
                        onlyStep = int.Parse(Value(), CultureInfo.InvariantCulture);
                    }
                    else
                    {
                        return Usage("Unknown argument: " + a);
                    }
                }
            }
            catch (FormatException e)
            {
                return Usage(e.Message);
            }
            if (list)
            {
                List();
                return 0;
            }
            Step[][] phases = { Steps.Notes, Steps.Controllers, Steps.Valid, Steps.Hostile };
            if (
                level < 1
                || level > 3
                || onlyPhase < 0
                || onlyPhase > 5
                || !(seconds > 0)
                || seconds > 24 * 3600
                || (
                    onlyStep != 0
                    && (onlyPhase < 1 || onlyPhase > 4 || onlyStep < 1 || onlyStep > phases[onlyPhase - 1].Length)
                )
            )
            {
                return Usage(
                    "--level is 1 to 3, --phase 1 to 5, --step a step of the phase given (1 to 4), the length above 0."
                );
            }
            if (port == null && !dry)
            {
                return Usage("Which port? --list shows them.");
            }

            // ---- the ports
            int outId = -1;
            int inId = -1;
            if (!dry)
            {
                var outs = Mm.Outputs();
                outId = Find(outs, port, "output");
                if (outId < 0)
                {
                    List();
                    return 2;
                }
                port = outs[outId];
                if (input != null)
                {
                    var ins = Mm.Inputs();
                    inId = Find(ins, input, "input");
                    if (inId < 0)
                    {
                        List();
                        return 2;
                    }
                    input = ins[inId];
                }
            }
            sender = new Sender(dry, dumpFile) { Level = level };
            Instructions(port ?? "(none)", dry ? null : input, seconds);
            if (!dry)
            {
                string why = sender.Open((uint)outId);
                if (why != null)
                {
                    Console.WriteLine("The output \"" + port + "\" does not open: " + why);
                    Console.WriteLine("midi-torture FAILED");
                    return 1;
                }
                if (inId >= 0)
                {
                    listener = new Listener();
                    why = listener.Open((uint)inId);
                    if (why != null)
                    {
                        Console.WriteLine("The input \"" + input + "\" does not open: " + why);
                        Console.WriteLine("midi-torture FAILED");
                        return 1;
                    }
                }
            }
            Console.CancelKeyPress += (o, e) =>
            {
                e.Cancel = true;
                sender.Cancel = true;
            };

            // ---- the run
            run.Restart();
            Probe("before the run (if not: is the plugin's MIDI output the port of --in?)");
            double each = onlyPhase > 0 ? seconds : seconds / 5;
            double rate =
                level == 1 ? 3000
                : level == 2 ? 30000
                : 300000;
            Phase(1, "notes", Steps.Notes, each, rate);
            Phase(2, "controllers", Steps.Controllers, each, rate);
            Phase(3, "SysEx, valid", Steps.Valid, each, rate);
            Phase(4, "SysEx, hostile", Steps.Hostile, each, rate);
            AllAtOnce(each);
            bool stopped = sender.Cancel;
            End();

            // ---- the totals
            Console.WriteLine();
            Console.WriteLine(
                "SENT      "
                    + sender.Shorts
                    + " short messages, "
                    + sender.Longs
                    + " long ones, "
                    + sender.Bytes
                    + " bytes"
                    + (stopped ? "  (stopped with Ctrl+C)" : "")
            );
            if (listener != null)
            {
                Console.WriteLine(
                    "RECEIVED  "
                        + Interlocked.Read(ref listener.Shorts)
                        + " short messages, "
                        + Interlocked.Read(ref listener.SysEx)
                        + " SysEx: "
                        + Interlocked.Read(ref listener.Identities)
                        + " identities, "
                        + Interlocked.Read(ref listener.Dumps)
                        + " sounds, "
                        + Interlocked.Read(ref listener.Pieces)
                        + " pieces of a song, "
                        + Interlocked.Read(ref listener.Others)
                        + " others"
                );
            }
            if (sender.Refused > 0)
            {
                Console.WriteLine(
                    "REFUSED   "
                        + sender.Refused
                        + " messages the port did not take (it said \"not ready\" for a tenth of a second)"
                );
            }
            Ok(
                sender.Errors == 0,
                sender.Errors == 0
                    ? (
                        dry
                            ? "every message was made (dry: none was sent)"
                            : "the port took every message it was handed"
                    )
                    : sender.Errors + " calls failed; the first: " + sender.FirstError
            );
            if (listener == null && !dry)
            {
                Console.WriteLine(
                    "  (no --in: whether the plugin is still well, only you can say. PASSED below means: all was sent.)"
                );
            }
            listener?.Dispose();
            sender.Dispose();
            Console.WriteLine(failed == 0 ? "midi-torture PASSED" : "midi-torture FAILED");
            return failed == 0 ? 0 : 1;
        }
    }
}
