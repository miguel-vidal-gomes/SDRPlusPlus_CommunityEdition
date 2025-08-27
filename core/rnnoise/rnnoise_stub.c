#include "rnnoise.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#define FRAME_SIZE 480

struct DenoiseState {
    float vad_probability;
    float last_gain;
    float noise_level;
    float noise_estimate[FRAME_SIZE];
    int frame_count;
    
    // Control parameters (will be set from the UI)
    float reduction_amount;    // 0.0 to 1.0, controls strength of reduction
    bool use_vad_gating;       // Whether to use VAD-based gating
    float vad_threshold;       // VAD threshold for gating
    int vad_grace_period;      // Frames to keep unmuted after speech
    int grace_counter;         // Counter for grace period
    float output_gain;         // Output gain multiplier
};

DenoiseState *rnnoise_create(void *model) {
    DenoiseState *st = (DenoiseState *)malloc(sizeof(DenoiseState));
    if (st == NULL) return NULL;
    
    st->vad_probability = 0.0f;
    st->last_gain = 1.0f;
    st->noise_level = 0.001f;
    st->frame_count = 0;
    
    // Initialize noise estimate
    for (int i = 0; i < FRAME_SIZE; i++) {
        st->noise_estimate[i] = 0.001f;
    }
    
    // Initialize control parameters with defaults
    st->reduction_amount = 0.7f;  // Default to moderate reduction
    st->use_vad_gating = false;   // Default to no gating
    st->vad_threshold = 0.6f;     // Default threshold
    st->vad_grace_period = 20;    // Default grace period (20 frames)
    st->grace_counter = 0;        // Initialize counter
    st->output_gain = 1.2f;       // Default output gain
    
    return st;
}

void rnnoise_destroy(DenoiseState *st) {
    if (st != NULL) {
        free(st);
    }
}

