/*
 * dtmf_windowing.c
 *
 * Windowing functions implementation
 */

#include "dtmf_windowing.h"
#include "dtmf_utils.h"

// =============================================================================
// WINDOWING FUNCTIONS IMPLEMENTATION
// =============================================================================

dtmf_result_t apply_hamming_window(const int16_t *input_samples, 
                                  size_t sample_count,
                                  double **windowed_samples_out) {
    if (!input_samples || sample_count == 0 || !windowed_samples_out) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    double *windowed = malloc(sizeof(double) * sample_count);
    if (!windowed) {
        return DTMF_ERROR_MEMORY;
    }
    
    for (size_t i = 0; i < sample_count; i++) {
        double window_coeff = 0.54 - 0.46 * cos(2.0 * M_PI * i / (sample_count - 1));
        windowed[i] = input_samples[i] * window_coeff;
    }
    
    *windowed_samples_out = windowed;
    return DTMF_SUCCESS;
}

dtmf_result_t apply_hanning_window(const int16_t *input_samples,
                                  size_t sample_count,
                                  double **windowed_samples_out) {
    if (!input_samples || sample_count == 0 || !windowed_samples_out) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    double *windowed = malloc(sizeof(double) * sample_count);
    if (!windowed) {
        return DTMF_ERROR_MEMORY;
    }
    
    for (size_t i = 0; i < sample_count; i++) {
        double window_coeff = 0.5 * (1.0 - cos(2.0 * M_PI * i / (sample_count - 1)));
        windowed[i] = input_samples[i] * window_coeff;
    }
    
    *windowed_samples_out = windowed;
    return DTMF_SUCCESS;
}

dtmf_result_t apply_blackman_window(const int16_t *input_samples,
                                   size_t sample_count,
                                   double **windowed_samples_out) {
    if (!input_samples || sample_count == 0 || !windowed_samples_out) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    double *windowed = malloc(sizeof(double) * sample_count);
    if (!windowed) {
        return DTMF_ERROR_MEMORY;
    }
    
    for (size_t i = 0; i < sample_count; i++) {
        double window_coeff = 0.42 - 0.5 * cos(2.0 * M_PI * i / (sample_count - 1)) 
                            + 0.08 * cos(4.0 * M_PI * i / (sample_count - 1));
        windowed[i] = input_samples[i] * window_coeff;
    }
    
    *windowed_samples_out = windowed;
    return DTMF_SUCCESS;
}

dtmf_result_t apply_kaiser_window(const int16_t *input_samples,
                                 size_t sample_count,
                                 double **windowed_samples_out) {
    if (!input_samples || sample_count == 0 || !windowed_samples_out) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    double *windowed = malloc(sizeof(double) * sample_count);
    if (!windowed) {
        return DTMF_ERROR_MEMORY;
    }
    
    // Kaiser window with beta = 5.0 (moderate sidelobe suppression)
    const double beta = 5.0;
    
    // Modified Bessel function I0 approximation
    double bessel_i0_beta = 1.0;
    double term_beta = 1.0;
    for (int i = 1; i < 50; i++) {
        term_beta *= (beta * beta) / (4.0 * i * i);
        bessel_i0_beta += term_beta;
        if (term_beta < 1e-15) break;
    }
    
    for (size_t i = 0; i < sample_count; i++) {
        double n = (double)i - (sample_count - 1) / 2.0;
        double arg_squared = beta * beta * (1.0 - pow(2.0 * n / (sample_count - 1), 2));
        
        // Calculate I0(arg) where arg = sqrt(arg_squared)
        double bessel_i0_arg = 1.0;
        double term_arg = 1.0;
        for (int j = 1; j < 50; j++) {
            term_arg *= arg_squared / (4.0 * j * j);
            bessel_i0_arg += term_arg;
            if (term_arg < 1e-15) break;
        }
        
        double window_coeff = bessel_i0_arg / bessel_i0_beta;
        windowed[i] = input_samples[i] * window_coeff;
    }
    
    *windowed_samples_out = windowed;
    return DTMF_SUCCESS;
}

dtmf_result_t apply_rectangular_window(const int16_t *input_samples,
                                      size_t sample_count,
                                      double **windowed_samples_out) {
    if (!input_samples || sample_count == 0 || !windowed_samples_out) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    double *windowed = malloc(sizeof(double) * sample_count);
    if (!windowed) {
        return DTMF_ERROR_MEMORY;
    }
    
    // Rectangular window (no windowing - just convert to double)
    for (size_t i = 0; i < sample_count; i++) {
        windowed[i] = (double)input_samples[i];
    }
    
    *windowed_samples_out = windowed;
    return DTMF_SUCCESS;
}

// =============================================================================
// WINDOW STRATEGIES ARRAY
// =============================================================================

const window_strategy_t WINDOW_STRATEGIES[] = {
    {"Rectangular", apply_rectangular_window},
    {"Hamming", apply_hamming_window},
    {"Hanning", apply_hanning_window},
    {"Blackman", apply_blackman_window},
    {"Kaiser", apply_kaiser_window}
};

const size_t NUM_WINDOW_STRATEGIES = sizeof(WINDOW_STRATEGIES) / sizeof(WINDOW_STRATEGIES[0]);
