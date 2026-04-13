#include "audio.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ---- Globals ---- */
DspState    g_dsp;
Sequencer   g_seq;
WaveBuffer  g_wave;
RecState    g_rec;
SDL_AudioDeviceID g_audio_dev;

/* ---- MIDI note to frequency ---- */
static float midi_to_freq(int note)
{
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

/* ---- Simple PRNG (xorshift32, lock-free, no stdlib rand) ---- */
static unsigned int s_rng_state = 12345;

static unsigned int rng_next(void)
{
    unsigned int x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x;
    return x;
}

static int rng_range(int lo, int hi)
{
    return lo + (int)(rng_next() % (unsigned int)(hi - lo + 1));
}

static float rng_float(void)
{
    return (rng_next() & 0xFFFF) / 65535.0f;
}

/* ---- Pattern Generation ---- */

/* Scale types for variety */
/* ---- Scales (dark/hard styles only) ---- */
static const int SCALE_MINOR_PENTA[]   = { 0, 3, 5, 7, 10 };
static const int SCALE_BLUES[]         = { 0, 3, 5, 6, 7, 10 };
static const int SCALE_PHRYGIAN[]      = { 0, 1, 3, 5, 7, 8, 10 };
static const int SCALE_MINOR[]         = { 0, 2, 3, 5, 7, 8, 10 };
static const int SCALE_HARMONIC_MINOR[]= { 0, 2, 3, 5, 7, 8, 11 };
static const int SCALE_DORIAN[]        = { 0, 2, 3, 5, 7, 9, 10 };

typedef struct {
    const int *intervals;
    int size;
} ScaleDef;

static const int SCALE_MAJOR[]         = { 0, 2, 4, 5, 7, 9, 11 };
static const int SCALE_MAJOR_PENTA[]   = { 0, 2, 4, 7, 9 };
static const int SCALE_LYDIAN[]        = { 0, 2, 4, 6, 7, 9, 11 };
static const int SCALE_CHROMATIC[]     = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const ScaleDef SCALES[] = {
    { SCALE_MINOR_PENTA,    5 },   /* 0 */
    { SCALE_BLUES,          6 },   /* 1 */
    { SCALE_PHRYGIAN,       7 },   /* 2 */
    { SCALE_MINOR,          7 },   /* 3 */
    { SCALE_HARMONIC_MINOR, 7 },   /* 4 */
    { SCALE_DORIAN,         7 },   /* 5 */
    { SCALE_CHROMATIC,     12 },   /* 6 */
    { SCALE_MAJOR,          7 },   /* 7 */
    { SCALE_MAJOR_PENTA,    5 },   /* 8 */
    { SCALE_LYDIAN,         7 },   /* 9 */
};
#define NUM_SCALES 10

/* Pattern personality types */
#define STYLE_DARK_ACID   0   /* classic dark acid */
#define STYLE_TRANCE      1   /* euphoric, major scales, uplifting */
#define STYLE_HARDCORE    2   /* aggressive, heavy accents */
#define STYLE_HYPNOTIC    3   /* minimal, trance-like, heavy slides */
#define STYLE_HARD_ACID   4   /* big intervals, screaming filter, wild */
#define NUM_STYLES        5

void audio_generate_pattern(void)
{
    /* Pick which pattern buffer to write into (the inactive one) */
    int write_buf = 1 - g_seq.active_pattern;
    Pattern *pat = &g_seq.patterns[write_buf];

    /* Seed PRNG */
    s_rng_state = (unsigned int)((size_t)pat ^ (unsigned int)(g_dsp.phase * 1000000.0f));
    if (s_rng_state == 0) s_rng_state = 42;
    for (int i = 0; i < 5; i++) rng_next();

    /* ---- Choose style ---- */
    int style = (g_seq.style_select >= 0) ? g_seq.style_select : rng_range(0, NUM_STYLES - 1);

    /* ---- Pick random root note ---- */
    int root;
    if (style == STYLE_DARK_ACID) {
        root = rng_range(24, 36);  /* C1-C2: low and menacing */
    } else if (style == STYLE_TRANCE) {
        root = rng_range(36, 52);  /* C2-E3: higher, brighter */
    } else {
        root = rng_range(24, 47);  /* C1-B2 */
    }

    /* ---- Pick a random scale ---- */
    int scale_idx = rng_range(0, NUM_SCALES - 1);
    /* Trance uses major/lydian scales */
    if (style == STYLE_TRANCE) {
        float r = rng_float();
        if (r < 0.35f)      scale_idx = 7;  /* major */
        else if (r < 0.60f) scale_idx = 8;  /* major pentatonic */
        else                scale_idx = 9;  /* lydian (dreamy) */
    }
    /* Dark/hardcore/hard acid prefer phrygian & harmonic minor */
    if ((style == STYLE_HARDCORE || style == STYLE_HARD_ACID) && rng_float() < 0.5f) {
        scale_idx = rng_range(2, 4);  /* phrygian, minor, harmonic minor */
    }
    if (style == STYLE_DARK_ACID) {
        /* Dark scales: phrygian, harmonic minor, or chromatic */
        float r = rng_float();
        if (r < 0.40f)      scale_idx = 2;  /* phrygian */
        else if (r < 0.70f) scale_idx = 4;  /* harmonic minor */
        else                scale_idx = 6;  /* chromatic - dissonant, unsettling */
    }
    const ScaleDef *sd = &SCALES[scale_idx];

    /* Build scale across 3 octaves */
    int scale[30];
    int scale_len = 0;
    int num_octaves = (style == STYLE_HARD_ACID || style == STYLE_HARDCORE) ? 3 : 2;
    for (int oct = 0; oct < num_octaves; oct++) {
        for (int i = 0; i < sd->size && scale_len < 30; i++) {
            scale[scale_len++] = root + oct * 12 + sd->intervals[i];
        }
    }

    /* Select core notes (2-5, style-dependent) */
    int num_core;
    if (style == STYLE_HYPNOTIC) {
        num_core = rng_range(2, 3);
    } else if (style == STYLE_TRANCE) {
        num_core = rng_range(2, 3);
    } else if (style == STYLE_HARD_ACID) {
        num_core = rng_range(3, 5);  /* more spread-out core notes */
    } else {
        num_core = rng_range(3, 4);
    }
    int core[5];
    core[0] = root;
    if (style == STYLE_HARD_ACID) {
        /* Deliberately spread core notes across octaves */
        core[1] = root + 12;  /* octave up */
        core[2] = scale[rng_range(scale_len/2, scale_len - 1)];  /* from upper half */
        for (int i = 3; i < num_core; i++)
            core[i] = scale[rng_range(0, scale_len - 1)];
    } else {
        for (int i = 1; i < num_core; i++)
            core[i] = scale[rng_range(0, scale_len - 1)];
    }

    /* ---- Style-dependent parameters ---- */
    float prob_accent, prob_slide;
    int max_leap;
    float core_weight;
    float repeat_prob;    /* chance to repeat previous note (pumping feel) */
    float env_decay_base; /* base filter envelope decay time in seconds */
    float env_depth_base; /* base filter envelope depth */

    switch (style) {
    case STYLE_TRANCE:
        prob_accent = 0.20f; prob_slide = 0.35f;
        max_leap = 7; core_weight = 0.55f;
        repeat_prob = 0.15f;
        env_decay_base = 0.03f; env_depth_base = 0.70f;  /* short snappy decay = fast ticking */
        break;
    case STYLE_HARDCORE:
        prob_accent = 0.45f; prob_slide = 0.20f;
        max_leap = 12; core_weight = 0.50f;
        repeat_prob = 0.20f;
        env_decay_base = 0.06f; env_depth_base = 0.75f;
        break;
    case STYLE_HARD_ACID:
        prob_accent = 0.40f; prob_slide = 0.35f;
        max_leap = 19; core_weight = 0.40f;
        repeat_prob = 0.10f;
        env_decay_base = 0.04f; env_depth_base = 0.85f;
        break;
    case STYLE_HYPNOTIC:
        prob_accent = 0.12f; prob_slide = 0.55f;
        max_leap = 5; core_weight = 0.80f;
        repeat_prob = 0.30f;
        env_decay_base = 0.25f; env_depth_base = 0.45f;
        break;
    default: /* STYLE_DARK_ACID */
        prob_accent = 0.18f; prob_slide = 0.45f;
        max_leap = 5; core_weight = 0.70f;
        repeat_prob = 0.30f;
        env_decay_base = 0.30f; env_depth_base = 0.40f;
        break;
    }

    /* ============================================================
     * Rhythm templates: define STRUCTURE, then fill with notes.
     * G=gate(new note), T=tie, R=rest
     * ============================================================ */

    /* Each template is 16 chars: 'G'=gate, 'T'=tie, 'R'=rest */
    static const char *RHYTHM_TEMPLATES[] = {
        /* Dense / straight 16ths */
        "GGGGGGGGGGGGGGGG",  /* 0: all notes */
        "GGGGGGTGGGGGGGTG",  /* 1: almost full, few ties */
        "GGTGGGTGGGTGGGTG",  /* 2: running with ties */

        /* Galloping / triplet-like */
        "GGRGGRGGRGGRGGR.",  /* 3: gallop */
        "GGRGGRGGTGGRGGRG",  /* 4: gallop + tie */
        "GGGTRRGGGTRRGGGT",  /* 5: triplet burst */

        /* Stuttered / pulsing */
        "GGRRGGRRGGRRGGRR",  /* 6: stutter pairs */
        "GGGRGGGRGGGRGGGG",  /* 7: 3+1 stutter */
        "GRGRGRGRGRGRGRGR",  /* 8: every other */

        /* Sparse / minimal */
        "GRRRGRRRGRRRGTTT",  /* 9: 4-on-floor with tail */
        "GRRRGRRRGTTRGRRG",  /* 10: breathing */
        "GTTTGRRRGTTTRRRG",  /* 11: long holds */

        /* Syncopated / offbeat */
        "GRGRRGRGGRGRRGRT",  /* 12: syncopated */
        "RGRGGRGRRGRGGRGR",  /* 13: offbeat heavy */
        "GRRGGRRGRRGGRRRG",  /* 14: dotted feel */
        "GRRGGRRGRRGGRRGG",  /* 15: swing pairs */

        /* Build-up / evolving */
        "GRRRRGRRGGRGGGRG",  /* 16: sparse to dense */
        "GRRRGRGRGGGGGGTG",  /* 17: accelerating */
        "GGGGGRRRRGGGTTTT",  /* 18: burst-rest-burst-sustain */

        /* Hardcore / aggressive */
        "GGGGRGGGGGRGGGGG",  /* 19: almost relentless */
        "GGGGRGGGGGGGRGRG",  /* 20: pounding with breaks */
        "GGRGGGRGGGRGGGRT",  /* 21: driving with gaps */

        /* Arpeggio / fast runs */
        "GGGGRRRRGGGGRRRR",  /* 22: 4-note burst, rest, repeat */
        "GGGGGGRRGGGGGGGG",  /* 23: almost full with gap */
        "GGGRRRGGGRRRGGGR",  /* 24: 3-note bursts */
        "GRGGGGRGGGGRGGGG",  /* 25: rest-arpeggio pattern */
        "RRRRGGGGGGGGRRRR",  /* 26: silence then fast run */
        "GTGGGGGTGGGGGTGG",  /* 27: arp with sustain anchors */
        "GGGGGGGGRRRRGGGG",  /* 28: run-silence-run */
    };
    #define NUM_TEMPLATES 29

    /* Pick a rhythm template, weighted by style */
    int tmpl_idx;
    switch (style) {
    case STYLE_TRANCE:
        /* Fast arpeggios and dense runs */
        if (rng_float() < 0.55f)
            tmpl_idx = rng_range(22, 28);  /* arpeggio */
        else
            tmpl_idx = rng_range(0, 2);    /* dense 16ths */
        break;
    case STYLE_HARDCORE:
        tmpl_idx = rng_range(0, 2);  /* dense */
        if (rng_float() < 0.4f) tmpl_idx = rng_range(19, 21); /* hardcore specific */
        if (rng_float() < 0.2f) tmpl_idx = rng_range(3, 5);   /* gallop */
        break;
    case STYLE_HARD_ACID:
        /* Mix of dense runs with breaks for dramatic leaps */
        tmpl_idx = rng_range(0, 2);   /* dense base */
        if (rng_float() < 0.3f) tmpl_idx = rng_range(3, 5);   /* gallop */
        if (rng_float() < 0.3f) tmpl_idx = rng_range(16, 18);  /* build-up */
        if (rng_float() < 0.2f) tmpl_idx = rng_range(12, 15);  /* syncopated */
        break;
    case STYLE_HYPNOTIC:
        tmpl_idx = rng_range(9, 11);  /* sparse */
        if (rng_float() < 0.3f) tmpl_idx = rng_range(8, 8);   /* every other */
        if (rng_float() < 0.2f) tmpl_idx = rng_range(14, 15);  /* dotted/swing */
        break;
    default: /* STYLE_DARK_ACID */
        /* Mix of arpeggio runs and sparse menace */
        if (rng_float() < 0.45f)
            tmpl_idx = rng_range(22, 28);  /* arpeggio runs */
        else if (rng_float() < 0.5f)
            tmpl_idx = rng_range(9, 15);   /* sparse/syncopated */
        else
            tmpl_idx = rng_range(3, 5);    /* gallop */
        break;
    }
    if (tmpl_idx >= NUM_TEMPLATES) tmpl_idx = 0;
    const char *tmpl = RHYTHM_TEMPLATES[tmpl_idx];

    /* ---- Randomize sonic character for this loop ---- */
    float loop_cutoff, loop_reso;
    if (style == STYLE_DARK_ACID) {
        loop_cutoff = 0.05f + rng_float() * 0.15f;  /* very low: dark, muffled */
        loop_reso   = 0.65f + rng_float() * 0.30f;  /* high: screaming resonance */
    } else if (style == STYLE_TRANCE) {
        loop_cutoff = 0.30f + rng_float() * 0.30f;  /* bright, open */
        loop_reso   = 0.35f + rng_float() * 0.35f;  /* moderate: clean not screamy */
    } else {
        loop_cutoff = 0.15f + rng_float() * 0.35f;
        loop_reso   = 0.45f + rng_float() * 0.45f;
    }

    /* ---- Accent pattern: separate from rhythm ---- */
    /* Pick which positions get accents (varies per loop) */
    int accent_positions[STEPS_PER_BAR] = {0};
    int rhythm_feel = rng_range(0, 3);
    for (int s = 0; s < STEPS_PER_BAR; s++) {
        int is_strong;
        switch (rhythm_feel) {
        case 0: is_strong = (s % 4 == 0); break;                           /* downbeat */
        case 1: is_strong = (s % 2 == 1); break;                           /* offbeat */
        case 2: is_strong = (s==0||s==3||s==6||s==10||s==13); break;        /* syncopated */
        default: is_strong = (s==0||s==2||s==4||s==7||s==8||s==11); break;  /* breakbeat-ish */
        }
        float ap = is_strong ? prob_accent * 1.8f : prob_accent * 0.5f;
        accent_positions[s] = (rng_float() < ap) ? 1 : 0;
    }

    /* ---- Fill notes into the rhythm template ---- */
    Step base[STEPS_PER_BAR];
    int prev_note = root;

    for (int s = 0; s < STEPS_PER_BAR; s++) {
        memset(&base[s], 0, sizeof(Step));
        char cell = tmpl[s];

        /* Random perturbation: occasionally flip a cell (keeps templates from being rigid) */
        if (s > 0 && s < STEPS_PER_BAR - 1 && rng_float() < 0.12f) {
            const char options[] = "GRT";
            cell = options[rng_range(0, 2)];
        }

        if (cell == 'R' || cell == '.') {
            /* Rest */
            base[s].note = 0;
            base[s].gate = 0;
        } else if (cell == 'T' && s > 0) {
            /* Tie */
            base[s].note = (uint8_t)prev_note;
            base[s].gate = 0;
            base[s].tie = 1;
        } else {
            /* Gate: pick a note */
            int note;
            if (rng_float() < repeat_prob && s > 0) {
                note = prev_note;
            } else if (rng_float() < core_weight) {
                note = core[rng_range(0, num_core - 1)];
            } else {
                note = scale[rng_range(0, scale_len - 1)];
            }

            /* Chromatic drift: occasional semitone deviation from scale */
            float chrom_prob = (style == STYLE_DARK_ACID)  ? 0.15f
                             : (style == STYLE_HARD_ACID)  ? 0.10f
                             : (style == STYLE_HARDCORE)   ? 0.08f : 0.03f;
            if (rng_float() < chrom_prob) {
                note += (rng_float() < 0.5f) ? 1 : -1;
            }

            /* Octave jumps - more frequent for hard styles */
            float oct_prob = (style == STYLE_HARD_ACID) ? 0.20f
                           : (style == STYLE_HARDCORE)  ? 0.12f : 0.06f;
            if (rng_float() < oct_prob) {
                note += (rng_float() < 0.6f) ? 12 : -12;
            }

            /* Melodic constraint */
            int diff = note - prev_note;
            if (diff > max_leap) note = prev_note + max_leap;
            if (diff < -max_leap) note = prev_note - max_leap;
            int range_lo = (style == STYLE_HARD_ACID) ? root - 12 : root - 5;
            int range_hi = (style == STYLE_HARD_ACID) ? root + 36 : root + 24;
            if (note < range_lo) note = range_lo;
            if (note > range_hi) note = range_hi;

            base[s].note = (uint8_t)note;
            base[s].gate = 1;
            prev_note = note;

            base[s].accent = (uint8_t)accent_positions[s];
            base[s].slide = (rng_float() < prob_slide) ? 1 : 0;
        }

        /* First step: root, or high octave for hard acid impact */
        if (s == 0) {
            int start_note = root;
            if (style == STYLE_HARD_ACID && rng_float() < 0.4f) {
                start_note = root + 12 + scale[rng_range(0, sd->size - 1)];
            }
            base[s].note = (uint8_t)start_note;
            base[s].gate = 1;
            base[s].tie = 0;
            prev_note = start_note;
        }
    }

    /* Last step tends toward root for clean loop */
    if (rng_float() < 0.55f) {
        base[STEPS_PER_BAR - 1].note = (uint8_t)root;
        base[STEPS_PER_BAR - 1].gate = 1;
    }

    /* Copy pattern + sonic presets into inactive buffer */
    memcpy(pat->steps, base, sizeof(base));
    pat->cutoff = loop_cutoff;
    pat->resonance = loop_reso;
    pat->env_decay_base = env_decay_base;
    pat->env_depth_base = env_depth_base;

    /* Insert brief silence gap so user hears the pattern change */
    g_seq.mute_samples = (int)(SAMPLE_RATE * 0.08f);

    /* Signal the audio thread to swap at next loop boundary */
    g_seq.pending_swap = 1;
}

/* ---- Kick drum synthesis ---- */
static void kick_trigger(KickState *k)
{
    k->phase = 0.0f;
    k->freq = k->freq_start;
    k->amp = 1.75f;
}

static float kick_process_sample(KickState *k)
{
    if (k->amp < 0.001f) return 0.0f;

    /* Sine oscillator */
    float out = sinf(k->phase * 6.283185307f) * k->amp;

    /* Advance phase */
    k->phase += k->freq / (float)SAMPLE_RATE;
    if (k->phase >= 1.0f) k->phase -= 1.0f;

    /* Pitch sweep: exponential decay toward end freq */
    k->freq += (k->freq_end - k->freq) * k->freq_decay;

    /* Amplitude decay */
    k->amp *= (1.0f - k->amp_decay);

    return out;
}

/* ---- Hihat synthesis: 6 detuned metallic oscillators + noise through bandpass ---- */
static const float HH_FREQS[6] = { 295.7f, 342.2f, 399.6f, 509.3f, 611.8f, 723.4f };

static void hihat_trigger(HihatState *h, int open)
{
    h->amp = open ? 0.85f : 1.0f;
    h->amp_decay = open ? 0.0008f : 0.0045f;  /* open = longer ring */
    h->bp_low = 0.0f;
    h->bp_band = 0.0f;
}

static float hihat_process_sample(HihatState *h)
{
    if (h->amp < 0.001f) return 0.0f;

    /* Mix 6 detuned square waves (metallic tone) */
    float metallic = 0.0f;
    for (int i = 0; i < 6; i++) {
        h->phases[i] += HH_FREQS[i] / (float)SAMPLE_RATE;
        if (h->phases[i] >= 1.0f) h->phases[i] -= 1.0f;
        metallic += (h->phases[i] < 0.5f) ? 0.16f : -0.16f;
    }

    /* Noise component */
    h->noise_seed ^= h->noise_seed << 13;
    h->noise_seed ^= h->noise_seed >> 17;
    h->noise_seed ^= h->noise_seed << 5;
    float noise = (float)(int)h->noise_seed / 2147483648.0f;

    /* Mix metallic + noise */
    float raw = metallic * 0.55f + noise * 0.45f;

    /* Bandpass filter (SVF) - centered around 8-10kHz range */
    float f = 0.7f;   /* high cutoff for sizzle */
    float q = 0.4f;   /* moderate resonance */
    h->bp_low += f * h->bp_band;
    float high = raw - h->bp_low - q * h->bp_band;
    h->bp_band += f * high;

    float out = h->bp_band * h->amp;  /* bandpass output */

    h->amp *= (1.0f - h->amp_decay);
    return out;
}

/* ---- Clap synthesis: filtered noise with "flam" micro-bursts ---- */
static void clap_trigger(ClapState *c)
{
    c->amp = 1.0f;
    c->amp_decay = 0.0005f;
    c->filter_coeff = 0.6f;
    c->filter_state = 0.0f;
    c->bursts_left = 3;       /* 3 micro-bursts before main hit */
    c->retrigger = 0;
}

static float clap_process_sample(ClapState *c)
{
    if (c->amp < 0.001f && c->bursts_left <= 0) return 0.0f;

    /* Flam effect: micro-bursts re-trigger the amplitude */
    if (c->bursts_left > 0) {
        c->retrigger--;
        if (c->retrigger <= 0) {
            c->amp = 0.35f + (float)c->bursts_left * 0.07f;
            c->retrigger = 400 + c->bursts_left * 120;  /* ~9-16ms between bursts */
            c->bursts_left--;
        }
    }

    /* Noise */
    c->noise_seed ^= c->noise_seed << 13;
    c->noise_seed ^= c->noise_seed >> 17;
    c->noise_seed ^= c->noise_seed << 5;
    float noise = (float)(int)c->noise_seed / 2147483648.0f;

    /* Bandpass-ish: one-pole then subtract DC */
    c->filter_state += c->filter_coeff * (noise - c->filter_state);
    float out = c->filter_state * c->amp;

    c->amp *= (1.0f - c->amp_decay);
    return out;
}

/* ---- Bassline synthesis (disabled, kept for iteration) ---- */
static void bass_note_on(BassState *b, float freq, int slide)
{
    (void)slide;
    b->freq = freq;
    b->target_freq = freq;
    b->slide_rate = 0.0f;
    b->phase = 0.0f;
    b->amp = 0.85f;
    b->amp_decay = 0.00004f;
    b->amp_stage = 1;
}

static void bass_note_off(BassState *b)
{
    b->amp_decay = 0.003f;
}

static float bass_process_sample(BassState *b)
{
    if (b->amp < 0.001f) return 0.0f;
    b->phase += b->freq / (float)SAMPLE_RATE;
    if (b->phase >= 1.0f) b->phase -= 1.0f;
    float sine = sinf(b->phase * 6.283185307f);
    float tri = (b->phase < 0.5f) ? (4.0f * b->phase - 1.0f) : (3.0f - 4.0f * b->phase);
    float out = (sine * 0.6f + tri * 0.4f) * b->amp;
    if (g_seq.kick_enabled && g_seq.kick.amp > 0.1f)
        out *= 1.0f - g_seq.kick.amp * 0.6f;
    b->amp *= (1.0f - b->amp_decay);
    return out;
}

/* ---- Set envelope parameters for a note, then trigger via NASM ---- */
static void trigger_note(DspState *s, int accent)
{
    Pattern *pat = &g_seq.patterns[g_seq.active_pattern];
    float decay_base = pat->env_decay_base;
    float depth_base = pat->env_depth_base;

    /* Filter envelope coefficients */
    s->flt_env_attack = 1.0f / (0.002f * SAMPLE_RATE);  /* ~2ms attack */

    if (accent) {
        /* Accented: shorter decay, deeper modulation, louder */
        s->flt_env_decay = 1.0f / (decay_base * 0.25f * SAMPLE_RATE);
        s->flt_env_depth = depth_base * 1.4f;
        s->amp_level = 1.3f;
    } else {
        s->flt_env_decay = 1.0f / (decay_base * SAMPLE_RATE);
        s->flt_env_depth = depth_base;
        s->amp_level = 1.0f;
    }

    /* Amplitude envelope coefficients */
    s->amp_env_attack = 1.0f / (0.002f * SAMPLE_RATE);
    s->amp_env_decay = 1.0f / (0.300f * SAMPLE_RATE);

    /* Trigger envelopes */
    dsp_note_on(s);
}

/* ---- Get samples for current step (handles shuffle) ---- */
static float get_step_duration(void)
{
    float base = g_seq.samples_per_step;
    if (!g_seq.shuffle) return base;
    /* Shuffle: even steps 75% longer, odd steps 25% (heavy swing) */
    if (g_seq.current_step % 2 == 0)
        return base * 1.5f;
    else
        return base * 0.5f;
}

/* ---- Advance sequencer to next step, set DSP params ---- */
static void sequencer_advance_step(void)
{
    Sequencer *seq = &g_seq;
    Pattern *pat = &seq->patterns[seq->active_pattern];

    /* Advance step, loop at time-signature-dependent length */
    seq->current_step++;
    if (seq->current_step >= seq->loop_steps) {
        seq->current_step = 0;
    }

    int s = seq->current_step;

    /* ---- Drums adapt to time signature ---- */
    switch (seq->time_sig) {
    case 1: /* 3/4: 12 steps = 3 beats of 4 */
        if (seq->kick_enabled && (s == 0 || s == 4 || s == 8))
            kick_trigger(&seq->kick);
        if (seq->clap_enabled && s == 8)
            clap_trigger(&seq->clap);
        break;
    case 2: /* 6/8: 12 steps = 4 groups of 3 (compound time) */
        if (seq->kick_enabled && (s == 0 || s == 6))
            kick_trigger(&seq->kick);
        if (seq->clap_enabled && s == 6)
            clap_trigger(&seq->clap);
        break;
    default: /* 4/4: 16 steps = 4 beats of 4 */
        if (seq->kick_enabled && (s % 4 == 0))
            kick_trigger(&seq->kick);
        if (seq->clap_enabled && (s == 4 || s == 12))
            clap_trigger(&seq->clap);
        break;
    }

    /* Hihat from pattern (works with all time signatures) */
    if (seq->hihat_enabled && s < 16) {
        uint8_t hh = seq->hihat.pattern[s];
        if (hh == 1)      hihat_trigger(&seq->hihat, 0);  /* closed */
        else if (hh == 2) hihat_trigger(&seq->hihat, 1);  /* open */
    }

    /* Bassline - always sequenced, button is just mute */
    {
        BassPattern *bp = &seq->bass_patterns[seq->bass_active_pattern];
        if (bp->gates[seq->current_step]) {
            float bfreq = midi_to_freq(bp->notes[seq->current_step]);
            bass_note_on(&seq->bass, bfreq, 0);
        } else if (bp->notes[seq->current_step] == 0) {
            bass_note_off(&seq->bass);
        }
    }

    /* Get current step data */
    Step *step = &pat->steps[seq->current_step];

    if (step->gate) {
        /* New note: set frequency */
        float new_freq = midi_to_freq(step->note);

        /* Check if previous step had slide */
        int prev_idx = (seq->current_step > 0) ? seq->current_step - 1 : TOTAL_STEPS - 1;
        Step *prev_step = &pat->steps[prev_idx];

        if (prev_step->slide) {
            /* Portamento: slide from current freq to new freq */
            g_dsp.target_freq = new_freq;
            g_dsp.slide_rate = 0.001f;  /* exponential slide coefficient */
        } else {
            g_dsp.freq = new_freq;
            g_dsp.target_freq = new_freq;
            g_dsp.slide_rate = 0.0f;
        }

        /* Trigger envelopes */
        trigger_note(&g_dsp, step->accent);
    } else if (step->tie) {
        /* Tie: do nothing, let note sustain */
    } else {
        /* Rest: kill amplitude quickly */
        g_dsp.amp_env_stage = 2;
        g_dsp.amp_env_decay = 1.0f / (0.010f * SAMPLE_RATE);  /* fast decay for rest */
    }
}

/* ---- Audio callback (runs on audio thread) ---- */
static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    float *out = (float *)stream;
    unsigned int frames = (unsigned int)len / sizeof(float);
    unsigned int remaining = frames;
    unsigned int pos = 0;

    while (remaining > 0) {
        if (!g_seq.playing) {
            /* Not playing: output silence */
            for (unsigned int i = 0; i < remaining; i++) {
                out[pos + i] = 0.0f;
                g_wave.data[g_wave.write_pos] = 0.0f;
                g_wave.write_pos = (g_wave.write_pos + 1) & (WAVE_BUF_LEN - 1);
            }
            return;
        }

        /* How many samples until next step boundary? */
        float until_next = get_step_duration() - g_seq.step_accumulator;
        unsigned int chunk = (unsigned int)until_next;
        if (chunk < 1) chunk = 1;
        if (chunk > remaining) chunk = remaining;

        /* Generate DSP samples for this chunk (NASM hot loop) */
        dsp_process_block(&g_dsp, out + pos, chunk);

        /* Hoist feature flags once per chunk */
        float vol = g_seq.volume;
        int do_kick  = g_seq.kick_enabled;
        int do_hh    = g_seq.hihat_enabled;
        int do_clap  = g_seq.clap_enabled;
        int do_bass  = g_seq.bass_enabled;
        int wpos     = g_wave.write_pos;
        int swapped  = 0;  /* track if swap happened mid-chunk */

        /* Single fused pass: scope copy, HP, percussion mix, volume/mute */
        static float hp_state = 0.0f;
        for (unsigned int i = 0; i < chunk; i++) {
            float s = out[pos + i];

            /* High-pass acid when bass active */
            if (do_bass) {
                hp_state += 0.05f * (s - hp_state);
                s -= hp_state;
            }

            /* Copy acid-only to waveform ring buffer (bitmask) */
            g_wave.data[wpos] = s;
            wpos = (wpos + 1) & (WAVE_BUF_LEN - 1);

            /* Mix percussion */
            if (do_kick)  s += kick_process_sample(&g_seq.kick);
            if (do_hh)    s += hihat_process_sample(&g_seq.hihat);
            if (do_clap)  s += clap_process_sample(&g_seq.clap);
            if (do_bass)  s += bass_process_sample(&g_seq.bass);

            /* Mute gap or apply volume */
            if (g_seq.mute_samples > 0) {
                s = 0.0f;
                g_seq.mute_samples--;
                if (g_seq.mute_samples == 0 && g_seq.pending_swap) {
                    g_seq.active_pattern = 1 - g_seq.active_pattern;
                    g_seq.pending_swap = 0;
                    if (g_seq.bass_pending_swap) {
                        g_seq.bass_active_pattern = 1 - g_seq.bass_active_pattern;
                        g_seq.bass_pending_swap = 0;
                    }
                    g_seq.current_step = 0;
                    g_seq.step_accumulator = 0.0f;
                    swapped = 1;
                    Pattern *newpat = &g_seq.patterns[g_seq.active_pattern];
                    g_dsp.base_cutoff = newpat->cutoff;
                    g_dsp.flt_resonance = newpat->resonance;
                    Step *first = &newpat->steps[0];
                    if (first->gate) {
                        g_dsp.freq = midi_to_freq(first->note);
                        g_dsp.target_freq = g_dsp.freq;
                        g_dsp.slide_rate = 0.0f;
                        trigger_note(&g_dsp, first->accent);
                    }
                }
            } else {
                s *= vol;
            }

            out[pos + i] = s;

            /* Record output */
            if (g_rec.recording && g_rec.pos < REC_MAX_SAMPLES) {
                g_rec.buffer[g_rec.pos++] = s;
            }
        }
        g_wave.write_pos = wpos;

        pos += chunk;
        remaining -= chunk;

        /* Don't accumulate samples from before swap */
        if (!swapped) {
            g_seq.step_accumulator += (float)chunk;
        }

        /* Step boundary reached? */
        float step_dur = get_step_duration();
        if (g_seq.step_accumulator >= step_dur) {
            g_seq.step_accumulator -= step_dur;
            sequencer_advance_step();
        }
    }
}

