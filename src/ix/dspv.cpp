/**
 * @file
 * @ingroup ixlib
 * @brief Voice control.
 *
 * Originally `D:\rnd\prj\ix\win95\dsp\dspv.cpp`, address range
 * 4467a0-446f73: compiler-generated initializers, then the voice-control
 * functions. The separately assembled sample mixer, dspmix.asm, follows.
 *
 * Built /Od like the rest of ix: a full EBP frame, EBX, ESI and EDI saved
 * whether used or not, and one shared epilogue.
 */
#include <signal.h>
#include "ix.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

IxVoice aVoices[34];             /* 0x005981A8 */
CRITICAL_SECTION csMixer;       /* 0x005985E8 */
int nVoiceCount;                /* 0x00598600 */
int nVoicesAllocated;           /* 0x00598604 */

/* Function start: 0x4467C5 */   /* source line 23 */
void ix_dspv_set_active(int voice)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 23);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    aVoices[voice].flags |= IX_VOICE_ACTIVE;
}

/* Function start: 0x446829 */   /* source line 29 */
void ix_dspv_clear_active(int voice)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 29);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    aVoices[voice].flags &= ~IX_VOICE_ACTIVE;
}

/* Function start: 0x44688E */   /* source line 35 */
void ix_dspv_clear_flag4(int voice)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 35);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    aVoices[voice].flags &= ~IX_VOICE_FLAG4;
}

/* Function start: 0x4468F2 */   /* source line 41 */
unsigned int ix_dspv_get_flags(int voice)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 41);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    return aVoices[voice].flags;
}

/* Function start: 0x446956 */
/* No bounds check in the original -- callers are internal. */
void ix_dspv_set_flag4(int voice, int on)
{
    if (on != 0)
        aVoices[voice].flags |= IX_VOICE_FLAG4;
    else
        aVoices[voice].flags &= ~IX_VOICE_FLAG4;
}

/* Function start: 0x44698F */   /* source line 55 */
void ix_dspv_set_buffer(int voice, unsigned char *p, int len)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 55);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    IxVoice *v = &aVoices[voice];

    EnterCriticalSection(&csMixer);
    v->cursor = p;
    v->start = p;
    v->end = p + len;
    LeaveCriticalSection(&csMixer);
}

/* Function start: 0x446A2B */   /* source line 69 */
int ix_dspv_get_position(int voice)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 69);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    IxVoice *v = &aVoices[voice];

#ifdef SDL_PORT
    /* unsigned long cannot hold a pointer on 64-bit Windows. */
    return (v->cursor - v->start);
#else
    return ((unsigned long)v->cursor - (unsigned long)v->start);
#endif
}

/* Function start: 0x446AA0 */   /* source line 77 */
void ix_dspv_set_position(int voice, int off)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 77);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    IxVoice *v = &aVoices[voice];

    v->cursor = v->start + off;
}

/* Function start: 0x446B18 */   /* source line 85 */
void ix_dspv_set_volume(int voice, short vol)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 85);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    aVoices[voice].volume = vol;
    ix_dspv_recalc_mix(voice);
}

/* Function start: 0x446B8C */   /* source line 93 */
void ix_dspv_set_pan(int voice, unsigned short angle)
{
    short pos;
    {
        IxVoice *state;
        {
            short delta;
            {
                short step;

                if (voice < 0 || nVoiceCount <= voice) {
                    ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 93);
                    ix_log_printf("%d Invalid voice index!", voice);
                    exit(-1);
                }
                state = &aVoices[voice];
                step = ((unsigned short)angle >> 9);
                delta = step - state->panAngle;
                if (delta > 0x40)
                    delta = delta - 0x80;
                if (delta < -0x40)
                    delta = delta + 0x80;
                pos = state->panPos + delta;
                if (pos < 0)
                    pos = pos + 0x100;
                if (pos >= 0x100)
                    pos = pos - 0x100;
                state->panAngle = step;
                state->panPos = pos;
                ix_dspv_recalc_mix(voice);
            }
        }
    }
}

