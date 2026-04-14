; dsp.asm - x86-64 NASM DSP core for NARINA acid techno synth
; Windows x64 calling convention: rcx=1st, rdx=2nd, r8=3rd
; Must preserve: rbx, rbp, rdi, rsi, r12-r15, xmm6-xmm15

default rel
bits 64

; ============================================================
; DspState struct offsets (MUST match audio.h byte-for-byte)
; ============================================================
; typedef struct DspState {
;     float phase;            //  0  sawtooth phase 0..1
;     float freq;             //  4  current frequency Hz
;     float target_freq;      //  8  slide target frequency
;     float slide_rate;       // 12  exponential slide coeff
;     float flt_low;          // 16  SVF low-pass state
;     float flt_band;         // 20  SVF band-pass state
;     float flt_high;         // 24  SVF high-pass state
;     float base_cutoff;      // 28  base cutoff 0..1
;     float flt_resonance;    // 32  resonance 0..1
;     float flt_env;          // 36  filter envelope 0..1
;     float flt_env_attack;   // 40  attack coeff per sample
;     float flt_env_decay;    // 44  decay coeff per sample
;     float flt_env_depth;    // 48  mod depth in cutoff units
;     int   flt_env_stage;    // 52  0=off, 1=attack, 2=decay
;     float amp_env;          // 56  amplitude 0..1
;     float amp_env_attack;   // 60  attack coeff per sample
;     float amp_env_decay;    // 64  decay coeff per sample
;     float amp_level;        // 68  note volume (accent)
;     int   amp_env_stage;    // 72  0=off, 1=attack, 2=decay
; } DspState;                 // 76 bytes total

%define OFF_PHASE           0
%define OFF_FREQ            4
%define OFF_TARGET_FREQ     8
%define OFF_SLIDE_RATE     12
%define OFF_FLT_LOW        16
%define OFF_FLT_BAND       20
%define OFF_FLT_HIGH       24
%define OFF_BASE_CUTOFF    28
%define OFF_FLT_RESONANCE  32
%define OFF_FLT_ENV        36
%define OFF_FLT_ENV_ATTACK 40
%define OFF_FLT_ENV_DECAY  44
%define OFF_FLT_ENV_DEPTH  48
%define OFF_FLT_ENV_STAGE  52
%define OFF_AMP_ENV        56
%define OFF_AMP_ENV_ATTACK 60
%define OFF_AMP_ENV_DECAY  64
%define OFF_AMP_LEVEL      68
%define OFF_AMP_ENV_STAGE  72

%define SAMPLE_RATE_F 44100.0

section .rdata align=16
    ; Float constants
    align 4
    const_zero:       dd 0.0
    const_one:        dd 1.0
    const_two:        dd 2.0
    const_sample_rate:dd 44100.0
    const_cutoff_max: dd 0.65
    const_cutoff_min: dd 0.005
    const_q_min:      dd 0.02
    const_27:         dd 27.0
    const_9:          dd 9.0
    const_0_4:        dd 0.4

section .text

; ============================================================
; void dsp_init(DspState *state)
; rcx = state pointer
; Zeros filter states (flt_low, flt_band, flt_high, envelopes)
; ============================================================
global dsp_init
dsp_init:
    ; Zero filter states
    xorps xmm0, xmm0
    movss [rcx + OFF_FLT_LOW],  xmm0
    movss [rcx + OFF_FLT_BAND], xmm0
    movss [rcx + OFF_FLT_HIGH], xmm0

    ; Zero envelopes
    movss [rcx + OFF_FLT_ENV],  xmm0
    mov   dword [rcx + OFF_FLT_ENV_STAGE], 0
    movss [rcx + OFF_AMP_ENV],  xmm0
    mov   dword [rcx + OFF_AMP_ENV_STAGE], 0

    ; Zero phase
    movss [rcx + OFF_PHASE], xmm0

    ret

; ============================================================
; void dsp_note_on(DspState *state)
; rcx = state pointer
; Triggers filter and amplitude envelopes.
; NOTE: C code calls dsp_note_on_c(state, accent) with accent
; param, but the NASM version reads accent info from the
; already-set amp_level/flt_env_depth/flt_env_decay fields.
; The C sequencer code sets these fields before calling.
; So this function just resets envelope phase and stage.
; ============================================================
global dsp_note_on
dsp_note_on:
    xorps xmm0, xmm0

    ; Reset filter envelope
    movss [rcx + OFF_FLT_ENV], xmm0
    mov   dword [rcx + OFF_FLT_ENV_STAGE], 1   ; attack stage

    ; Reset amplitude envelope
    movss [rcx + OFF_AMP_ENV], xmm0
    mov   dword [rcx + OFF_AMP_ENV_STAGE], 1   ; attack stage

    ret

