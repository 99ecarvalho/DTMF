/*
 * dtmf_algorithms.h
 *
 * DTMF detection algorithms
 */

#ifndef DTMF_ALGORITHMS_H
#define DTMF_ALGORITHMS_H

#include "dtmf_types.h"
#include "dtmf_windowing.h"

// =============================================================================
// FFT IMPLEMENTATION
// =============================================================================

/**
 * @brief Radix-2 FFT implementation
 * @param buffer Complex buffer (input/output)
 * @param n Size (must be power of 2)
 * @param inverse Whether to compute inverse FFT
 * @return Success or error code
 */
dtmf_result_t fft_radix2(complex_t *buffer, size_t n, bool inverse);

// =============================================================================
// GOERTZEL ALGORITHM
// =============================================================================

/**
 * @brief Calculate magnitude using Goertzel algorithm
 * @param samples Input samples
 * @param sample_count Number of samples
 * @param sample_rate Sample rate in Hz
 * @param target_frequency Target frequency in Hz
 * @return Magnitude at target frequency
 */
double goertzel_magnitude(const int16_t *samples, 
                         size_t sample_count,
                         double sample_rate, 
                         double target_frequency);

// =============================================================================
// GOERTZEL WITH TWIST DETECTION
// =============================================================================

/**
 * @brief Initialize Goertzel twist detector
 * @param detector Detector structure
 * @param sample_rate Sample rate
 * @param frame_length Frame length in samples
 * @return Success or error code
 */
dtmf_result_t init_goertzel_twist(dtmf_goertzel_twist_t *detector, 
                                 int sample_rate, int frame_length);

/**
 * @brief Process frame with twist detection
 * @param detector Twist detector
 * @param samples Input samples
 * @return Twist detection result
 */
dtmf_twist_result_t process_frame_with_twist(const dtmf_goertzel_twist_t *detector, 
                                            const int16_t *samples);

// =============================================================================
// FILTER BANK FUNCTIONS
// =============================================================================

/**
 * @brief Initialize IIR filter
 * @param filter Filter structure
 * @param order Filter order
 * @return Success or error code
 */
dtmf_result_t iir_filter_init(iir_filter_t *filter, int order);

/**
 * @brief Cleanup IIR filter
 * @param filter Filter structure
 */
void iir_filter_cleanup(iir_filter_t *filter);

/**
 * @brief Design bandpass filter
 * @param filter Filter structure
 * @param center_freq Center frequency
 * @param bandwidth Bandwidth
 * @param sample_rate Sample rate
 * @return Success or error code
 */
dtmf_result_t design_bandpass_filter(iir_filter_t *filter, 
                                    double center_freq, 
                                    double bandwidth,
                                    double sample_rate);

/**
 * @brief Process single sample through IIR filter
 * @param filter Filter structure
 * @param input Input sample
 * @return Filtered output
 */
double iir_filter_process_sample(iir_filter_t *filter, double input);

/**
 * @brief Calculate RMS energy
 * @param samples Sample array
 * @param count Number of samples
 * @return RMS energy
 */
double calculate_rms_energy(const double *samples, size_t count);

/**
 * @brief Calculate energy in frequency band from FFT
 * @param fft_buffer FFT buffer
 * @param fft_size FFT size
 * @param center_freq Center frequency
 * @param bandwidth Bandwidth
 * @param sample_rate Sample rate
 * @return Band energy
 */
double calculate_frequency_band_energy(const complex_t *fft_buffer, 
                                      size_t fft_size,
                                      double center_freq,
                                      double bandwidth,
                                      double sample_rate);

// =============================================================================
// CORRELATION FUNCTIONS
// =============================================================================

/**
 * @brief Correlate signal with reference sinusoid
 * @param samples Input samples
 * @param sample_count Number of samples
 * @param frequency Reference frequency
 * @param sample_rate Sample rate
 * @return Correlation magnitude
 */
double correlate_with_sinusoid(const int16_t *samples, 
                              size_t sample_count,
                              double frequency,
                              double sample_rate);

// =============================================================================
// MATRIX OPERATIONS (for MUSIC)
// =============================================================================

/**
 * @brief Create matrix
 * @param matrix Matrix structure
 * @param rows Number of rows
 * @param cols Number of columns
 * @return Success or error code
 */
dtmf_result_t matrix_create(matrix_t *matrix, int rows, int cols);

/**
 * @brief Destroy matrix
 * @param matrix Matrix structure
 */
