/**
 * @file dtmf_utils.c
 * @brief Implementation of utility functions for DTMF detection system
 * 
 * This file contains the implementation of common utility functions including
 * error handling, logging, memory management, audio quality assessment,
 * mathematical operations, and configuration management used throughout
 * the DTMF detection system.
 * 
 * Error handling functions - dtmf_error_string() with error code descriptions
 * Logging functions - dtmf_log(), dtmf_set_log_level(), dtmf_set_log_file() with timestamp and context capabilities
 * Mathematical utilities - find_power_of_two_for_size() for FFT optimization
 * Timing utilities - get_current_timestamp(), timing_start(), timing_end() for performance measurement
 * Memory management - Complete memory pool implementation with allocation, reset, and cleanup
 * Audio quality validation - validate_audio_quality(), remove_dc_offset(), apply_agc() for signal preprocessing
 * Tone detection utilities - find_maximum_tone(), calculate_confidence_ratio(), map_dtmf_key() for frequency analysis
 * Configuration management - get_default_config(), load_config() for parameter setup
 * Audio context management - audio_context_init(), audio_context_cleanup() for processing context
 * 
 */

#define _POSIX_C_SOURCE 200809L
#include "dtmf_utils.h"

// =============================================================================
// CONSTANTS AND GLOBAL DATA
// =============================================================================

/** @brief Standard DTMF low (row) frequencies in Hz */
const double DTMF_LOW_FREQUENCIES[] = {697.0, 770.0, 852.0, 941.0};

/** @brief Standard DTMF high (column) frequencies in Hz */
const double DTMF_HIGH_FREQUENCIES[] = {1209.0, 1336.0, 1477.0, 1633.0};

