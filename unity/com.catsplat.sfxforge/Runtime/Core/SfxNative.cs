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

        [DllImport(Lib)] static extern int sfxn_song_bytes();
        [DllImport(Lib)] static extern int sfxn_player_size();
        [DllImport(Lib)] static extern void sfxn_player_init(IntPtr player, float sr);
        [DllImport(Lib)] static extern int sfxn_player_set_patch(IntPtr player, int slot, byte[] patch13);
        [DllImport(Lib)] static extern int sfxn_player_set_song(IntPtr player, byte[] song, int n);
        [DllImport(Lib)] static extern void sfxn_player_start(IntPtr player, int pattern);
        [DllImport(Lib)] static extern void sfxn_player_stop(IntPtr player);
        [DllImport(Lib)] static extern void sfxn_player_cue(IntPtr player, int pattern);
        [DllImport(Lib)] static extern void sfxn_player_set_live(IntPtr player, ref SfxMods mods, float intensity);
        [DllImport(Lib)] static extern void sfxn_player_note_on(IntPtr player, int slot, float vel, int midiNote);
        [DllImport(Lib)] static extern void sfxn_player_note_off(IntPtr player, int slot, int midiNote);
        [DllImport(Lib)] static extern uint sfxn_player_render(IntPtr player, [In, Out] float[] buffer, int n);
        [DllImport(Lib)] static extern void sfxn_player_state(IntPtr player, [Out] int[] out4);

        static int available = -1;

        /// <summary>False where the library is missing (another platform): nothing plays locally, the link still works.</summary>
        public static bool Available
        {
            get
            {
                if (available < 0)
                {
                    try { available = sfxn_abi() == 2 ? 1 : 0; }
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

        // ---- music: the device's whole player (16 sounds, a song, its sequencer) in PlayerSize bytes you allocate ---------
        public static int PlayerSize => sfxn_player_size();
        public static void PlayerInit(IntPtr player, float sampleRate) => sfxn_player_init(player, sampleRate);
        public static bool PlayerSetPatch(IntPtr player, int slot, byte[] patch13) => sfxn_player_set_patch(player, slot, patch13) == 0;
        /// <summary>The packed song (what the device sends). False: not a song, the player keeps what it had.</summary>
        public static bool PlayerSetSong(IntPtr player, byte[] song) => song != null && sfxn_player_set_song(player, song, song.Length) == 0;
        /// <summary>pattern -1: the one it is on.</summary>
        public static void PlayerStart(IntPtr player, int pattern) => sfxn_player_start(player, pattern);
        public static void PlayerStop(IntPtr player) => sfxn_player_stop(player);
        /// <summary>Change to this pattern on the next bar line (at once while stopped).</summary>
        public static void PlayerCue(IntPtr player, int pattern) => sfxn_player_cue(player, pattern);
        public static void PlayerSetLive(IntPtr player, ref SfxMods mods, float intensity) => sfxn_player_set_live(player, ref mods, intensity);
        /// <summary>A sound by hand, beside the song. midiNote 0: as designed, else at that pitch.</summary>
        public static void PlayerNoteOn(IntPtr player, int slot, float velocity, int midiNote = 0) => sfxn_player_note_on(player, slot, velocity, midiNote);
        public static void PlayerNoteOff(IntPtr player, int slot, int midiNote = 0) => sfxn_player_note_off(player, slot, midiNote);
        /// <summary>Adds n mono samples to buffer. Returns the sounds that play, a bit each.</summary>
        public static uint PlayerRender(IntPtr player, float[] buffer, int n) => sfxn_player_render(player, buffer, n);
        /// <summary>out4: running, step (-1 = none yet), pattern, bar.</summary>
        public static void PlayerState(IntPtr player, int[] out4) => sfxn_player_state(player, out4);

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
