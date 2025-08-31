/**
 * @file dtmf_algorithms.h
 * @brief DTMF detection algorithm implementations
 * 
 * This header declares all DTMF detection algorithms including FFT-based analysis,
 * Goertzel algorithm variants, filter bank approach, correlation-based detection,
 * and the MUSIC algorithm. Each algorithm provides a different trade-off between
 * computational complexity, accuracy, and robustness.
 */

#ifndef DTMF_ALGORITHMS_H
#define DTMF_ALGORITHMS_H

#include "dtmf_types.h"
#include "dtmf_windowing.h"

// =============================================================================
// FFT IMPLEMENTATION
// =============================================================================

/**
 * @brief Radix-2 Fast Fourier Transform implementation
 * 
 * Computes FFT or IFFT using the efficient radix-2 decimation-in-time algorithm.
 * The input size must be a power of 2. Used for frequency domain analysis
 * in FFT-based DTMF detection.
 * 
 * @param buffer Complex buffer containing input data, overwritten with output
 * @param n Size of the buffer (must be power of 2)
 * @param inverse If true, compute inverse FFT; if false, compute forward FFT
 * @return DTMF_SUCCESS on success, DTMF_ERROR_INVALID_PARAM if n is not power of 2
 */
dtmf_result_t fft_radix2(complex_t *buffer, size_t n, bool inverse);

// =============================================================================
// GOERTZEL ALGORITHM
// =============================================================================

/**
 * @brief Calculate magnitude at specific frequency using Goertzel algorithm
 * 
 * The Goertzel algorithm is computationally efficient for detecting energy
 * at specific frequencies, making it ideal for DTMF detection. It's equivalent
 * to a single-bin DFT but requires fewer operations than full FFT.
 * 
 * @param samples Input audio samples (16-bit PCM)
 * @param sample_count Number of samples in the buffer
 * @param sample_rate Audio sampling rate in Hz
 * @param target_frequency The specific frequency to analyze in Hz
 * @return Magnitude (energy) at the target frequency
 */
double goertzel_magnitude(const int16_t *samples, 
                         size_t sample_count,
                         double sample_rate, 
                         double target_frequency);

// =============================================================================
// GOERTZEL WITH TWIST DETECTION
// =============================================================================

/**
 * @brief Initialize Goertzel detector with ITU-T twist tolerance
 * 
 * Sets up the Goertzel algorithm parameters for DTMF detection with
 * twist measurement. Twist refers to the power level difference between
 * low and high frequency components, which must be within ITU-T limits.
 * 
 * @param detector Pointer to detector structure to initialize
 * @param sample_rate Audio sampling rate in Hz
 * @param frame_length Frame length in samples
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t init_goertzel_twist(dtmf_goertzel_twist_t *detector, 
                                 int sample_rate, int frame_length);

/**
 * @brief Process audio frame with Goertzel twist detection
 * 
 * Analyzes an audio frame using the Goertzel algorithm to detect DTMF tones
 * and measures the twist (power level difference) between frequency components.
 * 
 * @param detector Initialized twist detector structure
 * @param samples Input audio samples for one frame
 * @return Structure containing detection result and twist measurement
 */
dtmf_twist_result_t process_frame_with_twist(const dtmf_goertzel_twist_t *detector, 
                                            const int16_t *samples);

// =============================================================================
// FILTER BANK FUNCTIONS
// =============================================================================

/**
 * @brief Initialize IIR (Infinite Impulse Response) filter
 * 
 * Allocates and initializes memory for IIR filter coefficients and delay lines.
 * Used by the filter bank algorithm to create frequency-selective filters.
 * 
 * @param filter Pointer to filter structure to initialize
 * @param order Filter order (number of poles/zeros)
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t iir_filter_init(iir_filter_t *filter, int order);

/**
 * @brief Clean up IIR filter resources
 * 
 * Frees all memory allocated for filter coefficients and delay lines.
 * 
 * @param filter Pointer to filter structure to clean up
 */
void iir_filter_cleanup(iir_filter_t *filter);

/**
 * @brief Design second-order bandpass filter
 * 
 * Creates IIR filter coefficients for a bandpass filter centered at the
 * specified frequency. Used by filter bank algorithm to isolate DTMF frequencies.
 * 
 * @param filter Initialized filter structure to configure
 * @param center_freq Center frequency of the bandpass filter in Hz
 * @param bandwidth Filter bandwidth in Hz
 * @param sample_rate Audio sampling rate in Hz
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t design_bandpass_filter(iir_filter_t *filter, 
                                    double center_freq, 
                                    double bandwidth,
                                    double sample_rate);

/**
 * @brief Process single sample through IIR filter
 * 
 * Applies the IIR filter to one input sample, updating internal delay lines.
 * 
 * @param filter Initialized filter structure
 * @param input Input audio sample
 * @return Filtered output sample
 */
