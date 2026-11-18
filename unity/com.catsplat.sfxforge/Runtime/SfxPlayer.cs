// SPDX-License-Identifier: MIT
using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using UnityEngine;

namespace CatSplat.SfxForge
{
    /// <summary>
    /// Plays a preset in the game with the device's own engine – synthesized live, so the four "Live" values can move the
    /// sound while it plays (an engine hum following speed), exactly as telemetry does on the device. No device needed.
    /// The sound is added to this object's AudioSource, which this component keeps running: leave its AudioClip empty.
    /// Not spatialized: for 3D one-shots bake a clip instead.
    /// </summary>
    [AddComponentMenu("SFX Forge/SFX Player")]
    [RequireComponent(typeof(AudioSource))]
    public class SfxPlayer : MonoBehaviour
    {
        public SfxPreset preset;
        [Range(1, 8)] public int voices = 4;
        [Range(0f, 1f)] public float volume = 0.8f;
        [Tooltip("Start the sound when this component is enabled (for a HOLD sound: an engine, wind).")]
        public bool playOnEnable;

        [Header("Live (what telemetry moves on the device)")]
        [Range(0f, 24f)] public float pitchSemitones;
        [Range(-4f, 4f)] public float cutoffOctaves;
        [Range(0f, 1f)] public float fm;
        [Range(0f, 1f)] public float level = 1f;

        struct Command { public byte[] patch; public float velocity, semitones; public int kind; }   // 0 play, 1 release, 2 new values
        readonly ConcurrentQueue<Command> commands = new ConcurrentQueue<Command>();
        readonly object gate = new object();
        IntPtr memory;
        int voiceSize, voiceCount, next, sampleRate;
        uint seed = 1;
        float[] mono = new float[2048];
        SfxMods current = SfxMods.None;
        SfxPatch lastPatch;
        AudioSource source;
        volatile bool sounding;
        static bool warned;

        public bool IsPlaying => sounding;

        void OnEnable()
        {
            if (!SfxNative.Available)
            {
                if (!warned) { warned = true; Debug.LogWarning("SFX Forge: the native library (sfxforge) is not available on this platform – SfxPlayer stays silent. Bake presets to clips instead."); }
                return;
            }
            sampleRate = AudioSettings.outputSampleRate;
            voiceSize = SfxNative.VoiceSize; voiceCount = Mathf.Clamp(voices, 1, 8);
            lock (gate)
            {
                memory = Marshal.AllocHGlobal(voiceSize * voiceCount);
                for (int i = 0; i < voiceSize * voiceCount; i += 4) Marshal.WriteInt32(memory, i, 0);
            }
            source = GetComponent<AudioSource>();
            if (preset != null) lastPatch = preset.patch;
            if (playOnEnable) Play();
        }

        void OnDisable()
        {
            lock (gate)
            {
                if (memory != IntPtr.Zero) Marshal.FreeHGlobal(memory);
                memory = IntPtr.Zero; sounding = false;
            }
            while (commands.TryDequeue(out _)) { }
        }

        void Update()
        {
            // Unity only runs OnAudioFilterRead while the AudioSource plays (it does so without a clip, too)
            if (source != null && memory != IntPtr.Zero && !source.isPlaying) source.Play();
            if (preset != null && sounding && !preset.patch.Equals(lastPatch))        // edited while it sounds: hear it
            {
                lastPatch = preset.patch;
                commands.Enqueue(new Command { kind = 2, patch = lastPatch.Pack() });
            }
        }

        /// <summary>Plays the preset. velocity 0..1; semitones transposes it.</summary>
        public void Play(float velocity = 1f, float semitones = 0f)
        {
            if (preset == null) return;
            lastPatch = preset.patch;
            Play(preset.patch, velocity, semitones);
        }

        public void Play(SfxPatch patch, float velocity = 1f, float semitones = 0f)
        {
            if (memory == IntPtr.Zero) return;
            commands.Enqueue(new Command { kind = 0, patch = patch.Pack(), velocity = Mathf.Clamp01(velocity), semitones = semitones });
        }

        /// <summary>Lets go of HOLD sounds (they fade); one-shots just finish.</summary>
        public void Release() { if (memory != IntPtr.Zero) commands.Enqueue(new Command { kind = 1 }); }

        /// <summary>The same as the device does with a telemetry value 0..1.</summary>
        public void ApplyTelemetry(SfxTelemetry channel, float value01)
        {
            var m = new SfxMods { pitchSemitones = pitchSemitones, cutoffOctaves = cutoffOctaves, fm = fm, level = level };
            SfxProtocol.Apply(ref m, channel, SfxProtocol.ToCc(value01));
            pitchSemitones = m.pitchSemitones; cutoffOctaves = m.cutoffOctaves; fm = m.fm; level = m.level;
        }

        void OnAudioFilterRead(float[] data, int channels)
        {
            lock (gate)
            {
                if (memory == IntPtr.Zero) return;
                while (commands.TryDequeue(out var c))
                {
                    if (c.kind == 0)
                    {
                        int pick = next;
                        for (int i = 0; i < voiceCount; i++) if (!SfxNative.Active(memory + i * voiceSize)) { pick = i; break; }
                        next = (pick + 1) % voiceCount;
                        SfxNative.Trigger(memory + pick * voiceSize, c.patch, c.velocity, c.semitones, sampleRate, seed += 0x9E3779B9u);
                    }
                    else for (int i = 0; i < voiceCount; i++)
                    {
                        if (c.kind == 1) SfxNative.Release(memory + i * voiceSize);
                        else SfxNative.Update(memory + i * voiceSize, c.patch);
                    }
                }

                int frames = data.Length / channels;
                if (mono.Length < frames) mono = new float[frames];
                Array.Clear(mono, 0, frames);
                float glide = Mathf.Exp(-frames / (0.03f * sampleRate));               // 30 ms, as on the device
                current.pitchSemitones = pitchSemitones + (current.pitchSemitones - pitchSemitones) * glide;
                current.cutoffOctaves = cutoffOctaves + (current.cutoffOctaves - cutoffOctaves) * glide;
                current.fm = fm + (current.fm - fm) * glide;
                current.level = level + (current.level - level) * glide;

                bool any = false;
                for (int i = 0; i < voiceCount; i++) any |= SfxNative.Render(memory + i * voiceSize, ref current, mono, frames);
                sounding = any;
                if (!any) return;
                float gain = volume * 1.25f;
                for (int f = 0; f < frames; f++)
                {
                    float x = mono[f] * gain;
                    x = x > 3f ? 1f : x < -3f ? -1f : x * (27f + x * x) / (27f + 9f * x * x);   // the device's soft clip
                    for (int ch = 0; ch < channels; ch++) data[f * channels + ch] += x;
                }
            }
        }
    }
}
