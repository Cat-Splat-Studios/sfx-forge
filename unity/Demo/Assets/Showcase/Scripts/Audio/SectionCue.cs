using System;
using SfxShowcase.Core;
using UnityEngine;

namespace SfxShowcase.Audio
{
    /// <summary>
    /// What the music does in one section of the level: which pattern of the song is cued, and how full the band is at
    /// the least. The table of all sections is on the <see cref="MusicDirector"/> (DESIGN.md, "Music").
    /// </summary>
    [Serializable]
    public struct SectionCue
    {
        /// <summary>The <see cref="Pattern"/> value that leaves the music on the pattern it is playing.</summary>
        public const int KeepPattern = -1;

        [Tooltip("The section of the level.")]
        [SerializeField] SectionId section;

        [Tooltip("The song's pattern for this section: 0 = A … 7 = H. -1 keeps the current pattern.")]
        [Range(-1, 7)] [SerializeField] int pattern;

        [Tooltip("The music's intensity never falls below this while the player is in the section.")]
        [Range(0f, 1f)] [SerializeField] float intensityFloor;

        /// <summary>The section of the level.</summary>
        public SectionId Section => section;
        /// <summary>The song's pattern for this section, 0 = A; <see cref="KeepPattern"/> leaves it.</summary>
        public int Pattern => pattern;
        /// <summary>The music's intensity never falls below this while the player is in the section.</summary>
        public float IntensityFloor => intensityFloor;

        /// <summary>Creates a row of the table.</summary>
        public SectionCue(SectionId section, int pattern, float intensityFloor)
        {
            this.section = section;
            this.pattern = pattern;
            this.intensityFloor = intensityFloor;
        }
    }
}
