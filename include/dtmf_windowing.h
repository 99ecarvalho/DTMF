/**
 * @file dtmf_windowing.h
 * @brief Windowing functions for DTMF frequency analysis
 * 
 * This header provides various windowing functions used to reduce spectral leakage
 * in FFT-based DTMF analysis. Different windows offer trade-offs between main lobe
 * width and side lobe suppression, affecting frequency resolution and dynamic range.
 */

#ifndef DTMF_WINDOWING_H
#define DTMF_WINDOWING_H

#include "dtmf_types.h"

// =============================================================================
// WINDOWING FUNCTIONS
// =============================================================================

/**
 * @brief Apply Hamming window to input samples
 * 
 * The Hamming window provides good frequency resolution with moderate side lobe
 * suppression (-43 dB). It's a raised cosine window that's widely used in
 * spectral analysis applications.
 * 
 * @param input_samples Input audio samples (16-bit PCM)
 * @param sample_count Number of samples in the input buffer
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t apply_hamming_window(const int16_t *input_samples, 
                                  size_t sample_count,
                                  double **windowed_samples_out);

/**
 * @brief Apply Hanning (von Hann) window to input samples
 * 
 * The Hanning window offers better side lobe suppression (-31 dB) than rectangular
 * window but slightly wider main lobe. Good general-purpose window for spectral analysis.
 * 
 * @param input_samples Input audio samples (16-bit PCM)
 * @param sample_count Number of samples in the input buffer
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t apply_hanning_window(const int16_t *input_samples,
                                  size_t sample_count,
                                  double **windowed_samples_out);

/**
 * @brief Apply Blackman window to input samples
 * 
 * The Blackman window provides excellent side lobe suppression (-58 dB) at the
 * cost of wider main lobe. Best for applications requiring high dynamic range.
 * 
 * @param input_samples Input audio samples (16-bit PCM)
 * @param sample_count Number of samples in the input buffer
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t apply_blackman_window(const int16_t *input_samples,
                                   size_t sample_count,
                                   double **windowed_samples_out);

/**
 * @brief Apply Kaiser window to input samples with configurable beta parameter
 * 
 * The Kaiser window provides adjustable trade-off between main lobe width and
 * side lobe suppression via the beta parameter. Higher beta values provide
 * better side lobe suppression but wider main lobe.
 * 
 * @param input_samples Input audio samples (16-bit PCM)
 * @param sample_count Number of samples in the input buffer
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t apply_kaiser_window(const int16_t *input_samples,
                                 size_t sample_count,
                                 double **windowed_samples_out);

/**
 * @brief Apply rectangular window (no windowing) to input samples
 * 
 * The rectangular window applies no modification to the input samples,
 * providing the best frequency resolution but worst side lobe suppression.
 * Used as a baseline for comparison with other window functions.
 * 
 * @param input_samples Input audio samples (16-bit PCM)
 * @param sample_count Number of samples in the input buffer
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t apply_rectangular_window(const int16_t *input_samples,
                                      size_t sample_count,
                                      double **windowed_samples_out);

// =============================================================================
// WINDOW STRATEGIES
// =============================================================================

/** @brief Array of available windowing strategies with function pointers */
extern const window_strategy_t WINDOW_STRATEGIES[];

/** @brief Number of available windowing strategies */
extern const size_t NUM_WINDOW_STRATEGIES;

#endif // DTMF_WINDOWING_H