float rnnoise_process_frame(DenoiseState *st, float *out, const float *in) {
    if (st == NULL) return 0.0f;
    
    // Copy input to output first as a safety measure
    // This ensures that if anything goes wrong, we at least have the original signal
    for (int i = 0; i < FRAME_SIZE; i++) {
        out[i] = in[i];
    }
    
    // If reduction amount is very low, just pass through with output gain
    if (st->reduction_amount < 0.05f) {
        for (int i = 0; i < FRAME_SIZE; i++) {
            out[i] = in[i] * st->output_gain;
        }
        return 0.0f;
    }
    
    st->frame_count++;
    
    // ====== STEP 1: Voice Activity Detection ======
    // Calculate energy and find max sample
    float energy = 0.0f;
    float max_sample = 0.0f;
    
    for (int i = 0; i < FRAME_SIZE; i++) {
        float sample = in[i];
        // Safety check for invalid input
        if (isnan(sample) || isinf(sample)) {
            sample = 0.0f;
        }
        energy += sample * sample;
        float abs_sample = fabsf(sample);
        if (abs_sample > max_sample) {
            max_sample = abs_sample;
        }
    }
    
    energy = sqrtf(energy / FRAME_SIZE);
    
    // Safety check for invalid energy
    if (isnan(energy) || isinf(energy)) {
        energy = 0.001f;
    }
    
    // Improved VAD with adaptive thresholds
    float energy_threshold = 0.001f;
    float energy_ratio = energy / (st->noise_level + 0.0001f);
    float energy_ratio_threshold = 1.2f;
    
    // Calculate VAD probability
    float vad_prob;
    if (energy_ratio > 3.0f) {
        vad_prob = 1.0f;  // Definitely speech
    } else if (energy_ratio > energy_ratio_threshold) {
        // Linear mapping from threshold to 1.0
        vad_prob = (energy_ratio - energy_ratio_threshold) / (3.0f - energy_ratio_threshold);
    } else {
        vad_prob = 0.0f;  // Definitely noise
    }
    
    // Smooth VAD probability with asymmetric time constants
    if (vad_prob > st->vad_probability) {
        // Fast attack for speech onset (50ms time constant)
        st->vad_probability = 0.5f * st->vad_probability + 0.5f * vad_prob;
    } else {
        // Slow decay for speech offset (500ms time constant)
        st->vad_probability = 0.95f * st->vad_probability + 0.05f * vad_prob;
    }
    
    // ====== STEP 2: Noise Estimation ======
    // Only update noise estimate during definite silence
    if (st->vad_probability < 0.2f) {
        // Slow adaptation for noise estimate (1-2 second time constant)
        const float alpha = 0.99f;
        st->noise_level = alpha * st->noise_level + (1.0f - alpha) * energy;
        
        // Update per-sample noise estimate for better noise tracking
        for (int i = 0; i < FRAME_SIZE; i++) {
            float abs_sample = fabsf(in[i]);
            st->noise_estimate[i] = alpha * st->noise_estimate[i] + (1.0f - alpha) * abs_sample;
        }
    }
    
    // ====== STEP 3: Noise Reduction ======
    // Calculate suppression gains based on estimated SNR
    float gains[FRAME_SIZE];
    
    // Apply reduction amount from UI (0.0 = no reduction, 1.0 = max reduction)
    // Scale between 0.3 (minimal) and 1.0 (maximum) reduction
    float reduction_strength = 0.3f + 0.7f * st->reduction_amount;
    
    // First pass - calculate initial gains
    for (int i = 0; i < FRAME_SIZE; i++) {
        float abs_sample = fabsf(in[i]);
        float sample_snr = abs_sample / (st->noise_estimate[i] + 0.0001f);
        
        // Safety check for invalid SNR
        if (isnan(sample_snr) || isinf(sample_snr)) {
            sample_snr = 1.0f;
        }
        
        // More conservative gain calculation to avoid artifacts
        float gain;
        
        if (sample_snr > 5.0f) {
            // High SNR - minimal processing to preserve voice quality
            gain = 1.0f - (0.05f * reduction_strength);
        } else if (sample_snr > 2.0f) {
            // Medium SNR - moderate suppression with non-linear curve
            float t = (sample_snr - 2.0f) / 3.0f;  // 0.0 to 1.0
            gain = 0.7f + 0.3f * t - (0.2f * reduction_strength);
        } else {
            // Low SNR - stronger suppression but avoid metallic artifacts
            gain = 0.4f + 0.15f * sample_snr - (0.2f * reduction_strength);
        }
        
        // Boost gain based on VAD probability to preserve speech
        gain = gain + (1.0f - gain) * (0.7f * st->vad_probability);
        
        // Limit gain - more conservative limits to avoid artifacts
        if (gain < 0.2f) gain = 0.2f;
        if (gain > 1.0f) gain = 1.0f;
        
        gains[i] = gain;
    }
    
    // ====== STEP 4: Smooth Gains to Avoid Artifacts ======
    // Temporal smoothing across frames
    const float frame_smooth = 0.8f;
    st->last_gain = frame_smooth * st->last_gain + (1.0f - frame_smooth) * (gains[0] + gains[FRAME_SIZE/2]) * 0.5f;
    
    // Second pass - apply spectral smoothing to gains
    float smoothed_gains[FRAME_SIZE];
    const int window_size = 2;  // Smaller spectral smoothing window to preserve detail
    
    for (int i = 0; i < FRAME_SIZE; i++) {
        float sum = 0.0f;
        int count = 0;
        
        for (int j = -window_size; j <= window_size; j++) {
            int idx = i + j;
            if (idx >= 0 && idx < FRAME_SIZE) {
                sum += gains[idx];
                count++;
            }
        }
        
        smoothed_gains[i] = sum / count;
    }
    
    // ====== STEP 5: Apply Processing ======
    // Apply VAD gating if enabled
    bool should_gate = false;
    
    if (st->use_vad_gating) {
        // Check if VAD is above threshold
        if (st->vad_probability >= st->vad_threshold) {
            // Voice detected, reset grace counter
            st->grace_counter = st->vad_grace_period;
        } else {
            // No voice, decrement grace counter
            if (st->grace_counter > 0) {
                st->grace_counter--;
            }
        }
        
        // Gate if no voice and grace period expired
        should_gate = (st->vad_probability < st->vad_threshold) && (st->grace_counter <= 0);
    }
    
    // Apply gains and output gain to input signal
    for (int i = 0; i < FRAME_SIZE; i++) {
        // Apply spectral gain with minimal artifacts
        float processed;
        
        if (should_gate) {
            // Apply gentler attenuation when gating to avoid complete silence
            processed = in[i] * 0.15f;
        } else {
            // Weighted combination to reduce metallic artifacts
            float final_gain = 0.7f * smoothed_gains[i] + 0.3f * st->last_gain;
            
            // Apply gain
            processed = in[i] * final_gain;
        }
        
        // Apply output gain from UI
        float result = processed * st->output_gain;
        
        // Final safety check - if result is invalid, use original signal
        if (isnan(result) || isinf(result)) {
            out[i] = in[i];
        } else {
            out[i] = result;
        }
    }
    
    return st->vad_probability;
}

int rnnoise_get_frame_size(void) {
    return FRAME_SIZE;
}

void rnnoise_set_reduction_amount(DenoiseState *st, float amount) {
    if (st == NULL) return;
    st->reduction_amount = amount;
}

void rnnoise_set_vad_gating(DenoiseState *st, bool use_vad_gating) {
    if (st == NULL) return;
    st->use_vad_gating = use_vad_gating;
}

void rnnoise_set_vad_threshold(DenoiseState *st, float threshold) {
    if (st == NULL) return;
    st->vad_threshold = threshold;
}

void rnnoise_set_vad_grace_period(DenoiseState *st, int grace_period) {
    if (st == NULL) return;
    st->vad_grace_period = grace_period;
}

void rnnoise_set_output_gain(DenoiseState *st, float gain) {
    if (st == NULL) return;
    st->output_gain = gain;
}