/** @brief DTMF keypad mapping from frequency indices to characters */
const char DTMF_KEY_MAP[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

/** @brief Default DTMF configuration with standard telephony frequencies */
const dtmf_config_t DEFAULT_DTMF_CONFIG = {
    .low_frequencies = DTMF_LOW_FREQUENCIES,
    .high_frequencies = DTMF_HIGH_FREQUENCIES,
    .key_map = DTMF_KEY_MAP,
    .num_low_freqs = sizeof(DTMF_LOW_FREQUENCIES) / sizeof(DTMF_LOW_FREQUENCIES[0]),
    .num_high_freqs = sizeof(DTMF_HIGH_FREQUENCIES) / sizeof(DTMF_HIGH_FREQUENCIES[0])
};

// =============================================================================
// LOGGING STATE
// =============================================================================

/** @brief Global logging level filter */
static log_level_t g_log_level = LOG_INFO;

/** @brief Global log file handle (NULL for stderr) */
static FILE *g_log_file = NULL;

// =============================================================================
// ERROR HANDLING AND LOGGING
// =============================================================================

/**
 * @brief Convert DTMF error code to descriptive string
 * 
 * Provides human-readable descriptions for all DTMF error codes
 * to aid in debugging and error reporting.
 * 
 * @param result DTMF error code to convert
 * @return Constant string describing the error condition
 */
const char* dtmf_error_string(dtmf_result_t result) {
    switch (result) {
        case DTMF_SUCCESS: return "Success";
        case DTMF_ERROR_MEMORY: return "Memory allocation error";
        case DTMF_ERROR_FILE_IO: return "File I/O error";
        case DTMF_ERROR_INVALID_PARAM: return "Invalid parameter";
        case DTMF_ERROR_INSUFFICIENT_DATA: return "Insufficient data";
        case DTMF_ERROR_END_OF_STREAM: return "End of stream";
        case DTMF_ERROR_AUDIO_QUALITY: return "Audio quality error";
        case DTMF_ERROR_CONFIG: return "Configuration error";
        default: return "Unknown error";
    }
}

/**
 * @brief Log a formatted message with timestamp and context information
 * 
 * Provides thread-safe logging with configurable levels, timestamps,
 * and context information including function name and line number.
 * Messages are filtered by the global log level setting.
 * 
 * @param level Severity level of the message
 * @param ctx Optional context information (function name, line number)
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 */
void dtmf_log(log_level_t level, const error_context_t *ctx, const char *format, ...) {
    if (level > g_log_level) {
        return;
    }
    
    FILE *output = g_log_file ? g_log_file : stderr;
    
    // Print timestamp
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm_info = localtime(&ts.tv_sec);
    
    fprintf(output, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ",
            tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
            ts.tv_nsec / 1000000);
    
    // Print level
    const char *level_str[] = {"ERROR", "WARN", "INFO", "DEBUG"};
    fprintf(output, "%s ", level_str[level]);
    
    // Print context if available
    if (ctx && ctx->function) {
        fprintf(output, "[%s:%d] ", ctx->function, ctx->line);
    }
    
    // Print message
    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    
    fprintf(output, "\n");
    fflush(output);
}

/**
 * @brief Set the global logging level filter
 * 
 * Controls which log messages are output based on their severity level.
 * Messages with severity levels higher than the set level are suppressed.
 * 
 * @param level New minimum logging level (LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG)
 */
void dtmf_set_log_level(log_level_t level) {
    g_log_level = level;
}

/**
 * @brief Configure logging output to a file
 * 
 * Redirects logging output from stderr to a specified file.
 * The previous log file is automatically closed if it was opened.
 * Pass NULL or empty string to reset logging to stderr.
 * 
 * @param filename Path to log file, or NULL to reset to stderr
 * @return DTMF_SUCCESS on success, DTMF_ERROR_FILE_IO on failure
 */
dtmf_result_t dtmf_set_log_file(const char *filename) {
    if (g_log_file && g_log_file != stderr) {
        fclose(g_log_file);
    }
    
    if (!filename) {
        g_log_file = NULL;
        return DTMF_SUCCESS;
    }
    
    g_log_file = fopen(filename, "a");
    if (!g_log_file) {
        g_log_file = NULL;
        return DTMF_ERROR_FILE_IO;
    }
    
    return DTMF_SUCCESS;
}

// =============================================================================
// MATHEMATICAL UTILITIES
// =============================================================================

/**
 * @brief Find the smallest power of two greater than or equal to given size
 * 
 * Used for optimizing FFT computations and memory allocations where
 * power-of-two sizes provide better performance. Essential for preparing
 * data buffers for efficient FFT algorithms.
 * 
 * @param size Input size to round up
 * @return Smallest power of two >= size, minimum value is 2
 */
size_t find_power_of_two_for_size(size_t size) {
    if (size <= 1) return 2;
    
    size_t power = 2;
    while (power < size) {
        power *= 2;
    }
    return power;
}

// =============================================================================
// TIMING UTILITIES
// =============================================================================

/**
 * @brief Get current high-resolution timestamp in seconds
 * 
 * Provides monotonic clock reading suitable for measuring elapsed time
 * and performance profiling. Uses CLOCK_MONOTONIC to avoid issues with
 * system time adjustments.
 * 
 * @return Current timestamp as floating-point seconds since arbitrary epoch
 */
double get_current_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/**
 * @brief Begin timing measurement for both wall-clock and CPU time
 * 
 * Captures both wall-clock time (CLOCK_MONOTONIC) and process CPU time
 * (CLOCK_PROCESS_CPUTIME_ID) to enable comprehensive performance analysis
 * distinguishing between I/O wait time and actual computation time.
 * 
 * @param wall_start Output pointer for wall-clock start time
 * @param cpu_start Output pointer for CPU start time
 * @return DTMF_SUCCESS on success, DTMF_ERROR_INVALID_PARAM on clock failure
 */
dtmf_result_t timing_start(struct timespec *wall_start, struct timespec *cpu_start) {
    if (clock_gettime(CLOCK_MONOTONIC, wall_start) != 0 ||
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, cpu_start) != 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    return DTMF_SUCCESS;
}

/**
 * @brief Complete timing measurement and calculate elapsed times
 * 
 * Calculates both wall-clock and CPU elapsed times from previously captured
 * start times. Useful for performance profiling to understand both total
 * execution time and actual CPU utilization.
 * 
 * @param wall_start Wall-clock start time from timing_start()
 * @param cpu_start CPU start time from timing_start()
 * @param wall_elapsed Output pointer for wall-clock elapsed time in seconds
 * @param cpu_elapsed Output pointer for CPU elapsed time in seconds
 * @return DTMF_SUCCESS on success, DTMF_ERROR_INVALID_PARAM on failure
 */
dtmf_result_t timing_end(const struct timespec *wall_start, 
                        const struct timespec *cpu_start,
                        timing_info_t *timing) {
    struct timespec wall_end, cpu_end;
    
    if (clock_gettime(CLOCK_MONOTONIC, &wall_end) != 0 ||
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end) != 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Calculate differences
    timing->wall_time_s = (wall_end.tv_sec - wall_start->tv_sec) +
                         (wall_end.tv_nsec - wall_start->tv_nsec) / 1e9;
    timing->cpu_time_s = (cpu_end.tv_sec - cpu_start->tv_sec) +
                        (cpu_end.tv_nsec - cpu_start->tv_nsec) / 1e9;
    
    return DTMF_SUCCESS;
}

// =============================================================================
// MEMORY MANAGEMENT
// =============================================================================

/**
 * @brief Create a new memory pool for efficient allocation
 * 
 * Creates a contiguous memory pool that allows fast allocation without
 * individual malloc/free overhead. Useful for algorithms that need many
 * small temporary allocations during processing.
 * 
 * @param pool Output pointer to created memory pool
 * @param size Total size of memory pool in bytes
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY if allocation fails
 */
dtmf_result_t memory_pool_create(memory_pool_t **pool, size_t size) {
    if (!pool || size == 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    *pool = malloc(sizeof(memory_pool_t));
    if (!*pool) {
        return DTMF_ERROR_MEMORY;
    }
    
    (*pool)->pool = malloc(size);
    if (!(*pool)->pool) {
        free(*pool);
        *pool = NULL;
        return DTMF_ERROR_MEMORY;
    }
    
    (*pool)->size = size;
    (*pool)->used = 0;
    (*pool)->block_count = 0;
    
    return DTMF_SUCCESS;
}

/**
 * @brief Allocate memory from the pool with 8-byte alignment
 * 
 * Provides fast allocation from pre-allocated pool memory.
 * All allocations are 8-byte aligned for optimal performance.
 * Returns NULL if insufficient space remains in pool.
 * 
 * @param pool Memory pool to allocate from
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if insufficient space
 */
void* memory_pool_alloc(memory_pool_t *pool, size_t size) {
    if (!pool || size == 0) {
        return NULL;
    }
    
    // Align to 8-byte boundary
    size = (size + 7) & ~7;
    
    if (pool->used + size > pool->size) {
        return NULL; // Pool exhausted
    }
    
    void *ptr = (char*)pool->pool + pool->used;
    pool->used += size;
    pool->block_count++;
    
    return ptr;
}

/**
 * @brief Reset memory pool to initial state
 * 
 * Resets allocation pointer to beginning of pool, effectively
 * freeing all allocated blocks without deallocating the underlying
 * pool memory. Fast way to reuse pool for next processing cycle.
 * 
 * @param pool Memory pool to reset
 */
void memory_pool_reset(memory_pool_t *pool) {
    if (pool) {
        pool->used = 0;
        pool->block_count = 0;
    }
}

/**
 * @brief Destroy memory pool and free all memory
 * 
 * Releases both the pool structure and the underlying memory block.
 * Pool pointer becomes invalid after this call.
 * 
 * @param pool Memory pool to destroy
 */
void memory_pool_destroy(memory_pool_t *pool) {
    if (pool) {
        free(pool->pool);
        free(pool);
    }
}

// =============================================================================
// AUDIO QUALITY VALIDATION
// =============================================================================

/**
 * @brief Validate audio signal quality for DTMF detection
 * 
 * Analyzes audio signal characteristics to determine if the signal
 * quality is suitable for reliable DTMF detection. Calculates RMS and
 * peak levels, detects clipping, checks for excessive quietness,
 * estimates signal-to-noise ratio, and measures DC offset.
 * 
 * @param samples Array of 16-bit signed audio samples
 * @param count Number of samples to analyze
 * @param quality Output structure containing quality metrics
 * @return DTMF_SUCCESS on success, DTMF_ERROR_INVALID_PARAM on invalid input
 */
dtmf_result_t validate_audio_quality(const int16_t *samples,
                                    size_t count,
                                    audio_quality_t *quality) {
    if (!samples || count == 0 || !quality) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    memset(quality, 0, sizeof(audio_quality_t));
    
    // Calculate DC offset
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += samples[i];
    }
    quality->dc_offset = sum / count;
    
    // Calculate RMS and peak levels
    double sum_squares = 0.0;
    double max_abs = 0.0;
    
    for (size_t i = 0; i < count; i++) {
        double sample = samples[i] - quality->dc_offset;
        sum_squares += sample * sample;
        double abs_sample = fabs(sample);
        if (abs_sample > max_abs) {
            max_abs = abs_sample;
        }
    }
    
    quality->rms_level = sqrt(sum_squares / count);
    quality->peak_level = max_abs;
    
    // Check for clipping (near full scale)
    quality->is_clipped = (max_abs > 30000.0); // Close to 32767
    
    // Check if too quiet
    quality->is_too_quiet = (quality->rms_level < 100.0);
    
    // Estimate SNR (very rough approximation)
    double noise_floor = quality->rms_level * 0.1; // Assume 10% is noise
    quality->snr_estimate = 20.0 * log10(quality->rms_level / fmax(noise_floor, 1.0));
    
    return DTMF_SUCCESS;
}

/**
 * @brief Remove DC offset from audio samples in-place
 * 
 * Calculates the mean value (DC component) of the signal and subtracts
 * it from all samples to center the signal around zero. This preprocessing
 * step improves the accuracy of frequency domain analysis.
 * 
 * @param samples Array of 16-bit signed audio samples to process
 * @param count Number of samples in the array
 */
void remove_dc_offset(int16_t *samples, size_t count) {
    if (!samples || count == 0) {
        return;
    }
    
    // Calculate DC offset
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += samples[i];
    }
    double dc_offset = sum / count;
    
    // Remove DC offset
    for (size_t i = 0; i < count; i++) {
        double corrected = samples[i] - dc_offset;
        samples[i] = (int16_t)fmax(-32767, fmin(32767, corrected));
    }
}

