/*
 * dtmf_fft.c
 *
 * FFT implementation and FFT-based analysis
 */

#include "dtmf_algorithms.h"
#include "dtmf_utils.h"

// =============================================================================
// FFT IMPLEMENTATION
// =============================================================================

dtmf_result_t fft_radix2(complex_t *buffer, size_t n, bool inverse) {
    if (!buffer || !is_power_of_two((int)n)) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        
        if (i < j) {
            complex_t temp = buffer[i];
            buffer[i] = buffer[j];
            buffer[j] = temp;
        }
    }
    
    // Cooley-Tukey FFT
    for (size_t step_size = 2; step_size <= n; step_size <<= 1) {
        double angle = (inverse ? M_PI : -M_PI) / (step_size >> 1);
        
        for (size_t i = 0; i < n; i += step_size) {
            for (size_t j = 0; j < step_size >> 1; j++) {
                double w_angle = angle * j;
                complex_t w = complex_new(cos(w_angle), sin(w_angle));
                
                complex_t u = buffer[i + j];
                complex_t v = complex_mul(w, buffer[i + j + (step_size >> 1)]);
                
                buffer[i + j] = complex_add(u, v);
                buffer[i + j + (step_size >> 1)] = complex_sub(u, v);
            }
        }
    }
    
    // Scale for inverse transform
    if (inverse) {
        for (size_t i = 0; i < n; i++) {
            buffer[i].re /= n;
            buffer[i].im /= n;
        }
    }
    
    return DTMF_SUCCESS;
}

// =============================================================================
// FFT-BASED ANALYSIS
// =============================================================================

dtmf_result_t analyze_fft_strategy(const audio_context_t *ctx,
                                  const dtmf_config_t *config,
                                  const window_strategy_t *window_strategy,
                                  dtmf_analysis_result_t *result) {
    if (!ctx || !config || !window_strategy || !result || !ctx->samples) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    if (timing_start(&wall_start, &cpu_start) != DTMF_SUCCESS) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Apply selected windowing function
    double *windowed_samples = NULL;
    dtmf_result_t window_result = window_strategy->apply(ctx->samples, ctx->sample_count, &windowed_samples);
    if (window_result != DTMF_SUCCESS) {
        dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(window_result), 
                "Failed to apply windowing function %s", window_strategy->name);
        return window_result;
    }
    
    // Use pre-allocated FFT buffer or allocate new one
    complex_t *fft_data = ctx->fft_buffer;
    bool allocated_locally = false;
    
    if (!fft_data) {
        fft_data = calloc(ctx->fft_size, sizeof(complex_t));
        if (!fft_data) {
            free(windowed_samples);
            return DTMF_ERROR_MEMORY;
        }
        allocated_locally = true;
    } else {
        // Clear the pre-allocated buffer
        memset(fft_data, 0, ctx->fft_size * sizeof(complex_t));
    }
    
    // Copy windowed samples to FFT buffer
    for (size_t i = 0; i < ctx->sample_count; i++) {
        fft_data[i].re = windowed_samples[i];
        fft_data[i].im = 0.0;
    }
    
    // Perform FFT
    dtmf_result_t fft_result = fft_radix2(fft_data, ctx->fft_size, false);
    if (fft_result != DTMF_SUCCESS) {
        dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(fft_result), "FFT computation failed");
        free(windowed_samples);
        if (allocated_locally) free(fft_data);
        return fft_result;
    }
    
    // Calculate frequency resolution
    double freq_resolution = (double)ctx->sample_rate / ctx->fft_size;
    int half_size = ctx->fft_size / 2;
    
    // Find magnitudes for DTMF frequencies
    double low_magnitudes[config->num_low_freqs];
    double high_magnitudes[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_magnitudes, 0, sizeof(low_magnitudes));
    memset(high_magnitudes, 0, sizeof(high_magnitudes));
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        int bin = (int)round(config->low_frequencies[i] / freq_resolution);
        bin = (bin < 0) ? 0 : (bin >= half_size) ? half_size - 1 : bin;
        low_magnitudes[i] = complex_magnitude(fft_data[bin]);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        int bin = (int)round(config->high_frequencies[i] / freq_resolution);
        bin = (bin < 0) ? 0 : (bin >= half_size) ? half_size - 1 : bin;
        high_magnitudes[i] = complex_magnitude(fft_data[bin]);
    }
    
    // Find dominant tones
    result->low_tone = find_maximum_tone(low_magnitudes, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_magnitudes, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence
    result->low_confidence = calculate_confidence_ratio(low_magnitudes, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_magnitudes, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    result->window_name = window_strategy->name;
    
    free(windowed_samples);
    if (allocated_locally) free(fft_data);
    
    return timing_end(&wall_start, &cpu_start, &result->timing);
}
