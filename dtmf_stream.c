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
// GOERTZEL TWIST ALGORITHM WITH ITU COMPLIANCE
// =============================================================================

typedef struct {
    int fs;             // Sample rate
    int N;              // Frame length (samples)
    int k[8];           // Goertzel bin indices for 8 DTMF frequencies
    double coeff[8];    // 2*cos(2*pi*k/N) coefficients
    double freqs[8];    // Frequency list
} dtmf_goertzel_twist_t;

typedef struct {
    int valid;          // 1 if valid DTMF digit detected
    char digit;         // Detected digit
    double twist_db;    // Twist in dB: 10*log10(P_H/P_L)
    double power_low;   // Selected low band power
    double power_high;  // Selected high band power
    double energy_ratio; // (P_L + P_H) / total_energy
} dtmf_twist_result_t;

// DTMF frequency mapping table
static const double DTMF_TWIST_FREQS[8] = {697.0, 770.0, 852.0, 941.0, 1209.0, 1336.0, 1477.0, 1633.0};
static const char DTMF_TWIST_TABLE[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'}, 
    {'7','8','9','C'},
    {'*','0','#','D'}
};

// Helper function to find nearest Goertzel bin
static int nearest_goertzel_bin(int N, int fs, double frequency) {
    double k_exact = (double)N * frequency / (double)fs;
    return (int)round(k_exact);
}

