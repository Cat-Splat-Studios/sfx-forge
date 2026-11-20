using System.Collections;
using CatSplat.SfxForge;
using SfxShowcase.Core;
using UnityEngine;

namespace SfxShowcase.Audio
{
    /// <summary>
    /// USE 3 OF 3 – ADAPTIVE MUSIC. One <see cref="SfxMusicPlayer"/> plays one <see cref="SfxSong"/>; this component
    /// turns the game into the five things the player understands:
    ///
    ///  • <c>intensity</c> (0..1) – how full the band is. Every instrument of the song has a threshold and plays only at
    ///    or above it. Here it follows the player's speed: fast in, slow out.
    ///  • <c>CuePattern(n)</c> – another pattern of the song from the next bar line: one per section of the level.
    ///    A song with an arrangement (the demo song has one, 16 bars) treats a cue as "jump to the next bar of the
    ///    arrangement with that pattern" and then walks on through the arrangement, so one cue lasts until the arrangement moves on (a bar, or two where a pattern repeats). To HOLD
    ///    a pattern, this component cues it again on every bar line while the player is in the section.
    ///  • <c>PlaySound(instrument, velocity, note)</c> – a note by hand on one of the song's instruments, on top of the
    ///    music: the bell answer at a checkpoint, the fanfare at the goal.
    ///  • <c>volume</c>, <c>SetTrackVolume</c>, <c>SetMaster</c> – the mix: a dip when hurt, brass forward and a bigger
    ///    room for the fanfare.
    ///  • <c>Stop()</c> / <c>Play()</c> – the song ends at the goal and starts again with a new run.
    ///
    /// Gameplay never calls this: it listens to <see cref="GameEvents"/> and reads the player and the session.
    /// </summary>
    public sealed class MusicDirector : MonoBehaviour
    {
        const int PatternA = 0, PatternB = 1, PatternC = 2, PatternD = 3, PatternE = 4, PatternF = 5;

        [Header("Wired by AudioInstaller")]
        [Tooltip("The SfxMusicPlayer that plays the song.")]
        [SerializeField] SfxMusicPlayer music;

        [Tooltip("The player whose speed drives the intensity.")]
        [SerializeField] PlayerBase player;

        [Tooltip("The run: the title card holds the music at the title intensity.")]
        [SerializeField] GameSessionBase session;

        [Header("Intensity follows speed")]
        [Tooltip("Seconds for the intensity to rise across its whole range: layers arrive with the rush.")]
        [Min(0.01f)] [SerializeField] float riseSeconds = 0.6f;

        [Tooltip("Seconds for the intensity to fall across its whole range: layers linger after the rush.")]
        [Min(0.01f)] [SerializeField] float fallSeconds = 2.5f;

        [Tooltip("The fixed intensity on the title card.")]
        [Range(0f, 1f)] [SerializeField] float titleIntensity = 0.15f;

        [Tooltip("The music's volume (SfxMusicPlayer.volume) when nothing ducks it. Effects sit above it.")]
        [Range(0f, 1f)] [SerializeField] float musicVolume = 0.6f;

        [Header("Sections cue patterns")]
        [Tooltip("What the music does in each section: the pattern cued on entering it and the least intensity inside it.")]
        [SerializeField] SectionCue[] sectionCues =
        {
            new SectionCue(SectionId.SoftStart, PatternA, 0.15f),
            new SectionCue(SectionId.RollingSines, PatternB, 0.25f),
            new SectionCue(SectionId.FullCycle, PatternC, 0.40f),
            new SectionCue(SectionId.WooferSteps, PatternD, 0.40f),
            new SectionCue(SectionId.StaticField, PatternE, 0.20f),
            new SectionCue(SectionId.SplitSignal, PatternC, 0.45f),
            new SectionCue(SectionId.Overdrive, PatternD, 0.70f),
            new SectionCue(SectionId.TheResonator, SectionCue.KeepPattern, 0f),
        };

        [Tooltip("The pattern cued when the player leaves Overdrive's launch ramp: the song's turnaround (5 = F).")]
        [Range(0, 7)] [SerializeField] int finalLaunchPattern = PatternF;