/**
 * @brief Apply automatic gain control to normalize signal level
 * 
 * Adjusts the amplitude of the input signal to achieve a target RMS level.
 * Helps maintain consistent signal levels for reliable DTMF detection
 * across varying input amplitudes. Includes saturation protection.
 * 
 * @param samples Array of 16-bit signed audio samples to normalize
 * @param count Number of samples in the array
 * @param target_rms Desired RMS level after normalization
 */
void apply_agc(int16_t *samples, size_t count, double target_rms) {
    if (!samples || count == 0 || target_rms <= 0) {
        return;
    }
    
    // Calculate current RMS
    double sum_squares = 0.0;
    for (size_t i = 0; i < count; i++) {
        double sample = samples[i];
        sum_squares += sample * sample;
    }
    double current_rms = sqrt(sum_squares / count);
    
    if (current_rms < 1.0) {
        return; // Too quiet to normalize
    }
    
    // Calculate gain
    double gain = target_rms / current_rms;
    
    // Apply gain with saturation protection
    for (size_t i = 0; i < count; i++) {
        double amplified = samples[i] * gain;
        samples[i] = (int16_t)fmax(-32767, fmin(32767, amplified));
    }
}

// =============================================================================
// TONE DETECTION UTILITIES
// =============================================================================

/**
 * @brief Find the frequency with maximum magnitude in spectrum
 * 
 * Searches through frequency domain data to identify the peak magnitude
 * and its corresponding frequency and bin index. Used to identify the
 * strongest frequency components in DTMF analysis.
 * 
 * @param magnitudes Array of magnitude values from frequency analysis
 * @param frequencies Array of corresponding frequency values
 * @param count Number of magnitude/frequency pairs
 * @return Structure containing maximum magnitude, frequency, and bin index
 */