/* ---- Public API ---- */

void audio_set_bpm(float bpm)
{
    if (bpm < 80.0f) bpm = 80.0f;
    if (bpm > 300.0f) bpm = 300.0f;
    g_seq.bpm = bpm;
    /* samples_per_step = (60/bpm) * 44100 / 4  (16th notes) */
    g_seq.samples_per_step = (60.0f / bpm) * SAMPLE_RATE / 4.0f;
}

void audio_set_volume(float vol)
{
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    g_seq.volume = vol;
}

void audio_generate_hihat_pattern(void)
{
    uint8_t *p = g_seq.hihat.pattern;

    /* ---- Rule-based generation ---- */

    /* 1. Pick density: how many of 16 steps have hits */
    int density = rng_range(8, 16);  /* 8 = 8ths minimum, 16 = full */

    /* 2. Pick base grid: 16ths (35%) or 8ths (65%) */
    int grid = (rng_float() < 0.35f) ? 1 : 2;

    /* 3. Fill base grid */
    for (int s = 0; s < 16; s++) {
        if (s % grid == 0) p[s] = 1;  /* closed */
        else p[s] = 0;
    }

    /* 4. Add fills/trills: pick 0-2 spots for rapid closed bursts (2-4 consecutive 16ths) */
    int num_trills = (density >= 14) ? 0 : rng_range(0, 2);
    for (int t = 0; t < num_trills; t++) {
        int start = rng_range(0, 12);
        int len = rng_range(2, 4);
        for (int i = 0; i < len && start + i < 16; i++)
            p[start + i] = 1;
    }

    /* 5. Place open hihats: 0-3 per bar (skip for very dense patterns) */
    /*    Avoid clap positions to prevent clashing */
    int num_opens = (density >= 14) ? 0 : rng_range(0, 3);
    for (int o = 0; o < num_opens; o++) {
        int pos;
        int attempts = 0;
        do {
            if (rng_float() < 0.6f)
                pos = rng_range(0, 7) * 2 + 1;
            else
                pos = rng_range(0, 3) * 4 + 3;
            attempts++;
        } while (attempts < 8 && pos < 16 && (
            /* Avoid clap positions ±1 step */
            (g_seq.time_sig == 0 && (abs(pos-4) <= 1 || abs(pos-12) <= 1)) ||
            (g_seq.time_sig == 1 && abs(pos-8) <= 1) ||
            (g_seq.time_sig == 2 && abs(pos-6) <= 1)
        ));
        if (pos < 16) {
            p[pos] = 2;
        }
    }

    /* 6. Thin out to target density: randomly remove some closed hits */
    int count = 0;
    for (int s = 0; s < 16; s++) if (p[s]) count++;
    while (count > density) {
        int r = rng_range(0, 15);
        if (p[r] == 1 && r != 0) {  /* don't remove beat 1, don't remove opens */
            p[r] = 0;
            count--;
        }
    }

    /* 7. Ensure beat 1 always has a hit */
    if (p[0] == 0) p[0] = 1;
}

