/*
 * dtmf_types.h
 *
 * Type definitions and constants for DTMF detection system
 */

#ifndef DTMF_TYPES_H
#define DTMF_TYPES_H

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
#include <stdarg.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// CONSTANTS
// =============================================================================

#define SAMPLE_RATE 8000
#define FRAME_MS 40
#define SAMPLES_PER_FRAME (SAMPLE_RATE * FRAME_MS / 1000)  // 320 samples
#define FFT_SIZE 512

// =============================================================================
// ENUMERATIONS
// =============================================================================

typedef enum {
    DTMF_SUCCESS = 0,
    DTMF_ERROR_MEMORY,
    DTMF_ERROR_FILE_IO,
    DTMF_ERROR_INVALID_PARAM,
    DTMF_ERROR_INSUFFICIENT_DATA,
    DTMF_ERROR_END_OF_STREAM,
    DTMF_ERROR_AUDIO_QUALITY,
    DTMF_ERROR_CONFIG
} dtmf_result_t;

typedef enum {
    LOG_ERROR = 0,
    LOG_WARNING = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3
} log_level_t;

// =============================================================================
// BASIC STRUCTURES
// =============================================================================

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

// =============================================================================
// AUDIO QUALITY STRUCTURES
// =============================================================================

typedef struct {
    double dc_offset;
    double rms_level;
    double peak_level;
    double snr_estimate;
    bool is_clipped;
    bool is_too_quiet;
} audio_quality_t;

// =============================================================================
// MEMORY MANAGEMENT
// =============================================================================

typedef struct {
    void *pool;
    size_t size;
    size_t used;
    size_t block_count;
} memory_pool_t;

// =============================================================================
// ERROR CONTEXT
// =============================================================================

typedef struct {
    const char *function;
    const char *file;
    int line;
    dtmf_result_t error_code;
    char message[256];
} error_context_t;

#define DTMF_ERROR_CTX(code) \
    ((error_context_t){__func__, __FILE__, __LINE__, (code), ""})

#define DTMF_ERROR_CTX_MSG(code, msg) \
    ((error_context_t){__func__, __FILE__, __LINE__, (code), msg})

// =============================================================================
// CONFIGURATION STRUCTURES
// =============================================================================

typedef struct {
    const double *low_frequencies;
    const double *high_frequencies;
    const char (*key_map)[4];
    size_t num_low_freqs;
    size_t num_high_freqs;
} dtmf_config_t;

typedef struct {
    // Detection thresholds
    double min_confidence_ratio;
    double energy_threshold;
    double twist_tolerance_db;
    double dc_offset_threshold;
    double clip_threshold;
    double min_snr_db;
    
    // Processing parameters
    size_t frame_size_ms;
    size_t sample_rate;
    size_t fft_size;
    
    // Algorithm selection
    bool enable_fft;
    bool enable_goertzel;
    bool enable_goertzel_twist;
    bool enable_filterbank;
    bool enable_correlation;
    bool enable_music;
    
    // Performance tuning
    size_t memory_pool_size;
    bool use_lookup_tables;
    bool enable_audio_validation;
    bool enable_dc_removal;
    bool enable_agc;
    
    // Logging
    log_level_t log_level;
    bool log_to_stderr;
    const char *log_file;
} dtmf_config_advanced_t;

// =============================================================================
// AUDIO CONTEXT WITH OPTIMIZATIONS
// =============================================================================

typedef struct {
    // Sample data
    int16_t *samples;
    size_t sample_count;
    int sample_rate;
    int window_ms;
    int fft_size;
    
    // Pre-allocated buffers for performance
    complex_t *fft_buffer;
    double *windowed_buffer;
    double *cos_table;
    double *sin_table;
    
    // Memory pool
    memory_pool_t *mem_pool;
    
    // Audio quality info
    audio_quality_t quality;
    
    // Configuration
    dtmf_config_advanced_t *config;
} audio_context_t;

// =============================================================================
// WINDOWING FUNCTION TYPES
// =============================================================================

typedef dtmf_result_t (*window_func_t)(const int16_t *input_samples,
                                       size_t sample_count,
                                       double **windowed_samples_out);

typedef struct {
    const char *name;
    window_func_t apply;
} window_strategy_t;

// =============================================================================
// ANALYSIS FUNCTION TYPES
// =============================================================================

typedef dtmf_result_t (*dtmf_analysis_func_t)(
    const audio_context_t *ctx,
    const dtmf_config_t *config,
    const window_strategy_t *window_strategy,
    dtmf_analysis_result_t *result
);

// =============================================================================
// STREAM PROCESSING CONTEXT
// =============================================================================

typedef struct {
    FILE *input;
    bool is_stdin;
    bool header_printed;
    size_t frame_count;
    double start_time;
    int16_t frame_buffer[SAMPLES_PER_FRAME];
    
    // Enhanced context
    audio_context_t *audio_ctx;
    dtmf_config_advanced_t *config;
    error_context_t last_error;
} stream_context_t;

// =============================================================================
// MATRIX OPERATIONS (for MUSIC algorithm)
// =============================================================================

typedef struct {
    double **data;
    int rows;
    int cols;
} matrix_t;

// =============================================================================
// ALGORITHM-SPECIFIC STRUCTURES
// =============================================================================

// Goertzel with twist detection
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

// IIR Filter for filter bank
typedef struct {
    double *coeffs_b;  // Numerator coefficients
    double *coeffs_a;  // Denominator coefficients  
    double *delay_x;   // Input delay line
    double *delay_y;   // Output delay line
    int order;         // Filter order
} iir_filter_t;

#endif // DTMF_TYPES_H
