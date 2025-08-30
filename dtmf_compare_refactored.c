/*
 * dtmf_compare_refactored.c
 *
 * Refactored DTMF tone detection system with modular design.
 *
 * Usage:
 *   ./dtmf_compare <raw_pcm16_file> [sample_rate] [window_ms] [zero_pad_N]
 *
 * Key improvements:
 * - Modular design with clear separation of concerns
 * - Proper error handling and memory management
 * - Struct-based encapsulation of related data
 * - Function pointers for flexible analysis strategies
 * - Const correctness and type safety
 * - Performance optimizations while maintaining clarity
 *
 * Build:
 *   gcc -O2 -Wall -std=c99 -o dtmf_compare_refactored dtmf_compare_refactored.c -lm
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// TYPE DEFINITIONS AND CONSTANTS
// =============================================================================

typedef enum {
    DTMF_SUCCESS = 0,
    DTMF_ERROR_MEMORY,
    DTMF_ERROR_FILE_IO,
    DTMF_ERROR_INVALID_PARAM,
    DTMF_ERROR_INSUFFICIENT_DATA
} dtmf_result_t;

typedef struct {
    double re, im;
} complex_t;

typedef struct {
    double wall_time_s;
    double cpu_time_s;
} timing_info_t;

typedef struct {
    double magnitude;
    double frequency;
    int index;
} tone_detection_t;

typedef struct {
    tone_detection_t low_tone;
    tone_detection_t high_tone;
    char detected_key;
    double low_confidence;
    double high_confidence;
    timing_info_t timing;
    const char *window_name;
} dtmf_analysis_result_t;

typedef struct {
    const double *low_frequencies;
    const double *high_frequencies;
    const char (*key_map)[4];
    size_t num_low_freqs;
    size_t num_high_freqs;
} dtmf_config_t;

typedef struct {
    int16_t *samples;
    size_t sample_count;
    int sample_rate;
    int window_ms;
    int fft_size;
} audio_context_t;

// Forward declarations for windowing
typedef dtmf_result_t (*window_func_t)(const int16_t *input_samples,
                                       size_t sample_count,
                                       double **windowed_samples_out);

typedef struct {
    const char *name;
    window_func_t apply;
} window_strategy_t;

// Function pointer type for analysis strategies
typedef dtmf_result_t (*dtmf_analysis_func_t)(
    const audio_context_t *ctx,
    const dtmf_config_t *config,
    const window_strategy_t *window_strategy,
    dtmf_analysis_result_t *result
);

// =============================================================================
// CONSTANTS
// =============================================================================

static const double DTMF_LOW_FREQUENCIES[] = {697.0, 770.0, 852.0, 941.0};
static const double DTMF_HIGH_FREQUENCIES[] = {1209.0, 1336.0, 1477.0, 1633.0};
static const char DTMF_KEY_MAP[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static const dtmf_config_t DEFAULT_DTMF_CONFIG = {
    .low_frequencies = DTMF_LOW_FREQUENCIES,
    .high_frequencies = DTMF_HIGH_FREQUENCIES,
    .key_map = DTMF_KEY_MAP,
    .num_low_freqs = sizeof(DTMF_LOW_FREQUENCIES) / sizeof(DTMF_LOW_FREQUENCIES[0]),
    .num_high_freqs = sizeof(DTMF_HIGH_FREQUENCIES) / sizeof(DTMF_HIGH_FREQUENCIES[0])
};

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

static const char* dtmf_error_string(dtmf_result_t result) {
    switch (result) {
        case DTMF_SUCCESS: return "Success";
        case DTMF_ERROR_MEMORY: return "Memory allocation error";
        case DTMF_ERROR_FILE_IO: return "File I/O error";
        case DTMF_ERROR_INVALID_PARAM: return "Invalid parameter";
        case DTMF_ERROR_INSUFFICIENT_DATA: return "Insufficient data";
        default: return "Unknown error";
    }
}

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

static inline bool is_power_of_two(int x) {
    return x > 0 && (x & (x - 1)) == 0;
}

static int next_power_of_two(int x) {
    if (x <= 1) return 1;
    int power = 1;
    while (power < x) {
        power <<= 1;
    }
    return power;
}

// =============================================================================
// TIMING UTILITIES
// =============================================================================

static dtmf_result_t timing_start(struct timespec *wall_start, struct timespec *cpu_start) {
    if (clock_gettime(CLOCK_MONOTONIC, wall_start) != 0 ||
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, cpu_start) != 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    return DTMF_SUCCESS;
}

static dtmf_result_t timing_end(const struct timespec *wall_start, 
                                const struct timespec *cpu_start,
                                timing_info_t *timing) {
    struct timespec wall_end, cpu_end, wall_diff, cpu_diff;
    
    if (clock_gettime(CLOCK_MONOTONIC, &wall_end) != 0 ||
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end) != 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Calculate differences
    wall_diff.tv_sec = wall_end.tv_sec - wall_start->tv_sec;
    wall_diff.tv_nsec = wall_end.tv_nsec - wall_start->tv_nsec;
    if (wall_diff.tv_nsec < 0) {
        wall_diff.tv_nsec += 1000000000L;
        wall_diff.tv_sec -= 1;
    }
    
    cpu_diff.tv_sec = cpu_end.tv_sec - cpu_start->tv_sec;
    cpu_diff.tv_nsec = cpu_end.tv_nsec - cpu_start->tv_nsec;
    if (cpu_diff.tv_nsec < 0) {
        cpu_diff.tv_nsec += 1000000000L;
        cpu_diff.tv_sec -= 1;
    }
    
    timing->wall_time_s = wall_diff.tv_sec + wall_diff.tv_nsec / 1e9;
    timing->cpu_time_s = cpu_diff.tv_sec + cpu_diff.tv_nsec / 1e9;
    
    return DTMF_SUCCESS;
}

// =============================================================================
// AUDIO CONTEXT MANAGEMENT
// =============================================================================

static dtmf_result_t audio_context_create(const char *filename, 
                                          int sample_rate,
                                          int window_ms, 
                                          int fft_size_hint,
                                          audio_context_t **ctx_out) {
    if (!filename || !ctx_out || sample_rate <= 0 || window_ms <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    audio_context_t *ctx = calloc(1, sizeof(audio_context_t));
    if (!ctx) {
        return DTMF_ERROR_MEMORY;
    }
    
    ctx->sample_rate = sample_rate;
    ctx->window_ms = window_ms;
    ctx->sample_count = (size_t)(sample_rate * window_ms) / 1000;
    
    if (ctx->sample_count < 8) {
        ctx->sample_count = 8;
    }
    
    // Determine FFT size
    if (fft_size_hint <= 0) {
        ctx->fft_size = next_power_of_two((int)ctx->sample_count);
    } else {
        if (!is_power_of_two(fft_size_hint)) {
            ctx->fft_size = next_power_of_two(fft_size_hint);
        } else {
            ctx->fft_size = fft_size_hint;
        }
        if (ctx->fft_size < (int)ctx->sample_count) {
            ctx->fft_size = next_power_of_two((int)ctx->sample_count);
        }
    }
    
    // Allocate sample buffer
    ctx->samples = malloc(sizeof(int16_t) * ctx->sample_count);
    if (!ctx->samples) {
        free(ctx);
        return DTMF_ERROR_MEMORY;
    }
    
    // Read samples from file
    FILE *file = fopen(filename, "rb");
    if (!file) {
        free(ctx->samples);
        free(ctx);
        return DTMF_ERROR_FILE_IO;
    }
    
    size_t samples_read = fread(ctx->samples, sizeof(int16_t), ctx->sample_count, file);
    fclose(file);
    
    if (samples_read < ctx->sample_count) {
        fprintf(stderr, "Warning: file shorter than requested window (%zu samples). Using %zu samples.\n", 
                ctx->sample_count, samples_read);
        ctx->sample_count = samples_read;
    }
    
    if (ctx->sample_count == 0) {
        free(ctx->samples);
        free(ctx);
        return DTMF_ERROR_INSUFFICIENT_DATA;
    }
    
    *ctx_out = ctx;
    return DTMF_SUCCESS;
}

static void audio_context_destroy(audio_context_t *ctx) {
    if (ctx) {
        free(ctx->samples);
        free(ctx);
    }
}

// =============================================================================
// WINDOWING FUNCTIONS
// =============================================================================

static dtmf_result_t apply_hamming_window(const int16_t *input_samples, 
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

static dtmf_result_t apply_hanning_window(const int16_t *input_samples,
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

static dtmf_result_t apply_blackman_window(const int16_t *input_samples,
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

static dtmf_result_t apply_kaiser_window(const int16_t *input_samples,
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

static dtmf_result_t apply_rectangular_window(const int16_t *input_samples,
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

// Window strategies array
static const window_strategy_t WINDOW_STRATEGIES[] = {
    {"Rectangular", apply_rectangular_window},
    {"Hamming", apply_hamming_window},
    {"Hanning", apply_hanning_window},
    {"Blackman", apply_blackman_window},
    {"Kaiser", apply_kaiser_window}
};

static const size_t NUM_WINDOW_STRATEGIES = sizeof(WINDOW_STRATEGIES) / sizeof(WINDOW_STRATEGIES[0]);

// =============================================================================
// FFT IMPLEMENTATION
// =============================================================================

static dtmf_result_t fft_radix2(complex_t *buffer, size_t n, bool inverse) {
    if (!buffer || !is_power_of_two((int)n)) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        
        if (i < j) {
            complex_t temp = buffer[i];
            buffer[i] = buffer[j];
            buffer[j] = temp;
        }
    }
    
    // Cooley-Tukey FFT
    for (size_t step_size = 2; step_size <= n; step_size <<= 1) {
        double angle = (inverse ? M_PI : -M_PI) / (step_size >> 1);
        
        for (size_t i = 0; i < n; i += step_size) {
            for (size_t j = 0; j < step_size >> 1; j++) {
                double w_angle = angle * j;
                complex_t w = complex_new(cos(w_angle), sin(w_angle));
                
                complex_t u = buffer[i + j];
                complex_t v = complex_mul(w, buffer[i + j + (step_size >> 1)]);
                
                buffer[i + j] = complex_add(u, v);
                buffer[i + j + (step_size >> 1)] = complex_sub(u, v);
            }
        }
    }
    
    // Scale for inverse transform
    if (inverse) {
        for (size_t i = 0; i < n; i++) {
            buffer[i].re /= n;
            buffer[i].im /= n;
        }
    }
    
    return DTMF_SUCCESS;
}

// =============================================================================
// TONE DETECTION UTILITIES
// =============================================================================

static tone_detection_t find_maximum_tone(const double *magnitudes, 
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

static double calculate_confidence_ratio(const double *magnitudes, 
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

static char map_dtmf_key(const dtmf_config_t *config, int low_idx, int high_idx) {
    if (!config || 
        low_idx < 0 || low_idx >= (int)config->num_low_freqs ||
        high_idx < 0 || high_idx >= (int)config->num_high_freqs) {
        return '?';
    }
    
    return config->key_map[low_idx][high_idx];
}

// =============================================================================
// GOERTZEL ALGORITHM
// =============================================================================

static double goertzel_magnitude(const int16_t *samples, 
                                size_t sample_count,
                                double sample_rate, 
                                double target_frequency) {
    if (!samples || sample_count == 0 || sample_rate <= 0 || target_frequency <= 0) {
        return 0.0;
    }
    
    double k = 0.5 + ((sample_count * target_frequency) / sample_rate);
    double omega = 2.0 * M_PI * k / sample_count;
    double coeff = 2.0 * cos(omega);
    
    double q0 = 0.0, q1 = 0.0, q2 = 0.0;
    
    for (size_t i = 0; i < sample_count; i++) {
        q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }
    
    double real_part = q1 - q2 * cos(omega);
    double imag_part = q2 * sin(omega);
    
    return sqrt(real_part * real_part + imag_part * imag_part);
}

// =============================================================================
// ANALYSIS STRATEGY IMPLEMENTATIONS
// =============================================================================

static dtmf_result_t analyze_fft_strategy(const audio_context_t *ctx,
                                         const dtmf_config_t *config,
                                         const window_strategy_t *window_strategy,
                                         dtmf_analysis_result_t *result) {
    if (!ctx || !config || !window_strategy || !result) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    dtmf_result_t timing_result = timing_start(&wall_start, &cpu_start);
    if (timing_result != DTMF_SUCCESS) {
        return timing_result;
    }
    
    // Apply selected windowing function
    double *windowed_samples = NULL;
    dtmf_result_t window_result = window_strategy->apply(ctx->samples, ctx->sample_count, &windowed_samples);
    if (window_result != DTMF_SUCCESS) {
        return window_result;
    }
    
    // Prepare FFT buffer
    complex_t *fft_buffer = calloc(ctx->fft_size, sizeof(complex_t));
    if (!fft_buffer) {
        free(windowed_samples);
        return DTMF_ERROR_MEMORY;
    }
    
    // Copy windowed samples to FFT buffer (zero-padded)
    for (size_t i = 0; i < ctx->sample_count; i++) {
        fft_buffer[i].re = windowed_samples[i];
        fft_buffer[i].im = 0.0;
    }
    
    // Perform FFT
    dtmf_result_t fft_result = fft_radix2(fft_buffer, ctx->fft_size, false);
    if (fft_result != DTMF_SUCCESS) {
        free(windowed_samples);
        free(fft_buffer);
        return fft_result;
    }
    
    // Calculate frequency resolution
    double freq_resolution = (double)ctx->sample_rate / ctx->fft_size;
    int half_size = ctx->fft_size / 2;
    
    // Find magnitudes for DTMF frequencies
    double low_magnitudes[config->num_low_freqs];
    double high_magnitudes[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_magnitudes, 0, sizeof(low_magnitudes));
    memset(high_magnitudes, 0, sizeof(high_magnitudes));
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        int bin = (int)round(config->low_frequencies[i] / freq_resolution);
        bin = (bin < 0) ? 0 : (bin >= half_size) ? half_size - 1 : bin;
        low_magnitudes[i] = complex_magnitude(fft_buffer[bin]);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        int bin = (int)round(config->high_frequencies[i] / freq_resolution);
        bin = (bin < 0) ? 0 : (bin >= half_size) ? half_size - 1 : bin;
        high_magnitudes[i] = complex_magnitude(fft_buffer[bin]);
    }
    
    // Find best tones
    result->low_tone = find_maximum_tone(low_magnitudes, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_magnitudes, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence ratios
    result->low_confidence = calculate_confidence_ratio(low_magnitudes, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_magnitudes, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    // Record window name
    result->window_name = window_strategy->name;
    
    // Record timing
    timing_end(&wall_start, &cpu_start, &result->timing);
    
    // Cleanup
    free(windowed_samples);
    free(fft_buffer);
    
    return DTMF_SUCCESS;
}

static dtmf_result_t analyze_goertzel_strategy(const audio_context_t *ctx,
                                              const dtmf_config_t *config,
                                              const window_strategy_t *window_strategy,
                                              dtmf_analysis_result_t *result) {
    if (!ctx || !config || !window_strategy || !result) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    dtmf_result_t timing_result = timing_start(&wall_start, &cpu_start);
    if (timing_result != DTMF_SUCCESS) {
        return timing_result;
    }
    
    // Calculate magnitudes for all DTMF frequencies
    double low_magnitudes[config->num_low_freqs];
    double high_magnitudes[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_magnitudes, 0, sizeof(low_magnitudes));
    memset(high_magnitudes, 0, sizeof(high_magnitudes));
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_magnitudes[i] = goertzel_magnitude(ctx->samples, ctx->sample_count,
                                             ctx->sample_rate, config->low_frequencies[i]);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_magnitudes[i] = goertzel_magnitude(ctx->samples, ctx->sample_count,
                                              ctx->sample_rate, config->high_frequencies[i]);
    }
    
    // Find best tones
    result->low_tone = find_maximum_tone(low_magnitudes, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_magnitudes, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence ratios
    result->low_confidence = calculate_confidence_ratio(low_magnitudes, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_magnitudes, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    // Record window name (Note: Goertzel uses raw samples, not windowed)
    result->window_name = "No Window";
    
    // Record timing
    timing_end(&wall_start, &cpu_start, &result->timing);
    
    return DTMF_SUCCESS;
}

// =============================================================================
// RESULT PRINTING
// =============================================================================

static void print_analysis_results(const char *method_name,
                                  const dtmf_analysis_result_t *result,
                                  const dtmf_config_t *config) __attribute__((unused));

static void print_analysis_results(const char *method_name,
                                  const dtmf_analysis_result_t *result,
                                  const dtmf_config_t *config) {
    printf("%s Results (Window: %s):\n", method_name, result->window_name);
    printf("┌─────────────────┬─────────────┐\n");
    printf("│ Frequency (Hz)  │ Magnitude   │\n");
    printf("├─────────────────┼─────────────┤\n");
    
    // Print low frequencies
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        double magnitude = (i == (size_t)result->low_tone.index) ? result->low_tone.magnitude : 0.0;
        const char *marker = (i == (size_t)result->low_tone.index) ? " *" : "  ";
        printf("│ Low  %6.1f%s   │ %11.6f │\n", 
               config->low_frequencies[i], marker, magnitude);
    }
    
    // Print high frequencies
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        double magnitude = (i == (size_t)result->high_tone.index) ? result->high_tone.magnitude : 0.0;
        const char *marker = (i == (size_t)result->high_tone.index) ? " *" : "  ";
        printf("│ High %6.1f%s   │ %11.6f │\n", 
               config->high_frequencies[i], marker, magnitude);
    }
    
    printf("└─────────────────┴─────────────┘\n");
    printf("Best Low:  %.1f Hz (idx %d)\n", result->low_tone.frequency, result->low_tone.index);
    printf("Best High: %.1f Hz (idx %d)\n", result->high_tone.frequency, result->high_tone.index);
    printf("Detected Key: %c\n", result->detected_key);
    printf("Confidence Ratios: Low=%.3f, High=%.3f\n", 
           result->low_confidence, result->high_confidence);
    printf("Timing: Wall=%.6fs, CPU=%.6fs\n\n", 
           result->timing.wall_time_s, result->timing.cpu_time_s);
}

static void print_parameters(const audio_context_t *ctx) {
    printf("Analysis Parameters:\n");
    printf("┌─────────────────┬─────────────┐\n");
    printf("│ Parameter       │ Value       │\n");
    printf("├─────────────────┼─────────────┤\n");
    printf("│ Sample Rate     │ %6d Hz   │\n", ctx->sample_rate);
    printf("│ Window Length   │ %6d ms   │\n", ctx->window_ms);
    printf("│ Window Samples  │ %6zu      │\n", ctx->sample_count);
    printf("│ FFT Size        │ %6d      │\n", ctx->fft_size);
    printf("│ Freq Resolution │ %6.3f Hz  │\n", (double)ctx->sample_rate / ctx->fft_size);
    printf("└─────────────────┴─────────────┘\n\n");
}

// =============================================================================
// MAIN APPLICATION
// =============================================================================

static void print_usage(const char *program_name) {
    printf("Usage: %s <raw_pcm16_file> [sample_rate=8000] [window_ms=40] [zero_pad_N=0]\n\n", program_name);
    printf("Arguments:\n");
    printf("  raw_pcm16_file  - 16-bit signed little-endian PCM mono audio file\n");
    printf("  sample_rate     - Sample rate in Hz (default: 8000)\n");
    printf("  window_ms       - Analysis window in milliseconds (default: 40)\n");
    printf("  zero_pad_N      - FFT size (power of two). If 0, auto-determined\n\n");
    printf("Examples:\n");
    printf("  %s audio.pcm\n", program_name);
    printf("  %s audio.pcm 8000 50 1024\n", program_name);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Parse command line arguments
    const char *filename = argv[1];
    int sample_rate = (argc >= 3) ? atoi(argv[2]) : 8000;
    int window_ms = (argc >= 4) ? atoi(argv[3]) : 40;
    int fft_size_hint = (argc >= 5) ? atoi(argv[4]) : 0;
    
    // Validate parameters
    if (sample_rate <= 0 || window_ms <= 0) {
        fprintf(stderr, "Error: Sample rate and window length must be positive\n");
        return 1;
    }
    
    // Create audio context
    audio_context_t *ctx = NULL;
    dtmf_result_t result = audio_context_create(filename, sample_rate, window_ms, fft_size_hint, &ctx);
    if (result != DTMF_SUCCESS) {
        fprintf(stderr, "Error creating audio context: %s\n", dtmf_error_string(result));
        return 1;
    }
    
    // Print analysis parameters
    print_parameters(ctx);
    
    // Define analysis strategies
    typedef struct {
        const char *name;
        dtmf_analysis_func_t analyze;
    } analysis_strategy_t;
    
    analysis_strategy_t strategies[] = {
        {"FFT with Zero-Padding", analyze_fft_strategy},
        {"Goertzel Algorithm", analyze_goertzel_strategy}
    };
    
    const size_t num_strategies = sizeof(strategies) / sizeof(strategies[0]);
    
    // Run analysis strategies with different windowing functions
    printf("🔬 Analysis Results with Multiple Windowing Functions:\n");
    printf("================================================================\n\n");
    
    for (size_t i = 0; i < num_strategies; i++) {
        printf("📊 %s Analysis:\n", strategies[i].name);
        printf("┌─────────────┬─────────────┬─────────────┬─────────────┬─────────────┐\n");
        printf("│ Window      │ Detected    │ Low Conf    │ High Conf   │ Timing (ms) │\n");
        printf("├─────────────┼─────────────┼─────────────┼─────────────┼─────────────┤\n");
        
        // Test with different windowing functions
        for (size_t w = 0; w < NUM_WINDOW_STRATEGIES; w++) {
            dtmf_analysis_result_t analysis_result = {0};
            
            // For Goertzel, we only test with the first window (Rectangular/Raw) since 
            // Goertzel works on raw samples, not windowed samples
            if (i == 1 && w > 0) continue; // Skip non-raw windows for Goertzel
            
            dtmf_result_t status = strategies[i].analyze(ctx, &DEFAULT_DTMF_CONFIG, &WINDOW_STRATEGIES[w], &analysis_result);
            
            if (status == DTMF_SUCCESS) {
                double timing_ms = analysis_result.timing.wall_time_s * 1000.0;
                printf("│ %-11s │ %-11c │ %-11.3f │ %-11.3f │ %-11.6f │\n",
                       analysis_result.window_name,
                       analysis_result.detected_key,
                       analysis_result.low_confidence,
                       analysis_result.high_confidence,
                       timing_ms);
            } else {
                printf("│ %-11s │ %-11s │ %-11s │ %-11s │ %-11s │\n",
                       WINDOW_STRATEGIES[w].name,
                       "ERROR", "N/A", "N/A", "N/A");
                fprintf(stderr, "Error in %s analysis with %s window: %s\n", 
                        strategies[i].name, 
                        WINDOW_STRATEGIES[w].name,
                        dtmf_error_string(status));
            }
        }
        
        printf("└─────────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n\n");
    }
    
    // Cleanup
    audio_context_destroy(ctx);
    
    return 0;
}