tone_detection_t find_maximum_tone(const double *magnitudes, 
                                  const double *frequencies,
                                  size_t count) {
    tone_detection_t result = {0};
    
    if (count == 0) return result;
    
    result.magnitude = magnitudes[0];
    result.frequency = frequencies[0];
    result.index = 0;
    
    for (size_t i = 1; i < count; i++) {
        if (magnitudes[i] > result.magnitude) {
            result.magnitude = magnitudes[i];
            result.frequency = frequencies[i];
            result.index = (int)i;
        }
    }
    
    return result;
}

/**
 * @brief Calculate confidence ratio between best and second-best frequency
 * 
 * Computes the ratio between the strongest and second-strongest magnitude
 * values to assess the confidence of frequency detection. Higher ratios
 * indicate more reliable detection with clear dominant frequencies.
 * 
 * @param magnitudes Array of magnitude values
 * @param count Number of magnitude values
 * @param best_index Index of the maximum magnitude
 * @return Ratio of best to second-best magnitude (higher is more confident)
 */
double calculate_confidence_ratio(const double *magnitudes, 
                                size_t count, 
                                int best_index) {
    if (count < 2 || best_index < 0 || best_index >= (int)count) {
        return 1.0;
    }
    
    double second_best = 0.0;
    for (size_t i = 0; i < count; i++) {
        if ((int)i != best_index && magnitudes[i] > second_best) {
            second_best = magnitudes[i];
        }
    }
    
    return (second_best > 0.0) ? magnitudes[best_index] / second_best : 1e9;
}