; ============================================================
; void dsp_process_block(DspState *state, float *output, uint32_t frame_count)
; rcx = DspState *state
; rdx = float *output
; r8d = uint32_t frame_count
;
; The hot inner loop. Generates frame_count samples of:
;   sawtooth -> SVF filter (with tanh) -> amplitude envelope
; ============================================================
global dsp_process_block
dsp_process_block:
    ; Prologue: save non-volatile registers
    push  rbx
    push  rdi
    push  rsi
    push  r12
    ; Save non-volatile XMM registers we use (xmm6-xmm10)
    ; 4 pushes (32 bytes) + sub 88 = 120 bytes; entry rsp is 8 mod 16 => 8-120 = 0 mod 16 (aligned)
    sub   rsp, 88              ; 80 bytes for 5 xmm regs + 8 padding for 16-byte alignment
    movdqu [rsp +  0], xmm6
    movdqu [rsp + 16], xmm7
    movdqu [rsp + 32], xmm8
    movdqu [rsp + 48], xmm9
    movdqu [rsp + 64], xmm10

    ; Store args in non-volatile registers
    mov   rbx, rcx             ; rbx = DspState *state
    mov   rdi, rdx             ; rdi = float *output
    mov   esi, r8d             ; esi = frame_count (loop counter)

    ; Load constants into non-volatile xmm registers for the loop
    movss xmm6, [const_sample_rate]  ; xmm6 = 44100.0
    movss xmm7, [const_one]          ; xmm7 = 1.0
    movss xmm8, [const_two]          ; xmm8 = 2.0
    movss xmm9, [const_27]           ; xmm9 = 27.0
    movss xmm10, [const_9]           ; xmm10 = 9.0

    ; r12d = loop index
    xor   r12d, r12d

    test  esi, esi
    jz    .loop_end

.loop_top:
    ; ---- Frequency slide ----
    movss xmm0, [rbx + OFF_SLIDE_RATE]
    xorps xmm1, xmm1
    ucomiss xmm0, xmm1
    jbe   .no_slide

    ; freq += (target_freq - freq) * slide_rate
    movss xmm2, [rbx + OFF_TARGET_FREQ]
    movss xmm3, [rbx + OFF_FREQ]
    subss xmm2, xmm3          ; xmm2 = target_freq - freq
    mulss xmm2, xmm0          ; xmm2 *= slide_rate
    addss xmm3, xmm2          ; freq += delta
    movss [rbx + OFF_FREQ], xmm3
.no_slide:

    ; ---- Sawtooth oscillator ----
    ; phase_inc = freq / SAMPLE_RATE
    movss xmm0, [rbx + OFF_FREQ]
    divss xmm0, xmm6          ; xmm0 = phase_inc

    ; saw = 2 * phase - 1
    movss xmm1, [rbx + OFF_PHASE]
    movaps xmm2, xmm1
    mulss xmm2, xmm8          ; xmm2 = 2 * phase
    subss xmm2, xmm7          ; xmm2 = 2*phase - 1  (this is 'saw')
    ; xmm2 = saw (keep for later)

    ; phase += phase_inc
    addss xmm1, xmm0
    ; if (phase >= 1.0) phase -= 1.0
    ucomiss xmm1, xmm7
    jb    .phase_ok
    subss xmm1, xmm7
.phase_ok:
    movss [rbx + OFF_PHASE], xmm1

    ; xmm2 = saw value, save it in xmm5 for later
    movaps xmm5, xmm2

    ; ---- Filter envelope ----
    mov   eax, [rbx + OFF_FLT_ENV_STAGE]
    test  eax, eax
    jz    .flt_env_done

    cmp   eax, 1
    jne   .flt_env_decay

    ; Attack stage
    movss xmm0, [rbx + OFF_FLT_ENV]
    addss xmm0, [rbx + OFF_FLT_ENV_ATTACK]
    ucomiss xmm0, xmm7
    jb    .flt_env_store
    ; Reached 1.0 -> switch to decay
    movaps xmm0, xmm7
    mov   dword [rbx + OFF_FLT_ENV_STAGE], 2
    jmp   .flt_env_store

.flt_env_decay:
    ; Decay stage (eax == 2)
    movss xmm0, [rbx + OFF_FLT_ENV]
    subss xmm0, [rbx + OFF_FLT_ENV_DECAY]
    xorps xmm1, xmm1
    ucomiss xmm0, xmm1
    ja    .flt_env_store
    ; Reached 0.0 -> stage off
    xorps xmm0, xmm0
    mov   dword [rbx + OFF_FLT_ENV_STAGE], 0

.flt_env_store:
    movss [rbx + OFF_FLT_ENV], xmm0
    ; Reload xmm7 = 1.0 (may have been clobbered)
    movss xmm7, [const_one]
.flt_env_done:

    ; ---- Amplitude envelope ----
    mov   eax, [rbx + OFF_AMP_ENV_STAGE]
    test  eax, eax
    jz    .amp_env_done

    cmp   eax, 1
    jne   .amp_env_decay

    ; Attack stage
    movss xmm0, [rbx + OFF_AMP_ENV]
    addss xmm0, [rbx + OFF_AMP_ENV_ATTACK]
    ucomiss xmm0, xmm7
    jb    .amp_env_store
    movaps xmm0, xmm7
    mov   dword [rbx + OFF_AMP_ENV_STAGE], 2
    jmp   .amp_env_store