void audio_set_time_sig(int ts)
{
    g_seq.time_sig = ts;
    g_seq.loop_steps = (ts == 0) ? 16 : 12;
    /* Reset step if beyond new loop length */
    if (g_seq.current_step >= g_seq.loop_steps)
        g_seq.current_step = 0;
}

void audio_rec_start(void)
{
    g_rec.pos = 0;
    g_rec.recording = 1;
}

int audio_rec_stop(const char *filename)
{
    SDL_LockAudioDevice(g_audio_dev);
    g_rec.recording = 0;
    int num_samples = g_rec.pos;
    SDL_UnlockAudioDevice(g_audio_dev);
    if (num_samples == 0) return 0;

    FILE *f = fopen(filename, "wb");
    if (!f) return 0;

    /* WAV header */
    int data_size = num_samples * (int)sizeof(int16_t);
    int file_size = 36 + data_size;
    int16_t *pcm = (int16_t *)malloc(num_samples * sizeof(int16_t));
    if (!pcm) { fclose(f); return 0; }

    /* Convert float -> 16-bit PCM */
    for (int i = 0; i < num_samples; i++) {
        float s = g_rec.buffer[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        pcm[i] = (int16_t)(s * 32767.0f);
    }

    /* RIFF header */
    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, f);
    int fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    int16_t audio_fmt = 1;  /* PCM */
    fwrite(&audio_fmt, 2, 1, f);
    int16_t channels = 1;
    fwrite(&channels, 2, 1, f);
    int sample_rate = SAMPLE_RATE;
    fwrite(&sample_rate, 4, 1, f);
    int byte_rate = SAMPLE_RATE * 2;
    fwrite(&byte_rate, 4, 1, f);
    int16_t block_align = 2;
    fwrite(&block_align, 2, 1, f);
    int16_t bits = 16;
    fwrite(&bits, 2, 1, f);

    /* data chunk */
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fwrite(pcm, sizeof(int16_t), num_samples, f);

    fclose(f);
    free(pcm);
    return 1;
}

