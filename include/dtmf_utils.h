/*
 * dtmf_utils.h
 *
 * Utility functions for DTMF detection system
 */

#ifndef DTMF_UTILS_H
#define DTMF_UTILS_H

#include "dtmf_types.h"

// =============================================================================
// ERROR HANDLING AND LOGGING
// =============================================================================

/**
 * @brief Convert error code to human-readable string
 * @param result Error code to convert
 * @return String description of error
 */
const char* dtmf_error_string(dtmf_result_t result);

/**
 * @brief Log message with level and context
 * @param level Log level
 * @param ctx Error context (can be NULL)
 * @param format Printf-style format string
 */
void dtmf_log(log_level_t level, const error_context_t *ctx, 
              const char *format, ...);

/**
 * @brief Set global log level
 * @param level New log level
 */
void dtmf_set_log_level(log_level_t level);

/**
 * @brief Set log output file
 * @param filename Log file name (NULL for stderr)
 */
dtmf_result_t dtmf_set_log_file(const char *filename);

// =============================================================================
// COMPLEX NUMBER OPERATIONS
// =============================================================================

static inline complex_t complex_new(double re, double im) {
    complex_t z = {.re = re, .im = im};
    return z;
}

static inline complex_t complex_add(complex_t a, complex_t b) {
    return complex_new(a.re + b.re, a.im + b.im);
}

static inline complex_t complex_sub(complex_t a, complex_t b) {
    return complex_new(a.re - b.re, a.im - b.im);
}

static inline complex_t complex_mul(complex_t a, complex_t b) {
    return complex_new(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

static inline double complex_magnitude(complex_t z) {
    return sqrt(z.re * z.re + z.im * z.im);
}

// =============================================================================
// MATHEMATICAL UTILITIES
// =============================================================================

static inline bool is_power_of_two(int x) {
    return x > 0 && (x & (x - 1)) == 0;
}

/**
 * @brief Find next power of two that is >= the given size
 * @param size Input size
 * @return Next power of two
 */
size_t find_power_of_two_for_size(size_t size);

// =============================================================================
// TIMING UTILITIES
// =============================================================================

/**
 * @brief Get current timestamp in seconds
 * @return Current timestamp
 */
double get_current_timestamp(void);

/**
 * @brief Start timing measurement
 * @param wall_start Output for wall time start
 * @param cpu_start Output for CPU time start
 * @return Success or error code
 */
dtmf_result_t timing_start(struct timespec *wall_start, struct timespec *cpu_start);

/**
 * @brief End timing measurement
 * @param wall_start Wall time start
 * @param cpu_start CPU time start
 * @param timing Output timing information
 * @return Success or error code
 */
dtmf_result_t timing_end(const struct timespec *wall_start, 
                        const struct timespec *cpu_start,
                        timing_info_t *timing);

// =============================================================================
// MEMORY MANAGEMENT
// =============================================================================

/**
 * @brief Create a memory pool
 * @param pool Output memory pool
 * @param size Pool size in bytes
 * @return Success or error code
 */
dtmf_result_t memory_pool_create(memory_pool_t **pool, size_t size);

/**
 * @brief Allocate from memory pool
 * @param pool Memory pool
 * @param size Bytes to allocate
 * @return Allocated memory or NULL
 */
void* memory_pool_alloc(memory_pool_t *pool, size_t size);

/**
 * @brief Reset memory pool (mark all as free)
 * @param pool Memory pool
 */
void memory_pool_reset(memory_pool_t *pool);

/**
 * @brief Destroy memory pool
 * @param pool Memory pool to destroy
 */
void memory_pool_destroy(memory_pool_t *pool);

// =============================================================================
// AUDIO QUALITY VALIDATION
// =============================================================================

/**
 * @brief Validate audio quality and extract metrics
 * @param samples Audio samples
 * @param count Number of samples
 * @param quality Output quality metrics
 * @return Success or error code
 */
dtmf_result_t validate_audio_quality(const int16_t *samples,
                                    size_t count,
                                    audio_quality_t *quality);

/**
 * @brief Remove DC offset from audio samples
 * @param samples Audio samples (modified in place)
 * @param count Number of samples
 */
void remove_dc_offset(int16_t *samples, size_t count);

/**
 * @brief Apply automatic gain control
 * @param samples Audio samples (modified in place)
 * @param count Number of samples
 * @param target_rms Target RMS level
 */
void apply_agc(int16_t *samples, size_t count, double target_rms);

// =============================================================================
// TONE DETECTION UTILITIES
// =============================================================================

/**
 * @brief Find maximum tone from magnitude array
 * @param magnitudes Array of magnitudes
 * @param frequencies Array of corresponding frequencies
 * @param count Number of elements
 * @return Tone detection result
 */
tone_detection_t find_maximum_tone(const double *magnitudes, 
                                  const double *frequencies,
                                  size_t count);

/**
 * @brief Calculate confidence ratio
 * @param magnitudes Array of magnitudes
 * @param count Number of elements
 * @param best_index Index of best detection
 * @return Confidence ratio
 */
double calculate_confidence_ratio(const double *magnitudes, 
                                size_t count, 
                                int best_index);

/**
 * @brief Map DTMF indices to key character
 * @param config DTMF configuration
 * @param low_idx Low frequency index
 * @param high_idx High frequency index
 * @return DTMF key character
 */
char map_dtmf_key(const dtmf_config_t *config, int low_idx, int high_idx);

// =============================================================================
// CONFIGURATION MANAGEMENT
// =============================================================================

/**
 * @brief Load configuration from file
 * @param config_file Configuration file path
 * @param config Output configuration
 * @return Success or error code
 */
dtmf_result_t load_config(const char *config_file, 
                         dtmf_config_advanced_t *config);

/**
 * @brief Get default configuration
 * @param config Output configuration
 */
void get_default_config(dtmf_config_advanced_t *config);

/**
 * @brief Initialize audio context with configuration
 * @param ctx Audio context to initialize
 * @param config Configuration
 * @return Success or error code
 */
dtmf_result_t audio_context_init(audio_context_t *ctx, 
                                const dtmf_config_advanced_t *config);

/**
 * @brief Cleanup audio context
 * @param ctx Audio context to cleanup
 */
void audio_context_cleanup(audio_context_t *ctx);

// =============================================================================
// CONSTANTS ACCESS
// =============================================================================

extern const double DTMF_LOW_FREQUENCIES[4];
extern const double DTMF_HIGH_FREQUENCIES[4];
extern const char DTMF_KEY_MAP[4][4];
extern const dtmf_config_t DEFAULT_DTMF_CONFIG;

#endif // DTMF_UTILS_H