void matrix_destroy(matrix_t *matrix);

/**
 * @brief Matrix multiplication
 * @param a First matrix
 * @param b Second matrix
 * @param result Result matrix
 * @return Success or error code
 */
dtmf_result_t matrix_multiply(const matrix_t *a, const matrix_t *b, matrix_t *result);

/**
 * @brief Matrix transpose
 * @param input Input matrix
 * @param result Result matrix
 * @return Success or error code
 */
dtmf_result_t matrix_transpose(const matrix_t *input, matrix_t *result);

/**
 * @brief Symmetric eigendecomposition
 * @param matrix Input matrix
 * @param eigenvalues Output eigenvalues
 * @param eigenvectors Output eigenvectors
 * @return Success or error code
 */
dtmf_result_t symmetric_eigendecomposition(matrix_t *matrix, 
                                          double *eigenvalues,
                                          matrix_t *eigenvectors);

/**
 * @brief Create Hankel matrix
 * @param samples Input samples
 * @param sample_count Number of samples
 * @param L Subspace dimension
 * @param hankel Output Hankel matrix
 * @return Success or error code
 */
dtmf_result_t create_hankel_matrix(const int16_t *samples, 
                                  size_t sample_count,
                                  int L,
                                  matrix_t *hankel);

/**
 * @brief Calculate MUSIC pseudospectrum
 * @param noise_subspace Noise subspace matrix
 * @param frequency Target frequency
 * @param sample_rate Sample rate
 * @param L Subspace dimension
 * @return Pseudospectrum value
 */
double music_pseudospectrum(const matrix_t *noise_subspace,
                           double frequency,
                           double sample_rate,
                           int L);

// =============================================================================
// ANALYSIS ALGORITHMS
// =============================================================================

/**
 * @brief FFT-based DTMF analysis
 * @param ctx Audio context
 * @param config DTMF configuration
 * @param window_strategy Windowing strategy
 * @param result Analysis result
 * @return Success or error code
 */
dtmf_result_t analyze_fft_strategy(const audio_context_t *ctx,
                                  const dtmf_config_t *config,
                                  const window_strategy_t *window_strategy,
                                  dtmf_analysis_result_t *result);

/**
 * @brief Goertzel-based DTMF analysis
 * @param ctx Audio context
 * @param config DTMF configuration
 * @param window_strategy Windowing strategy (unused)
 * @param result Analysis result
 * @return Success or error code
 */
dtmf_result_t analyze_goertzel_strategy(const audio_context_t *ctx,
                                       const dtmf_config_t *config,
                                       const window_strategy_t *window_strategy,
                                       dtmf_analysis_result_t *result);

/**
 * @brief Goertzel with twist DTMF analysis
 * @param ctx Audio context
 * @param config DTMF configuration
 * @param window_strategy Windowing strategy (unused)
 * @param result Analysis result
 * @return Success or error code
 */
dtmf_result_t analyze_goertzel_twist_strategy(const audio_context_t *ctx,
                                             const dtmf_config_t *config,
                                             const window_strategy_t *window_strategy,
                                             dtmf_analysis_result_t *result);

/**
 * @brief Filter bank DTMF analysis
 * @param ctx Audio context
 * @param config DTMF configuration
 * @param window_strategy Windowing strategy
 * @param result Analysis result
 * @return Success or error code
 */
dtmf_result_t analyze_filterbank_strategy(const audio_context_t *ctx,
                                         const dtmf_config_t *config,
                                         const window_strategy_t *window_strategy,
                                         dtmf_analysis_result_t *result);

/**
 * @brief Correlation-based DTMF analysis
 * @param ctx Audio context
 * @param config DTMF configuration
 * @param window_strategy Windowing strategy (unused)
 * @param result Analysis result
 * @return Success or error code
 */
dtmf_result_t analyze_correlation_strategy(const audio_context_t *ctx,
                                          const dtmf_config_t *config,
                                          const window_strategy_t *window_strategy,
                                          dtmf_analysis_result_t *result);

/**
 * @brief MUSIC-based DTMF analysis
 * @param ctx Audio context
 * @param config DTMF configuration
 * @param window_strategy Windowing strategy
 * @param result Analysis result
 * @return Success or error code
 */
dtmf_result_t analyze_music_strategy(const audio_context_t *ctx,
                                    const dtmf_config_t *config,
                                    const window_strategy_t *window_strategy,
                                    dtmf_analysis_result_t *result);

#endif // DTMF_ALGORITHMS_H