        [Header("Hurt: the band flinches")]
        [Tooltip("The music's volume right after a hit, as a part of its normal volume.")]
        [Range(0f, 1f)] [SerializeField] float hurtVolume = 0.5f;

        [Tooltip("Seconds for the volume and the intensity to recover after a hit.")]
        [Min(0.01f)] [SerializeField] float hurtRecoverSeconds = 2f;

        [Tooltip("Seconds the volume takes to dip after a hit. SfxMusicPlayer.volume is applied per audio block without a ramp, so a dip in one step would click.")]
        [Min(0.01f)] [SerializeField] float hurtDipSeconds = 0.05f;

        [Header("Checkpoint: the music answers the fork")]
        [Tooltip("Notes played on the song's instruments on top of the running song.")]
        [SerializeField] MusicNote[] checkpointAnswer =
        {
            new MusicNote(MusicKit.Bells, Pitch.E5, 0f, 0f, 0.9f),
            new MusicNote(MusicKit.Bells, Pitch.B5, 1f, 0f, 0.9f),
            new MusicNote(MusicKit.Bells, Pitch.E6, 2f, 0f, 1f),
        };

        [Tooltip("The intensity is lifted to at least this after a checkpoint.")]
        [Range(0f, 1f)] [SerializeField] float checkpointIntensity = 0.6f;

        [Tooltip("For how many bars the intensity stays lifted.")]
        [Min(0f)] [SerializeField] float checkpointLiftBars = 1f;

        [Header("Goal: the song stops, a fanfare is played by hand")]
        [Tooltip("The fanfare: brass E G B E an eighth apart, bells and a timpani on the last note, strings holding E underneath.")]
        [SerializeField] MusicNote[] fanfare =
        {
            new MusicNote(MusicKit.Strings, Pitch.E3, 0f, 16f, 0.8f),
            new MusicNote(MusicKit.Brass, Pitch.E4, 0f, 2f, 0.9f),
            new MusicNote(MusicKit.Brass, Pitch.G4, 2f, 2f, 0.9f),
            new MusicNote(MusicKit.Brass, Pitch.B4, 4f, 2f, 0.95f),
            new MusicNote(MusicKit.Brass, Pitch.E5, 6f, 8f, 1f),
            new MusicNote(MusicKit.Bells, Pitch.E6, 6f, 0f, 1f),
            new MusicNote(MusicKit.Timpani, Pitch.E2, 6f, 0f, 1f),
        };

        [Tooltip("The fader (1 = as designed) of every instrument the fanfare uses, so it stands in front. The song's own mix comes back with the next run.")]
        [Range(0f, 1.27f)] [SerializeField] float fanfareTrackVolume = 0.9f;

        [Tooltip("The master reverb's size for the fanfare (0..127; the song's own is restored with the next run).")]
        [Range(0, 127)] [SerializeField] int fanfareReverbSize = 110;

        [Header("Tempo")]
        [Tooltip("Beats a minute assumed when the song asset is missing, for the timing of the phrases.")]
        [Min(40)] [SerializeField] int fallbackBpm = 120;

        const int SixteenthsPerBeat = 4;
        const int DefaultStepsPerBar = 16;
        const float SecondsPerMinute = 60f;

        SectionCue currentCue;
        float intensity;            // what is sent to the music player, smoothed
        float duck;                 // how far the volume is dipped, 0..1: up over hurtDipSeconds, down over hurtRecoverSeconds
        float duckTarget;           // 1 from a hit until the dip is reached, then 0
        int heldPattern = SectionCue.KeepPattern;   // cued again on every bar line; KeepPattern = the song runs on by itself
        int lastBar = -1;           // the bar the hold was last cued in
        float liftedUntil = float.NegativeInfinity;
        bool songEnded;             // the goal stopped the song and changed the mix

        /// <summary>Sets what the director drives and what it reads; called by <see cref="AudioInstaller"/>.</summary>
        public void Wire(SfxMusicPlayer musicPlayer, PlayerBase playerToFollow, GameSessionBase gameSession)
        {
            music = musicPlayer;
            player = playerToFollow;
            session = gameSession;
        }

        /// <summary>The intensity being sent to the music player now, 0..1.</summary>
        public float Intensity => intensity;