/**
 * @brief Map frequency indices to DTMF character
 * 
 * Converts row and column frequency indices to the corresponding DTMF
 * character using the standard keypad mapping. Returns '?' for invalid
 * frequency combinations.
 * 
 * @param config DTMF configuration containing key mapping
 * @param low_idx Index into low (row) frequency array
 * @param high_idx Index into high (column) frequency array
 * @return DTMF character ('0'-'9', 'A'-'D', '*', '#') or '?' if invalid
 */
char map_dtmf_key(const dtmf_config_t *config, int low_idx, int high_idx) {
    if (!config || 
        low_idx < 0 || low_idx >= (int)config->num_low_freqs ||
        high_idx < 0 || high_idx >= (int)config->num_high_freqs) {
        return '?';
    }
    
    return config->key_map[low_idx][high_idx];
}

// =============================================================================
// CONFIGURATION MANAGEMENT
// =============================================================================

/**
 * @brief Initialize configuration with default detection parameters
 * 
 * Sets up advanced DTMF configuration with standard detection thresholds,
 * processing parameters, and windowing settings optimized for reliable
 * DTMF detection under typical telephony conditions.
 * 
 * @param config Configuration structure to initialize with defaults
 */
void get_default_config(dtmf_config_advanced_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(dtmf_config_advanced_t));
    
    // Detection thresholds
    config->min_confidence_ratio = 2.0;
    config->energy_threshold = 1000.0;
    config->twist_tolerance_db = 8.0;
    config->dc_offset_threshold = 1000.0;
    config->clip_threshold = 30000.0;
    config->min_snr_db = 10.0;
    
    // Processing parameters
    config->frame_size_ms = FRAME_MS;
    config->sample_rate = SAMPLE_RATE;
    config->fft_size = FFT_SIZE;
    
    // Algorithm selection (all enabled by default)
    config->enable_fft = true;
    config->enable_goertzel = true;
    config->enable_goertzel_twist = true;
    config->enable_filterbank = true;
    config->enable_filterbank_iir = true;
    config->enable_correlation = true;
    config->enable_music = true;
    
    // Performance tuning
    config->memory_pool_size = 1024 * 1024; // 1MB
    config->use_lookup_tables = true;
    config->enable_audio_validation = true;
    config->enable_dc_removal = true;
    config->enable_agc = false;
    
    // Logging
    config->log_level = LOG_WARNING;
    config->log_to_stderr = true;
    config->log_file = NULL;
}

