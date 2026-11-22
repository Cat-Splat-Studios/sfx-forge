using System;
using System.Collections.Generic;
using System.IO;
using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;

namespace Inspector.Harness
{
    /// <summary>
    /// The number an asset keeps of the asset format that wrote it, with real asset files: copies of this project's
    /// own (which have no such line) are written into a folder of their own, as they are and with "format: 2" put
    /// in, and Unity imports them. The folder is deleted afterwards.
    /// </summary>
    public class SfxAssetFormatTests
    {
        const string Folder = "Assets/FormatTemp";

        // (the song copied here is the one from before the asset kept its
        // format: a file without the line, holding a format-5 song)
        const string JumpPath = "Assets/Data/Presets/Jump.asset";
        const string DemoPath = "Assets/Data/DemoSong-format5.asset";
        const string Demo6Path = "Assets/Data/DemoSong.asset";
        const string After = "  m_EditorClassIdentifier:"; // the line the new ones are put after

        readonly List<string> warnings = new List<string>();

        void OnLog(string text, string stack, LogType type)
        {
            if (type == LogType.Warning)
            {
                warnings.Add(text);
            }
        }

        int Warned(string name) =>
            warnings.FindAll(w => w.Contains("'" + name + "'") && w.Contains("newer SFX Forge")).Count;

        [SetUp]
        public void SetUp()
        {
            if (!AssetDatabase.IsValidFolder(Folder))
            {
                AssetDatabase.CreateFolder("Assets", "FormatTemp");
            }

            warnings.Clear();
            Application.logMessageReceived += OnLog;
        }

        [TearDown]
        public void TearDown()
        {
            Application.logMessageReceived -= OnLog;
            AssetDatabase.DeleteAsset(Folder);
        }

        // (a name of its own each run: the warning is said once a session for an asset file)
        static string Unique(string word) => word + Guid.NewGuid().ToString("N").Substring(0, 8);

        /// <summary>
        /// A copy of an asset file under another name, with lines put in after its class line; imported and loaded.
        /// </summary>
        static T Copy<T>(string source, string name, string linesIn, string folder = Folder)
            where T : ScriptableObject
        {
            string text = File.ReadAllText(source).Replace("\r\n", "\n");
            Assert.IsFalse(
                text.Contains("\n  format:"),
                source + " is an asset from before the format number was kept"
            );
            string was = "  m_Name: " + Path.GetFileNameWithoutExtension(source) + "\n";
            StringAssert.Contains(was, text);
            text = text.Replace(was, "  m_Name: " + name + "\n");
            if (linesIn != null)
            {
                int at = text.IndexOf(After, StringComparison.Ordinal);
                Assert.GreaterOrEqual(at, 0);
                at = text.IndexOf('\n', at) + 1;
                text = text.Insert(at, linesIn);
            }

            string path = folder + "/" + name + ".asset";
            File.WriteAllText(path, text);
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceSynchronousImport | ImportAssetOptions.ForceUpdate);
            var asset = AssetDatabase.LoadAssetAtPath<T>(path);
            Assert.IsNotNull(asset, path);
            return asset;
        }

        [Test]
        public void AnAssetFileWithoutTheLineIsFormatOne()
        {
            Assert.AreEqual(1, SfxPreset.CurrentFormat);
            Assert.AreEqual(1, SfxAssetFormat.Current);
            Assert.AreEqual(
                2,
                SfxSong.CurrentFormat,
                "a song asset counts its own format: 2 since the packed song has format 6"
            );
            var jump = AssetDatabase.LoadAssetAtPath<SfxPreset>(JumpPath);
            var demo = AssetDatabase.LoadAssetAtPath<SfxSong>(DemoPath);
            var demo6 = AssetDatabase.LoadAssetAtPath<SfxSong>(Demo6Path);
            Assert.AreEqual(1, jump.Format);
            Assert.IsFalse(jump.IsFromNewerVersion);
            Assert.AreEqual(1, demo.Format);
            Assert.IsFalse(demo.IsFromNewerVersion);
            Assert.AreEqual(5, demo.PackedFormat);
            Assert.AreEqual(2, demo6.Format, "the demo song's asset says 2: it holds a format-6 song");
            Assert.IsFalse(demo6.IsFromNewerVersion);
            Assert.AreEqual(6, demo6.PackedFormat);
            StringAssert.Contains("\n  format: 2\n", File.ReadAllText(Demo6Path).Replace("\r\n", "\n"));

            string name = Unique("Plain");
            var copy = Copy<SfxPreset>(JumpPath, name, null);
            Assert.AreEqual(1, copy.Format);
            Assert.IsFalse(copy.IsFromNewerVersion);
            Assert.IsTrue(copy.patch.Equals(jump.patch));
            var songCopy = Copy<SfxSong>(DemoPath, Unique("PlainSong"), null);
            Assert.AreEqual(1, songCopy.Format);
            Assert.IsTrue(songCopy.IsValid);
            Assert.AreEqual(0, Warned(name), "nothing is said of an asset of this version");
            Assert.IsFalse(((SfxPresetPanel)SfxPresetEditor.Build(new SerializedObject(copy))).NewerBanner.Shown);
        }

