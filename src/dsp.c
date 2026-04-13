/*
 * dsp.c - C fallback for DSP core (used on non-x86 platforms, e.g. ARM64 macOS)
 * Functionally identical to dsp.asm.
 * Build with DSP_C_FALLBACK defined to use this instead of NASM.
 */
#include "audio.h"
#include <math.h>

#define INV_SAMPLE_RATE (1.0f / 44100.0f)
#define CUTOFF_MAX 0.65f
#define CUTOFF_MIN 0.005f
#define Q_MIN      0.02f

static float fast_tanh(float x)
{
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void dsp_init(DspState *s)
{
    s->flt_low = 0.0f;
    s->flt_band = 0.0f;
    s->flt_high = 0.0f;
    s->flt_env = 0.0f;
    s->flt_env_stage = 0;
    s->amp_env = 0.0f;
    s->amp_env_stage = 0;
    s->phase = 0.0f;
}

void dsp_note_on(DspState *s)
{
    s->flt_env = 0.0f;
    s->flt_env_stage = 1;
    s->amp_env = 0.0f;
    s->amp_env_stage = 1;
}

void dsp_process_block(DspState *s, float *output, uint32_t frame_count)
{
    for (uint32_t i = 0; i < frame_count; i++) {
        /* Frequency slide */
        if (s->slide_rate > 0.0f) {
            s->freq += (s->target_freq - s->freq) * s->slide_rate;
        }

        /* Sawtooth oscillator */
        float saw = 2.0f * s->phase - 1.0f;
        s->phase += s->freq * INV_SAMPLE_RATE;
        if (s->phase >= 1.0f) s->phase -= 1.0f;

        /* Filter envelope */
        if (s->flt_env_stage == 1) {
            s->flt_env += s->flt_env_attack;
            if (s->flt_env >= 1.0f) {
                s->flt_env = 1.0f;
                s->flt_env_stage = 2;
            }
        } else if (s->flt_env_stage == 2) {
            s->flt_env -= s->flt_env_decay;
            if (s->flt_env <= 0.0f) {
                s->flt_env = 0.0f;
                s->flt_env_stage = 0;
            }
        }

        /* Amplitude envelope */
        if (s->amp_env_stage == 1) {
            s->amp_env += s->amp_env_attack;
            if (s->amp_env >= 1.0f) {
                s->amp_env = 1.0f;
                s->amp_env_stage = 2;
            }
        } else if (s->amp_env_stage == 2) {
            s->amp_env -= s->amp_env_decay;
            if (s->amp_env <= 0.0f) {
                s->amp_env = 0.0f;
                s->amp_env_stage = 0;
            }
        }

        /* SVF resonant filter */
        float cutoff = s->base_cutoff + s->flt_env_depth * s->flt_env;
        if (cutoff > CUTOFF_MAX) cutoff = CUTOFF_MAX;
        if (cutoff < CUTOFF_MIN) cutoff = CUTOFF_MIN;

        float f = 2.0f * cutoff;
        float q = (1.0f - s->flt_resonance);
        q = q * q;  /* squared for aggressive curve */
        if (q < Q_MIN) q = Q_MIN;

        s->flt_low += f * s->flt_band;
        s->flt_high = saw - s->flt_low - q * s->flt_band;
        s->flt_band += f * s->flt_high;
        s->flt_band = fast_tanh(s->flt_band);

        /* Output */
        output[i] = s->flt_low * s->amp_env * s->amp_level * 0.4f;
    }
}
