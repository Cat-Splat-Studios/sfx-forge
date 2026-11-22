// MIDI: what can be tried without a device - the ports, an output, an input.
// Part of the test program: Program.cs has Main, which calls these in order.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using CatSplat.SfxForge;

static partial class Tests
{
    // MIDI: what can be tried without a device
    static void Midi()
    {
        string[] ins = MidiPorts.Inputs();
        string[] outs = MidiPorts.Outputs();
        Console.WriteLine("  ports in:  " + (ins.Length > 0 ? string.Join(", ", ins) : "(none)"));
        Console.WriteLine("  ports out: " + (outs.Length > 0 ? string.Join(", ", outs) : "(none)"));
        if (SfxLink.Find(outs, "GS Wavetable") >= 0)
        {
            using var link = new SfxLink();
            bool open = link.Open(null, "GS Wavetable");
            bool sent =
                open
                && link.SendTelemetry(SfxTelemetry.Pitch, 0.5f)
                && !link.SendTelemetry(SfxTelemetry.Pitch, 0.5f)
                && link.SendText("TEST")
                && link.SendPatch(0, SfxPatch.Default);
            Ok(sent, "output: opens, sends CC (once per change) and SysEx");
        }
        else
        {
            Console.WriteLine("  ----  no built-in synth port: output not tried");
        }

        if (ins.Length > 0)
        {
            var port = MidiIn.TryOpen(0);
            Thread.Sleep(100);
            bool opened = port != null;
            if (port != null)
            {
                port.Dispose();
            }

            Ok(opened, $"input: '{ins[0]}' opens and closes cleanly");
        }

        Console.WriteLine(
            "  ----  receiving MIDI is not covered here: it needs the device. See "
                + "unity/Demo/Assets/Showcase/Tests/LinkTakes.cs"
        );
    }
}
