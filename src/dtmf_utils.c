/*
 * dtmf_utils.c
 *
 * Utility functions implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "dtmf_utils.h"

// =============================================================================
// CONSTANTS
// =============================================================================

const double DTMF_LOW_FREQUENCIES[] = {697.0, 770.0, 852.0, 941.0};
const double DTMF_HIGH_FREQUENCIES[] = {1209.0, 1336.0, 1477.0, 1633.0};
const char DTMF_KEY_MAP[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

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

static log_level_t g_log_level = LOG_INFO;
static FILE *g_log_file = NULL;

// =============================================================================
// ERROR HANDLING AND LOGGING
// =============================================================================

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

void dtmf_set_log_level(log_level_t level) {
    g_log_level = level;
}

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

double get_current_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

dtmf_result_t timing_start(struct timespec *wall_start, struct timespec *cpu_start) {
    if (clock_gettime(CLOCK_MONOTONIC, wall_start) != 0 ||
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, cpu_start) != 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    return DTMF_SUCCESS;
}

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

void memory_pool_reset(memory_pool_t *pool) {
    if (pool) {
        pool->used = 0;
        pool->block_count = 0;
    }
}

void memory_pool_destroy(memory_pool_t *pool) {
    if (pool) {
        free(pool->pool);
        free(pool);
    }
}

// =============================================================================
// AUDIO QUALITY VALIDATION
// =============================================================================

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

dtmf_result_t load_config(const char *config_file, 
                         dtmf_config_advanced_t *config) {
    if (!config_file || !config) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
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
        if (strcmp(key, "min_confidence_ratio") == 0) {
            config->min_confidence_ratio = atof(value);
        } else if (strcmp(key, "energy_threshold") == 0) {
            config->energy_threshold = atof(value);
        } else if (strcmp(key, "twist_tolerance_db") == 0) {
            config->twist_tolerance_db = atof(value);
        } else if (strcmp(key, "log_level") == 0) {
            config->log_level = (log_level_t)atoi(value);
        } else if (strcmp(key, "enable_audio_validation") == 0) {
            config->enable_audio_validation = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_dc_removal") == 0) {
            config->enable_dc_removal = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_agc") == 0) {
            config->enable_agc = (strcmp(value, "true") == 0);
        }
        // Add more configuration options as needed
    }
    
    fclose(file);
    return DTMF_SUCCESS;
}

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

void audio_context_cleanup(audio_context_t *ctx) {
    if (ctx) {
        memory_pool_destroy(ctx->mem_pool);
        memset(ctx, 0, sizeof(audio_context_t));
    }
}
