using System.Collections;
using CatSplat.SfxForge;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;

namespace SfxDemo.Tests
{
    /// <summary>The demo scene in Play mode: do the sounds really start, does speed really move the engine?</summary>
    public class SfxDemoTests
    {
        HoverCraft craft;

        [UnitySetUp]
        public IEnumerator Load()
        {
            yield return SceneManager.LoadSceneAsync("SfxDemo", LoadSceneMode.Single);
            yield return null;
            craft = Object.FindFirstObjectByType<HoverCraft>();
            Assert.IsNotNull(craft, "the scene has its hovercraft");
        }

        static IEnumerator Until(System.Func<bool> condition, float seconds)
        {
            float end = Time.realtimeSinceStartup + seconds;                // (real time: a frame after loading can be long)
            while (Time.realtimeSinceStartup < end && !condition()) yield return null;
        }

        [UnityTest]
        public IEnumerator EngineHumPlaysFromTheStart()
        {
            yield return Until(() => craft.engineSound.IsPlaying, 2f);
            Assert.IsTrue(craft.engineSound.IsPlaying, "the HOLD preset sounds as soon as the scene runs");
            yield return new WaitForSecondsRealtime(1f);
            Assert.IsTrue(craft.engineSound.IsPlaying, "and keeps sounding");
        }

        [UnityTest]
        public IEnumerator SpeedMovesTheEngine()
        {
            var senders = craft.engineSound.GetComponents<SfxTelemetrySender>();
            Assert.AreEqual(2, senders.Length);
            yield return null;
            Assert.AreEqual(0f, craft.engineSound.pitchSemitones, 0.01f, "at rest: no pitch");
            craft.GetComponent<Rigidbody>().linearVelocity = craft.transform.forward * 20f;
            yield return new WaitForFixedUpdate();
            yield return null;
            Assert.Greater(senders[0].Normalized, 0.3f, "the sender reads the Rigidbody's speed");
            Assert.Greater(craft.engineSound.pitchSemitones, 6f, "and the local player's pitch follows, as the device's would");
            Assert.Greater(craft.engineSound.fm, 0.2f);
        }

        [UnityTest]
        public IEnumerator FiringPlaysTheLaserAndSpawnsABolt()
        {
            Assert.IsFalse(craft.laserSound.IsPlaying);
            craft.Fire();
            yield return Until(() => craft.laserSound.IsPlaying, 2f);
            Assert.IsTrue(craft.laserSound.IsPlaying, "a one-shot preset starts when asked");
            Assert.IsNotNull(Object.FindFirstObjectByType<Bolt>(), "a bolt flies");
            yield return Until(() => !craft.laserSound.IsPlaying, 4f);
            Assert.IsFalse(craft.laserSound.IsPlaying, "and the voice ends by itself");
        }

        [UnityTest]
        public IEnumerator TheMusicPlaysAndFollowsTheSpeed()
        {
            var music = craft.music;
            Assert.IsNotNull(music, "the scene has its music player");
            yield return Until(() => music.IsPlaying && music.Step >= 0, 2f);
            Assert.IsTrue(music.IsPlaying, "the song runs from the start");
            int first = music.Step + music.Bar * 16;
            yield return new WaitForSecondsRealtime(0.6f);
            Assert.Greater(music.Step + music.Bar * 16, first + 2, "and moves on: 8 steps a second at 120 BPM");
            Assert.Less(music.intensity, 0.05f, "at rest the game is cold: only the layers that always play");
            craft.GetComponent<Rigidbody>().linearVelocity = craft.transform.forward * 20f;
            yield return new WaitForFixedUpdate();
            yield return null;
            Assert.Greater(music.intensity, 0.6f, "at speed the intensity is up, and with it the layers");
            music.CuePattern(3);
            yield return Until(() => music.Pattern == 3, 3f);
            Assert.AreEqual(3, music.Pattern, "a cued pattern takes over on the next bar line");
        }

        [UnityTest]
        public IEnumerator EncoderOneSetsTheThrust()
        {
            var binding = Object.FindFirstObjectByType<SfxEncoderBinding>();
            Assert.IsNotNull(binding);
            binding.onValue.Invoke(42f);                               // what a turn of encoder 1 on the device ends in
            yield return null;
            Assert.AreEqual(42f, craft.thrust, 0.001f);
        }
    }
}
