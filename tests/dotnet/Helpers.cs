// What the checks measure and build with: the pitch and level of a stretch of sound, the files of the test
// performance, a hash of a rendered song, packed songs written by hand.
// Part of the test program: Program.cs has Main.
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
    // Sizes the checks count in. (The head of a format-5 or format-6 song is SfxProtocol.SongHead: 147 bytes.)
    // The 16 sounds a song file begins with: 448 bytes.
    const int SoundsBytes = 16 * SfxPatch.Bytes;

    // a format-6 pattern block with every row and every lane at 64 steps, the largest there is
    const int Block64 = 4776;

    // the pitch of a stretch of sound, by its autocorrelation (FM and wavetables cross zero more than once a cycle)
    static double Pitch(float[] a, double t0, double t1, int sr = 48000)
    {
        int i0 = (int)(t0 * sr);
        int n = Math.Min(a.Length, (int)(t1 * sr)) - i0;
        int best = 0;
        double top = 0;
        double r0 = 0;
        bool dipped = false;
        for (int i = 0; i < n; i++)
        {
            r0 += a[i0 + i] * a[i0 + i];
        }

        r0 /= Math.Max(1, n);

        // the first peak after the first dip
        for (int lag = sr / 2000; lag < sr / 30 && lag < n / 2; lag++)
        {
            double c = 0;
            for (int i = 0; i < n - lag; i++)
            {
                c += a[i0 + i] * a[i0 + i + lag];
            }

            c /= n - lag;
            if (c < r0 * 0.3)
            {
                dipped = true;
            }

            if (dipped && c > top * 1.02)
            {
                top = c;
                best = lag;
            }
        }

        return best > 0 ? (double)sr / best : 0;
    }

    static double Rms(float[] a, double t0, double t1, int sr = 48000)
    {
        double s = 0;
        int i0 = (int)(t0 * sr);
        int i1 = Math.Min(a.Length, (int)(t1 * sr));
        for (int i = i0; i < i1; i++)
        {
            s += a[i] * a[i];
        }

        return i1 > i0 ? Math.Sqrt(s / (i1 - i0)) : 0;
    }

    static int Cycles(float[] a, double t0, double t1, int sr = 48000)
    {
        int c = 0;
        for (int i = (int)(t0 * sr) + 1; i < (int)(t1 * sr); i++)
        {
            if (a[i - 1] < 0 && a[i] >= 0)
            {
                c++;
            }
        }

        return c;
    }

    static List<byte[]> ReadMidi(string file) =>
        JsonSerializer
            .Deserialize<JsonElement>(File.ReadAllText(file))
            .EnumerateArray()
            .Select(e => e.GetProperty("bytes").EnumerateArray().Select(b => (byte)b.GetInt32()).ToArray())
            .ToList();

    static float[] ReadWavLeft(string file)
    {
        byte[] w = File.ReadAllBytes(file);
        var x = new float[(w.Length - 44) / 4];
        for (int i = 0; i < x.Length; i++)
        {
            x[i] = BitConverter.ToInt16(w, 44 + i * 4) / 32768f;
        }

        return x;
    }

    static double Correlate(float[] mine, float[] device, int at, int n, out int bestOff)
    {
        double best = 0;
        bestOff = 0;
        for (int off = -64; off <= 512; off++)
        {
            double ab = 0;
            double aa = 0;
            double bb = 0;
            for (int i = 0; i < n; i += 2)
            {
                double x = Math.Tanh(mine[i]);
                double y = device[at + off + i];
                ab += x * y;
                aa += x * x;
                bb += y * y;
            }

            double c = ab / Math.Sqrt(aa * bb + 1e-12);
            if (c > best)
            {
                best = c;
                bestOff = off;
            }
        }

        return best;
    }

    // ---- a song rendered by the native player, as one number: a hash over the float bits of both channels
    static ulong Fnv(ulong h, float[] a, int n)
    {
        for (int i = 0; i < n; i++)
        {
            uint b = BitConverter.SingleToUInt32Bits(a[i]);
            for (int k = 0; k < 4; k++)
            {
                h ^= (b >> (k * 8)) & 0xFF;
                h *= 0x100000001B3UL;
            }
        }

        return h;
    }

    static ulong RenderHash(
        byte[] sounds,
        byte[] song,
        int pattern,
        bool loop,
        float intensity,
        int bars,
        Action<IntPtr> before = null
    )
    {
        IntPtr pl = Marshal.AllocHGlobal(SfxNative.PlayerSize);
        try
        {
            SfxNative.PlayerInit(pl, 48000f);
            if (!SfxNative.PlayerSetSong(pl, song))
            {
                return 0;
            }

            for (int i = 0; i < 16; i++)
            {
                if (!SfxNative.PlayerSetPatch(pl, i, sounds.Skip(i * SfxPatch.Bytes).Take(SfxPatch.Bytes).ToArray()))
                {
                    return 0;
                }
            }

            var none = SfxMods.None;
            SfxNative.PlayerSetLive(pl, ref none, intensity);
            if (loop)
            {
                SfxNative.PlayerSetLoop(pl, true);
            }

            before?.Invoke(pl);
            SfxNative.PlayerStart(pl, pattern);
            int bpm = song[2] << 7 | song[3];
            int total = (int)Math.Ceiling(bars * song[1] * 60.0 / bpm / 4.0 * 48000.0);
            var l = new float[256];
            var r = new float[256];
            ulong h = 0xCBF29CE484222325UL;
            for (int done = 0; done < total; done += 256)
            {
                Array.Clear(l, 0, 256);
                Array.Clear(r, 0, 256);
                SfxNative.PlayerRender(pl, l, r, 256);
                h = Fnv(Fnv(h, l, 256), r, 256);
            }

            return h;
        }
        finally
        {
            Marshal.FreeHGlobal(pl);
        }
    }

    // (a) 16 bars from the top at intensity 1, (b) pattern F looped 4 bars, (c) 8 bars at intensity 0.4
    static ulong[] ThreeHashes(byte[] sounds, byte[] song) =>
        new[]
        {
            RenderHash(sounds, song, 0, false, 1f, 16),
            RenderHash(sounds, song, 5, true, 1f, 4),
            RenderHash(sounds, song, 0, false, 0.4f, 8),
        };

    static string Hex(ulong[] h) => string.Join(" ", h.Select(x => x.ToString("X16")));

    // ---- a packed song of format 6 written here, by the layout in core/sfx_song.h (not by the C packer: a second pair
    // of eyes). head: 147 bytes of any format-5 or format-6 song. grids: 0, 1, 2 for each of the 8 patterns. A step:
    // pattern, sound, step, note, velocity, length. A point: pattern, lane, step, value.
    static byte[] Pack6(
        byte[] head,
        int[] grids,
        IEnumerable<(int p, int s, int i, int note, int vel, int len)> steps,
        IEnumerable<(int p, int l, int i, int v)> points = null
    )
    {
        var o = new List<byte>(head.Take(SfxProtocol.SongHead));
        o[0] = 6;
        for (int p = 0; p < 8; p++)
        {
            int g = 16 << grids[p];
            int nb = (g + 6) / 7;
            var mine = steps.Where(x => x.p == p).ToList();
            var pts = (points ?? new (int, int, int, int)[0]).Where(x => x.p == p).ToList();
            int rows = mine.Aggregate(0, (a, x) => a | 1 << x.s);
            int lanes = pts.Aggregate(0, (a, x) => a | 1 << x.l);

            // csharpier-ignore
            o.AddRange(
                new[]
                {
                    (byte)grids[p], (byte)(rows & 127), (byte)(rows >> 7 & 127), (byte)(rows >> 14),
                    (byte)(lanes & 127), (byte)(lanes >> 7 & 127), (byte)(lanes >> 14 & 127), (byte)(lanes >> 21),
                }
            );
            for (int s = 0; s < 16; s++)
            {
                if ((rows >> s & 1) == 0)
                {
                    continue;
                }

                var row = new byte[nb + 3 * g];
                for (int i = 0; i < g; i++)
                {
                    row[nb + g + i] = 5;
                }

                foreach (var x in mine.Where(x => x.s == s))
                {
                    row[x.i / 7] |= (byte)(1 << x.i % 7);
                    row[nb + x.i] = (byte)x.note;
                    row[nb + g + x.i] = (byte)x.vel;
                    row[nb + 2 * g + x.i] = (byte)x.len;
                }

                o.AddRange(row);
            }

            for (int l = 0; l < 24; l++)
            {
                if ((lanes >> l & 1) == 0)
                {
                    continue;
                }

                var row = new byte[g];
                foreach (var x in pts.Where(x => x.l == l))
                {
                    row[x.i] = (byte)(x.v + 1);
                }

                o.AddRange(row);
            }
        }

        return o.ToArray();
    }

    // a head to build test songs on: the given song's, with this tempo and
    // swing, no layers, no arrangement, a chain of `chain` patterns
    static byte[] PlainHead(byte[] song, int bpm, int swing, int chain)
    {
        var h = song.Take(SfxProtocol.SongHead).ToArray();
        h[2] = (byte)(bpm >> 7);
        h[3] = (byte)(bpm & 127);
        h[4] = (byte)swing;
        h[6] = (byte)chain;
        h[66] = 0;
        for (int t = 0; t < 16; t++)
        {
            h[7 + t] = 0;
        }

        return h;
    }

    // the left channel of a song played from its pattern A, and (out) for
    // every block of 256 samples the sounds that played in it
    static float[] RenderSong(
        byte[] sounds,
        byte[] song,
        double seconds,
        out uint[] playing,
        Action<IntPtr, int> eachBlock = null
    )
    {
        IntPtr pl = Marshal.AllocHGlobal(SfxNative.PlayerSize);
        try
        {
            SfxNative.PlayerInit(pl, 48000f);
            if (!SfxNative.PlayerSetSong(pl, song))
            {
                throw new Exception("the test's song was refused");
            }

            for (int i = 0; i < 16; i++)
            {
                SfxNative.PlayerSetPatch(pl, i, sounds.Skip(i * SfxPatch.Bytes).Take(SfxPatch.Bytes).ToArray());
            }

            var none = SfxMods.None;
            SfxNative.PlayerSetLive(pl, ref none, 1f);
            SfxNative.PlayerStart(pl, 0);
            int blocks = (int)Math.Ceiling(seconds * 48000 / 256);
            var all = new float[blocks * 256];
            playing = new uint[blocks];
            var l = new float[256];
            var r = new float[256];
            for (int b = 0; b < blocks; b++)
            {
                Array.Clear(l, 0, 256);
                Array.Clear(r, 0, 256);
                playing[b] = SfxNative.PlayerRender(pl, l, r, 256);
                Array.Copy(l, 0, all, b * 256, 256);
                eachBlock?.Invoke(pl, b);
            }

            return all;
        }
        finally
        {
            Marshal.FreeHGlobal(pl);
        }
    }

    // where sounds begin: much more in 2 ms than in the 9 ms before them (in seconds)
    static List<double> Onsets(float[] a, double from, double to)
    {
        var found = new List<double>();
        double E(int i0, int i1)
        {
            double s = 0;
            for (int i = Math.Max(0, i0); i < Math.Min(a.Length, i1); i++)
            {
                s += a[i] * a[i];
            }

            return Math.Sqrt(s / Math.Max(1, i1 - i0));
        }

        for (int i = (int)(from * 48000); i < (int)(to * 48000); i += 8)
        {
            double now = E(i, i + 96);
            double before = E(i - 480, i - 48);
            if (now > 0.01 && now > before * 4)
            {
                found.Add(i / 48000.0);
                i += 960;
            }
        }

        return found;
    }
}
