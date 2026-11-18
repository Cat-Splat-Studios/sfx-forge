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
