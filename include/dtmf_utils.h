/**
 * @file dtmf_utils.h
 * @brief Utility functions and helpers for DTMF detection system
 * 
 * This header provides common utility functions including error handling,
 * logging, memory management, audio quality assessment, and mathematical
 * operations used throughout the DTMF detection system.
 *
 * @author Eduardo Correia <ecorreia@apliant.com.br>
 * @date August 2025
 */

#ifndef DTMF_UTILS_H
#define DTMF_UTILS_H

#include "dtmf_types.h"

// =============================================================================
// ERROR HANDLING AND LOGGING
// =============================================================================

/**
 * @brief Convert DTMF error code to human-readable string
 * 
 * Provides descriptive error messages for debugging and user feedback.
 * 
 * @param result DTMF error code to convert
 * @return Constant string describing the error condition
 */
const char* dtmf_error_string(dtmf_result_t result);

/**
 * @brief Log message with level filtering and context information
 * 
 * Central logging function with level-based filtering and optional
 * error context for detailed debugging information.
 * 
 * @param level Severity level of the message
 * @param ctx Optional error context with location info (can be NULL)
 * @param format Printf-style format string for the message
 * @param ... Variable arguments for format string
 */
void dtmf_log(log_level_t level, const error_context_t *ctx, 
              const char *format, ...);

/**
 * @brief Set global logging verbosity level
 * 
 * @param level New minimum log level (messages below this level are filtered)
 */
void dtmf_set_log_level(log_level_t level);

/**
 * @brief Configure log output destination
 * 
 * @param filename Log file path (NULL to use stderr)
 * @return DTMF_SUCCESS on success, DTMF_ERROR_FILE_IO on file error
 */
dtmf_result_t dtmf_set_log_file(const char *filename);

// =============================================================================
// COMPLEX NUMBER OPERATIONS
// =============================================================================

/**
 * @brief Create complex number from real and imaginary parts
 * 
 * @param re Real component
 * @param im Imaginary component
 * @return Complex number structure
 */
static inline complex_t complex_new(double re, double im) {
    complex_t z = {.re = re, .im = im};
    return z;
}

/**
 * @brief Add two complex numbers
 * 
 * @param a First complex number
 * @param b Second complex number
 * @return Sum a + b
 */
static inline complex_t complex_add(complex_t a, complex_t b) {
    return complex_new(a.re + b.re, a.im + b.im);
}

/**
 * @brief Subtract two complex numbers
 * 
 * @param a First complex number (minuend)
 * @param b Second complex number (subtrahend)
 * @return Difference a - b
 */
static inline complex_t complex_sub(complex_t a, complex_t b) {
    return complex_new(a.re - b.re, a.im - b.im);
}

/**
 * @brief Multiply two complex numbers
 * 
 * @param a First complex number
 * @param b Second complex number
 * @return Product a * b
 */
