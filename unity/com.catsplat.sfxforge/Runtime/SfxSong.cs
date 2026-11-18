// SPDX-License-Identifier: MIT
using UnityEngine;

namespace CatSplat.SfxForge
{
    /// <summary>
    /// A piece of music made on SFX FORGE: its 16 sounds and its song (8 patterns, a chain, a layer threshold per sound).
    /// Captured from the device in Window > SFX Forge; played in the game by SfxMusicPlayer.
    /// </summary>
    [CreateAssetMenu(menuName = "SFX Forge/Song", fileName = "New Song", order = 221)]
    public class SfxSong : ScriptableObject
    {
        [Tooltip("The 16 sounds the song plays (pads 1-16 of the device).")]
        public SfxPatch[] sounds = new SfxPatch[SfxProtocol.Slots];

        [Tooltip("The song as the device packs it (core/sfx_song.h).")]
        [HideInInspector] public byte[] data = new byte[0];

        public bool IsValid => data != null && data.Length == SfxProtocol.SongBytes && data[0] == 1 && sounds != null && sounds.Length == SfxProtocol.Slots;
        public int StepsPerPattern => IsValid ? data[1] : 0;
        public int Bpm => IsValid ? data[2] << 7 | data[3] : 0;
        /// <summary>How many patterns it walks through by itself (1 = it stays on one).</summary>
        public int Chain => IsValid ? data[6] : 0;
        /// <summary>From which intensity (0..1) a sound plays: 0 = always.</summary>
        public float Layer(int sound) => IsValid && sound >= 0 && sound < SfxProtocol.Slots ? data[7 + sound] / 127f : 0f;

        public void Set(SfxPatch[] newSounds, byte[] packedSong)
        {
            sounds = (SfxPatch[])newSounds.Clone();
            data = (byte[])packedSong.Clone();
        }
    }
}