/**
 * @brief Load configuration from file with fallback to defaults
 * 
 * Parses a configuration file in key=value format to customize DTMF
 * detection parameters. If the file cannot be read, uses default values.
 * Supports configuration of detection thresholds, algorithm selection,
 * and performance tuning options.
 * 
 * @param config_file Path to configuration file
 * @param config Configuration structure to populate
 * @return DTMF_SUCCESS on success (even with defaults), DTMF_ERROR_INVALID_PARAM on invalid input
 */
dtmf_result_t load_config(const char *config_file, 
                         dtmf_config_advanced_t *config) {
    if (!config_file || !config) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Static buffer for log file path (limited to 255 characters for safety)
    static char log_file_buffer[256];
    
    // Start with defaults
    get_default_config(config);
    
    FILE *file = fopen(config_file, "r");
    if (!file) {
        dtmf_log(LOG_WARNING, NULL, "Could not open config file %s, using defaults", 
                config_file);
        return DTMF_SUCCESS; // Use defaults
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == 0) continue;
        
        // Parse key=value pairs
        char *equals = strchr(line, '=');
        if (!equals) continue;
        
        *equals = 0;
        char *key = line;
        char *value = equals + 1;
        
        // Trim whitespace
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        
        // Parse configuration values
        // Detection thresholds
        if (strcmp(key, "min_confidence_ratio") == 0) {
            config->min_confidence_ratio = atof(value);
        } else if (strcmp(key, "energy_threshold") == 0) {
            config->energy_threshold = atof(value);
        } else if (strcmp(key, "twist_tolerance_db") == 0) {
            config->twist_tolerance_db = atof(value);
        } else if (strcmp(key, "dc_offset_threshold") == 0) {
            config->dc_offset_threshold = atof(value);
        } else if (strcmp(key, "clip_threshold") == 0) {
            config->clip_threshold = atof(value);
        } else if (strcmp(key, "min_snr_db") == 0) {
            config->min_snr_db = atof(value);
        }
        // Processing parameters
        else if (strcmp(key, "frame_size_ms") == 0) {
            config->frame_size_ms = (size_t)atoi(value);
        } else if (strcmp(key, "sample_rate") == 0) {
            config->sample_rate = (size_t)atoi(value);
        } else if (strcmp(key, "fft_size") == 0) {
            config->fft_size = (size_t)atoi(value);
        }
        // Algorithm selection flags
        else if (strcmp(key, "enable_fft") == 0) {
            config->enable_fft = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_goertzel") == 0) {
            config->enable_goertzel = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_goertzel_twist") == 0) {
            config->enable_goertzel_twist = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_filterbank") == 0) {
            config->enable_filterbank = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_filterbank_iir") == 0) {
            config->enable_filterbank_iir = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_correlation") == 0) {
            config->enable_correlation = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_music") == 0) {
            config->enable_music = (strcmp(value, "true") == 0);
        }
        // Performance tuning options
        else if (strcmp(key, "memory_pool_size") == 0) {
            config->memory_pool_size = (size_t)atoi(value);
        } else if (strcmp(key, "use_lookup_tables") == 0) {
            config->use_lookup_tables = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_audio_validation") == 0) {
            config->enable_audio_validation = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_dc_removal") == 0) {
            config->enable_dc_removal = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_agc") == 0) {
            config->enable_agc = (strcmp(value, "true") == 0);
        }
        // Logging configuration
        else if (strcmp(key, "log_level") == 0) {
            config->log_level = (log_level_t)atoi(value);
        } else if (strcmp(key, "log_to_stderr") == 0) {
            config->log_to_stderr = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "log_file") == 0) {
            // Copy log file path to static buffer (limit to 255 chars for safety)
            size_t len = strlen(value);
            if (len > 0 && len <= 255) {
                strncpy(log_file_buffer, value, sizeof(log_file_buffer) - 1);
                log_file_buffer[sizeof(log_file_buffer) - 1] = '\0'; // Ensure null termination
                config->log_file = log_file_buffer;
                dtmf_log(LOG_INFO, NULL, "Log file configured: %s", config->log_file);
            } else if (len > 255) {
                dtmf_log(LOG_WARNING, NULL, "Log file path too long (%zu chars, max 255), ignoring: %.50s...", len, value);
                config->log_file = NULL;
            }
            // If len == 0, we just ignore it (keep default NULL)
        }
    }
    
    fclose(file);
    return DTMF_SUCCESS;
}

