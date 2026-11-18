// SPDX-License-Identifier: MIT
using System;
using System.Runtime.InteropServices;

namespace CatSplat.SfxForge
{
    /// <summary>
    /// The device's own synth engine as a native library (native/sfx_native.c): a patch sounds here as it does there.
    /// Windows x64 for now.
    /// </summary>
    public static class SfxNative
    {
        const string Lib = "sfxforge";

        [DllImport(Lib)] static extern int sfxn_abi();
        [DllImport(Lib)] static extern int sfxn_voice_size();
        [DllImport(Lib)] static extern int sfxn_trigger(IntPtr voice, byte[] patch13, float vel, float noteSt, float sr, uint seed);
        [DllImport(Lib)] static extern int sfxn_update(IntPtr voice, byte[] patch13);
        [DllImport(Lib)] static extern void sfxn_release(IntPtr voice);
        [DllImport(Lib)] static extern int sfxn_active(IntPtr voice);
        [DllImport(Lib)] static extern int sfxn_render(IntPtr voice, ref SfxMods mods, [In, Out] float[] buffer, int n);
        [DllImport(Lib)] static extern int sfxn_bake(byte[] patch13, float sr, float vel, float noteSt, float holdSeconds, uint seed, [Out] float[] buffer, int max);

        static int available = -1;

        /// <summary>False where the library is missing (another platform): nothing plays locally, the link still works.</summary>
        public static bool Available
        {
            get
            {
                if (available < 0)
                {
                    try { available = sfxn_abi() == 1 ? 1 : 0; }
                    catch (Exception e) when (e is DllNotFoundException || e is EntryPointNotFoundException || e is BadImageFormatException) { available = 0; }
                }
                return available == 1;
            }
        }

        public static int VoiceSize => sfxn_voice_size();

        public static bool Trigger(IntPtr voice, byte[] patch13, float velocity, float semitones, float sampleRate, uint seed)
            => sfxn_trigger(voice, patch13, velocity, semitones, sampleRate, seed) == 0;

        public static void Update(IntPtr voice, byte[] patch13) => sfxn_update(voice, patch13);
        public static void Release(IntPtr voice) => sfxn_release(voice);
        public static bool Active(IntPtr voice) => sfxn_active(voice) != 0;

        /// <summary>Adds n mono samples to buffer. True while the voice sounds.</summary>
        public static bool Render(IntPtr voice, ref SfxMods mods, float[] buffer, int n) => sfxn_render(voice, ref mods, buffer, n) != 0;

        /// <summary>
        /// The whole sound as mono samples. A HOLD patch is held for holdSeconds, then let go.
        /// Null: the library is missing or the patch is not valid.
        /// </summary>
        public static float[] Bake(SfxPatch patch, int sampleRate = 48000, float velocity = 1f, float semitones = 0f, float holdSeconds = 1.5f, uint seed = 1, float maxSeconds = 12f)
        {
            if (!Available) return null;
            var tmp = new float[(int)(maxSeconds * sampleRate)];
            int n = sfxn_bake(patch.Pack(), sampleRate, velocity, semitones, holdSeconds, seed, tmp, tmp.Length);
            if (n < 0) return null;
            var result = new float[n];
            Array.Copy(tmp, result, n);
            return result;
        }
    }
}