// Initialize Goertzel twist detector
static dtmf_result_t init_goertzel_twist(dtmf_goertzel_twist_t *detector, int sample_rate, int frame_length) {
    if (!detector || sample_rate <= 0 || frame_length <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    memset(detector, 0, sizeof(*detector));
    detector->fs = sample_rate;
    detector->N = frame_length;
    
    for (int i = 0; i < 8; i++) {
        detector->freqs[i] = DTMF_TWIST_FREQS[i];
        detector->k[i] = nearest_goertzel_bin(frame_length, sample_rate, detector->freqs[i]);
        detector->coeff[i] = 2.0 * cos(2.0 * M_PI * detector->k[i] / (double)frame_length);
    }
    
    return DTMF_SUCCESS;
}

// Compute power using Goertzel algorithm (returns magnitude squared)
static double goertzel_power(const int16_t *samples, int N, double coeff) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    
    for (int n = 0; n < N; n++) {
        s0 = (double)samples[n] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    
    // Power at bin: s1^2 + s2^2 - coeff*s1*s2
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Calculate total frame energy for normalization
static double calculate_frame_energy(const int16_t *samples, int N) {
    double energy = 0.0;
    for (int i = 0; i < N; i++) {
        double sample = (double)samples[i];
        energy += sample * sample;
    }
    return energy;
}

// Process frame with twist detection and ITU compliance
static dtmf_twist_result_t process_frame_with_twist(const dtmf_goertzel_twist_t *detector, 
                                                   const int16_t *samples) {
    dtmf_twist_result_t result = {0};
    
    if (!detector || !samples) {
        return result;
    }
    
    // Compute power at all 8 DTMF frequencies
    double powers[8];
    for (int i = 0; i < 8; i++) {
        powers[i] = goertzel_power(samples, detector->N, detector->coeff[i]);
    }
    
    // Find strongest in low group (0..3) and high group (4..7)
    int idx_low = 0, idx_high = 4;
    double power_low = powers[0], power_high = powers[4];
    
    for (int i = 1; i < 4; i++) {
        if (powers[i] > power_low) {
            power_low = powers[i];
            idx_low = i;
        }
    }
    
    for (int i = 5; i < 8; i++) {
        if (powers[i] > power_high) {
            power_high = powers[i];
            idx_high = i;
        }
    }
    
    // Find second strongest in each group (for guard ratio)
    double second_low = -1.0, second_high = -1.0;
    for (int i = 0; i < 4; i++) {
        if (i != idx_low && powers[i] > second_low) {
            second_low = powers[i];
        }
    }
    
    for (int i = 4; i < 8; i++) {
        if (i != idx_high && powers[i] > second_high) {
            second_high = powers[i];
        }
    }
    
    // Detection parameters (ITU-T Q.24 compliant)
    const double ENERGY_MIN_REL = 0.02;    // Minimum (P_L + P_H) / total_energy
    const double GUARD_RATIO = 5.0;        // Minimum ratio between strongest and second strongest
    const double MIN_ABS_POWER = 1e-6;     // Absolute minimum power threshold
    const double TWIST_MIN_DB = -8.0;      // ITU minimum twist limit (reverse twist tolerance)
    const double TWIST_MAX_DB = 4.0;       // ITU maximum twist limit (forward twist tolerance)
    
    // Calculate total frame energy for VAD
    double total_energy = calculate_frame_energy(samples, detector->N);
    if (total_energy <= 0.0) {
        return result;
    }
    
    // Basic power checks
    if (power_low < MIN_ABS_POWER || power_high < MIN_ABS_POWER) {
        return result;
    }
    
    // Energy relative to total frame energy (simple VAD)
    double energy_ratio = (power_low + power_high) / total_energy;
    if (energy_ratio < ENERGY_MIN_REL) {
        return result;
    }
    
    // Guard ratio checks - strongest must be sufficiently above runner-up
    if (second_low > 0.0 && power_low / second_low < GUARD_RATIO) {
        return result;
    }
    
    if (second_high > 0.0 && power_high / second_high < GUARD_RATIO) {
        return result;
    }
    
    // ITU twist check (power domain)
    // Standard DTMF: high frequencies should be ~6dB above low frequencies
    // ITU-T Q.24 tolerates -8dB (reverse twist) to +4dB (forward twist)
    double twist_db = 10.0 * log10(power_high / power_low);
    if (twist_db < TWIST_MIN_DB || twist_db > TWIST_MAX_DB) {
        return result;
    }
    
    // Map indices to DTMF digit
    int row = idx_low;          // 0..3
    int col = idx_high - 4;     // 0..3 (convert high index 4..7 to 0..3)
    
    if (row >= 0 && row < 4 && col >= 0 && col < 4) {
        result.valid = 1;
        result.digit = DTMF_TWIST_TABLE[row][col];
        result.twist_db = twist_db;
        result.power_low = power_low;
        result.power_high = power_high;
        result.energy_ratio = energy_ratio;
    }
    
    return result;
}

// =============================================================================
// FILTER BANK + ENERGY DETECTION
// =============================================================================

typedef struct {
    double *coeffs_b;  // Numerator coefficients
    double *coeffs_a;  // Denominator coefficients  
    double *delay_x;   // Input delay line
    double *delay_y;   // Output delay line
    int order;         // Filter order
} iir_filter_t;

static dtmf_result_t iir_filter_init(iir_filter_t *filter, int order) {
    if (!filter || order <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    filter->order = order;
    filter->coeffs_b = calloc(order + 1, sizeof(double));
    filter->coeffs_a = calloc(order + 1, sizeof(double));
    filter->delay_x = calloc(order + 1, sizeof(double));
    filter->delay_y = calloc(order + 1, sizeof(double));
    
    if (!filter->coeffs_b || !filter->coeffs_a || !filter->delay_x || !filter->delay_y) {
        free(filter->coeffs_b);
        free(filter->coeffs_a);
        free(filter->delay_x);
        free(filter->delay_y);
        return DTMF_ERROR_MEMORY;
    }
    
    return DTMF_SUCCESS;
}

static void iir_filter_cleanup(iir_filter_t *filter) {
    if (filter) {
        free(filter->coeffs_b);
        free(filter->coeffs_a);
        free(filter->delay_x);
        free(filter->delay_y);
        memset(filter, 0, sizeof(iir_filter_t));
    }
}

static dtmf_result_t design_bandpass_filter(iir_filter_t *filter, 
                                           double center_freq, 
                                           double bandwidth,
                                           double sample_rate) {
    if (!filter || center_freq <= 0 || bandwidth <= 0 || sample_rate <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Design 2nd order Butterworth bandpass filter
    dtmf_result_t result = iir_filter_init(filter, 2);
    if (result != DTMF_SUCCESS) {
        return result;
    }
    
    double omega = 2.0 * M_PI * center_freq / sample_rate;
    double bw = 2.0 * M_PI * bandwidth / sample_rate;
    
    double r = 1.0 - 3.0 * bw;
    double cosine = 2.0 * r * cos(omega) / (1.0 + r);
    
    // Coefficients for bandpass filter
    filter->coeffs_b[0] = (1.0 - r) / 2.0;
    filter->coeffs_b[1] = 0.0;
    filter->coeffs_b[2] = -(1.0 - r) / 2.0;
    
    filter->coeffs_a[0] = 1.0;
    filter->coeffs_a[1] = -cosine;
    filter->coeffs_a[2] = r;
    
    return DTMF_SUCCESS;
}

static double iir_filter_process_sample(iir_filter_t *filter, double input) {
    if (!filter) return 0.0;
    
    // Shift delay lines
    for (int i = filter->order; i > 0; i--) {
        filter->delay_x[i] = filter->delay_x[i-1];
        filter->delay_y[i] = filter->delay_y[i-1];
    }
    
    filter->delay_x[0] = input;
    
    // Calculate output
    double output = 0.0;
    
    // Feed-forward (numerator)
    for (int i = 0; i <= filter->order; i++) {
        output += filter->coeffs_b[i] * filter->delay_x[i];
    }
    
    // Feedback (denominator, skip a[0] which is 1.0)
    for (int i = 1; i <= filter->order; i++) {
        output -= filter->coeffs_a[i] * filter->delay_y[i];
    }
    
    filter->delay_y[0] = output;
    return output;
}

static double calculate_rms_energy(const double *samples, size_t count) {
    if (!samples || count == 0) return 0.0;
    
    double sum_squares = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum_squares += samples[i] * samples[i];
    }
    
    return sqrt(sum_squares / count);
}

// =============================================================================
// CORRELATION-BASED DETECTION
// =============================================================================

static double correlate_with_sinusoid(const int16_t *samples, 
                                     size_t sample_count,
                                     double frequency,
                                     double sample_rate) {
    if (!samples || sample_count == 0 || frequency <= 0 || sample_rate <= 0) {
        return 0.0;
    }
    
    double correlation = 0.0;
    double omega = 2.0 * M_PI * frequency / sample_rate;
    
    // Calculate correlation with both sine and cosine components
    double cos_corr = 0.0, sin_corr = 0.0;
    
    for (size_t i = 0; i < sample_count; i++) {
        double phase = omega * i;
        cos_corr += samples[i] * cos(phase);
        sin_corr += samples[i] * sin(phase);
    }
    
    // Normalize by sample count
    cos_corr /= sample_count;
    sin_corr /= sample_count;
    
    // Calculate magnitude of complex correlation
    correlation = sqrt(cos_corr * cos_corr + sin_corr * sin_corr);
    
    return correlation;
}

// =============================================================================
// MUSIC (Multiple Signal Classification) ALGORITHM
// =============================================================================

/*
MUSIC (Multiple Signal Classification) is a subspace-based spectral estimation method 
that's particularly effective for detecting sinusoids in noise. It's considered one of 
the most powerful high-resolution spectral analysis techniques.

How MUSIC Works:
1. Subspace Decomposition:
   - Creates a Hankel matrix from the input signal
   - Computes the autocorrelation matrix
   - Performs eigendecomposition to separate signal and noise subspaces

2. Signal vs Noise Subspace:
   - Signal Subspace: Eigenvectors corresponding to large eigenvalues
   - Noise Subspace: Eigenvectors corresponding to small eigenvalues
   - Assumes 2 dominant sinusoids (low + high DTMF tones)

3. Pseudospectrum Calculation:
   - For each DTMF frequency, creates a "steering vector" (complex exponential)
   - Calculates how orthogonal this vector is to the noise subspace
   - High orthogonality = signal present at that frequency
*/

typedef struct {
    double **data;
    int rows;
    int cols;
} matrix_t;

static dtmf_result_t matrix_create(matrix_t *matrix, int rows, int cols) {
    if (!matrix || rows <= 0 || cols <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    matrix->data = malloc(rows * sizeof(double*));
    if (!matrix->data) {
        return DTMF_ERROR_MEMORY;
    }
    
    for (int i = 0; i < rows; i++) {
        matrix->data[i] = calloc(cols, sizeof(double));
        if (!matrix->data[i]) {
            // Cleanup previously allocated rows
            for (int j = 0; j < i; j++) {
                free(matrix->data[j]);
            }
            free(matrix->data);
            return DTMF_ERROR_MEMORY;
        }
    }
    
    matrix->rows = rows;
    matrix->cols = cols;
    return DTMF_SUCCESS;
}

static void matrix_destroy(matrix_t *matrix) {
    if (matrix && matrix->data) {
        for (int i = 0; i < matrix->rows; i++) {
            free(matrix->data[i]);
        }
        free(matrix->data);
        matrix->data = NULL;
        matrix->rows = 0;
        matrix->cols = 0;
    }
}

static dtmf_result_t matrix_multiply(const matrix_t *a, const matrix_t *b, matrix_t *result) {
    if (!a || !b || !result || a->cols != b->rows) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    dtmf_result_t status = matrix_create(result, a->rows, b->cols);
    if (status != DTMF_SUCCESS) {
        return status;
    }
    
    for (int i = 0; i < a->rows; i++) {
        for (int j = 0; j < b->cols; j++) {
            result->data[i][j] = 0.0;
            for (int k = 0; k < a->cols; k++) {
                result->data[i][j] += a->data[i][k] * b->data[k][j];
            }
        }
    }
    
    return DTMF_SUCCESS;
}

static dtmf_result_t matrix_transpose(const matrix_t *input, matrix_t *result) {
    if (!input || !result) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    dtmf_result_t status = matrix_create(result, input->cols, input->rows);
    if (status != DTMF_SUCCESS) {
        return status;
    }
    
    for (int i = 0; i < input->rows; i++) {
        for (int j = 0; j < input->cols; j++) {
            result->data[j][i] = input->data[i][j];
        }
    }
    
    return DTMF_SUCCESS;
}

// Simple eigenvalue decomposition for symmetric matrices (Jacobi method)
static dtmf_result_t symmetric_eigendecomposition(matrix_t *matrix, 
                                                 double *eigenvalues,
                                                 matrix_t *eigenvectors) {
    if (!matrix || !eigenvalues || !eigenvectors || matrix->rows != matrix->cols) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    int n = matrix->rows;
    const int max_iterations = 100;
    const double tolerance = 1e-10;
    
    // Initialize eigenvectors as identity matrix
    dtmf_result_t status = matrix_create(eigenvectors, n, n);
    if (status != DTMF_SUCCESS) {
        return status;
    }
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            eigenvectors->data[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }
    
    // Copy matrix data for in-place operations
    matrix_t a;
    status = matrix_create(&a, n, n);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(eigenvectors);
        return status;
    }
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            a.data[i][j] = matrix->data[i][j];
        }
    }
    
    // Jacobi iteration
    for (int iter = 0; iter < max_iterations; iter++) {
        // Find largest off-diagonal element
        double max_val = 0.0;
        int p = 0, q = 1;
        
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (fabs(a.data[i][j]) > max_val) {
                    max_val = fabs(a.data[i][j]);
                    p = i;
                    q = j;
                }
            }
        }
        
        if (max_val < tolerance) {
            break; // Converged
        }
        
        // Calculate rotation angle
        double theta;
        if (fabs(a.data[p][p] - a.data[q][q]) < tolerance) {
            theta = M_PI / 4.0;
        } else {
            theta = 0.5 * atan(2.0 * a.data[p][q] / (a.data[p][p] - a.data[q][q]));
        }
        
        double c = cos(theta);
        double s = sin(theta);
        
        // Apply Givens rotation
        for (int i = 0; i < n; i++) {
            if (i != p && i != q) {
                double aip = a.data[i][p];
                double aiq = a.data[i][q];
                a.data[i][p] = c * aip - s * aiq;
                a.data[p][i] = a.data[i][p];
                a.data[i][q] = s * aip + c * aiq;
                a.data[q][i] = a.data[i][q];
            }
        }
        
        double app = a.data[p][p];
        double aqq = a.data[q][q];
        double apq = a.data[p][q];
        
        a.data[p][p] = c * c * app - 2.0 * c * s * apq + s * s * aqq;
        a.data[q][q] = s * s * app + 2.0 * c * s * apq + c * c * aqq;
        a.data[p][q] = 0.0;
        a.data[q][p] = 0.0;
        
        // Update eigenvectors
        for (int i = 0; i < n; i++) {
            double vip = eigenvectors->data[i][p];
            double viq = eigenvectors->data[i][q];
            eigenvectors->data[i][p] = c * vip - s * viq;
            eigenvectors->data[i][q] = s * vip + c * viq;
        }
    }
    
    // Extract eigenvalues from diagonal
    for (int i = 0; i < n; i++) {
        eigenvalues[i] = a.data[i][i];
    }
    
    matrix_destroy(&a);
    return DTMF_SUCCESS;
}

