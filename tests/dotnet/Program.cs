// Tests of the Unity package's engine-independent code (unity/com.catsplat.sfxforge/Runtime/Core), without Unity.
// Run after `sh build.sh` (it reads out/midi.json and out/audio.wav):
//   dotnet run -c Release --project tests/dotnet -- <repo root>
//
// One program, one class Tests in several files. A check is a call of Ok(condition, what it says): it prints
// "  ok    ..." or "  FAIL  ..." and counts the failures; the last line is PASSED or FAILED (n).
//   Program.cs          Main: the order of the sections, and what they share
//   Helpers.cs          what the checks measure and build with
//   ProtocolTests.cs    the patch, the messages, the firmware's EXPORT
//   IdentityTests.cs    the identity of a device, and what is sent to whom
//   ToolsTests.cs       generators, MUTATE, variation order, levels, a patch as text
//   SongFormatTests.cs  the packed song in its formats and grids (needs the native library, as all below)
//   EngineTests.cs      bakes, a voice, the tools with the engine, envelopes and effects
//   MusicTests.cs       the song of the test performance, played here
//   MidiTests.cs        what MIDI can be tried without a device
// The sections run in the order of Main and some use what an earlier one made (the fields below): to run one alone,
// keep those before it that fill what it reads.
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
    static int failed;

    // One check: prints its line, counts a failure.
    static void Ok(bool c, string t)
    {
        Console.WriteLine((c ? "  ok    " : "  FAIL  ") + t);
        if (!c)
        {
            failed++;
        }
    }

    // ---- what the sections share ----------------------------------------------------------------------------------

    // Main fills these: the repository's root, the two files of the test performance (out/, written by build.sh),
    // and the one random generator - its numbers are drawn in the order of the sections.
    static string root;
    static string midiFile;
    static string wavFile;
    static Random rng;

    // PatchAndProtocol makes four sounds that later sections play.
    static SfxPatch laser;
    static SfxPatch hum;
    static SfxPatch thud;
    static SfxPatch saw;

    // FirmwareExport reads the performance: its MIDI messages and the left channel of its sound (null: no file).
    static List<byte[]> msgs;
    static float[] deviceAudioLeft;

    // Format5Fixture reads the format-5 song of firmware 1.5.0: its 16 sounds, the packed song, the hashes it
    // rendered to then. OlderFormats makes its format-6 form and its forms in the formats 1 to 4.
    static byte[] fixSounds;
    static byte[] fix5;
    static ulong[] wantHash;
    static byte[] fix6;
    static List<byte[]> olderFormats;

    static int Main(string[] args)
    {
        root = args.Length > 0 ? args[0] : ".";
        midiFile = Path.Combine(root, "out", "midi.json");
        wavFile = Path.Combine(root, "out", "audio.wav");
        rng = new Random(7);

        PatchAndProtocol();
        Identity();
        FirmwareExport();
        Generators();
        MutateWithAmountAndLocks();
        VariationOrderAndStamp();
        Levels();
        PatchAsTextAndSyx();

        // the native engine: everything below needs it
        Ok(SfxNative.Available, "sfxforge.dll loads");
        if (!SfxNative.Available)
        {
            Console.WriteLine("FAILED");
            return 1;
        }

        Format5Fixture();
        OlderFormats();
        Format6();
        IsPlayable();
        RaisingTheGrid();
        AFinerGridSoundsFiner();
        LoweringTheGrid();
        BakesAndASingleVoice();
        ToolsWithTheEngine();
        EnvelopesWavetableEffects();
        Music();
        Midi();

        Console.WriteLine(failed > 0 ? $"FAILED ({failed})" : "PASSED");
        return failed > 0 ? 1 : 0;
    }
}