        [Test]
        public void AnAssetFileThatSaysFormatTwoIsFromANewerVersionAndSaysSoOnce()
        {
            var jump = AssetDatabase.LoadAssetAtPath<SfxPreset>(JumpPath);
            string name = Unique("Later");
            var later = Copy<SfxPreset>(JumpPath, name, "  format: 2\n  somethingThisVersionDoesNotKnow: 7\n");
            Assert.AreEqual(2, later.Format);
            Assert.IsTrue(later.IsFromNewerVersion);
            Assert.IsTrue(later.patch.Equals(jump.patch), "what this version reads of it is all there");
            Assert.AreEqual(1, Warned(name), "one warning when it is loaded: " + string.Join(" / ", warnings));

            // loaded again (imported again), asked again: nothing more is said
            AssetDatabase.ImportAsset(
                AssetDatabase.GetAssetPath(later),
                ImportAssetOptions.ForceSynchronousImport | ImportAssetOptions.ForceUpdate
            );
            later = AssetDatabase.LoadAssetAtPath<SfxPreset>(Folder + "/" + name + ".asset");
            SfxAssetFormat.WarnIfNewer(later, later.Format);
            Assert.AreEqual(2, later.Format);
            Assert.AreEqual(1, Warned(name), "and no second one: " + string.Join(" / ", warnings));

            // the Inspector says it too
            var panel = (SfxPresetPanel)SfxPresetEditor.Build(new SerializedObject(later));
            Assert.IsTrue(panel.NewerBanner.Shown);
            StringAssert.Contains("asset format 2", panel.NewerBanner.Small);

            string songName = Unique("LaterSong");

            // (a song asset's own number: 2 is this version's, 3 a later one's)
            var song = Copy<SfxSong>(DemoPath, songName, "  format: 3\n");
            Assert.AreEqual(3, song.Format);
            Assert.IsTrue(song.IsFromNewerVersion);
            Assert.IsTrue(song.IsValid, "the song this version reads of it plays");
            Assert.AreEqual(120, song.Bpm);
            Assert.AreEqual(1, Warned(songName));
            StringAssert.Contains("asset format 3, this version knows 2", warnings.Find(w => w.Contains(songName)));
            string twoName = Unique("SongOfTwo");
            var two = Copy<SfxSong>(DemoPath, twoName, "  format: 2\n");
            Assert.AreEqual(2, two.Format);
            Assert.IsFalse(two.IsFromNewerVersion);
            Assert.AreEqual(0, Warned(twoName), "a song asset that says 2 is of this version: nothing is said");
        }

        [Test]
        public void SavingANewerAssetHereLosesWhatThisVersionDoesNotKnow()
        {
            string name = Unique("Saved");
            var later = Copy<SfxPreset>(
                JumpPath,
                name,
                "  format: 2\n  savedBy: 2\n  somethingThisVersionDoesNotKnow: 7\n"
            );
            string path = AssetDatabase.GetAssetPath(later);
            StringAssert.Contains("somethingThisVersionDoesNotKnow", File.ReadAllText(path));
            StringAssert.Contains("  savedBy: 2", File.ReadAllText(path));
            later.patch.level = 77;
            EditorUtility.SetDirty(later);
            AssetDatabase.SaveAssetIfDirty(later);
            string saved = File.ReadAllText(path);
            StringAssert.DoesNotContain(
                "somethingThisVersionDoesNotKnow",
                saved,
                "what the banner warns of: Unity writes only what this version's class has"
            );
            StringAssert.Contains("  format: 2", saved, "the number stays as the file had it");
            StringAssert.Contains(
                "  savedBy: 1",
                saved,
                "and the file says which version wrote it back: a newer one can tell it was cut down"
            );
            StringAssert.DoesNotContain("  savedBy: 2", saved);
            StringAssert.Contains("  level: 77", saved);
        }

