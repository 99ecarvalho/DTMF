/*
 * dtmf_correlation.c
 *
 * Correlation-based DTMF detection implementation
 */

#include "dtmf_algorithms.h"
#include "dtmf_utils.h"

// =============================================================================
// CORRELATION IMPLEMENTATION
// =============================================================================

double correlate_with_sinusoid(const int16_t *samples, 
                              size_t sample_count,
                              double frequency,
                              double sample_rate) {
    if (!samples || sample_count == 0 || frequency <= 0 || sample_rate <= 0) {
        return 0.0;
    }
    
    double correlation = 0.0;
    double omega = 2.0 * M_PI * frequency / sample_rate;
    
    // Calculate correlation with both sine and cosine components
    double cos_corr = 0.0, sin_corr = 0.0;
    
    for (size_t i = 0; i < sample_count; i++) {
        double phase = omega * i;
        cos_corr += samples[i] * cos(phase);
        sin_corr += samples[i] * sin(phase);
    }
    
    // Normalize by sample count
    cos_corr /= sample_count;
    sin_corr /= sample_count;
    
    // Calculate magnitude of complex correlation
    correlation = sqrt(cos_corr * cos_corr + sin_corr * sin_corr);
    
    return correlation;
}

// =============================================================================
// CORRELATION-BASED ANALYSIS
// =============================================================================

dtmf_result_t analyze_correlation_strategy(const audio_context_t *ctx,
                                          const dtmf_config_t *config,
                                          const window_strategy_t *window_strategy,
                                          dtmf_analysis_result_t *result) {
    if (!ctx || !config || !window_strategy || !result || !ctx->samples) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    dtmf_result_t timing_result = timing_start(&wall_start, &cpu_start);
    if (timing_result != DTMF_SUCCESS) {
        return timing_result;
    }
    
    // Calculate correlation magnitudes for all DTMF frequencies
    double low_correlations[config->num_low_freqs];
    double high_correlations[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_correlations, 0, sizeof(low_correlations));
    memset(high_correlations, 0, sizeof(high_correlations));
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_correlations[i] = correlate_with_sinusoid(ctx->samples, ctx->sample_count,
                                                    config->low_frequencies[i], ctx->sample_rate);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_correlations[i] = correlate_with_sinusoid(ctx->samples, ctx->sample_count,
                                                     config->high_frequencies[i], ctx->sample_rate);
    }
    
    // Find best tones
    result->low_tone = find_maximum_tone(low_correlations, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_correlations, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence ratios
    result->low_confidence = calculate_confidence_ratio(low_correlations, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_correlations, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    // Record window name
    result->window_name = "Correlation";
    
    // Record timing
    timing_end(&wall_start, &cpu_start, &result->timing);
    
    return DTMF_SUCCESS;
}
