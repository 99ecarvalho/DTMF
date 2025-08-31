/*
 * dtmf_windowing.h
 *
 * Windowing functions for DTMF analysis
 */

#ifndef DTMF_WINDOWING_H
#define DTMF_WINDOWING_H

#include "dtmf_types.h"

// =============================================================================
// WINDOWING FUNCTIONS
// =============================================================================

/**
 * @brief Apply Hamming window to input samples
 * @param input_samples Input audio samples
 * @param sample_count Number of samples
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return Success or error code
 */
dtmf_result_t apply_hamming_window(const int16_t *input_samples, 
                                  size_t sample_count,
                                  double **windowed_samples_out);

/**
 * @brief Apply Hanning window to input samples
 * @param input_samples Input audio samples
 * @param sample_count Number of samples
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return Success or error code
 */
dtmf_result_t apply_hanning_window(const int16_t *input_samples,
                                  size_t sample_count,
                                  double **windowed_samples_out);

/**
 * @brief Apply Blackman window to input samples
 * @param input_samples Input audio samples
 * @param sample_count Number of samples
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return Success or error code
 */
dtmf_result_t apply_blackman_window(const int16_t *input_samples,
                                   size_t sample_count,
                                   double **windowed_samples_out);

/**
 * @brief Apply Kaiser window to input samples
 * @param input_samples Input audio samples
 * @param sample_count Number of samples
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return Success or error code
 */
dtmf_result_t apply_kaiser_window(const int16_t *input_samples,
                                 size_t sample_count,
                                 double **windowed_samples_out);

/**
 * @brief Apply rectangular window (no windowing) to input samples
 * @param input_samples Input audio samples
 * @param sample_count Number of samples
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return Success or error code
 */
dtmf_result_t apply_rectangular_window(const int16_t *input_samples,
                                      size_t sample_count,
                                      double **windowed_samples_out);

// =============================================================================
// WINDOW STRATEGIES
// =============================================================================

extern const window_strategy_t WINDOW_STRATEGIES[];
extern const size_t NUM_WINDOW_STRATEGIES;

#endif // DTMF_WINDOWING_H