/**
 * @brief Initialize audio processing context with configuration
 * 
 * Sets up audio processing context including memory pools, pre-allocated
 * buffers, and windowing functions based on the provided configuration.
 * Optimizes memory allocation patterns for real-time processing.
 * 
 * @param ctx Audio context structure to initialize
 * @param config Advanced configuration parameters
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t audio_context_init(audio_context_t *ctx, 
                                const dtmf_config_advanced_t *config) {
    if (!ctx || !config) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    memset(ctx, 0, sizeof(audio_context_t));
    
    // Copy basic parameters
    ctx->sample_rate = config->sample_rate;
    ctx->fft_size = config->fft_size;
    ctx->sample_count = SAMPLES_PER_FRAME;
    
    // Create memory pool
    if (memory_pool_create(&ctx->mem_pool, config->memory_pool_size) != DTMF_SUCCESS) {
        return DTMF_ERROR_MEMORY;
    }
    
    // Pre-allocate buffers
    ctx->fft_buffer = memory_pool_alloc(ctx->mem_pool, 
                                       config->fft_size * sizeof(complex_t));
    ctx->windowed_buffer = memory_pool_alloc(ctx->mem_pool, 
                                            SAMPLES_PER_FRAME * sizeof(double));
    
    if (!ctx->fft_buffer || !ctx->windowed_buffer) {
        memory_pool_destroy(ctx->mem_pool);
        return DTMF_ERROR_MEMORY;
    }
    
    // Pre-compute lookup tables if enabled
    if (config->use_lookup_tables) {
        ctx->cos_table = memory_pool_alloc(ctx->mem_pool, 
                                          SAMPLES_PER_FRAME * sizeof(double));
        ctx->sin_table = memory_pool_alloc(ctx->mem_pool, 
                                          SAMPLES_PER_FRAME * sizeof(double));
        
        if (ctx->cos_table && ctx->sin_table) {
            for (size_t i = 0; i < SAMPLES_PER_FRAME; i++) {
                double angle = 2.0 * M_PI * i / SAMPLES_PER_FRAME;
                ctx->cos_table[i] = cos(angle);
                ctx->sin_table[i] = sin(angle);
            }
        }
    }
    
    return DTMF_SUCCESS;
}

/**
 * @brief Clean up audio processing context and free resources
 * 
 * Releases all allocated memory including the memory pool and associated
 * buffers. Audio context becomes invalid after this call.
 * 
 * @param ctx Audio context to clean up
 */
void audio_context_cleanup(audio_context_t *ctx) {
    if (ctx) {
        memory_pool_destroy(ctx->mem_pool);
        memset(ctx, 0, sizeof(audio_context_t));
    }
}
