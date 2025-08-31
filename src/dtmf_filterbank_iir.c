/**
 * @file dtmf_filterbank_iir.c
 * @brief Implementation of IIR filter bank approach for DTMF detection
 * 
 * This file contains the implementation of an IIR (Infinite Impulse Response)
 * filter bank approach to DTMF detection using real-time digital bandpass
 * filters tuned to each DTMF frequency with energy-based detection.
 * 
 * Mathematical foundation:
 * Each IIR bandpass filter implements a second-order Butterworth filter with
 * transfer function:
 * 
 * H(z) = (b₀ + b₁z⁻¹ + b₂z⁻²) / (1 + a₁z⁻¹ + a₂z⁻²)
 * 
 * The difference equation is:
 * y[n] = b₀x[n] + b₁x[n-1] + b₂x[n-2] - a₁y[n-1] - a₂y[n-2]
 * 
 * Energy detection is performed by calculating RMS energy of the filtered
 * output over the analysis frame, providing frequency-selective detection
 * with good noise rejection characteristics.
 */

#include "dtmf_algorithms.h"
#include "dtmf_utils.h"
#include <string.h>

// =============================================================================
// FILTERBANK IIR IMPLEMENTATION
// =============================================================================

/**
 * @brief Process audio samples through IIR filter bank and detect DTMF tones
 * 
 * This function implements a complete IIR filter bank for DTMF detection:
 * 1. Creates bandpass IIR filters for each DTMF frequency
 * 2. Processes input samples through each filter
 * 3. Calculates RMS energy of each filtered output
 * 4. Determines the strongest frequency components
 * 5. Maps the results to DTMF keys
 * 
 * The IIR approach provides real-time processing with direct time-domain
 * filtering, avoiding the overhead of FFT computation while maintaining
 * good frequency selectivity.
 * 
 * @param ctx Audio context containing samples and processing parameters
 * @param config DTMF frequency configuration and thresholds
 * @param window_strategy Window strategy (not used by IIR filters)
 * @param result Output structure for detection results
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t analyze_filterbank_iir_strategy(const audio_context_t *ctx,
                                              const dtmf_config_t *config,
                                              const window_strategy_t *window_strategy __attribute__((unused)),
                                              dtmf_analysis_result_t *result) {
    if (!ctx || !config || !result) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    dtmf_result_t timing_result = timing_start(&wall_start, &cpu_start);
    if (timing_result != DTMF_SUCCESS) {
        return timing_result;
    }

    // Initialize result structure
    memset(result, 0, sizeof(dtmf_analysis_result_t));
    
    // Create IIR filters for each DTMF frequency
    size_t total_frequencies = config->num_low_freqs + config->num_high_freqs;
    iir_filter_t *filters = calloc(total_frequencies, sizeof(iir_filter_t));
    if (!filters) {
        dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(DTMF_ERROR_MEMORY), 
                "Failed to allocate memory for IIR filters");
        return DTMF_ERROR_MEMORY;
    }
    
    // Filter design parameters
    const double filter_bandwidth = 80.0; // Hz - wider bandwidth for better frequency capture
    
    // Initialize low frequency filters
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        dtmf_result_t filter_result = design_bandpass_filter(&filters[i], 
                                                           config->low_frequencies[i], 
                                                           filter_bandwidth,
                                                           ctx->sample_rate);
        if (filter_result != DTMF_SUCCESS) {
            dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(filter_result), 
                    "Failed to design bandpass filter for %.1f Hz", 
                    config->low_frequencies[i]);
            // Cleanup already initialized filters
            for (size_t j = 0; j < i; j++) {
                iir_filter_cleanup(&filters[j]);
            }
            free(filters);
            return filter_result;
        }
    }
    
    // Initialize high frequency filters
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        size_t filter_idx = config->num_low_freqs + i;
        dtmf_result_t filter_result = design_bandpass_filter(&filters[filter_idx], 
                                                           config->high_frequencies[i], 
                                                           filter_bandwidth,
                                                           ctx->sample_rate);
        if (filter_result != DTMF_SUCCESS) {
            dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(filter_result), 
                    "Failed to design bandpass filter for %.1f Hz", 
                    config->high_frequencies[i]);
            // Cleanup already initialized filters
            for (size_t j = 0; j < filter_idx; j++) {
                iir_filter_cleanup(&filters[j]);
            }
            free(filters);
            return filter_result;
        }
    }
    
    // Allocate buffers for filtered outputs
    double **filtered_outputs = calloc(total_frequencies, sizeof(double*));
    if (!filtered_outputs) {
        dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(DTMF_ERROR_MEMORY), 
                "Failed to allocate memory for filtered output buffers");
        for (size_t i = 0; i < total_frequencies; i++) {
            iir_filter_cleanup(&filters[i]);
        }
        free(filters);
        return DTMF_ERROR_MEMORY;
    }
    
    for (size_t i = 0; i < total_frequencies; i++) {
        filtered_outputs[i] = calloc(ctx->sample_count, sizeof(double));
        if (!filtered_outputs[i]) {
            dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(DTMF_ERROR_MEMORY), 
                    "Failed to allocate memory for filtered output buffer %zu", i);
            // Cleanup already allocated buffers
            for (size_t j = 0; j < i; j++) {
                free(filtered_outputs[j]);
            }
            free(filtered_outputs);
            for (size_t j = 0; j < total_frequencies; j++) {
                iir_filter_cleanup(&filters[j]);
            }
            free(filters);
            return DTMF_ERROR_MEMORY;
        }
    }
    
    // Process each sample through all filters
    for (size_t sample = 0; sample < ctx->sample_count; sample++) {
        double input_sample = (double)ctx->samples[sample];
        
        for (size_t filter_idx = 0; filter_idx < total_frequencies; filter_idx++) {
            filtered_outputs[filter_idx][sample] = 
                iir_filter_process_sample(&filters[filter_idx], input_sample);
        }
    }
    
    // Calculate RMS energy for each filtered output
    double low_energies[config->num_low_freqs];
    double high_energies[config->num_high_freqs];
    
    // Initialize arrays
    memset(low_energies, 0, sizeof(low_energies));
    memset(high_energies, 0, sizeof(high_energies));
    
    // Calculate energies for low frequencies
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_energies[i] = calculate_rms_energy(filtered_outputs[i], ctx->sample_count);
        dtmf_log(LOG_DEBUG, NULL, "Low freq %.1f Hz: energy = %.6f", 
                config->low_frequencies[i], low_energies[i]);
    }
    
    // Calculate energies for high frequencies
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        size_t filter_idx = config->num_low_freqs + i;
        high_energies[i] = calculate_rms_energy(filtered_outputs[filter_idx], ctx->sample_count);
        dtmf_log(LOG_DEBUG, NULL, "High freq %.1f Hz: energy = %.6f", 
                config->high_frequencies[i], high_energies[i]);
    }
    
    // Find the strongest tones
    result->low_tone = find_maximum_tone(low_energies, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_energies, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence ratios (ratio of strongest to second strongest)
    result->low_confidence = calculate_confidence_ratio(low_energies, config->num_low_freqs, 
                                                      result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_energies, config->num_high_freqs, 
                                                       result->high_tone.index);
    
    // Map frequencies to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    // Record algorithm name
    result->window_name = "IIR FilterBank";
    
    dtmf_log(LOG_DEBUG, NULL, 
            "FilterBankIIR: Low=%.1f(%.3f) High=%.1f(%.3f) Key='%c'", 
            result->low_tone.frequency, result->low_confidence,
            result->high_tone.frequency, result->high_confidence,
            result->detected_key);
    
    // Cleanup resources
    for (size_t i = 0; i < total_frequencies; i++) {
        free(filtered_outputs[i]);
        iir_filter_cleanup(&filters[i]);
    }
    free(filtered_outputs);
    free(filters);
    
    // Record timing
    timing_end(&wall_start, &cpu_start, &result->timing);
    
    return DTMF_SUCCESS;
}
