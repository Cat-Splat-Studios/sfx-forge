using System;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using UnityEditor;
using UnityEditor.Events;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace SfxDemo.Editor
{
    /// <summary>
    /// Builds the demo from nothing (SFX Forge > Rebuild Demo Scene): the presets, the materials and the scene. The scene in
    /// the repository is this script's output, so change the demo here.
    /// </summary>
    public static class SfxDemoBuilder
    {
        const string Root = "Assets/SfxDemo", ScenePath = Root + "/SfxDemo.unity";

        static SfxPreset Preset(string name, SfxEngine engine, params byte[] v)
        {
            string path = $"{Root}/Presets/{name}.asset";
            var preset = AssetDatabase.LoadAssetAtPath<SfxPreset>(path);
            if (preset == null) { preset = ScriptableObject.CreateInstance<SfxPreset>(); AssetDatabase.CreateAsset(preset, path); }
            preset.patch = SfxPatch.Create(engine, v);
            EditorUtility.SetDirty(preset);
            return preset;
        }

        static Material Mat(string name, Color color, bool glow = false)
        {
            string path = $"{Root}/Materials/{name}.mat";
            var m = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (m == null) { m = new Material(Shader.Find("Standard")); AssetDatabase.CreateAsset(m, path); }
            m.color = color;
            if (glow) { m.EnableKeyword("_EMISSION"); m.SetColor("_EmissionColor", color * 1.5f); }
            EditorUtility.SetDirty(m);
            return m;
        }

        static GameObject Shape(PrimitiveType type, string name, Vector3 position, Vector3 scale, Material material, Transform parent = null)
        {
            var go = GameObject.CreatePrimitive(type);
            go.name = name; go.transform.SetParent(parent, false);
            go.transform.localPosition = position; go.transform.localScale = scale;
            go.GetComponent<Renderer>().sharedMaterial = material;
            return go;
        }

        static SfxPlayer Sound(string name, Transform parent, SfxPreset preset, float volume)
        {
            var go = new GameObject(name);
            go.transform.SetParent(parent, false);
            var source = go.AddComponent<AudioSource>();
            source.playOnAwake = false; source.spatialBlend = 0f;
            var player = go.AddComponent<SfxPlayer>();
            player.preset = preset; player.volume = volume;
            return player;
        }

        [MenuItem("SFX Forge/Rebuild Demo Scene")]
        public static void Build()
        {
            foreach (string folder in new[] { "Presets", "Materials" })
                if (!AssetDatabase.IsValidFolder($"{Root}/{folder}")) AssetDatabase.CreateFolder(Root, folder);

            // the sounds: FREQ, PITCH ENV, ENV TIME, A, B, NOISE, CUTOFF, RESO, DECAY, CRUSH, then ATTACK, SUSTAIN … – as on the device
            SfxPreset hum = Preset("EngineHum", SfxEngine.SciFiFm, 22, 64, 0, 21, 55, 8, 78, 45, 60, 0, 0, 127);   // sustained
            SfxPreset laser = Preset("Laser", SfxEngine.SciFiFm, 70, 110, 45, 50, 70, 0, 120, 20, 58, 0);
            SfxPreset impact = Preset("Impact", SfxEngine.Impact, 32, 100, 28, 0, 20, 25, 90, 5, 55, 0);
            SfxPreset coin = Preset("Coin", SfxEngine.UiBlip, 86, 64, 0, 26, 60, 0, 127, 0, 62, 0);

            Material ground = Mat("Ground", new Color(0.10f, 0.12f, 0.17f)), hull = Mat("Hull", new Color(0.21f, 0.77f, 1f)),
                     crate = Mat("Crate", new Color(1f, 0.35f, 0.21f)), gold = Mat("Pickup", new Color(1f, 0.82f, 0.25f), true),
                     beam = Mat("Bolt", new Color(0.43f, 0.91f, 0.66f), true);

            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            RenderSettings.ambientLight = new Color(0.35f, 0.38f, 0.45f);

            var sun = new GameObject("Sun").AddComponent<Light>();
            sun.type = LightType.Directional; sun.intensity = 1.1f;
            sun.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            var floor = Shape(PrimitiveType.Plane, "Ground", Vector3.zero, new Vector3(10f, 1f, 10f), ground);
            floor.isStatic = true;

            // the hovercraft
            var craftGo = Shape(PrimitiveType.Cube, "HoverCraft", new Vector3(0f, 0.6f, 0f), new Vector3(1.6f, 0.4f, 2.4f), hull);
            Shape(PrimitiveType.Cube, "Nose", new Vector3(0f, 0.6f, 0.35f), new Vector3(0.5f, 1f, 0.4f), crate, craftGo.transform).GetComponent<Collider>().enabled = false;
            var body = craftGo.AddComponent<Rigidbody>();
            body.useGravity = false; body.mass = 4f; body.linearDamping = 1.2f; body.angularDamping = 6f;
            body.constraints = RigidbodyConstraints.FreezePositionY | RigidbodyConstraints.FreezeRotationX | RigidbodyConstraints.FreezeRotationZ;
            var craft = craftGo.AddComponent<HoverCraft>();

            craft.engineSound = Sound("EngineSound", craftGo.transform, hum, 0.5f);
            craft.engineSound.playOnEnable = true;
            var pitch = craft.engineSound.gameObject.AddComponent<SfxTelemetrySender>();   // speed -> pitch: up to about an octave
            pitch.channel = SfxTelemetry.Pitch; pitch.source = SfxTelemetrySender.Source.RigidbodySpeed; pitch.max = 40f;
            var fm = craft.engineSound.gameObject.AddComponent<SfxTelemetrySender>();      // speed -> a rougher tone
            fm.channel = SfxTelemetry.FmDepth; fm.source = SfxTelemetrySender.Source.RigidbodySpeed; fm.max = 45f;
            pitch.localPlayers = fm.localPlayers = new[] { craft.engineSound };
            craft.laserSound = Sound("LaserSound", craftGo.transform, laser, 0.6f);
            craft.impactSound = Sound("ImpactSound", craftGo.transform, impact, 0.9f);
            craft.pickupSound = Sound("PickupSound", craftGo.transform, coin, 0.6f);

            // the music: the song as SFX FORGE sent it (DemoSong.bytes: 16 sounds, then the packed song). Its layers
            // come in with the speed – at rest only the kick, flat out the lead and the effects too.
            byte[] raw = AssetDatabase.LoadAssetAtPath<TextAsset>(Root + "/DemoSong.bytes").bytes;
            var sounds = new SfxPatch[SfxProtocol.Slots];
            for (int i = 0; i < sounds.Length; i++) SfxPatch.TryUnpack(raw, i * SfxPatch.Bytes, out sounds[i]);
            var packed = new byte[SfxProtocol.SongBytes];
            Array.Copy(raw, sounds.Length * SfxPatch.Bytes, packed, 0, packed.Length);
            var song = AssetDatabase.LoadAssetAtPath<SfxSong>(Root + "/DemoSong.asset");
            if (song == null) { song = ScriptableObject.CreateInstance<SfxSong>(); AssetDatabase.CreateAsset(song, Root + "/DemoSong.asset"); }
            song.Set(sounds, packed);
            EditorUtility.SetDirty(song);

            var musicGo = new GameObject("Music");
            var musicSource = musicGo.AddComponent<AudioSource>();
            musicSource.playOnAwake = false; musicSource.spatialBlend = 0f;
            var music = musicGo.AddComponent<SfxMusicPlayer>();
            music.song = song; music.volume = 0.45f; music.intensity = 0f;
            var heat = craftGo.AddComponent<SfxTelemetrySender>();
            heat.channel = SfxTelemetry.Intensity; heat.source = SfxTelemetrySender.Source.RigidbodySpeed; heat.max = 22f;
            heat.musicPlayers = new[] { music };
            craft.music = music;

            var boltGo = Shape(PrimitiveType.Cube, "BoltTemplate", Vector3.zero, new Vector3(0.12f, 0.12f, 1.4f), beam);
            UnityEngine.Object.DestroyImmediate(boltGo.GetComponent<Collider>());
            craft.boltPrefab = boltGo.AddComponent<Bolt>();
            boltGo.SetActive(false);

            // things to hit and to collect (the same places every time)
            var rng = new System.Random(4);
            Func<float> place = () => (float)(rng.NextDouble() * 60.0 - 30.0);
            var crates = new GameObject("Crates").transform;
            for (int i = 0; i < 14; i++)
            {
                var c = Shape(PrimitiveType.Cube, "Crate " + (i + 1), new Vector3(place(), 0.75f, place()), Vector3.one * 1.5f, crate, crates);
                if (c.transform.position.magnitude < 5f) c.transform.position += new Vector3(8f, 0f, 8f);
                c.AddComponent<Rigidbody>().mass = 1.5f;
            }
            var pickups = new GameObject("Pickups").transform;
            for (int i = 0; i < 8; i++)
            {
                var p = Shape(PrimitiveType.Cube, "Pickup " + (i + 1), new Vector3(place(), 0.8f, place()), Vector3.one * 0.7f, gold, pickups);
                p.transform.rotation = Quaternion.Euler(45f, 0f, 45f);
                p.GetComponent<Collider>().isTrigger = true;
                p.AddComponent<Pickup>();
            }

            var cam = new GameObject("Main Camera") { tag = "MainCamera" };
            cam.AddComponent<Camera>().backgroundColor = new Color(0.04f, 0.06f, 0.09f);
            cam.GetComponent<Camera>().clearFlags = CameraClearFlags.SolidColor;
            cam.AddComponent<AudioListener>();
            cam.transform.position = new Vector3(0f, 9f, -12f);
            cam.AddComponent<FollowCamera>().target = craftGo.transform;

            // the device: its top line says where the game is, its encoder 1 is the engine's power
            var linkGo = new GameObject("SFX Forge Link");
            linkGo.AddComponent<SfxForgeLink>().textOnStart = "HOVER DEMO";
            var binding = linkGo.AddComponent<SfxEncoderBinding>();
            binding.encoder = SfxEncoderBinding.Encoder.Encoder1; binding.min = 8f; binding.max = 60f;
            UnityEventTools.AddPersistentListener(binding.onValue, craft.SetThrust);

            EditorSceneManager.SaveScene(scene, ScenePath);
            EditorBuildSettings.scenes = new[] { new EditorBuildSettingsScene(ScenePath, true) };
            AssetDatabase.SaveAssets();
            Debug.Log("SFXDEMO: scene built at " + ScenePath);
        }

        /// <summary>What can be checked in the editor without pressing Play (Unity -batchmode -executeMethod).</summary>
        public static void SelfTest()
        {
            int failed = 0;
            Action<bool, string> ok = (c, t) => { Debug.Log("SFXDEMO: " + (c ? "ok    " : "FAIL  ") + t); if (!c) failed++; };
            try
            {
                Build();
                ok(SfxNative.Available, "the native library loads inside Unity");
                var laser = AssetDatabase.LoadAssetAtPath<SfxPreset>($"{Root}/Presets/Laser.asset");
                ok(laser != null && laser.patch.engine == SfxEngine.SciFiFm && laser.patch.baseFreq == 70, "a preset asset keeps its patch");
                AudioClip clip = laser != null ? laser.Bake() : null;
                ok(clip != null && clip.samples > 4800 && clip.channels == 2, $"SfxPreset.Bake gives a clip ({(clip != null ? clip.length : 0f):0.00} s)");
                AudioClip wav = laser != null ? SfxForgeEditorLink.BakeWav(laser) : null;
                ok(wav != null && wav.length > 0.1f, "Bake WAV writes a file Unity imports as an AudioClip");
                AssetDatabase.DeleteAsset($"{Root}/Presets/Laser.wav");
                var made = SfxForgeEditorLink.CreatePreset(SfxPatch.Default, Root + "/Presets", "SelfTest");
                ok(made != null && AssetDatabase.Contains(made), "a patch from the device becomes a preset asset");
                AssetDatabase.DeleteAsset(AssetDatabase.GetAssetPath(made));
                string[] outs = MidiPorts.Outputs();
                Debug.Log("SFXDEMO: MIDI out ports: " + string.Join(", ", outs) + " | in: " + string.Join(", ", MidiPorts.Inputs()));
                if (SfxLink.Find(outs, "GS Wavetable") >= 0)
                {
                    bool sent = SfxLink.Shared.Open(null, "GS Wavetable") && SfxLink.Shared.SendPatch(0, SfxPatch.Default) && SfxLink.Shared.SendTelemetry(SfxTelemetry.Pitch, 0.5f);
                    SfxLink.Shared.Close();
                    ok(sent, "MIDI out from inside Unity: a patch as SysEx and a telemetry CC");
                }
                var demoSong = AssetDatabase.LoadAssetAtPath<SfxSong>(Root + "/DemoSong.asset");
                ok(demoSong != null && demoSong.IsValid && demoSong.Bpm == 120 && demoSong.Chain == 2 && demoSong.Layer(7) > 0.5f && demoSong.Layer(2) == 0f && demoSong.sounds[15].engine == SfxEngine.Wavetable,
                   "the song asset holds what the firmware sent (120 BPM, a chain of 2, the lead a late layer, the kick always, strings on pad 16)");
                var made2 = SfxForgeEditorLink.CreateSong(demoSong.sounds, demoSong.data, Root + "/Presets", "SelfTestSong");
                ok(made2 != null && made2.IsValid, "a song from the device becomes a Song asset");
                AssetDatabase.DeleteAsset(AssetDatabase.GetAssetPath(made2));
                var scene = EditorSceneManager.OpenScene(ScenePath);
                var craft = UnityEngine.Object.FindFirstObjectByType<HoverCraft>();
                ok(scene.IsValid() && craft != null && craft.engineSound != null && craft.engineSound.preset != null && craft.boltPrefab != null, "the saved scene opens with its references");
            }
            catch (Exception e) { Debug.LogError("SFXDEMO: FAIL  exception: " + e); failed++; }
            Debug.Log(failed == 0 ? "SFXDEMO: PASSED" : $"SFXDEMO: FAILED ({failed})");
            EditorApplication.Exit(failed == 0 ? 0 : 1);
        }
    }
}