double iir_filter_process_sample(iir_filter_t *filter, double input);

/**
 * @brief Calculate Root Mean Square (RMS) energy of signal
 * 
 * Computes the RMS energy which represents the signal power level.
 * Used for energy-based detection thresholds.
 * 
 * @param samples Array of audio samples
 * @param count Number of samples in the array
 * @return RMS energy value
 */
double calculate_rms_energy(const double *samples, size_t count);

/**
 * @brief Calculate energy in a specific frequency band from FFT
 * 
 * Integrates FFT magnitude spectrum over a frequency band centered
 * at the specified frequency. Used by filter bank algorithm.
 * 
 * @param fft_buffer Complex FFT output buffer
 * @param fft_size Size of the FFT
 * @param center_freq Center frequency of the band in Hz
 * @param bandwidth Bandwidth of integration in Hz
 * @param sample_rate Audio sampling rate in Hz
 * @return Total energy in the specified frequency band
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
 * @brief Cross-correlate signal with reference sinusoid
 * 
 * Calculates the correlation between input signal and a pure sinusoid
 * at the specified frequency. High correlation indicates presence of
 * that frequency component in the signal.
 * 
 * @param samples Input audio samples (16-bit PCM)
 * @param sample_count Number of samples to process
 * @param frequency Reference sinusoid frequency in Hz
 * @param sample_rate Audio sampling rate in Hz
 * @return Correlation magnitude (higher values indicate stronger correlation)
 */
double correlate_with_sinusoid(const int16_t *samples, 
                              size_t sample_count,
                              double frequency,
                              double sample_rate);

// =============================================================================
// MATRIX OPERATIONS (for MUSIC algorithm)
// =============================================================================

/**
 * @brief Create and initialize matrix structure
 * 
 * Allocates memory for a 2D matrix used in MUSIC algorithm computations.
 * The matrix data is stored as an array of row pointers for efficient access.
 * 
 * @param matrix Pointer to matrix structure to initialize
 * @param rows Number of matrix rows
 * @param cols Number of matrix columns
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t matrix_create(matrix_t *matrix, int rows, int cols);

/**
 * @brief Destroy matrix and free allocated memory
 * 
 * Frees all memory allocated for matrix data and row pointers.
 * 
 * @param matrix Pointer to matrix structure to destroy
 */
void matrix_destroy(matrix_t *matrix);

/**
 * @brief Multiply two matrices: result = a × b
 * 
 * Performs standard matrix multiplication. Matrix dimensions must be compatible
 * (a.cols must equal b.rows).
 * 
 * @param a First matrix (left operand)
 * @param b Second matrix (right operand)
 * @param result Output matrix to store the result
 * @return DTMF_SUCCESS on success, DTMF_ERROR_INVALID_PARAM on dimension mismatch
 */
dtmf_result_t matrix_multiply(const matrix_t *a, const matrix_t *b, matrix_t *result);

/**
 * @brief Transpose matrix: result = input^T
 * 
 * Creates the transpose of the input matrix where rows and columns are swapped.
 * 
 * @param input Input matrix to transpose
 * @param result Output matrix to store the transpose (must have swapped dimensions)
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t matrix_transpose(const matrix_t *input, matrix_t *result);

/**
 * @brief Compute eigendecomposition of symmetric matrix
 * 
 * Finds eigenvalues and eigenvectors of a symmetric matrix using Jacobi method.
 * This is a key operation in the MUSIC algorithm for subspace decomposition.
 * 
 * @param matrix Input symmetric matrix (modified during computation)
 * @param eigenvalues Output array for eigenvalues (must be allocated)
 * @param eigenvectors Output matrix for eigenvectors
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t symmetric_eigendecomposition(matrix_t *matrix, 
                                          double *eigenvalues,
                                          matrix_t *eigenvectors);

/**
 * @brief Create Hankel matrix from signal samples
 * 
 * Constructs a Hankel matrix for MUSIC algorithm. A Hankel matrix has
 * constant values along each anti-diagonal, used for signal subspace analysis.
 * 
 * @param samples Input signal samples
 * @param sample_count Total number of input samples
 * @param L Subspace dimension parameter
 * @param hankel Output Hankel matrix structure
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t create_hankel_matrix(const int16_t *samples, 
                                  size_t sample_count,
                                  int L,
                                  matrix_t *hankel);

/**
 * @brief Calculate MUSIC algorithm pseudospectrum at specific frequency
 * 
 * Computes the MUSIC pseudospectrum value at a given frequency using the
 * noise subspace. High pseudospectrum values indicate signal frequencies.
 * 
 * @param noise_subspace Matrix containing noise subspace vectors
 * @param frequency Target frequency to evaluate in Hz
 * @param sample_rate Audio sampling rate in Hz
 * @param L Subspace dimension parameter
 * @return Pseudospectrum magnitude at the specified frequency
 */
