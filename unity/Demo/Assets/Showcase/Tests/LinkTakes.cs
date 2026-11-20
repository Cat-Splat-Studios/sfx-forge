using System;
using System.Collections;
using System.Collections.Generic;
using CatSplat.SfxForge;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace SfxShowcase.Tests
{
    /// <summary>
    /// THE LINK, END TO END: Unity on one side of two MIDI ports, SFX FORGE on the other (the emulator in Chrome or
    /// Edge, the plugin, or the hardware). These tests need that other side to be there, so they never run by
    /// themselves: they are explicit, and without the ports they end as "inconclusive" with the list of ports found.
    ///
    /// What must be set up first (docs/UNITY.md, "From the device to the game"):
    ///  • two MIDI ports, one each way. Without the hardware: a virtual MIDI cable (for example loopMIDI) with two
    ///    ports. Their names are searched for <see cref="FromDevice"/> and <see cref="ToDevice"/>; other names go in
    ///    the environment variables SFXFORGE_LINK_IN and SFXFORGE_LINK_OUT;
    ///  • SFX FORGE running with its MIDI out on the first port and its MIDI in on the second.
    ///
    ///     Unity -batchmode -projectPath unity/Demo -runTests -testPlatform PlayMode
    ///           -testFilter SfxShowcase.Tests.LinkTakes -testResults out/link.xml -logFile out/link.log
    ///
    /// Every step that worked writes a line starting with "LINK: ok" to the log.
    /// </summary>
    [Category("Link")]
    public sealed class LinkTakes
    {
        /// <summary>Part of the name of the port the device sends on, unless SFXFORGE_LINK_IN names another.</summary>
        public const string FromDevice = "SFX to Unity";
        /// <summary>Part of the name of the port the device listens on, unless SFXFORGE_LINK_OUT names another.</summary>
        public const string ToDevice = "Unity to SFX";

        const string InVariable = "SFXFORGE_LINK_IN", OutVariable = "SFXFORGE_LINK_OUT";
        const float AnswerSeconds = 15f;
        const string SongAssetPath = "Assets/SfxDemo/DemoSong.asset";

        SfxLink link;
        readonly List<byte[]> messages = new List<byte[]>();

        [SetUp]
        public void Open()
        {
            string wantedIn = Environment.GetEnvironmentVariable(InVariable);
            string wantedOut = Environment.GetEnvironmentVariable(OutVariable);
            if (string.IsNullOrEmpty(wantedIn)) wantedIn = FromDevice;
            if (string.IsNullOrEmpty(wantedOut)) wantedOut = ToDevice;

            string[] inputs = MidiPorts.Inputs(), outputs = MidiPorts.Outputs();
            string found = $"inputs: [{string.Join(", ", inputs)}], outputs: [{string.Join(", ", outputs)}]";
            if (SfxLink.Find(inputs, wantedIn) < 0 || SfxLink.Find(outputs, wantedOut) < 0)
                Assert.Inconclusive($"the link needs a MIDI input named like \"{wantedIn}\" and an output named like \"{wantedOut}\"; {found}");

            link = new SfxLink();
            Assert.IsTrue(link.Open(wantedIn, wantedOut), $"both ports open ({found}); another program may hold one of them");
            messages.Clear();
            link.MessageReceived += messages.Add;
            Debug.Log($"LINK: ok ports open: from the device \"{link.InputName}\", to the device \"{link.OutputName}\"");
        }

        [TearDown]
        public void Close()
        {
            if (link != null) link.Dispose();
            link = null;
        }

        /// <summary>Polls the link every frame until the condition holds or the time is up (real time).</summary>
        IEnumerator Until(Func<bool> done, float seconds)
        {
            float end = Time.realtimeSinceStartup + seconds;
            while (!done() && Time.realtimeSinceStartup < end)
            {
                link.Poll();
                yield return null;
            }
        }

        /// <summary>Unity asks for a pad's patch; the device answers with it.</summary>
        [UnityTest, Explicit]
        public IEnumerator TheDeviceAnswersWithAPatch()
        {
            const int pad = 0;
            int gotSlot = -1;
            SfxPatch got = default;
            link.PatchReceived += (slot, patch) => { gotSlot = slot; got = patch; };

            Assert.IsTrue(link.SendText("UNITY: PATCH?"), "text goes out");
            Assert.IsTrue(link.RequestPatch(pad), "the request goes out");
            yield return Until(() => gotSlot >= 0, AnswerSeconds);

            Assert.AreEqual(pad, gotSlot, $"the device answered with pad {pad + 1}'s patch within {AnswerSeconds} s ({messages.Count} messages came in). Is SFX FORGE running, with its MIDI in and out on these ports?");
            Assert.Greater((int)got.level, 0, "and the patch is a sound, not silence");
            Debug.Log($"LINK: ok a patch came from the device: pad {gotSlot + 1}, engine {got.engine}, {messages.Count} message(s) in");
        }

        /// <summary>
        /// Unity asks for the song; the device answers with its 16 sounds and the song in 39 pieces, and what arrives
        /// plays: an <see cref="SfxMusicPlayer"/> is given the received song and its bar counter moves.
        /// </summary>
        [UnityTest, Explicit]
        public IEnumerator TheDeviceSendsItsSongAndItPlays()
        {
            byte[] packed = null;
            SfxPatch[] sounds = null;
            link.SongReceived += (song, itsSounds) => { packed = song; sounds = itsSounds; };

            Assert.IsTrue(link.SendText("UNITY: SONG?"), "text goes out");
            Assert.IsTrue(link.RequestSong(), "the request goes out");
            yield return Until(() => packed != null, AnswerSeconds);

            Assert.IsNotNull(packed, $"the song arrived within {AnswerSeconds} s ({messages.Count} messages came in: {Describe(messages)}). Is SFX FORGE running, with its MIDI in and out on these ports?");
            Assert.AreEqual(SfxProtocol.SongBytes, packed.Length, "a whole packed song");
            Assert.IsNotNull(sounds, "and all 16 sounds came with it");
            Debug.Log($"LINK: ok the song came from the device: {packed.Length} bytes and {sounds.Length} sounds in {messages.Count} messages");

            var song = ScriptableObject.CreateInstance<SfxSong>();
            song.Set(sounds, packed);
            Assert.IsTrue(song.IsValid, "what arrived is a song the package can play");
            Debug.Log($"LINK: ok the song is valid: {song.Bpm} BPM, {song.StepsPerPattern} steps a pattern, {song.ArrangementBars} bars arranged; {CompareWithTheDemoSong(song)}");

            if (!SfxNative.Available) Assert.Inconclusive("the native library is not here, so the song cannot be played");
            var listener = new GameObject("Listener").AddComponent<AudioListener>();
            var music = new GameObject("Received Song").AddComponent<SfxMusicPlayer>();
            music.song = song;
            music.Play();
            yield return Until(() => music.IsPlaying && music.Bar >= 1, 6f);
            bool played = music.IsPlaying && music.Bar >= 1;
            music.Stop();
            UnityEngine.Object.Destroy(music.gameObject);
            UnityEngine.Object.Destroy(listener.gameObject);
            Assert.IsTrue(played, "the received song plays: its second bar was reached");
            link.SendText("UNITY: GOT THE SONG");
            Debug.Log("LINK: ok the received song plays in an SfxMusicPlayer");
        }

        /// <summary>
        /// The song twenty times in a row, while the game keeps sending to the device as it does in play (a value
        /// every frame): every transfer must arrive whole. One that does not is written out piece by piece.
        /// </summary>
        [UnityTest, Explicit]
        public IEnumerator TheSongArrivesEveryTime()
        {
            const int transfers = 20;
            int arrived = 0;
            link.SongReceived += (song, itsSounds) => arrived++;
            var failures = new List<string>();
            for (int n = 0; n < transfers; n++)
            {
                messages.Clear();
                int before = arrived, frame = 0;
                Assert.IsTrue(link.RequestSong(), "the request goes out");
                yield return Until(() => { link.SendIntensity((frame++ % 100) / 100f); return arrived > before; }, 6f);
                if (arrived == before) failures.Add($"transfer {n + 1}: {messages.Count} messages: {Describe(messages)}");
                yield return Until(() => false, 0.3f);
            }
            Debug.Log($"LINK: {(failures.Count == 0 ? "ok" : "FAILED")} the song arrived whole {arrived} times out of {transfers}");
            Assert.IsEmpty(failures, $"{failures.Count} of {transfers} transfers did not arrive whole: {string.Join(" | ", failures)}");
        }

        /// <summary>
        /// The game drives the device: pads played, intensity moved, a pattern cued, text shown. Nothing comes back for
        /// these, so the test can only say that every message left; whether the device followed is seen and heard there.
        /// </summary>
        [UnityTest, Explicit]
        public IEnumerator TheGameDrivesTheDevice()
        {
            Assert.IsTrue(link.SendText("UNITY DRIVES"), "text goes out");
            for (int pad = 0; pad < 4; pad++)
            {
                Assert.IsTrue(link.PlayPad(pad, 0.9f), $"pad {pad + 1} is played");
                yield return Until(() => false, 0.3f);
                Assert.IsTrue(link.ReleasePad(pad), $"pad {pad + 1} is released");
            }
            for (int step = 0; step <= 10; step++)
            {
                link.SendIntensity(step / 10f);                      // (false when the 7-bit value did not change)
                yield return Until(() => false, 0.2f);
            }
            Assert.IsTrue(link.CuePattern(2), "pattern C is cued");
            Assert.IsTrue(link.SendText("UNITY: DONE"), "text goes out");
            Debug.Log("LINK: ok pads, intensity, a cue and text were sent to the device");
        }

        /// <summary>What came in, in order and in short: "p3" a patch for pad 4, "s7/208" piece 7 of the song and its length, otherwise the first byte and the length.</summary>
        public static string Describe(List<byte[]> incoming)
        {
            var text = new System.Text.StringBuilder();
            foreach (byte[] m in incoming)
            {
                if (m.Length > 6 && m[0] == 0xF0 && m[1] == 0x7D && m[4] == SfxProtocol.Dump) text.Append('p').Append(m[5]);
                else if (m.Length > 6 && m[0] == 0xF0 && m[1] == 0x7D && m[4] == SfxProtocol.SongChunk) text.Append('s').Append(m[5]).Append('/').Append(m.Length);
                else text.Append(m.Length > 0 ? m[0].ToString("X2") : "--").Append('/').Append(m.Length);
                text.Append(' ');
            }
            return text.ToString().TrimEnd();
        }

        /// <summary>Says whether the received song is the demo song of the demo project, byte for byte.</summary>
        static string CompareWithTheDemoSong(SfxSong received)
        {
#if UNITY_EDITOR
            var demo = UnityEditor.AssetDatabase.LoadAssetAtPath<SfxSong>(SongAssetPath);
            if (demo == null || !demo.IsValid) return "the demo song asset is not here to compare with";
            if (demo.data.Length != received.data.Length) return "it is not the demo song (another length)";
            int different = 0;
            for (int i = 0; i < demo.data.Length; i++) if (demo.data[i] != received.data[i]) different++;
            int soundsDifferent = 0;
            for (int i = 0; i < SfxProtocol.Slots; i++)
            {
                byte[] a = demo.sounds[i].Pack(), b = received.sounds[i].Pack();
                for (int k = 0; k < a.Length; k++) if (a[k] != b[k]) { soundsDifferent++; break; }
            }
            return different == 0 && soundsDifferent == 0
                ? "it is the demo song of the demo project, byte for byte"
                : $"it differs from the demo project's song in {different} byte(s) and {soundsDifferent} sound(s) (the device's song was edited, or is another)";
#else
            return "";
#endif
        }
    }
}
