using System;
using System.Reflection;
using CatSplat.SfxForge;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using Object = UnityEngine.Object;

namespace Inspector.Harness
{
    /// <summary>
    /// SfxMusicPlayer and the song a script assigns: Play() takes the song when the field holds another asset than
    /// the one the player took. Edit mode calls none of a component's messages and plays no audio, so the test calls
    /// OnEnable, OnAudioFilterRead and OnDisable itself and compares the blocks the players render.
    /// </summary>
    public class SfxMusicPlayerTests
    {
        const int Blocks = 48; // of 1024 frames: about a second
        const int Frames = 1024;
        const BindingFlags Hidden = BindingFlags.Instance | BindingFlags.NonPublic;

        SfxSong demo;
        SfxSong other;

        [SetUp]
        public void SetUp()
        {
            if (!SfxNative.Available)
            {
                Assert.Ignore("the native library is for Windows x64");
            }

            demo = AssetDatabase.LoadAssetAtPath<SfxSong>(SfxSongPanelTests.DemoPath);
            Assert.IsNotNull(demo, "the demo song is in this project");
            Assert.IsTrue(demo.IsValid, "and it is a song");

            // the same song with other sounds: every one of its 16 is the default patch
            other = Object.Instantiate(demo);
            other.name = "Other";
            for (int i = 0; i < other.sounds.Length; i++)
            {
                other.sounds[i] = SfxPatch.Default;
            }
        }

        [TearDown]
        public void TearDown()
        {
            if (other != null)
            {
                Object.DestroyImmediate(other);
            }
        }

        static void Call(SfxMusicPlayer player, string message, params object[] arguments) =>
            typeof(SfxMusicPlayer).GetMethod(message, Hidden).Invoke(player, arguments);

        static SfxMusicPlayer Make(SfxSong song)
        {
            var go = new GameObject("music test", typeof(AudioSource), typeof(SfxMusicPlayer));
            var player = go.GetComponent<SfxMusicPlayer>();
            player.song = song;
            player.playOnEnable = false;
            Call(player, "OnEnable");
            return player;
        }

        static void Drop(SfxMusicPlayer player)
        {
            Call(player, "OnDisable");
            Object.DestroyImmediate(player.gameObject);
        }

        // what the player adds to about a second of silence, as the audio thread would ask for it
        static float[] Render(SfxMusicPlayer player)
        {
            var all = new float[Blocks * Frames * 2];
            var block = new float[Frames * 2];
            for (int b = 0; b < Blocks; b++)
            {
                Array.Clear(block, 0, block.Length);
                Call(player, "OnAudioFilterRead", block, 2);
                Array.Copy(block, 0, all, b * block.Length, block.Length);
            }

            return all;
        }

        static float Peak(float[] samples)
        {
            float peak = 0f;
            foreach (float s in samples)
            {
                peak = Mathf.Max(peak, Mathf.Abs(s));
            }

            return peak;
        }

        // the song a player plays that had it when it was enabled
        static float[] Reference(SfxSong song)
        {
            var player = Make(song);
            try
            {
                player.Play();
                return Render(player);
            }
            finally
            {
                Drop(player);
            }
        }

        // a player that was enabled with one song (or none) and is given another by a script, then started
        static float[] Assigned(SfxSong first, SfxSong then)
        {
            var player = Make(first);
            try
            {
                player.song = then;
                player.Play();
                return Render(player);
            }
            finally
            {
                Drop(player);
            }
        }

        [Test]
        public void PlayTakesASongAssignedAfterThePlayerWasEnabled()
        {
            float[] demoSound = Reference(demo);
            float[] otherSound = Reference(other);
            Assert.Greater(Peak(demoSound), 0.001f, "the demo song sounds");
            Assert.Greater(Peak(otherSound), 0.001f, "and so does the one with other sounds");
            CollectionAssert.AreNotEqual(demoSound, otherSound, "the two songs can be told apart");
            CollectionAssert.AreEqual(demoSound, Reference(demo), "and a song renders the same samples again");

            CollectionAssert.AreEqual(
                demoSound,
                Assigned(null, demo),
                "enabled without a song, then given one: Play() plays it, with no Reload()"
            );
            CollectionAssert.AreEqual(
                otherSound,
                Assigned(demo, other),
                "enabled with one song, then given another: Play() plays the other one"
            );
        }

        [Test]
        public void WithoutASongPlayIsSilent()
        {
            Assert.AreEqual(0f, Peak(Reference(null)), "a player without a song adds nothing");
        }
    }
}
