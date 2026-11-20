using System;
using System.Collections;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Inspector.Harness
{
    /// <summary>
    /// Photographs a VisualElement tree: it is put into a runtime panel (a UIDocument whose PanelSettings render into a
    /// RenderTexture) while the editor is in play mode, and the texture is written as a PNG into out/inspector-shots at the repository's root.
    /// The picture is the same element tree and stylesheet the Inspector hosts – not a photograph of the Inspector.
    /// </summary>
    public static class Shot
    {
        public const int MaxHeight = 1400;
        public static readonly Color InspectorGrey = new Color32(56, 56, 56, 255);
        public static string Folder => Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "..", "out", "inspector-shots"));

        /// <summary>Call in play mode. then: instead of a picture, this is called with the element once it is laid out. build: the element to photograph, made when the panel exists. width: as wide as an Inspector would be.</summary>
        public static IEnumerator Take(string name, int width, Func<VisualElement> build, Action<VisualElement> then = null)
        {
            var rt = new RenderTexture(width, MaxHeight, 24, RenderTextureFormat.ARGB32);
            rt.Create();
            var settings = ScriptableObject.CreateInstance<PanelSettings>();
            settings.themeStyleSheet = ScriptableObject.CreateInstance<ThemeStyleSheet>();
            settings.scaleMode = PanelScaleMode.ConstantPixelSize;
            settings.scale = 1f;
            settings.targetTexture = rt;
            settings.clearColor = true;
            settings.colorClearValue = InspectorGrey;

            var go = new GameObject("Shot " + name);
            var doc = go.AddComponent<UIDocument>();
            doc.panelSettings = settings;
            yield return null;

            var root = doc.rootVisualElement;
            var font = EditorGUIUtility.Load("Fonts/Inter/Inter-Regular.ttf") as Font;      // the Inspector's own font
            if (font == null) font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            root.style.unityFontDefinition = FontDefinition.FromFont(font);
            root.style.fontSize = 12;
            root.style.color = new Color(0.82f, 0.82f, 0.82f);
            var host = new VisualElement();                 // where the Inspector would put the editor: its margins
            host.style.width = width;
            host.style.paddingLeft = 6; host.style.paddingRight = 6; host.style.paddingTop = 6; host.style.paddingBottom = 6;
            host.style.alignSelf = Align.FlexStart;
            host.Add(build());
            root.Add(host);
            for (int i = 0; i < 6; i++) yield return null;
            yield return new WaitForEndOfFrame();

            if (then != null)                               // not a picture: something to do with the element in its panel
            {
                then(host[0]);
                UnityEngine.Object.Destroy(go);
                yield return null;
                rt.Release();
                UnityEngine.Object.Destroy(rt);
                UnityEngine.Object.Destroy(settings);
                yield break;
            }

            int height = Mathf.Clamp(Mathf.CeilToInt(host.layout.height), 8, MaxHeight);
            var before = RenderTexture.active;
            RenderTexture.active = rt;
            var tex = new Texture2D(width, height, TextureFormat.RGBA32, false);
            tex.ReadPixels(new Rect(0, MaxHeight - height, width, height), 0, 0);   // (the panel's top is the texture's top)
            tex.Apply();
            RenderTexture.active = before;
            Directory.CreateDirectory(Folder);
            string path = Path.Combine(Folder, $"{name}-{width}.png");
            File.WriteAllBytes(path, tex.EncodeToPNG());
            Debug.Log($"SHOT {name}-{width}.png {width}x{height} (laid out {host.layout.width:0}x{host.layout.height:0})");

            UnityEngine.Object.Destroy(tex);
            UnityEngine.Object.Destroy(go);
            yield return null;
            rt.Release();
            UnityEngine.Object.Destroy(rt);
            UnityEngine.Object.Destroy(settings);
        }
    }
}
