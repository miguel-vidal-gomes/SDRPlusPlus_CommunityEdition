#ifndef RNNOISE_H
#define RNNOISE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DenoiseState DenoiseState;

/**
 * Allocates and initializes a new denoising state
 *
 * @return Newly created denoising state
 */
DenoiseState *rnnoise_create(void *model);

/**
 * Destroys a denoising state
 *
 * @param st Denoising state to be destroyed
 */
void rnnoise_destroy(DenoiseState *st);

/**
 * Processes a frame of audio samples through the denoiser
 *
 * @param st Denoising state
 * @param out Output audio samples (size: frame_size)
 * @param in Input audio samples (size: frame_size)
 * @return VAD probability (0.0 - 1.0) that the frame contains voice
 */
float rnnoise_process_frame(DenoiseState *st, float *out, const float *in);

/**
 * Returns the frame size used by the denoiser
 *
 * @return Frame size in samples
 */
int rnnoise_get_frame_size(void);

/**
 * Parameter control API
 */

/**
 * Set the reduction amount (0.0 to 1.0)
 * 
 * @param st Denoising state
 * @param amount Reduction amount (0.0 = minimal, 1.0 = maximum)
 */
void rnnoise_set_reduction_amount(DenoiseState *st, float amount);

/**
 * Set whether to use VAD-based gating
 * 
 * @param st Denoising state
 * @param use_vad_gating Whether to use VAD gating
 */
void rnnoise_set_vad_gating(DenoiseState *st, bool use_vad_gating);

/**
 * Set the VAD threshold for gating (0.0 to 1.0)
 * 
 * @param st Denoising state
 * @param threshold VAD threshold
 */
void rnnoise_set_vad_threshold(DenoiseState *st, float threshold);

/**
 * Set the VAD grace period in frames
 * 
 * @param st Denoising state
 * @param grace_period Grace period in frames
 */
void rnnoise_set_vad_grace_period(DenoiseState *st, int grace_period);

/**
 * Set the output gain multiplier
 * 
 * @param st Denoising state
 * @param gain Output gain multiplier
 */
void rnnoise_set_output_gain(DenoiseState *st, float gain);

#ifdef __cplusplus
}
#endif

#endif /* RNNOISE_H */