static inline complex_t complex_mul(complex_t a, complex_t b) {
    return complex_new(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

/**
 * @brief Calculate magnitude (absolute value) of complex number
 * 
 * @param z Complex number
 * @return Magnitude |z| = sqrt(re² + im²)
 */
static inline double complex_magnitude(complex_t z) {
    return sqrt(z.re * z.re + z.im * z.im);
}

// =============================================================================
// MATHEMATICAL UTILITIES
// =============================================================================

/**
 * @brief Check if integer is a power of two
 * 
 * @param x Integer to test
 * @return True if x is a positive power of two
 */
static inline bool is_power_of_two(int x) {
    return x > 0 && (x & (x - 1)) == 0;
}

/**
 * @brief Find next power of two greater than or equal to given size
 * 
 * Used for determining appropriate FFT sizes.
 * 
 * @param size Input size
 * @return Next power of two >= size
 */
size_t find_power_of_two_for_size(size_t size);

// =============================================================================
// TIMING UTILITIES
// =============================================================================

/**
 * @brief Get current timestamp in seconds since epoch
 * 
 * @return Current wall clock timestamp in seconds
 */
double get_current_timestamp(void);

/**
 * @brief Start timing measurement for performance analysis
 * 
 * Records wall clock and CPU time for algorithm performance measurement.
 * 
 * @param wall_start Output pointer for wall clock start time
 * @param cpu_start Output pointer for CPU time start
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t timing_start(struct timespec *wall_start, struct timespec *cpu_start);

/**
 * @brief Complete timing measurement and calculate elapsed times
 * 
 * @param wall_start Wall clock start time from timing_start()
 * @param cpu_start CPU time start from timing_start()
 * @param timing Output structure for calculated timing information
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t timing_end(const struct timespec *wall_start, 
                        const struct timespec *cpu_start,
                        timing_info_t *timing);

// =============================================================================
// MEMORY MANAGEMENT
// =============================================================================

/**
 * @brief Create a pre-allocated memory pool for efficient allocation
 * 
 * Memory pools reduce malloc/free overhead during real-time processing
 * by pre-allocating a large block and managing it internally.
 * 
 * @param pool Output pointer for created memory pool
 * @param size Total pool size in bytes
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t memory_pool_create(memory_pool_t **pool, size_t size);

/**
 * @brief Allocate memory from pool
 * 
 * Fast allocation from pre-allocated pool. No individual free() needed.
 * 
 * @param pool Memory pool to allocate from
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if pool exhausted
 */
void* memory_pool_alloc(memory_pool_t *pool, size_t size);

/**
 * @brief Reset memory pool to empty state
 * 
 * Marks all memory as available again without deallocating the pool.
 * 
 * @param pool Memory pool to reset
 */
void memory_pool_reset(memory_pool_t *pool);

/**
 * @brief Destroy memory pool and free all resources
 * 
 * @param pool Memory pool to destroy
 */
void memory_pool_destroy(memory_pool_t *pool);

// =============================================================================
// AUDIO QUALITY VALIDATION
// =============================================================================

/**
 * @brief Analyze audio quality and extract diagnostic metrics
 * 
 * Performs comprehensive audio quality assessment including DC offset
 * detection, clipping analysis, signal level measurement, and SNR estimation.
 * 
 * @param samples Input audio samples (16-bit PCM)
 * @param count Number of samples to analyze
 * @param quality Output structure for quality metrics
 * @return DTMF_SUCCESS on success, error code otherwise
 */
dtmf_result_t validate_audio_quality(const int16_t *samples,
                                    size_t count,
                                    audio_quality_t *quality);

/**
 * @brief Remove DC offset from audio samples in-place
 * 
 * Calculates and removes the average DC component from the signal.
 * DC offset can interfere with spectral analysis accuracy.
 * 
 * @param samples Audio samples to modify (16-bit PCM)
 * @param count Number of samples in the buffer
 */
void remove_dc_offset(int16_t *samples, size_t count);

/**
 * @brief Apply Automatic Gain Control to normalize signal level
 * 
 * Adjusts the signal amplitude to achieve a target RMS level,
 * improving detection consistency across varying input levels.
 * 
 * @param samples Audio samples to modify (16-bit PCM)
 * @param count Number of samples in the buffer
 * @param target_rms Desired RMS level after gain adjustment
 */
void apply_agc(int16_t *samples, size_t count, double target_rms);

// =============================================================================
// TONE DETECTION UTILITIES
// =============================================================================

/**
 * @brief Find the tone with maximum magnitude from detection results
 * 
 * Searches through magnitude array to find the strongest detected tone,
 * returning both the magnitude and corresponding frequency information.
 * 
 * @param magnitudes Array of tone magnitudes/energies
 * @param frequencies Array of corresponding frequencies in Hz
 * @param count Number of elements in both arrays
 * @return Tone detection structure with strongest detection
 */
tone_detection_t find_maximum_tone(const double *magnitudes, 
                                  const double *frequencies,
                                  size_t count);

/**
 * @brief Calculate confidence ratio for tone detection
 * 
 * Computes the ratio between the strongest detected tone and the average
 * of other tones. Higher ratios indicate more confident detections.
 * 
 * @param magnitudes Array of tone magnitudes/energies
 * @param count Number of elements in magnitude array
 * @param best_index Index of the strongest detected tone
 * @return Confidence ratio (higher values = more confident)
 */
double calculate_confidence_ratio(const double *magnitudes, 
                                size_t count, 
                                int best_index);

/**
 * @brief Map frequency indices to DTMF key character
 * 
 * Converts low and high frequency indices to the corresponding DTMF
 * key character using the configured key mapping table.
 * 
 * @param config DTMF configuration with key mapping
 * @param low_idx Index of detected low (row) frequency
 * @param high_idx Index of detected high (column) frequency
 * @return DTMF key character ('0'-'9', '*', '#', 'A'-'D', or '\0' for invalid)
 */
char map_dtmf_key(const dtmf_config_t *config, int low_idx, int high_idx);

// =============================================================================
// CONFIGURATION MANAGEMENT
// =============================================================================

/**
 * @brief Load algorithm configuration from file
 * 
 * Parses configuration file to load algorithm parameters, thresholds,
 * and processing options. File format uses key=value pairs.
 * 
 * @param config_file Path to configuration file
 * @param config Output structure to populate with loaded configuration
 * @return DTMF_SUCCESS on success, DTMF_ERROR_CONFIG on parse error
 */
dtmf_result_t load_config(const char *config_file, 
                         dtmf_config_advanced_t *config);

/**
 * @brief Initialize configuration structure with sensible defaults
 * 
 * Sets up default values for all configuration parameters suitable
 * for general-purpose DTMF detection applications.
 * 
 * @param config Configuration structure to initialize
 */
void get_default_config(dtmf_config_advanced_t *config);

/**
 * @brief Initialize audio processing context with given configuration
 * 
 * Sets up audio context including buffer allocation, lookup table creation,
 * memory pool initialization, and other resources needed for processing.
 * 
 * @param ctx Audio context structure to initialize
 * @param config Configuration to use for setup
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t audio_context_init(audio_context_t *ctx, 
                                const dtmf_config_advanced_t *config);

/**
 * @brief Clean up audio context and free all allocated resources
 * 
 * @param ctx Audio context to clean up
 */
void audio_context_cleanup(audio_context_t *ctx);

// =============================================================================
// CONSTANTS ACCESS
// =============================================================================

/** @brief Standard DTMF low (row) frequencies: 697, 770, 852, 941 Hz */
extern const double DTMF_LOW_FREQUENCIES[4];

/** @brief Standard DTMF high (column) frequencies: 1209, 1336, 1477, 1633 Hz */
extern const double DTMF_HIGH_FREQUENCIES[4];

/** @brief DTMF key mapping matrix [row][column] */
extern const char DTMF_KEY_MAP[4][4];

/** @brief Default DTMF configuration with standard frequencies */
extern const dtmf_config_t DEFAULT_DTMF_CONFIG;

#endif // DTMF_UTILS_H