static dtmf_result_t create_hankel_matrix(const int16_t *samples, 
                                        size_t sample_count,
                                        int L,
                                        matrix_t *hankel) {
    if (!samples || sample_count == 0 || L <= 0 || !hankel) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    int M = (int)sample_count - L + 1;
    if (M <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    dtmf_result_t status = matrix_create(hankel, L, M);
    if (status != DTMF_SUCCESS) {
        return status;
    }
    
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < M; j++) {
            hankel->data[i][j] = (double)samples[i + j];
        }
    }
    
    return DTMF_SUCCESS;
}

static double music_pseudospectrum(const matrix_t *noise_subspace,
                                  double frequency,
                                  double sample_rate,
                                  int L) {
    if (!noise_subspace || frequency <= 0 || sample_rate <= 0 || L <= 0) {
        return 0.0;
    }
    
    // Create steering vector for the frequency
    complex_t *steering = malloc(L * sizeof(complex_t));
    if (!steering) {
        return 0.0;
    }
    
    double omega = 2.0 * M_PI * frequency / sample_rate;
    for (int i = 0; i < L; i++) {
        steering[i].re = cos(omega * i);
        steering[i].im = sin(omega * i);
    }
    
    // Calculate a^H * En * En^H * a
    double numerator = 0.0;
    
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < L; j++) {
            // steering[i]* * noise_subspace * noise_subspace^H * steering[j]
            double sum_real = 0.0;
            
            for (int k = 0; k < noise_subspace->cols; k++) {
                // noise_subspace[i][k] * noise_subspace[j][k]*
                sum_real += noise_subspace->data[i][k] * noise_subspace->data[j][k];
            }
            
            // steering[i]* * sum * steering[j]
            numerator += (steering[i].re * sum_real + steering[i].im * 0.0) * steering[j].re +
                        (steering[i].im * sum_real - steering[i].re * 0.0) * steering[j].im;
        }
    }
    
    free(steering);
    
    // MUSIC pseudospectrum is 1 / (denominator)
    return (numerator > 0.0) ? 1.0 / numerator : 0.0;
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
    dtmf_result_t fft_result = fft_radix2(fft_data, ctx->fft_size, false);
    if (fft_result != DTMF_SUCCESS) {
        free(windowed_samples);
        free(fft_data);
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
        low_magnitudes[i] = complex_magnitude(fft_data[bin]);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        int bin = (int)round(config->high_frequencies[i] / freq_resolution);
        bin = (bin < 0) ? 0 : (bin >= half_size) ? half_size - 1 : bin;
        high_magnitudes[i] = complex_magnitude(fft_data[bin]);
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

// Simplified Goertzel with Twist Check (ITU-T Q.24 compliant implementation)
static dtmf_result_t analyze_goertzel_twist_strategy(const audio_context_t *ctx,
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
    
    // Initialize Goertzel twist detector
    dtmf_goertzel_twist_t detector;
    dtmf_result_t init_result = init_goertzel_twist(&detector, ctx->sample_rate, (int)ctx->sample_count);
    if (init_result != DTMF_SUCCESS) {
        return init_result;
    }
    
    // Process frame with twist detection
    dtmf_twist_result_t twist_result = process_frame_with_twist(&detector, ctx->samples);
    
    if (twist_result.valid) {
        // Map the detected digit back to our analysis result structure
        // Find the indices for the detected frequencies
        int low_idx = -1, high_idx = -1;
        
        // Determine which DTMF frequencies correspond to the detected digit
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                if (DTMF_TWIST_TABLE[row][col] == twist_result.digit) {
                    low_idx = row;
                    high_idx = col;
                    break;
                }
            }
            if (low_idx != -1) break;
        }
        
        if (low_idx != -1 && high_idx != -1) {
            // Set up tone detection results
            result->low_tone.index = low_idx;
            result->low_tone.frequency = config->low_frequencies[low_idx];
            result->low_tone.magnitude = sqrt(twist_result.power_low); // Convert power to magnitude
            
            result->high_tone.index = high_idx;
            result->high_tone.frequency = config->high_frequencies[high_idx];
            result->high_tone.magnitude = sqrt(twist_result.power_high);
            
            result->detected_key = twist_result.digit;
            
            // Calculate confidence ratios based on twist constraints and expected behavior
            // Low confidence: based on energy ratio (how strong the signal is relative to total energy)
            result->low_confidence = twist_result.energy_ratio * 100.0; // Scale energy ratio
            
            // High confidence: based on how close the twist is to the expected 6dB
            // Expected twist is 6dB (high frequencies should be 6dB above low frequencies)
            double expected_twist = 6.0;
            double twist_error = fabs(twist_result.twist_db - expected_twist);
            // Scale so that perfect 6dB twist gives confidence ~2.0, acceptable range gives >1.0
            result->high_confidence = 2.0 * exp(-twist_error / 3.0); // Gaussian-like decay from ideal
            
        } else {
            // Fallback if mapping fails
            result->detected_key = '?';
            result->low_confidence = 0.0;
            result->high_confidence = 0.0;
        }
    } else {
        // No valid DTMF detected
        result->detected_key = '?';
        result->low_confidence = 0.0;
        result->high_confidence = 0.0;
        
        // Set default values
        result->low_tone.index = 0;
        result->low_tone.frequency = config->low_frequencies[0];
        result->low_tone.magnitude = 0.0;
        
        result->high_tone.index = 0;
        result->high_tone.frequency = config->high_frequencies[0];
        result->high_tone.magnitude = 0.0;
    }
    
    // Record window name (Goertzel Twist uses raw samples, not windowed)
    result->window_name = "Twist";
    
    // Record timing
    timing_end(&wall_start, &cpu_start, &result->timing);
    
    return DTMF_SUCCESS;
}