#ifdef SDL_PORT
void ix_dspv_set_spatial(int voice, int earDelay, int rearAmount)
{
    IxVoice *state;

    if (voice < 0 || nVoiceCount <= voice)
        return;
    state = &aVoices[voice];
    state->earDelay = (short)earDelay;
    state->rearAmount = (short)rearAmount;
    state->lowpassLeft = 0;
    state->lowpassRight = 0;
    ix_dspv_recalc_mix(voice);
}
#endif

/* Function start: 0x446CB1 */   /* source line 131 */
void ix_dspv_set_frequency(int voice, unsigned int hz)
{
    if (voice < 0 || nVoiceCount <= voice) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 131);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    aVoices[voice].rate =
        (short)(((int)(hz & 0xffff) << 8) / IX_MIXER_BASE_RATE);
}

/* Function start: 0x446D2C */   /* source lines 137, 138 */
void ix_dspv_set_bits_per_sample(int voice, int bps)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 137);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    if (bps != 8 && bps != 16) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 138);
        ix_log_printf("%d is an unsupported bps!", bps);
        exit(-1);
    }
    if (bps == 16)
        aVoices[voice].flags |= IX_VOICE_16BIT;
    else
        aVoices[voice].flags &= ~IX_VOICE_16BIT;
}

/* Function start: 0x446DF5 */   /* source lines 148, 149 */
void ix_dspv_set_channels(int voice, int channels)
{
    if (voice < 0 || voice >= nVoiceCount) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 148);
        ix_log_printf("%d Invalid voice index!", voice);
        exit(-1);
    }
    if (channels != 1 && channels != 2) {
        ix_log_printf("Fatal [%s - %d]:\n", IX_DSPV_FILE, 149);
        ix_log_printf("%d is an unsupported number of channels!", channels);
        exit(-1);
    }
    if (channels == 2)
        aVoices[voice].flags |= IX_VOICE_STEREO;
    else
        aVoices[voice].flags &= ~IX_VOICE_STEREO;
}

/* Function start: 0x446EBF */
void ix_dspv_recalc_mix(int voice)
{
    IxVoice *voiceState;
    short panPosition;
    int scaledVolume;

    voiceState = &aVoices[voice];
    panPosition = voiceState->panPos;
    scaledVolume = (voiceState->volume
                    * (unsigned int)nMasterVolume) / 0xffff;
    EnterCriticalSection(&csMixer);
    voiceState->leftGain =
        (asPanGains[panPosition * 2] * scaledVolume >> 0x10);
    voiceState->rightGain =
        (asPanGains[panPosition * 2 + 1] * scaledVolume >> 0x10);
#ifdef SDL_PORT
    /* Sources behind the listener lose up to 2.5 dB. */
    voiceState->leftGain = (short)(voiceState->leftGain *
        (0x100 - voiceState->rearAmount * 75 / 0x100) >> 8);
    voiceState->rightGain = (short)(voiceState->rightGain *
        (0x100 - voiceState->rearAmount * 75 / 0x100) >> 8);
#endif
    voiceState->leftGainHi = (voiceState->leftGain >> 8);
    voiceState->rightGainHi = (voiceState->rightGain >> 8);
    LeaveCriticalSection(&csMixer);
}

/* Portable counterpart of the assembled sample mixer in dspmix.asm. */
#ifdef SDL_PORT
/* One channel of the voice's current frame, read delayFrames source frames
   behind the cursor (wrapping inside a looping sample, silent before the
   start of a one-shot one). */
int ReadVoiceSample(const IxVoice *state, const unsigned char *cursor,
                           int delayFrames, unsigned int bytesPerFrame,
                           int is16, int channelOffset)
{
    const unsigned char *at;

    at = cursor;
    if (delayFrames != 0) {
        ptrdiff_t behind = (ptrdiff_t)delayFrames * bytesPerFrame;

        if (cursor - state->start >= behind) {
            at = cursor - behind;
        } else if ((state->flags & IX_VOICE_FLAG4) != 0 &&
                   state->end - state->start >= behind) {
            at = state->end - (behind - (cursor - state->start));
        } else {
            return 0;
        }
    }
    at += channelOffset;
    if (is16) {
        short sample;

        memcpy(&sample, at, sizeof(sample));
        return sample;
    }
    return *(const signed char *)at;
}

