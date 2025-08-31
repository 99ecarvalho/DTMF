/**
 * @file dtmf_windowing.c
 * @brief Implementation of windowing functions for DTMF signal processing
 * 
 * This file contains the implementation of various windowing functions used
 * to reduce spectral leakage in frequency domain analysis. Windowing is
 * essential for accurate DTMF detection as it minimizes artifacts introduced
 * by the finite observation window in FFT analysis.
 * 
 * Mathematical foundation:
 * Window functions w[n] multiply the input signal x[n] to create:
 * x_windowed[n] = x[n] * w[n]
 * 
 * This reduces discontinuities at frame boundaries that cause spectral
 * leakage and improves frequency resolution in DTMF tone detection.
 * 
 * Detailed explanation of windowing functions and their mathematical foundation
 * Hamming window - Formula and frequency resolution characteristics
 * Hanning window - Raised cosine properties and sidelobe suppression
 * Blackman window - Three-term cosine series with excellent 74 dB suppression
 * Kaiser window - Adjustable β parameter with Bessel function implementation
 * Rectangular window - Baseline comparison with maximum resolution but spectral leakage
 */

#include "dtmf_windowing.h"
#include "dtmf_utils.h"

// =============================================================================
// WINDOWING FUNCTIONS IMPLEMENTATION
// =============================================================================

/**
 * @brief Apply Hamming window to audio samples
 * 
 * Applies the Hamming window function to reduce spectral leakage in FFT analysis.
 * The Hamming window provides good frequency resolution and moderate sidelobe
 * suppression, making it suitable for DTMF detection.
 * 
 * Mathematical formula:
 * w[n] = 0.54 - 0.46 * cos(2π * n / (N-1))
 * 
 * Where N is the window length and n ranges from 0 to N-1.
 * 
 * @param input_samples Input array of 16-bit signed audio samples
 * @param sample_count Number of samples to process
 * @param windowed_samples_out Output pointer for windowed double-precision samples
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
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

/**
 * @brief Apply Hanning window to audio samples
 * 
 * Applies the Hanning (Hann) window function for spectral analysis with
 * excellent sidelobe suppression. The Hanning window provides better
 * spectral leakage reduction than rectangular windowing while maintaining
 * reasonable frequency resolution.
 * 
 * Mathematical formula:
 * w[n] = 0.5 * (1 - cos(2π * n / (N-1)))
 * 
 * Also known as the raised cosine window, it smoothly tapers to zero
 * at both ends, eliminating discontinuities.
 * 
 * @param input_samples Input array of 16-bit signed audio samples
 * @param sample_count Number of samples to process
 * @param windowed_samples_out Output pointer for windowed double-precision samples
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
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

/**
 * @brief Apply Blackman window to audio samples
 * 
 * Applies the Blackman window function which provides excellent sidelobe
 * suppression at the cost of wider main lobe. The Blackman window is
 * optimal for applications requiring minimal spectral leakage and high
 * dynamic range in frequency analysis.
 * 
 * Mathematical formula:
 * w[n] = 0.42 - 0.5 * cos(2π * n / (N-1)) + 0.08 * cos(4π * n / (N-1))
 * 
 * The three-term cosine series provides approximately 74 dB sidelobe
 * suppression, making it excellent for detecting weak DTMF signals
 * in the presence of strong interfering tones.
 * 
 * @param input_samples Input array of 16-bit signed audio samples
 * @param sample_count Number of samples to process
 * @param windowed_samples_out Output pointer for windowed double-precision samples
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
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

/**
 * @brief Apply Kaiser window to audio samples
 * 
 * Applies the Kaiser window function which provides adjustable sidelobe
 * suppression through the beta parameter. The Kaiser window offers optimal
 * trade-off between main lobe width and sidelobe level, making it highly
 * effective for DTMF detection in varying noise environments.
 * 
 * Mathematical formula:
 * w[n] = I₀(β√(1 - ((n - N/2)/(N/2))²)) / I₀(β)
 * 
 * Where I₀ is the modified Bessel function of the first kind and β controls
 * the sidelobe suppression (β = 5.0 provides ~50 dB suppression).
 * 
 * @param input_samples Input array of 16-bit signed audio samples
 * @param sample_count Number of samples to process
 * @param windowed_samples_out Output pointer for windowed double-precision samples
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
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

/**
 * @brief Apply rectangular (no) window to audio samples
 * 
 * Applies no windowing function - effectively a rectangular window with
 * all coefficients equal to 1.0. Simply converts integer samples to
 * double precision. The rectangular window provides maximum frequency
 * resolution but suffers from significant spectral leakage.
 * 
 * Mathematical formula:
 * w[n] = 1.0 for all n
 * 
 * Used as a baseline for comparison with other windowing functions or
 * when maximum frequency resolution is more important than sidelobe
 * suppression (rare in DTMF applications).
 * 
 * @param input_samples Input array of 16-bit signed audio samples
 * @param sample_count Number of samples to process
 * @param windowed_samples_out Output pointer for converted double-precision samples
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
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
