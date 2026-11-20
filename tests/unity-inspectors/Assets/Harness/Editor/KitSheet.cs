using CatSplat.SfxForge;
using CatSplat.SfxForge.Editor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Inspector.Harness
{
    /// <summary>Every control of the kit on one sheet, with made-up content: what the song inspector and the window are built from.</summary>
    public static class KitSheet
    {
        static readonly string[] Names = { "TIMPANI", "SNARE", "KICK", "COWBELL", "PIZZICATO", "BRASS", "FM BASS", "FM KEYS", "HARP", "CELESTA", "ORGAN", "WT SWEEP", "HI-HAT", "BELLS", "CHOIR", "STRINGS" };
        static readonly int[] Engines = { 0, 3, 0, 0, 4, 4, 2, 2, 4, 2, 4, 4, 3, 4, 4, 4 };

        public static VisualElement Build()
        {
            var root = SfxUI.Panel();
            var header = new SfxHeader();
            header.Set("7", "FM BASS", "SCI-FI FM", SfxColors.Engine(SfxEngine.SciFiFm));
            header.SetLink(true);
            root.Add(header);

            var tabs = new PageTabs("SOUND", "ENV", "LFO", "FX", "SEQ", "LINK") { Accent = SfxColors.Engine(SfxEngine.SciFiFm) };
            tabs.SetIndex(4);
            root.Add(tabs);

            var pads = new PadGrid();
            for (int i = 0; i < PadGrid.Pads; i++)
                pads.SetPad(i, Names[i], SfxColors.Engines[Engines[i]], i == 6 ? PadState.Selected : i == 2 || i == 12 ? PadState.Lit : i == 15 ? PadState.Empty : PadState.Dim);
            root.Add(pads);

            var row = SfxUI.Box("sfx-row");
            row.style.flexWrap = Wrap.Wrap;
            for (int i = 0; i < 4; i++)
            {
                var tile = new ValueTile(i + 1, false, ValueTile.Look.Row);
                tile.style.minWidth = 130; tile.style.marginRight = 4; tile.style.marginBottom = 4;
                if (i == 0) tile.Show("PATTERN", "A", 1f / 8f, false, SfxColors.Pattern);
                if (i == 1) tile.Show("CHAIN", "A-D", 4f / 8f, false, SfxColors.Pattern);
                if (i == 2) tile.Show("INTENSITY", "62%", 0.62f, false, SfxColors.Ember);
                if (i == 3) tile.Show("PAN", "L12", 52f / 127f, true, SfxColors.Engine(SfxEngine.SciFiFm));
                row.Add(tile);
            }
            root.Add(row);

            var patterns = SfxUI.Box("sfx-row");
            patterns.style.flexWrap = Wrap.Wrap;
            for (int k = 0; k < 8; k++)
            {
                var tile = new PatternTile((char)('A' + k));
                var counts = new int[16];
                for (int i = 0; i < 16; i++) counts[i] = k < 5 ? (i % 4 == 0 ? 3 + k : i % 2 == 0 ? 1 + k / 2 : (i * 7 + k) % 3) : 0;
                tile.Set(counts, k == 0 ? PatternState.Playing : k == 1 ? PatternState.Selected : k < 5 ? PatternState.Used : PatternState.Unused);
                patterns.Add(tile);
            }
            root.Add(patterns);

            for (int s = 0; s < 3; s++)
            {
                var line = SfxUI.Box("sfx-row");
                line.style.marginBottom = 2;
                var name = SfxUI.Text(Names[s + 1], "sfx-tile__label");
                name.style.width = 56; name.style.color = SfxColors.Engines[Engines[s + 1]];
                var steps = new StepRow { Accent = SfxColors.Engines[Engines[s + 1]], Length = 16, Head = 4, Dimmed = s == 2 };
                for (int i = 0; i < 16; i++) steps.Level[i] = s == 0 ? (i % 8 == 4 ? 0.8f : 0f) : s == 1 ? (i % 4 == 0 ? 1f : i == 10 ? 0.5f : 0f) : (i % 2 == 0 ? 0.6f : 0f);
                var fader = new SfxFader(); fader.style.width = 44; fader.style.marginLeft = 6;
                fader.SetLevel(0.7f + s * 0.1f, 100f / 127f, SfxColors.Engines[Engines[s + 1]]);
                var pan = new SfxFader(); pan.style.width = 44; pan.style.marginLeft = 6;
                pan.SetPan(0.3f + s * 0.2f, SfxColors.Text);
                line.Add(name); line.Add(steps); line.Add(fader); line.Add(pan);
                root.Add(line);
            }

            var footer = new SfxFooter();
            footer.Set("WAITING FOR A GAME", SfxColors.Dim, "the knobs send CC 20-23 to it");
            root.Add(footer);

            var keys = new SoftKeyRow();
            keys.Add("PLAY", null).Lit = true;
            keys.Add("COPY", null); keys.Add("CLR PAT", null); keys.Add("DEMO", null); keys.Add("SEND", null).Off = true; keys.Add("SAVE", null);
            root.Add(keys);
            return root;
        }
    }
}