        /// <summary>The length of a sixteenth note at the song's tempo, seconds.</summary>
        float SixteenthSeconds
        {
            get
            {
                int bpm = music.song != null && music.song.Bpm > 0 ? music.song.Bpm : fallbackBpm;
                return SecondsPerMinute / bpm / SixteenthsPerBeat;
            }
        }

        /// <summary>The length of a bar (one pattern) at the song's tempo, seconds.</summary>
        float BarSeconds
        {
            get
            {
                int steps = music.song != null && music.song.StepsPerPattern > 0 ? music.song.StepsPerPattern : DefaultStepsPerBar;
                return steps * SixteenthSeconds;
            }
        }

        void Awake()
        {
            if (music == null)
            {
                Debug.LogWarning("MusicDirector has no SfxMusicPlayer: there will be no music.", this);
                enabled = false;
                return;
            }
            currentCue = FindCue(SectionId.SoftStart);
            intensity = titleIntensity;
            music.intensity = intensity;
            music.volume = musicVolume;
        }

        void OnEnable()
        {
            GameEvents.GameStarted += OnGameStarted;
            GameEvents.SectionEntered += OnSectionEntered;
            GameEvents.Respawned += OnRespawned;
            GameEvents.FinalLaunch += OnFinalLaunch;
            GameEvents.Hurt += OnHurt;
            GameEvents.CheckpointReached += OnCheckpointReached;
            GameEvents.GoalReached += OnGoalReached;
        }

        void OnDisable()
        {
            GameEvents.GameStarted -= OnGameStarted;
            GameEvents.SectionEntered -= OnSectionEntered;
            GameEvents.Respawned -= OnRespawned;
            GameEvents.FinalLaunch -= OnFinalLaunch;
            GameEvents.Hurt -= OnHurt;
            GameEvents.CheckpointReached -= OnCheckpointReached;
            GameEvents.GoalReached -= OnGoalReached;
        }

        // ---- intensity: every frame, from the player's speed -------------------------------------------------------------

        void Update()
        {
            float target = TargetIntensity();
            float seconds = target > intensity ? riseSeconds : fallSeconds;
            intensity = Mathf.MoveTowards(intensity, target, Time.deltaTime / seconds);

            // the dip after a hit: quickly down (but not in one step), slowly back
            duck = Mathf.MoveTowards(duck, duckTarget, Time.deltaTime / (duckTarget > duck ? hurtDipSeconds : hurtRecoverSeconds));
            if (duck >= duckTarget) duckTarget = 0f;

            music.intensity = intensity;
            music.volume = musicVolume * Mathf.Lerp(1f, hurtVolume, duck);
            HoldPattern();
        }

        /// <summary>
        /// Keeps the song on <see cref="heldPattern"/>: whenever the song has moved into a new bar, the pattern is cued
        /// again for the next bar line. Without this the demo song's arrangement would take over after one bar.
        /// </summary>
        void HoldPattern()
        {
            int bar = music.Bar;
            if (bar == lastBar) return;
            lastBar = bar;
            if (heldPattern != SectionCue.KeepPattern) music.CuePattern(heldPattern);
        }

        /// <summary>Speed ÷ top speed, but never below the section's floor; held down for a moment after a hit.</summary>
        float TargetIntensity()
        {
            if (session != null && session.Phase == GamePhase.Title) return titleIntensity;
            float target = player != null ? player.Speed01 : 0f;
            target = Mathf.Max(target, currentCue.IntensityFloor);
            if (Time.time < liftedUntil) target = Mathf.Max(target, checkpointIntensity);
            // held down from the moment of the hit (duckTarget), not only once the volume has dipped (duck)
            return target * (1f - Mathf.Max(duck, duckTarget));
        }

        // ---- sections: patterns ----------------------------------------------------------------------------------------------

        void OnSectionEntered(SectionId section) => EnterSection(section);

        /// <summary>Back at a Tuning Fork: that section's pattern again, and the band starts from its floor.</summary>
        void OnRespawned(SectionId section)
        {
            EnterSection(section);
            intensity = currentCue.IntensityFloor;
        }