double music_pseudospectrum(const matrix_t *noise_subspace,
                           double frequency,
                           double sample_rate,
                           int L);

// =============================================================================
// HIGH-LEVEL ANALYSIS ALGORITHMS
// =============================================================================

/**
 * @brief FFT-based DTMF analysis with windowing
 * 
 * Performs DTMF detection using FFT after applying a windowing function.
 * This is the most common approach, offering good frequency resolution
 * and moderate computational requirements.
 * 
 * @param ctx Audio context containing samples and buffers
 * @param config DTMF frequency configuration and thresholds
 * @param window_strategy Windowing function to apply before FFT
 * @param result Output structure for detection results
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t analyze_fft_strategy(const audio_context_t *ctx,
                                  const dtmf_config_t *config,
                                  const window_strategy_t *window_strategy,
                                  dtmf_analysis_result_t *result);

/**
 * @brief Standard Goertzel algorithm DTMF analysis
 * 
 * Uses the Goertzel algorithm to detect DTMF tones by computing energy
 * at specific frequencies. More computationally efficient than FFT for
 * detecting a small number of known frequencies.
 * 
 * @param ctx Audio context containing samples
 * @param config DTMF frequency configuration
 * @param window_strategy Windowing strategy (not used by Goertzel)
 * @param result Output structure for detection results
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t analyze_goertzel_strategy(const audio_context_t *ctx,
                                       const dtmf_config_t *config,
                                       const window_strategy_t *window_strategy,
                                       dtmf_analysis_result_t *result);

/**
 * @brief Goertzel algorithm with ITU-T twist detection
 * 
 * Enhanced Goertzel analysis that includes twist measurement for ITU-T
 * compliance. Twist is the power level difference between high and low
 * frequency components, which must be within specified limits.
 * 
 * @param ctx Audio context containing samples
 * @param config DTMF frequency configuration
 * @param window_strategy Windowing strategy (not used by Goertzel)
 * @param result Output structure for detection results
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t analyze_goertzel_twist_strategy(const audio_context_t *ctx,
                                             const dtmf_config_t *config,
                                             const window_strategy_t *window_strategy,
                                             dtmf_analysis_result_t *result);

/**
 * @brief Filter bank approach to DTMF analysis
 * 
 * Uses multiple bandpass filters to isolate each DTMF frequency component.
 * Provides good noise rejection but requires more computational resources
 * than Goertzel algorithm.
 * 
 * @param ctx Audio context containing samples
 * @param config DTMF frequency configuration
 * @param window_strategy Windowing strategy (applied before filtering)
 * @param result Output structure for detection results
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t analyze_filterbank_strategy(const audio_context_t *ctx,
                                          const dtmf_config_t *config,
                                          const window_strategy_t *window_strategy,
                                          dtmf_analysis_result_t *result);

/**
 * @brief Cross-correlation based DTMF analysis
 * 
 * Detects DTMF tones by correlating the input signal with reference
 * sinusoids at DTMF frequencies. Provides good performance in noisy
 * environments but requires more computation than Goertzel.
 * 
 * @param ctx Audio context containing samples
 * @param config DTMF frequency configuration
 * @param window_strategy Windowing strategy (not typically used)
 * @param result Output structure for detection results
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t analyze_correlation_strategy(const audio_context_t *ctx,
                                          const dtmf_config_t *config,
                                          const window_strategy_t *window_strategy,
                                          dtmf_analysis_result_t *result);

/**
 * @brief MUSIC (Multiple Signal Classification) algorithm for DTMF analysis
 * 
 * Uses eigendecomposition of the signal covariance matrix to separate
 * signal and noise subspaces. Provides superior frequency resolution
 * and noise rejection but requires significant computational resources.
 * Best suited for challenging signal conditions.
 * 
 * @param ctx Audio context containing samples
 * @param config DTMF frequency configuration
 * @param window_strategy Windowing strategy (applied to input)
 * @param result Output structure for detection results
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t analyze_music_strategy(const audio_context_t *ctx,
                                    const dtmf_config_t *config,
                                    const window_strategy_t *window_strategy,
                                    dtmf_analysis_result_t *result);

#endif // DTMF_ALGORITHMS_H