// FilterBank Algorithm (IIR bandpass filters + energy detection)
static dtmf_result_t analyze_filterbank_strategy(const audio_context_t *ctx,
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
    
    const double bandwidth = 50.0; // Hz, bandwidth for each bandpass filter
    
    // Create bandpass filters for each DTMF frequency
    iir_filter_t low_filters[config->num_low_freqs];
    iir_filter_t high_filters[config->num_high_freqs];
    
    // Initialize low frequency filters
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        dtmf_result_t filter_result = design_bandpass_filter(&low_filters[i], 
                                                           config->low_frequencies[i], 
                                                           bandwidth, 
                                                           ctx->sample_rate);
        if (filter_result != DTMF_SUCCESS) {
            // Cleanup any initialized filters
            for (size_t j = 0; j < i; j++) {
                iir_filter_cleanup(&low_filters[j]);
            }
            return filter_result;
        }
    }
    
    // Initialize high frequency filters
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        dtmf_result_t filter_result = design_bandpass_filter(&high_filters[i], 
                                                           config->high_frequencies[i], 
                                                           bandwidth, 
                                                           ctx->sample_rate);
        if (filter_result != DTMF_SUCCESS) {
            // Cleanup filters
            for (size_t j = 0; j < config->num_low_freqs; j++) {
                iir_filter_cleanup(&low_filters[j]);
            }
            for (size_t j = 0; j < i; j++) {
                iir_filter_cleanup(&high_filters[j]);
            }
            return filter_result;
        }
    }
    
    // Allocate output buffers for filtered signals
    double **low_filtered = malloc(config->num_low_freqs * sizeof(double*));
    double **high_filtered = malloc(config->num_high_freqs * sizeof(double*));
    
    if (!low_filtered || !high_filtered) {
        free(low_filtered);
        free(high_filtered);
        // Cleanup filters
        for (size_t i = 0; i < config->num_low_freqs; i++) {
            iir_filter_cleanup(&low_filters[i]);
        }
        for (size_t i = 0; i < config->num_high_freqs; i++) {
            iir_filter_cleanup(&high_filters[i]);
        }
        return DTMF_ERROR_MEMORY;
    }
    
    // Allocate memory for filtered signals
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_filtered[i] = malloc(ctx->sample_count * sizeof(double));
        if (!low_filtered[i]) {
            // Cleanup
            for (size_t j = 0; j < i; j++) {
                free(low_filtered[j]);
            }
            free(low_filtered);
            free(high_filtered);
            for (size_t j = 0; j < config->num_low_freqs; j++) {
                iir_filter_cleanup(&low_filters[j]);
            }
            for (size_t j = 0; j < config->num_high_freqs; j++) {
                iir_filter_cleanup(&high_filters[j]);
            }
            return DTMF_ERROR_MEMORY;
        }
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_filtered[i] = malloc(ctx->sample_count * sizeof(double));
        if (!high_filtered[i]) {
            // Cleanup
            for (size_t j = 0; j < config->num_low_freqs; j++) {
                free(low_filtered[j]);
            }
            for (size_t j = 0; j < i; j++) {
                free(high_filtered[j]);
            }
            free(low_filtered);
            free(high_filtered);
            for (size_t j = 0; j < config->num_low_freqs; j++) {
                iir_filter_cleanup(&low_filters[j]);
            }
            for (size_t j = 0; j < config->num_high_freqs; j++) {
                iir_filter_cleanup(&high_filters[j]);
            }
            return DTMF_ERROR_MEMORY;
        }
    }
    
    // Process samples through all filters
    for (size_t sample = 0; sample < ctx->sample_count; sample++) {
        double input = (double)ctx->samples[sample];
        
        // Process through low frequency filters
        for (size_t i = 0; i < config->num_low_freqs; i++) {
            low_filtered[i][sample] = iir_filter_process_sample(&low_filters[i], input);
        }
        
        // Process through high frequency filters
        for (size_t i = 0; i < config->num_high_freqs; i++) {
            high_filtered[i][sample] = iir_filter_process_sample(&high_filters[i], input);
        }
    }
    
    // Calculate RMS energy for each filtered signal
    double low_energies[config->num_low_freqs];
    double high_energies[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_energies, 0, sizeof(low_energies));
    memset(high_energies, 0, sizeof(high_energies));
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_energies[i] = calculate_rms_energy(low_filtered[i], ctx->sample_count);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_energies[i] = calculate_rms_energy(high_filtered[i], ctx->sample_count);
    }
    
    // Find best tones
    result->low_tone = find_maximum_tone(low_energies, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_energies, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence ratios
    result->low_confidence = calculate_confidence_ratio(low_energies, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_energies, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    // Record window name
    result->window_name = "Filter Bank";
    
    // Record timing
    timing_end(&wall_start, &cpu_start, &result->timing);
    
    // Cleanup
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        free(low_filtered[i]);
        iir_filter_cleanup(&low_filters[i]);
    }
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        free(high_filtered[i]);
        iir_filter_cleanup(&high_filters[i]);
    }
    free(low_filtered);
    free(high_filtered);
    
    return DTMF_SUCCESS;
}