.amp_env_decay:
    movss xmm0, [rbx + OFF_AMP_ENV]
    subss xmm0, [rbx + OFF_AMP_ENV_DECAY]
    xorps xmm1, xmm1
    ucomiss xmm0, xmm1
    ja    .amp_env_store
    xorps xmm0, xmm0
    mov   dword [rbx + OFF_AMP_ENV_STAGE], 0

.amp_env_store:
    movss [rbx + OFF_AMP_ENV], xmm0
.amp_env_done:

    ; ---- SVF resonant filter ----
    ; cutoff = base_cutoff + flt_env_depth * flt_env
    movss xmm0, [rbx + OFF_FLT_ENV_DEPTH]
    mulss xmm0, [rbx + OFF_FLT_ENV]
    addss xmm0, [rbx + OFF_BASE_CUTOFF]

    ; Clamp cutoff to [0.01, 0.99]
    movss xmm1, [const_cutoff_max]
    ucomiss xmm0, xmm1
    jb    .cutoff_not_high
    movaps xmm0, xmm1
.cutoff_not_high:
    movss xmm1, [const_cutoff_min]
    ucomiss xmm0, xmm1
    ja    .cutoff_not_low
    movaps xmm0, xmm1
.cutoff_not_low:

    ; f = 2.0 * cutoff
    mulss xmm0, xmm8          ; xmm0 = f (2 * cutoff)

    ; q = (1.0 - resonance)^2  -- squared for aggressive curve at high reso
    movss xmm1, xmm7          ; xmm1 = 1.0
    subss xmm1, [rbx + OFF_FLT_RESONANCE]   ; xmm1 = (1 - reso)
    mulss xmm1, xmm1          ; xmm1 = (1 - reso)^2  -- squaring makes top end extreme
    movss xmm3, [const_q_min]
    ucomiss xmm1, xmm3
    ja    .q_ok
    movaps xmm1, xmm3
.q_ok:
    ; xmm0 = f, xmm1 = q, xmm5 = saw

    ; flt_low += f * flt_band
    movss xmm2, [rbx + OFF_FLT_BAND]
    movaps xmm3, xmm0
    mulss xmm3, xmm2          ; xmm3 = f * flt_band
    movss xmm4, [rbx + OFF_FLT_LOW]
    addss xmm4, xmm3          ; flt_low += f * flt_band
    movss [rbx + OFF_FLT_LOW], xmm4

    ; flt_high = saw - flt_low - q * flt_band
    movaps xmm3, xmm5         ; saw
    subss xmm3, xmm4          ; saw - flt_low
    movaps xmm4, xmm1         ; q
    mulss xmm4, xmm2          ; q * flt_band
    subss xmm3, xmm4          ; saw - flt_low - q * flt_band
    movss [rbx + OFF_FLT_HIGH], xmm3

    ; flt_band += f * flt_high
    movaps xmm4, xmm0
    mulss xmm4, xmm3          ; f * flt_high
    addss xmm2, xmm4          ; flt_band += f * flt_high

    ; flt_band = fast_tanh(flt_band)
    ; tanh(x) ~ x * (27 + x^2) / (27 + 9 * x^2)
    movaps xmm3, xmm2
    mulss xmm3, xmm2          ; xmm3 = x^2
    movaps xmm4, xmm9         ; xmm4 = 27.0
    addss xmm4, xmm3          ; xmm4 = 27 + x^2  (numerator part)
    mulss xmm4, xmm2          ; xmm4 = x * (27 + x^2)
    movaps xmm0, xmm10        ; xmm0 = 9.0
    mulss xmm0, xmm3          ; xmm0 = 9 * x^2
    addss xmm0, xmm9          ; xmm0 = 27 + 9*x^2 (denominator)
    divss xmm4, xmm0          ; xmm4 = tanh(flt_band)
    movss [rbx + OFF_FLT_BAND], xmm4

    ; ---- Output: flt_low * amp_env * amp_level * 0.4 ----
    movss xmm0, [rbx + OFF_FLT_LOW]
    mulss xmm0, [rbx + OFF_AMP_ENV]
    mulss xmm0, [rbx + OFF_AMP_LEVEL]
    mulss xmm0, [const_0_4]

    ; Store sample to output buffer
    movss [rdi + r12 * 4], xmm0

    ; Advance loop
    inc   r12d
    cmp   r12d, esi
    jb    .loop_top

.loop_end:
    ; Epilogue: restore non-volatile registers
    movdqu xmm6,  [rsp +  0]
    movdqu xmm7,  [rsp + 16]
    movdqu xmm8,  [rsp + 32]
    movdqu xmm9,  [rsp + 48]
    movdqu xmm10, [rsp + 64]
    add   rsp, 88

    pop   r12
    pop   rsi
    pop   rdi
    pop   rbx
    ret
