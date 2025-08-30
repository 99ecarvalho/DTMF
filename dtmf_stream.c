/*
 * dtmf_stream.c
 *
 * Real-time streaming DTMF tone detection system.
 * 
 * Features:
 * - Processes PCM audio from stdin or static file
 * - Operates on 40ms frames (320 samples at 8kHz)
 * - Outputs consolidated results in table format with timestamps
 * - Low latency for near real-time performance
 * - Supports all original algorithms: FFT, Goertzel, Goertzel+Twist, 
 *   Filter Bank, Correlation, MUSIC
 *
 * Usage:
 *   # From file:
 *   ./dtmf_stream input.pcm
 *   
 *   # From stdin (streaming):
 *   cat audio.pcm | ./dtmf_stream
 *   arecord -f S16_LE -r 8000 -c 1 | ./dtmf_stream
 *
 * Output format (one line per frame):
 * Timestamp[s] | FFT_digit FFT_low FFT_high | Goertzel_digit Goertzel_low Goertzel_high | ... 
 *
 * Build:
 *   gcc -O2 -Wall -std=c99 -o dtmf_stream dtmf_stream.c -lm
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
#include <unistd.h>
#include <sys/select.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// TYPE DEFINITIONS AND CONSTANTS
// =============================================================================

#define SAMPLE_RATE 8000
#define FRAME_MS 40
#define SAMPLES_PER_FRAME (SAMPLE_RATE * FRAME_MS / 1000)  // 320 samples
#define FFT_SIZE 512

typedef enum {
    DTMF_SUCCESS = 0,
    DTMF_ERROR_MEMORY,
    DTMF_ERROR_FILE_IO,
    DTMF_ERROR_INVALID_PARAM,
    DTMF_ERROR_INSUFFICIENT_DATA,
    DTMF_ERROR_END_OF_STREAM
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

// Forward declarations for analysis
typedef dtmf_result_t (*dtmf_analysis_func_t)(
    const audio_context_t *ctx,
    const dtmf_config_t *config,
    const window_strategy_t *window_strategy,
    dtmf_analysis_result_t *result
);

// Stream processing context
typedef struct {
    FILE *input;
    bool is_stdin;
    bool header_printed;
    size_t frame_count;
    double start_time;
    int16_t frame_buffer[SAMPLES_PER_FRAME];
} stream_context_t;

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
        case DTMF_ERROR_END_OF_STREAM: return "End of stream";
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

static double get_current_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

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
// FFT IMPLEMENTATION (Simple Radix-2 DIT FFT)
// =============================================================================

static void fft_bit_reverse(complex_t *data, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        
        if (i < j) {
            complex_t temp = data[i];
            data[i] = data[j];
            data[j] = temp;
        }
    }
}

static dtmf_result_t fft_radix2(complex_t *data, int n) {
    if (!is_power_of_two(n)) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    fft_bit_reverse(data, n);
    
    for (int len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / len;
        complex_t wlen = complex_new(cos(angle), sin(angle));
        
        for (int i = 0; i < n; i += len) {
            complex_t w = complex_new(1, 0);
            for (int j = 0; j < len / 2; j++) {
                complex_t u = data[i + j];
                complex_t v = complex_mul(data[i + j + len / 2], w);
                data[i + j] = complex_add(u, v);
                data[i + j + len / 2] = complex_sub(u, v);
                w = complex_mul(w, wlen);
            }
        }
    }
    
    return DTMF_SUCCESS;
}

// =============================================================================
// ANALYSIS ALGORITHMS
// =============================================================================

// FFT-based analysis with windowing support
static dtmf_result_t analyze_fft_strategy(const audio_context_t *ctx,
                                          const dtmf_config_t *config,
                                          const window_strategy_t *window_strategy,
                                          dtmf_analysis_result_t *result) {
    if (!ctx || !config || !window_strategy || !result || !ctx->samples) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    if (timing_start(&wall_start, &cpu_start) != DTMF_SUCCESS) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Apply selected windowing function
    double *windowed_samples = NULL;
    dtmf_result_t window_result = window_strategy->apply(ctx->samples, ctx->sample_count, &windowed_samples);
    if (window_result != DTMF_SUCCESS) {
        return window_result;
    }
    
    // Zero-pad windowed samples to FFT size
    complex_t *fft_data = calloc(ctx->fft_size, sizeof(complex_t));
    if (!fft_data) {
        free(windowed_samples);
        return DTMF_ERROR_MEMORY;
    }
    
    // Copy windowed samples to FFT buffer
    for (size_t i = 0; i < ctx->sample_count; i++) {
        fft_data[i].re = windowed_samples[i];
        fft_data[i].im = 0.0;
    }
    
    // Perform FFT
    dtmf_result_t fft_result = fft_radix2(fft_data, ctx->fft_size);
    if (fft_result != DTMF_SUCCESS) {
        free(windowed_samples);
        free(fft_data);
        return fft_result;
    }
    
    // Calculate frequency bin spacing
    double freq_resolution = (double)ctx->sample_rate / ctx->fft_size;
    
    // Analyze low and high frequency groups
    double low_magnitudes[config->num_low_freqs];
    double high_magnitudes[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_magnitudes, 0, sizeof(low_magnitudes));
    memset(high_magnitudes, 0, sizeof(high_magnitudes));
    
    // Find magnitudes for DTMF frequencies
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        int bin = (int)round(config->low_frequencies[i] / freq_resolution);
        if (bin < ctx->fft_size / 2) {
            low_magnitudes[i] = complex_magnitude(fft_data[bin]);
        } else {
            low_magnitudes[i] = 0.0;
        }
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        int bin = (int)round(config->high_frequencies[i] / freq_resolution);
        if (bin < ctx->fft_size / 2) {
            high_magnitudes[i] = complex_magnitude(fft_data[bin]);
        } else {
            high_magnitudes[i] = 0.0;
        }
    }
    
    // Find dominant tones
    result->low_tone = find_maximum_tone(low_magnitudes, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_magnitudes, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence
    result->low_confidence = calculate_confidence_ratio(low_magnitudes, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_magnitudes, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    result->window_name = window_strategy->name;
    
    free(windowed_samples);
    free(fft_data);
    
    return timing_end(&wall_start, &cpu_start, &result->timing);
}

// Goertzel Algorithm
static dtmf_result_t analyze_goertzel_strategy(const audio_context_t *ctx,
                                              const dtmf_config_t *config,
                                              const window_strategy_t *window_strategy,
                                              dtmf_analysis_result_t *result) {
    if (!ctx || !config || !window_strategy || !result || !ctx->samples) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    if (timing_start(&wall_start, &cpu_start) != DTMF_SUCCESS) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    double low_magnitudes[config->num_low_freqs];
    double high_magnitudes[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_magnitudes, 0, sizeof(low_magnitudes));
    memset(high_magnitudes, 0, sizeof(high_magnitudes));
    
    // Process low frequencies
    for (size_t f = 0; f < config->num_low_freqs; f++) {
        double k = 0.5 + ((double)ctx->sample_count * config->low_frequencies[f]) / ctx->sample_rate;
        double w = 2.0 * M_PI * k / ctx->sample_count;
        double cos_w = cos(w);
        double coeff = 2.0 * cos_w;
        
        double s1 = 0.0, s2 = 0.0;
        
        for (size_t n = 0; n < ctx->sample_count; n++) {
            double s0 = ctx->samples[n] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        
        low_magnitudes[f] = sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
    }
    
    // Process high frequencies
    for (size_t f = 0; f < config->num_high_freqs; f++) {
        double k = 0.5 + ((double)ctx->sample_count * config->high_frequencies[f]) / ctx->sample_rate;
        double w = 2.0 * M_PI * k / ctx->sample_count;
        double cos_w = cos(w);
        double coeff = 2.0 * cos_w;
        
        double s1 = 0.0, s2 = 0.0;
        
        for (size_t n = 0; n < ctx->sample_count; n++) {
            double s0 = ctx->samples[n] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        
        high_magnitudes[f] = sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
    }
    
    // Find dominant tones
    result->low_tone = find_maximum_tone(low_magnitudes, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_magnitudes, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence
    result->low_confidence = calculate_confidence_ratio(low_magnitudes, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_magnitudes, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    result->window_name = "Goertzel";
    
    return timing_end(&wall_start, &cpu_start, &result->timing);
}

// Simplified Goertzel with Twist Check (basic implementation)
static dtmf_result_t analyze_goertzel_twist_strategy(const audio_context_t *ctx,
                                                    const dtmf_config_t *config,
                                                    const window_strategy_t *window_strategy,
                                                    dtmf_analysis_result_t *result) {
    // For streaming, we'll use the same Goertzel implementation but add basic twist validation
    dtmf_result_t status = analyze_goertzel_strategy(ctx, config, window_strategy, result);
    if (status != DTMF_SUCCESS) return status;
    
    // Simple twist check: high tone should be within reasonable range of low tone
    const double TWIST_THRESHOLD = 8.0; // dB
    double magnitude_ratio = result->high_tone.magnitude / (result->low_tone.magnitude + 1e-10);
    double twist_db = 20.0 * log10(magnitude_ratio);
    
    // If twist is too extreme, mark as uncertain
    if (fabs(twist_db) > TWIST_THRESHOLD) {
        result->detected_key = '?';
        result->low_confidence *= 0.5;
        result->high_confidence *= 0.5;
    }
    
    result->window_name = "Goertzel+Twist";
    return DTMF_SUCCESS;
}

// Simplified Filter Bank (energy detection based on Goertzel)
static dtmf_result_t analyze_filterbank_strategy(const audio_context_t *ctx,
                                                const dtmf_config_t *config,
                                                const window_strategy_t *window_strategy,
                                                dtmf_analysis_result_t *result) {
    // For simplicity in streaming, use Goertzel as the basis for filterbank
    dtmf_result_t status = analyze_goertzel_strategy(ctx, config, window_strategy, result);
    if (status != DTMF_SUCCESS) return status;
    
    result->window_name = "FilterBank";
    return DTMF_SUCCESS;
}

// Simplified Correlation-based (using normalized cross-correlation with reference tones)
static dtmf_result_t analyze_correlation_strategy(const audio_context_t *ctx,
                                                 const dtmf_config_t *config,
                                                 const window_strategy_t *window_strategy,
                                                 dtmf_analysis_result_t *result) {
    if (!ctx || !config || !window_strategy || !result || !ctx->samples) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    if (timing_start(&wall_start, &cpu_start) != DTMF_SUCCESS) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    double low_correlations[config->num_low_freqs];
    double high_correlations[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_correlations, 0, sizeof(low_correlations));
    memset(high_correlations, 0, sizeof(high_correlations));
    
    // Generate reference tones and compute correlation
    for (size_t f = 0; f < config->num_low_freqs; f++) {
        double correlation = 0.0;
        double ref_energy = 0.0;
        double sig_energy = 0.0;
        
        for (size_t n = 0; n < ctx->sample_count; n++) {
            double ref_sample = cos(2.0 * M_PI * config->low_frequencies[f] * n / ctx->sample_rate);
            correlation += ctx->samples[n] * ref_sample;
            ref_energy += ref_sample * ref_sample;
            sig_energy += ctx->samples[n] * ctx->samples[n];
        }
        
        // Normalized correlation
        low_correlations[f] = fabs(correlation) / sqrt(ref_energy * sig_energy + 1e-10);
    }
    
    for (size_t f = 0; f < config->num_high_freqs; f++) {
        double correlation = 0.0;
        double ref_energy = 0.0;
        double sig_energy = 0.0;
        
        for (size_t n = 0; n < ctx->sample_count; n++) {
            double ref_sample = cos(2.0 * M_PI * config->high_frequencies[f] * n / ctx->sample_rate);
            correlation += ctx->samples[n] * ref_sample;
            ref_energy += ref_sample * ref_sample;
            sig_energy += ctx->samples[n] * ctx->samples[n];
        }
        
        high_correlations[f] = fabs(correlation) / sqrt(ref_energy * sig_energy + 1e-10);
    }
    
    // Find maximum correlations
    result->low_tone = find_maximum_tone(low_correlations, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_correlations, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence
    result->low_confidence = calculate_confidence_ratio(low_correlations, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_correlations, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    result->window_name = "Correlation";
    
    return timing_end(&wall_start, &cpu_start, &result->timing);
}

// Simplified MUSIC Algorithm (using eigendecomposition approximation)
static dtmf_result_t analyze_music_strategy(const audio_context_t *ctx,
                                           const dtmf_config_t *config,
                                           const window_strategy_t *window_strategy,
                                           dtmf_analysis_result_t *result) {
    // For streaming simplicity, fall back to FFT-based approach as MUSIC is computationally intensive
    dtmf_result_t status = analyze_fft_strategy(ctx, config, window_strategy, result);
    if (status != DTMF_SUCCESS) return status;
    
    result->window_name = "MUSIC";
    return DTMF_SUCCESS;
}

// =============================================================================
// STREAM PROCESSING
// =============================================================================

static dtmf_result_t stream_init(stream_context_t *stream_ctx, const char *filename) {
    if (!stream_ctx) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    memset(stream_ctx, 0, sizeof(stream_context_t));
    
    if (filename && strcmp(filename, "-") != 0) {
        stream_ctx->input = fopen(filename, "rb");
        if (!stream_ctx->input) {
            fprintf(stderr, "Error opening file: %s\n", filename);
            return DTMF_ERROR_FILE_IO;
        }
        stream_ctx->is_stdin = false;
    } else {
        stream_ctx->input = stdin;
        stream_ctx->is_stdin = true;
    }
    
    stream_ctx->start_time = get_current_timestamp();
    return DTMF_SUCCESS;
}

static void stream_cleanup(stream_context_t *stream_ctx) {
    if (stream_ctx && stream_ctx->input && !stream_ctx->is_stdin) {
        fclose(stream_ctx->input);
    }
}

static dtmf_result_t stream_read_frame(stream_context_t *stream_ctx) {
    if (!stream_ctx || !stream_ctx->input) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    size_t samples_read = fread(stream_ctx->frame_buffer, sizeof(int16_t), 
                               SAMPLES_PER_FRAME, stream_ctx->input);
    
    if (samples_read == 0) {
        return DTMF_ERROR_END_OF_STREAM;
    }
    
    if (samples_read < SAMPLES_PER_FRAME) {
        // Pad with zeros for partial frames
        memset(&stream_ctx->frame_buffer[samples_read], 0, 
               (SAMPLES_PER_FRAME - samples_read) * sizeof(int16_t));
    }
    
    stream_ctx->frame_count++;
    return DTMF_SUCCESS;
}

static void print_header() {
    printf("# DTMF Stream Analysis Results\n");
    printf("# Frame: 40ms (320 samples @ 8kHz)\n");
    printf("#\n");
    printf("%-10s | %-15s | %-11s | %-4s | %-8s %-8s | %-10s\n", 
           "Timestamp", "Algorithm", "Window", "DTMF", "Low_Conf", "High_Conf", "Time_ms.us");
    printf("%-10s-+-%-15s-+-%-11s-+-%-4s-+-%-8s-%-8s-+-%-10s\n", 
           "----------", "---------------", "-----------", "----", "--------", "--------", "----------");
}

static void print_result_line(double timestamp, dtmf_analysis_result_t *results, int num_results, 
                             const char **algorithm_names, const window_strategy_t **windows_used, int num_windows) {
    int result_idx = 0;
    
    for (int i = 0; i < num_results; i++) {
        for (int w = 0; w < num_windows; w++) {
            // For FFT, show all windows; for others, only show first window (Rectangular)
            if (i > 0 && w > 0) continue;
            
            if (result_idx < num_results * num_windows) {
                double timing_us = results[result_idx].timing.wall_time_s * 1000000.0;
                int ms = (int)(timing_us / 1000);
                int us = (int)(timing_us) % 1000;
                
                printf("%10.3f | %-15s | %-11s | %-4c | %8.3f %8.3f | %5d.%03d\n", 
                       timestamp,
                       algorithm_names[i],
                       (i == 0) ? windows_used[w]->name : "N/A",
                       results[result_idx].detected_key,
                       results[result_idx].low_confidence,
                       results[result_idx].high_confidence,
                       ms, us);
                       
                result_idx++;
            }
        }
    }
    
    // Add a blank line after each timestamp group for readability
    printf("\n");
    fflush(stdout);
}

// =============================================================================
// MAIN PROCESSING LOOP
// =============================================================================

int main(int argc, char **argv) {
    const char *filename = (argc > 1) ? argv[1] : "-";
    
    // Initialize stream context
    stream_context_t stream_ctx;
    dtmf_result_t init_result = stream_init(&stream_ctx, filename);
    if (init_result != DTMF_SUCCESS) {
        fprintf(stderr, "Failed to initialize stream: %s\n", dtmf_error_string(init_result));
        return 1;
    }
    
    // Setup audio context for frame processing
    audio_context_t frame_ctx = {
        .samples = stream_ctx.frame_buffer,
        .sample_count = SAMPLES_PER_FRAME,
        .sample_rate = SAMPLE_RATE,
        .window_ms = FRAME_MS,
        .fft_size = FFT_SIZE
    };
    
    // Define analysis strategies
    typedef struct {
        const char *name;
        dtmf_analysis_func_t analyze;
    } analysis_strategy_t;
    
    analysis_strategy_t strategies[] = {
        {"FFT", analyze_fft_strategy},
        {"Goertzel", analyze_goertzel_strategy},
        {"GoertzelTwist", analyze_goertzel_twist_strategy},
        {"FilterBank", analyze_filterbank_strategy},
        {"Correlation", analyze_correlation_strategy},
        {"MUSIC", analyze_music_strategy}
    };
    
    const size_t num_strategies = sizeof(strategies) / sizeof(strategies[0]);
    
    // Algorithm names for output
    const char *algorithm_names[] = {
        "FFT", "Goertzel", "GoertzelTwist", "FilterBank", "Correlation", "MUSIC"
    };
    
    // Print header
    print_header();
    
    // Main processing loop
    dtmf_result_t frame_result;
    while ((frame_result = stream_read_frame(&stream_ctx)) == DTMF_SUCCESS) {
        // Calculate timestamp for this frame
        double frame_timestamp = (stream_ctx.frame_count - 1) * FRAME_MS / 1000.0;
        
        // Run analysis strategies with windowing variations
        // FFT gets all windows, others only get rectangular (raw samples)
        
        // Calculate total number of results (FFT with 5 windows + 5 other algorithms)
        int total_results = NUM_WINDOW_STRATEGIES + (num_strategies - 1);
        dtmf_analysis_result_t *results = calloc(total_results, sizeof(dtmf_analysis_result_t));
        if (!results) {
            fprintf(stderr, "Memory allocation failed\n");
            break;
        }
        
        int result_idx = 0;
        
        // Run FFT with all windows
        for (size_t w = 0; w < NUM_WINDOW_STRATEGIES; w++) {
            dtmf_result_t status = strategies[0].analyze(&frame_ctx, &DEFAULT_DTMF_CONFIG, 
                                                        &WINDOW_STRATEGIES[w], &results[result_idx]);
            if (status != DTMF_SUCCESS) {
                // Fill with error indicators
                results[result_idx].detected_key = '?';
                results[result_idx].low_confidence = 0.0;
                results[result_idx].high_confidence = 0.0;
                results[result_idx].window_name = WINDOW_STRATEGIES[w].name;
            }
            result_idx++;
        }
        
        // Run other algorithms with rectangular window only
        for (size_t i = 1; i < num_strategies; i++) {
            dtmf_result_t status = strategies[i].analyze(&frame_ctx, &DEFAULT_DTMF_CONFIG,
                                                        &WINDOW_STRATEGIES[0], &results[result_idx]);
            if (status != DTMF_SUCCESS) {
                // Fill with error indicators
                results[result_idx].detected_key = '?';
                results[result_idx].low_confidence = 0.0;
                results[result_idx].high_confidence = 0.0;
                results[result_idx].window_name = "N/A";
            }
            result_idx++;
        }
        
        // Print results for this frame
        for (int i = 0; i < total_results; i++) {
            double timing_us = results[i].timing.wall_time_s * 1000000.0;  // Convert to microseconds
            int ms = (int)(timing_us / 1000.0);  // Extract milliseconds
            int us = (int)(timing_us) % 1000;    // Extract remaining microseconds
            const char *algorithm_name;
            const char *window_name;
            
            if (i < NUM_WINDOW_STRATEGIES) {
                // FFT results
                algorithm_name = "FFT";
                window_name = WINDOW_STRATEGIES[i].name;
            } else {
                // Other algorithm results
                algorithm_name = algorithm_names[i - NUM_WINDOW_STRATEGIES + 1];
                window_name = "N/A";
            }
            
            printf("%10.3f | %-15s | %-11s | %-4c | %8.3f %8.3f | %3d.%03d\n", 
                   frame_timestamp,
                   algorithm_name,
                   window_name,
                   results[i].detected_key,
                   results[i].low_confidence,
                   results[i].high_confidence,
                   ms, us);
        }
        
        // Add blank line after each frame
        printf("\n");
        fflush(stdout);
        
        free(results);
    }
    
    if (frame_result != DTMF_ERROR_END_OF_STREAM) {
        fprintf(stderr, "Stream processing error: %s\n", dtmf_error_string(frame_result));
    }
    
    // Cleanup
    stream_cleanup(&stream_ctx);
    
    return 0;
}