// Correlation-based analysis (cross-correlation with reference sinusoids)
static dtmf_result_t analyze_correlation_strategy(const audio_context_t *ctx,
                                                 const dtmf_config_t *config,
                                                 const window_strategy_t *window_strategy,
                                                 dtmf_analysis_result_t *result) {
    if (!ctx || !config || !window_strategy || !result || !ctx->samples) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    struct timespec wall_start, cpu_start;
    dtmf_result_t timing_result = timing_start(&wall_start, &cpu_start);
    if (timing_result != DTMF_SUCCESS) {
        return timing_result;
    }
    
    // Calculate correlation magnitudes for all DTMF frequencies
    double low_correlations[config->num_low_freqs];
    double high_correlations[config->num_high_freqs];
    
    // Initialize arrays to prevent uninitialized usage warnings
    memset(low_correlations, 0, sizeof(low_correlations));
    memset(high_correlations, 0, sizeof(high_correlations));
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_correlations[i] = correlate_with_sinusoid(ctx->samples, ctx->sample_count,
                                                    config->low_frequencies[i], ctx->sample_rate);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_correlations[i] = correlate_with_sinusoid(ctx->samples, ctx->sample_count,
                                                     config->high_frequencies[i], ctx->sample_rate);
    }
    
    // Find best tones
    result->low_tone = find_maximum_tone(low_correlations, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_correlations, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence ratios
    result->low_confidence = calculate_confidence_ratio(low_correlations, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_correlations, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    // Record window name
    result->window_name = "Correlation";
    
    // Record timing
    timing_end(&wall_start, &cpu_start, &result->timing);
    
    return DTMF_SUCCESS;
}

// Simplified MUSIC Algorithm (using eigendecomposition approximation)
static dtmf_result_t analyze_music_strategy(const audio_context_t *ctx,
                                           const dtmf_config_t *config,
                                           const window_strategy_t *window_strategy,
                                           dtmf_analysis_result_t *result) {
    if (!ctx || !config || !result) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    clock_t start_time = clock();
    
    // Initialize result
    result->low_tone.index = 0;
    result->low_tone.frequency = 0.0;
    result->low_tone.magnitude = 0.0;
    result->high_tone.index = 0;
    result->high_tone.frequency = 0.0;
    result->high_tone.magnitude = 0.0;
    result->low_confidence = 0.0;
    result->high_confidence = 0.0;
    result->detected_key = '\0';
    result->window_name = "MUSIC";
    
    // Get the windowed samples
    const int16_t *samples = ctx->samples;
    size_t sample_count = ctx->sample_count;
    
    // Apply windowing
    int16_t *windowed_samples = malloc(sample_count * sizeof(int16_t));
    if (!windowed_samples) {
        return DTMF_ERROR_MEMORY;
    }
    
    for (size_t i = 0; i < sample_count; i++) {
        double window_value = 1.0; // Default (rectangular)
        if (strcmp(window_strategy->name, "Hann") == 0) {
            window_value = 0.5 * (1.0 - cos(2.0 * M_PI * i / (sample_count - 1)));
        } else if (strcmp(window_strategy->name, "Hamming") == 0) {
            window_value = 0.54 - 0.46 * cos(2.0 * M_PI * i / (sample_count - 1));
        } else if (strcmp(window_strategy->name, "Blackman") == 0) {
            window_value = 0.42 - 0.5 * cos(2.0 * M_PI * i / (sample_count - 1)) +
                          0.08 * cos(4.0 * M_PI * i / (sample_count - 1));
        }
        windowed_samples[i] = (int16_t)(samples[i] * window_value);
    }
    
    // MUSIC parameters
    int L = sample_count / 3;  // Subspace dimension (typically N/3)
    if (L < 10) {
        L = 10;  // Minimum subspace size
    }
    
    int noise_dim = L - 2;  // Assume 2 signal components (low + high tone)
    if (noise_dim <= 0) {
        noise_dim = L / 2;
    }
    
    matrix_t hankel, correlation, eigenvectors;
    double *eigenvalues = NULL;
    
    // Create Hankel matrix
    dtmf_result_t status = create_hankel_matrix(windowed_samples, sample_count, L, &hankel);
    if (status != DTMF_SUCCESS) {
        free(windowed_samples);
        return status;
    }
    
    // Compute autocorrelation matrix R = H * H^T
    matrix_t hankel_transpose;
    status = matrix_transpose(&hankel, &hankel_transpose);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(&hankel);
        free(windowed_samples);
        return status;
    }
    
    status = matrix_multiply(&hankel, &hankel_transpose, &correlation);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(&hankel);
        matrix_destroy(&hankel_transpose);
        free(windowed_samples);
        return status;
    }
    
    matrix_destroy(&hankel);
    matrix_destroy(&hankel_transpose);
    
    // Eigendecomposition
    eigenvalues = malloc(L * sizeof(double));
    if (!eigenvalues) {
        matrix_destroy(&correlation);
        free(windowed_samples);
        return DTMF_ERROR_MEMORY;
    }
    
    status = symmetric_eigendecomposition(&correlation, eigenvalues, &eigenvectors);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(&correlation);
        free(eigenvalues);
        free(windowed_samples);
        return status;
    }
    
    matrix_destroy(&correlation);
    
    // Sort eigenvalues and eigenvectors in descending order
    for (int i = 0; i < L - 1; i++) {
        for (int j = i + 1; j < L; j++) {
            if (eigenvalues[i] < eigenvalues[j]) {
                // Swap eigenvalues
                double temp = eigenvalues[i];
                eigenvalues[i] = eigenvalues[j];
                eigenvalues[j] = temp;
                
                // Swap corresponding eigenvectors
                for (int k = 0; k < L; k++) {
                    double temp_vec = eigenvectors.data[k][i];
                    eigenvectors.data[k][i] = eigenvectors.data[k][j];
                    eigenvectors.data[k][j] = temp_vec;
                }
            }
        }
    }
    
    // Extract noise subspace (last 'noise_dim' eigenvectors)
    matrix_t noise_subspace;
    status = matrix_create(&noise_subspace, L, noise_dim);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(&eigenvectors);
        free(eigenvalues);
        free(windowed_samples);
        return status;
    }
    
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < noise_dim; j++) {
            noise_subspace.data[i][j] = eigenvectors.data[i][L - noise_dim + j];
        }
    }
    
    matrix_destroy(&eigenvectors);
    free(eigenvalues);
    
    // Calculate MUSIC pseudospectrum for each DTMF frequency (separate low/high)
    double low_pseudospectra[config->num_low_freqs];
    double high_pseudospectra[config->num_high_freqs];
    
    // Initialize arrays to zero
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_pseudospectra[i] = 0.0;
    }
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_pseudospectra[i] = 0.0;
    }
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_pseudospectra[i] = music_pseudospectrum(&noise_subspace, 
                                                   config->low_frequencies[i],
                                                   (double)ctx->sample_rate, 
                                                   L);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_pseudospectra[i] = music_pseudospectrum(&noise_subspace, 
                                                    config->high_frequencies[i],
                                                    (double)ctx->sample_rate, 
                                                    L);
    }
    
    matrix_destroy(&noise_subspace);
    
    // Find best tones using the existing helper functions
    result->low_tone = find_maximum_tone(low_pseudospectra, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_pseudospectra, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence ratios
    result->low_confidence = calculate_confidence_ratio(low_pseudospectra, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_pseudospectra, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    free(windowed_samples);
    
    clock_t end_time = clock();
    result->timing.wall_time_s = ((double)(end_time - start_time) / CLOCKS_PER_SEC);
    result->timing.cpu_time_s = result->timing.wall_time_s; // Simplified
    
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
