#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <SDL.h>

#define SAMPLE_RATE     44100
#define STREAM_CHANNELS 1
#define STREAM_BITS     32

/* Ring buffer for waveform visualization */
#define WAVE_BUF_LEN    2048

/* ---- DspState: all synthesis state in one struct ---- */
typedef struct DspState {
    /* Oscillator */
    float phase;            /* 0..1 sawtooth phase accumulator */
    float freq;             /* current frequency in Hz */
    float target_freq;      /* slide target frequency */
    float slide_rate;       /* exponential slide coefficient (0 = no slide) */

    /* Filter (SVF) */
    float flt_low;          /* SVF low-pass state */
    float flt_band;         /* SVF band-pass state */
    float flt_high;         /* SVF high-pass state */
    float base_cutoff;      /* base cutoff 0..1 (set by UI slider) */
    float flt_resonance;    /* resonance 0..1 (set by UI slider) */

    /* Filter envelope */
    float flt_env;          /* current filter envelope value 0..1 */
    float flt_env_attack;   /* attack coefficient per sample */
    float flt_env_decay;    /* decay coefficient per sample */
    float flt_env_depth;    /* modulation depth in cutoff units */
    int   flt_env_stage;    /* 0=off, 1=attack, 2=decay */

    /* Amplitude envelope */
    float amp_env;          /* current amplitude 0..1 */
    float amp_env_attack;   /* attack coefficient per sample */
    float amp_env_decay;    /* decay coefficient per sample */
    float amp_level;        /* note volume (accent scaling) */
    int   amp_env_stage;    /* 0=off, 1=attack, 2=decay */
} DspState;

/* ---- Step: one 16th-note in a pattern ---- */
typedef struct Step {
    uint8_t note;       /* MIDI note number (0 = rest) */
    uint8_t gate;       /* 1 = trigger, 0 = rest/tie */
    uint8_t accent;     /* 1 = accented */
    uint8_t slide;      /* 1 = portamento to next note */
    uint8_t tie;        /* 1 = tied from previous step */
    uint8_t pad[3];     /* padding to 8 bytes */
} Step;

/* ---- Pattern: 1 bar of 16 steps, looped ---- */
#define STEPS_PER_BAR  16
#define TOTAL_STEPS    STEPS_PER_BAR

typedef struct Pattern {
    Step steps[TOTAL_STEPS];
    float cutoff;           /* sonic preset: base filter cutoff */
    float resonance;        /* sonic preset: filter resonance */
    float env_decay_base;   /* sonic preset: envelope decay time */
    float env_depth_base;   /* sonic preset: envelope depth */
} Pattern;

/* ---- Kick drum state (simple sine with pitch sweep) ---- */
typedef struct KickState {
    float phase;
    float freq;
    float freq_start;
    float freq_end;
    float freq_decay;
    float amp;
    float amp_decay;
} KickState;

/* ---- Hihat state (metallic oscillators + noise + bandpass) ---- */
typedef struct HihatState {
    float phases[6];        /* 6 detuned square oscillators */
    float amp;
    float amp_decay;
    unsigned int noise_seed;
    float bp_low;           /* bandpass filter states */
    float bp_band;
    uint8_t pattern[16];    /* 0=off, 1=closed, 2=open */
} HihatState;

/* ---- Clap state (filtered noise burst) ---- */
typedef struct ClapState {
    unsigned int noise_seed;
    float amp;
    float amp_decay;
    float filter_state;
    float filter_coeff;
    int   retrigger;        /* samples until next micro-burst */
    int   bursts_left;      /* remaining bursts for "flam" effect */
} ClapState;

/* ---- Bassline (disabled, kept for iteration) ---- */
typedef struct BassState {
    float phase;
    float freq;
    float target_freq;
    float slide_rate;
    float flt_state;
    float flt_state2;
    float flt_coeff;
    float amp;
    float amp_decay;
    int   amp_stage;
} BassState;

typedef struct BassPattern {
    uint8_t notes[TOTAL_STEPS];
    uint8_t gates[TOTAL_STEPS];
    uint8_t slides[TOTAL_STEPS];
} BassPattern;

/* ---- Sequencer: playback state ---- */
typedef struct Sequencer {
    Pattern patterns[2];        /* double buffer for lock-free swap */
    volatile int active_pattern; /* 0 or 1 — which pattern is playing */
    volatile int pending_swap;   /* 1 = swap to other pattern at next loop boundary */
    int     current_step;       /* 0..STEPS_PER_BAR-1 */
    float   bpm;
    float   samples_per_step;   /* precomputed from bpm */
    float   step_accumulator;   /* fractional sample counter within step */
    int     playing;            /* 1 = sequencer is running */
    float   volume;             /* master volume 0..1 */
    int     mute_samples;       /* samples of silence remaining (for generate gap) */
    int     style_select;       /* -1 = random, 0..NUM_STYLES-1 = locked */
    int     time_sig;           /* 0=4/4, 1=3/4, 2=6/8 */
    int     shuffle;            /* 0=off, 1=on */
    int     loop_steps;         /* effective loop length (16 or 12) */
    int     kick_enabled;
    int     hihat_enabled;
    int     clap_enabled;
    int     bass_enabled;       /* disabled, kept for iteration */
    KickState kick;
    HihatState hihat;
    ClapState clap;
    BassState bass;
    BassPattern bass_patterns[2];
    volatile int bass_active_pattern;
    volatile int bass_pending_swap;
} Sequencer;

/* ---- Waveform ring buffer (written by audio thread, read by UI) ---- */
typedef struct WaveBuffer {
    float   data[WAVE_BUF_LEN];
    volatile int write_pos;
} WaveBuffer;

/* ---- DSP functions (dsp.asm on x86-64 Windows, dsp.c elsewhere) ---- */
extern void dsp_init(DspState *state);
extern void dsp_process_block(DspState *state, float *output, uint32_t frame_count);
extern void dsp_note_on(DspState *state);

/* ---- Recording state ---- */
#define REC_MAX_SAMPLES (SAMPLE_RATE * 300)  /* max 5 minutes */

typedef struct RecState {
    float  *buffer;         /* malloc'd on init */
    int     pos;            /* current write position */
    int     recording;      /* 1 = recording active */
} RecState;

/* ---- Public API ---- */
void audio_init(void);
void audio_shutdown(void);
void audio_generate_pattern(void);
void audio_set_bpm(float bpm);
void audio_set_volume(float vol);
void audio_set_time_sig(int ts);
void audio_generate_hihat_pattern(void);
void audio_rec_start(void);
int  audio_rec_stop(const char *filename);  /* returns 1 on success */

/* Globals accessible from main.c for visualization / UI */
extern DspState    g_dsp;
extern Sequencer   g_seq;
extern WaveBuffer  g_wave;
extern RecState    g_rec;
extern SDL_AudioDeviceID g_audio_dev;

#endif /* AUDIO_H */
