// SPDX-License-Identifier: MIT
using System;

namespace CatSplat.SfxForge
{
    public enum SfxEngine : byte { Impact, UiBlip, SciFiFm, Foley }

    /// <summary>The ten parameters of a patch, in wire order.</summary>
    public enum SfxParam { BaseFreq, PitchEnvAmount, PitchEnvDecay, FmRatio, FmDepth, NoiseMix, FilterCutoff, FilterRes, AmpDecay, Bitcrush }

    /// <summary>
    /// One SFX FORGE sound: the same 13 bytes the device keeps (core/sfx_core.h). Every value is 0..127.
    /// </summary>
    [Serializable]
    public struct SfxPatch : IEquatable<SfxPatch>
    {
        public const int Bytes = 13;
        public const int Version = 1;
        public const int ParamCount = 10;
        public const int EngineCount = 4;
        /// <summary>ampDecay at this value: the sound holds while its pad or note is held.</summary>
        public const byte Hold = 127;

        public SfxEngine engine;
        public byte baseFreq, pitchEnvAmount, pitchEnvDecay, fmRatio, fmDepth, noiseMix, filterCutoff, filterRes, ampDecay, bitcrush;

        public static SfxPatch Default => new SfxPatch
        {
            engine = SfxEngine.SciFiFm, baseFreq = 70, pitchEnvAmount = 110, pitchEnvDecay = 45, fmRatio = 50, fmDepth = 70,
            noiseMix = 0, filterCutoff = 120, filterRes = 20, ampDecay = 58, bitcrush = 0,
        };

        public bool IsHold => ampDecay == Hold;

        public byte this[SfxParam p]
        {
            get
            {
                switch (p)
                {
                    case SfxParam.BaseFreq: return baseFreq;
                    case SfxParam.PitchEnvAmount: return pitchEnvAmount;
                    case SfxParam.PitchEnvDecay: return pitchEnvDecay;
                    case SfxParam.FmRatio: return fmRatio;
                    case SfxParam.FmDepth: return fmDepth;
                    case SfxParam.NoiseMix: return noiseMix;
                    case SfxParam.FilterCutoff: return filterCutoff;
                    case SfxParam.FilterRes: return filterRes;
                    case SfxParam.AmpDecay: return ampDecay;
                    default: return bitcrush;
                }
            }
            set
            {
                byte v = value > 127 ? (byte)127 : value;
                switch (p)
                {
                    case SfxParam.BaseFreq: baseFreq = v; break;
                    case SfxParam.PitchEnvAmount: pitchEnvAmount = v; break;
                    case SfxParam.PitchEnvDecay: pitchEnvDecay = v; break;
                    case SfxParam.FmRatio: fmRatio = v; break;
                    case SfxParam.FmDepth: fmDepth = v; break;
                    case SfxParam.NoiseMix: noiseMix = v; break;
                    case SfxParam.FilterCutoff: filterCutoff = v; break;
                    case SfxParam.FilterRes: filterRes = v; break;
                    case SfxParam.AmpDecay: ampDecay = v; break;
                    default: bitcrush = v; break;
                }
            }
        }

        /// <summary>What a parameter is called for an engine (two of them change their job per engine).</summary>
        public static string Label(SfxParam p, SfxEngine engine)
        {
            if (engine == SfxEngine.UiBlip && p == SfxParam.FmRatio) return "Arp Step";
            if (engine == SfxEngine.UiBlip && p == SfxParam.FmDepth) return "Arp Speed";
            if (engine == SfxEngine.Foley && p == SfxParam.FmRatio) return "Grain Rate";
            if (engine == SfxEngine.Foley && p == SfxParam.FmDepth) return "Grain Amount";
            if (engine == SfxEngine.Foley && p == SfxParam.FilterCutoff) return "Band";
            switch (p)
            {
                case SfxParam.BaseFreq: return "Frequency";
                case SfxParam.PitchEnvAmount: return "Pitch Env (64 = none)";
                case SfxParam.PitchEnvDecay: return "Pitch Env Time";
                case SfxParam.FmRatio: return "FM Ratio";
                case SfxParam.FmDepth: return "FM Depth";
                case SfxParam.NoiseMix: return "Noise";
                case SfxParam.FilterCutoff: return "Cutoff";
                case SfxParam.FilterRes: return "Resonance";
                case SfxParam.AmpDecay: return "Decay (127 = hold)";
                default: return "Bitcrush";
            }
        }

        // ---- the 13 bytes ------------------------------------------------------------------------------------------
        public static byte Checksum(byte[] b, int offset)
        {
            int s = 0;
            for (int i = 0; i < Bytes - 1; i++) s += b[offset + i];
            return (byte)(s & 0x7F);
        }

        public void Pack(byte[] dst, int offset)
        {
            dst[offset] = Version;
            dst[offset + 1] = (byte)((int)engine % EngineCount);
            for (int i = 0; i < ParamCount; i++) dst[offset + 2 + i] = (byte)(this[(SfxParam)i] & 0x7F);
            dst[offset + Bytes - 1] = Checksum(dst, offset);
        }

        public byte[] Pack()
        {
            var b = new byte[Bytes];
            Pack(b, 0);
            return b;
        }

        /// <summary>False: not a valid patch (too short, wrong version, a byte over 127, checksum).</summary>
        public static bool TryUnpack(byte[] src, int offset, out SfxPatch patch)
        {
            patch = default;
            if (src == null || offset < 0 || src.Length < offset + Bytes) return false;
            if (src[offset] != Version || src[offset + 1] >= EngineCount) return false;
            for (int i = 0; i < Bytes; i++) if (src[offset + i] > 127) return false;
            if (src[offset + Bytes - 1] != Checksum(src, offset)) return false;
            patch.engine = (SfxEngine)src[offset + 1];
            for (int i = 0; i < ParamCount; i++) patch[(SfxParam)i] = src[offset + 2 + i];
            return true;
        }

        /// <summary>Every parameter moves by at most 15 % of its range; the engine and HOLD stay (as MUTATE on the device).</summary>
        public void Mutate(Random rng)
        {
            for (int i = 0; i < ParamCount; i++)
            {
                var p = (SfxParam)i;
                if (p == SfxParam.AmpDecay && ampDecay == Hold) continue;
                int v = this[p] + rng.Next(-19, 20);
                int top = p == SfxParam.AmpDecay ? Hold - 1 : 127;
                this[p] = (byte)(v < 0 ? 0 : v > top ? top : v);
            }
        }

        public bool Equals(SfxPatch o)
        {
            if (engine != o.engine) return false;
            for (int i = 0; i < ParamCount; i++) if (this[(SfxParam)i] != o[(SfxParam)i]) return false;
            return true;
        }

        public override bool Equals(object obj) => obj is SfxPatch o && Equals(o);

        public override int GetHashCode()
        {
            int h = (int)engine;
            for (int i = 0; i < ParamCount; i++) h = h * 31 + this[(SfxParam)i];
            return h;
        }
    }
}