void ix_dspv_mix(void *outputBuffer, unsigned int outputBytes)
{
    short *output;
    unsigned int frameCount;
    int voice;

    memset(outputBuffer, 0, outputBytes);
    output = (short *)outputBuffer;
    frameCount = outputBytes / 4;
    voice = 0;
    while (voice < nVoiceCount + nStreamCount) {
        IxVoice *state;

        state = &aVoices[voice];
        if ((state->flags & IX_VOICE_ACTIVE) != 0) {
            unsigned char *cursor;
            unsigned int frame;

            cursor = state->cursor;
            frame = 0;
            while (frame < frameCount) {
                int left;
                int right;
                int mixed;
                unsigned int bytesPerFrame;
                unsigned short phase;
                unsigned int advance;

                bytesPerFrame = (state->flags & IX_VOICE_16BIT) != 0 ? 2 : 1;
                if ((state->flags & IX_VOICE_STEREO) != 0)
                    bytesPerFrame *= 2;
                if (cursor >= state->end) {
                    if ((state->flags & IX_VOICE_FLAG4) == 0) {
                        state->flags &= ~IX_VOICE_ACTIVE;
                        break;
                    }
                    cursor = state->start;
                }
                {
                    int is16 = (state->flags & IX_VOICE_16BIT) != 0;
                    int isStereo = (state->flags & IX_VOICE_STEREO) != 0;
                    int sampleBytes = is16 ? 2 : 1;
                    int delayLeft = state->earDelay > 0 ? state->earDelay : 0;
                    int delayRight = state->earDelay < 0 ? -state->earDelay : 0;
                    int sampleLeft;
                    int sampleRight;

                    /* The far ear hears the same source frames a little
                       later; delays are output frames, so convert them to
                       source frames. */
                    delayLeft = (delayLeft * state->rate) >> 8;
                    delayRight = (delayRight * state->rate) >> 8;
                    sampleLeft = ReadVoiceSample(
                        state, cursor, delayLeft, bytesPerFrame, is16, 0);
                    sampleRight = ReadVoiceSample(
                        state, cursor, delayRight, bytesPerFrame, is16,
                        isStereo ? sampleBytes : 0);
                    if (is16) {
                        left = (sampleLeft * state->leftGain) >> 16;
                        right = (sampleRight * state->rightGain) >> 16;
                    } else {
                        left = sampleLeft * (signed char)state->leftGainHi;
                        right = sampleRight * (signed char)state->rightGainHi;
                    }
                    if (state->rearAmount != 0) {
                        /* One-pole low-pass, down to about 1.2 kHz when the
                           source is directly behind. */
                        int alpha = 0x100 - state->rearAmount * 166 / 0x100;

                        state->lowpassLeft +=
                            (left - state->lowpassLeft) * alpha >> 8;
                        state->lowpassRight +=
                            (right - state->lowpassRight) * alpha >> 8;
                        left = state->lowpassLeft;
                        right = state->lowpassRight;
                    }
                }
                mixed = output[frame * 2] + left;
                if (mixed > 0x7fff)
                    mixed = 0x7fff;
                else if (mixed < -0x8000)
                    mixed = -0x8000;
                output[frame * 2] = mixed;
                mixed = output[frame * 2 + 1] + right;
                if (mixed > 0x7fff)
                    mixed = 0x7fff;
                else if (mixed < -0x8000)
                    mixed = -0x8000;
                output[frame * 2 + 1] = mixed;
                frame++;
                if (frame == frameCount)
                    break;
                phase = (unsigned short)state->field_10 +
                        (unsigned short)state->rate;
                state->field_10 = (phase & 0xff);
                advance = phase >> 8;
                if (advance != 0) {
                    cursor += advance * bytesPerFrame;
                    if (cursor >= state->end) {
                        if ((state->flags & IX_VOICE_FLAG4) == 0) {
                            state->flags &= ~IX_VOICE_ACTIVE;
                            break;
                        }
                        cursor = state->start;
                    }
                }
            }
            state->cursor = cursor;
        }
        voice++;
    }
}
#endif