void audio_init(void)
{
    /* Zero all state */
    memset(&g_dsp, 0, sizeof(g_dsp));
    memset(&g_seq, 0, sizeof(g_seq));
    memset(&g_wave, 0, sizeof(g_wave));
    memset(&g_rec, 0, sizeof(g_rec));
    g_rec.buffer = (float *)malloc(REC_MAX_SAMPLES * sizeof(float));

    /* Default DSP parameters */
    g_dsp.freq = 220.0f;
    g_dsp.target_freq = 220.0f;
    g_dsp.base_cutoff = 0.35f;
    g_dsp.flt_resonance = 0.70f;

    /* Default sequencer state */
    audio_set_bpm(138.0f);
    g_seq.volume = 0.5f;
    g_seq.mute_samples = 0;
    g_seq.style_select = -1;
    g_seq.time_sig = 0;     /* 4/4 */
    g_seq.shuffle = 0;
    g_seq.loop_steps = 16;
    g_seq.kick_enabled = 0;
    g_seq.hihat_enabled = 0;
    g_seq.clap_enabled = 0;
    g_seq.kick.freq_start = 170.0f;
    g_seq.kick.freq_end = 38.0f;
    g_seq.kick.freq_decay = 0.0009f;
    g_seq.kick.amp_decay = 0.0003f;
    g_seq.kick.amp = 0.0f;
    g_seq.kick.phase = 0.0f;
    memset(&g_seq.hihat, 0, sizeof(g_seq.hihat));
    g_seq.hihat.noise_seed = 54321;
    audio_generate_hihat_pattern();
    memset(&g_seq.clap, 0, sizeof(g_seq.clap));
    g_seq.clap.noise_seed = 98765;
    g_seq.bass_enabled = 0;
    memset(&g_seq.bass, 0, sizeof(g_seq.bass));
    g_seq.bass_active_pattern = 0;
    g_seq.bass_pending_swap = 0;
    g_seq.playing = 0;

    /* Generate initial pattern and start playing */
    s_rng_state = 12345;
    audio_generate_pattern();
    /* Force swap immediately (no audio thread running yet) */
    g_seq.active_pattern = 1 - g_seq.active_pattern;
    g_seq.pending_swap = 0;
    g_seq.bass_active_pattern = 1 - g_seq.bass_active_pattern;
    g_seq.bass_pending_swap = 0;
    g_seq.current_step = 0;
    g_seq.step_accumulator = 0.0f;
    /* Apply sonic preset from generated pattern */
    {
        Pattern *pat = &g_seq.patterns[g_seq.active_pattern];
        g_dsp.base_cutoff = pat->cutoff;
        g_dsp.flt_resonance = pat->resonance;
    }
    g_seq.playing = 1;

    /* Trigger the first step */
    Step *first = &g_seq.patterns[g_seq.active_pattern].steps[0];
    if (first->gate) {
        g_dsp.freq = midi_to_freq(first->note);
        g_dsp.target_freq = g_dsp.freq;
        trigger_note(&g_dsp, first->accent);
    }

    /* Create audio device: 44100 Hz, 32-bit float, mono */
    SDL_AudioSpec want = {0}, have;
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = STREAM_CHANNELS;
    want.samples = 1024;
    want.callback = audio_callback;
    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    SDL_PauseAudioDevice(g_audio_dev, 0);  /* start playing */
}

void audio_shutdown(void)
{
    g_seq.playing = 0;
    SDL_CloseAudioDevice(g_audio_dev);
    free(g_rec.buffer);
    g_rec.buffer = NULL;
}