        /// <summary>CuePattern changes on the next bar line, so a section change is always musical.</summary>
        void EnterSection(SectionId section)
        {
            currentCue = FindCue(section);
            if (currentCue.Pattern != SectionCue.KeepPattern) Cue(currentCue.Pattern);
        }

        /// <summary>The turnaround, held until the goal stops the song (The Resonator's own cue keeps the pattern).</summary>
        void OnFinalLaunch() => Cue(finalLaunchPattern);

        /// <summary>Cues a pattern for the next bar line and holds it from then on (see <see cref="HoldPattern"/>).</summary>
        void Cue(int pattern)
        {
            heldPattern = pattern;
            music.CuePattern(pattern);
        }

        SectionCue FindCue(SectionId section)
        {
            foreach (SectionCue cue in sectionCues)
                if (cue.Section == section) return cue;
            return new SectionCue(section, SectionCue.KeepPattern, 0f);
        }

        // ---- moments -----------------------------------------------------------------------------------------------------------

        /// <summary>The band flinches: only the kick is left and the music dips; Update brings both back.</summary>
        void OnHurt()
        {
            duckTarget = 1f;
            intensity = 0f;
        }

        /// <summary>
        /// Three bell notes on the song's own bells, a sixteenth apart and starting ON the song's next sixteenth (notes
        /// played by hand are not quantised by the music player), and a fuller band for a bar.
        /// </summary>
        void OnCheckpointReached(int index)
        {
            StartCoroutine(PlayPhraseOnTheNextStep(checkpointAnswer));
            liftedUntil = Time.time + checkpointLiftBars * BarSeconds;
        }

        /// <summary>The song stops; the fanfare is played by hand on its instruments, brought forward in a bigger room.</summary>
        void OnGoalReached()
        {
            music.Stop();
            songEnded = true;
            foreach (MusicNote note in fanfare) music.SetTrackVolume(note.Sound, fanfareTrackVolume);
            music.SetMaster(SfxMaster.ReverbSize, fanfareReverbSize);
            PlayPhrase(fanfare);
        }

        /// <summary>
        /// A run begins. From the title card the song is already playing and only the section's pattern is cued; after
        /// the goal the fanfare is cut off, the song's own mix is put back (Reload) and the song starts from pattern A.
        /// </summary>
        void OnGameStarted()
        {
            if (songEnded)
            {
                songEnded = false;
                StopAllCoroutines();
                ReleasePhrase(fanfare);
                music.Reload();
                music.startPattern = PatternA;
                music.Play();
            }
            duck = 0f;
            duckTarget = 0f;
            liftedUntil = float.NegativeInfinity;
            EnterSection(SectionId.SoftStart);
        }

        // ---- notes by hand ---------------------------------------------------------------------------------------------------------

        void PlayPhrase(MusicNote[] phrase)
        {
            foreach (MusicNote note in phrase) StartCoroutine(PlayNote(note));
        }

        /// <summary>
        /// Waits until the running song reaches its next step (at most one sixteenth), then plays the phrase, so the
        /// phrase's own sixteenths fall on the song's.
        /// </summary>
        IEnumerator PlayPhraseOnTheNextStep(MusicNote[] phrase)
        {
            int step = music.Step;
            float latest = Time.time + SixteenthSeconds;
            while (music.IsPlaying && music.Step == step && Time.time < latest) yield return null;
            PlayPhrase(phrase);
        }

        /// <summary>Waits for the note's place in the phrase, plays it, and lets a held instrument go after its length.</summary>
        IEnumerator PlayNote(MusicNote note)
        {
            float sixteenth = SixteenthSeconds;
            if (note.StartSixteenths > 0f) yield return new WaitForSeconds(note.StartSixteenths * sixteenth);
            music.PlaySound(note.Sound, note.Velocity, note.MidiNote);
            if (note.LengthSixteenths <= 0f) yield break;
            yield return new WaitForSeconds(note.LengthSixteenths * sixteenth);
            music.ReleaseSound(note.Sound, note.MidiNote);
        }

        void ReleasePhrase(MusicNote[] phrase)
        {
            foreach (MusicNote note in phrase)
                if (note.LengthSixteenths > 0f) music.ReleaseSound(note.Sound, note.MidiNote);
        }
    }
}