        [Test]
        public void ANewAssetIsWrittenWithThisVersionsNumber()
        {
            var made = SfxForgeEditorLink.CreatePreset(SfxPatch.Default, Folder, "Made");
            string text = File.ReadAllText(AssetDatabase.GetAssetPath(made));
            StringAssert.Contains("  format: 1", text);
            StringAssert.Contains("  savedBy: 1", text);
            Assert.AreEqual(1, made.Format);

            // a song asset's number follows the packed song: 2 for a format-6 song (firmware 1.6.0), 1 for an older one
            var song = SfxForgeEditorLink.CreateSong(
                SfxSongPanelTests.Demo().sounds,
                SfxSongPanelTests.Demo().data,
                Folder,
                "MadeSong"
            );
            string songText = File.ReadAllText(AssetDatabase.GetAssetPath(song));
            StringAssert.Contains("  format: 2", songText);
            StringAssert.Contains("  savedBy: 2", songText);
            Assert.AreEqual(2, song.Format);
            Assert.AreEqual(6, song.PackedFormat);
            var older = SfxForgeEditorLink.CreateSong(
                SfxSongPanelTests.Demo5().sounds,
                SfxSongPanelTests.Demo5().data,
                Folder,
                "MadeOlderSong"
            );
            string olderText = File.ReadAllText(AssetDatabase.GetAssetPath(older));
            StringAssert.Contains("  format: 1", olderText);
            StringAssert.Contains("  savedBy: 2", olderText);
            Assert.AreEqual(1, older.Format);
            Assert.AreEqual(5, older.PackedFormat);

            // Set writes the number: a format-6 song into the older asset makes it 2, a format-5 song back makes it 1
            older.Set(song.sounds, song.data);
            Assert.AreEqual(2, older.Format);
            older.Set(song.sounds, SfxSongPanelTests.Demo5().data);
            Assert.AreEqual(1, older.Format);

            // … and a format-6 song that reached the bytes past Set (the
            // Inspector's undo, a script) is written with 2 all the same
            older.data = (byte[])song.data.Clone();
            EditorUtility.SetDirty(older);
            AssetDatabase.SaveAssetIfDirty(older);
            StringAssert.Contains("  format: 2", File.ReadAllText(AssetDatabase.GetAssetPath(older)));
            Assert.AreEqual(2, older.Format);
        }

        [Test]
        public void TwoNewerAssetsOfTheSameNameInTwoFoldersAreWarnedOfEach()
        {
            string name = Unique("Twin");
            string sub = Folder + "/Other";
            AssetDatabase.CreateFolder(Folder, "Other");
            var here = Copy<SfxPreset>(JumpPath, name, "  format: 2\n");
            Assert.AreEqual(1, Warned(name));
            var there = Copy<SfxPreset>(JumpPath, name, "  format: 2\n", sub);
            Assert.AreNotSame(here, there);
            Assert.AreEqual(2, Warned(name), "one for each file: " + string.Join(" / ", warnings));
            for (int i = 0; i < 2; i++)
            {
                SfxAssetFormat.WarnIfNewer(here, here.Format);
                SfxAssetFormat.WarnIfNewer(there, there.Format);
            }

            AssetDatabase.ImportAsset(
                sub + "/" + name + ".asset",
                ImportAssetOptions.ForceSynchronousImport | ImportAssetOptions.ForceUpdate
            );
            Assert.AreEqual(2, Warned(name), "and no more than that");
        }

        [Test]
        public void TheWarningIsSaidOnceForAnAsset()
        {
            var one = ScriptableObject.CreateInstance<SfxPreset>();
            one.name = Unique("Once");
            var same = ScriptableObject.CreateInstance<SfxPreset>();
            same.name = Unique("Never");
            for (int i = 0; i < 3; i++)
            {
                SfxAssetFormat.WarnIfNewer(one, 3);
                SfxAssetFormat.WarnIfNewer(same, 1);
            }

            Assert.AreEqual(1, Warned(one.name));
            Assert.AreEqual(0, Warned(same.name));
            StringAssert.Contains("asset format 3", warnings.Find(w => w.Contains(one.name)));
            UnityEngine.Object.DestroyImmediate(one);
            UnityEngine.Object.DestroyImmediate(same);
        }
    }
}
