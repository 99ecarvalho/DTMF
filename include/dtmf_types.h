/**
 * @file dtmf_types.h
 * @brief Core type definitions and constants for DTMF detection system
 * 
 * This header contains all fundamental data structures, enumerations, and constants
 * used throughout the DTMF (Dual-Tone Multi-Frequency) detection system. It defines
 * the common interface between all modules and provides standardized error handling.
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
#define M_PI 3.14159265358979323846 ///< Mathematical constant PI
#endif

// =============================================================================
// CONSTANTS
// =============================================================================

/** @brief Audio sampling rate in Hz (8 kHz for telephony standard) */
#define SAMPLE_RATE 8000

/** @brief Analysis frame duration in milliseconds */
#define FRAME_MS 40

/** @brief Number of audio samples per analysis frame (320 at 8kHz) */
#define SAMPLES_PER_FRAME (SAMPLE_RATE * FRAME_MS / 1000)

/** @brief FFT size for frequency domain analysis (power of 2 >= frame size) */
#define FFT_SIZE 512

// =============================================================================
// ENUMERATIONS
// =============================================================================

/**
 * @brief Return codes for DTMF detection functions
 * 
 * Standardized error handling across all DTMF detection modules.
 * Success is indicated by DTMF_SUCCESS (0), all other values indicate errors.
 */
typedef enum {
    DTMF_SUCCESS = 0,              ///< Operation completed successfully
    DTMF_ERROR_MEMORY,             ///< Memory allocation failed
    DTMF_ERROR_FILE_IO,            ///< File input/output error
    DTMF_ERROR_INVALID_PARAM,      ///< Invalid parameter passed to function
    DTMF_ERROR_INSUFFICIENT_DATA,  ///< Not enough data for processing
    DTMF_ERROR_END_OF_STREAM,      ///< End of input stream reached
    DTMF_ERROR_AUDIO_QUALITY,      ///< Audio quality issues detected
    DTMF_ERROR_CONFIG              ///< Configuration file error
} dtmf_result_t;

/**
 * @brief Logging severity levels
 * 
 * Controls verbosity of diagnostic output. Higher values include all lower levels.
 */
typedef enum {
    LOG_ERROR = 0,    ///< Critical errors only
    LOG_WARNING = 1,  ///< Warnings and errors
    LOG_INFO = 2,     ///< General information, warnings, and errors
    LOG_DEBUG = 3     ///< All debug information (most verbose)
} log_level_t;

// =============================================================================
// BASIC STRUCTURES
// =============================================================================

/**
 * @brief Complex number representation
 * 
 * Used for FFT calculations and frequency domain analysis.
 */
typedef struct {
    double re; ///< Real component
    double im; ///< Imaginary component
} complex_t;

/**
 * @brief Performance timing measurements
 * 
 * Tracks wall clock and CPU time for algorithm performance analysis.
 */
typedef struct {
    double wall_time_s; ///< Wall clock time in seconds
    double cpu_time_s;  ///< CPU time in seconds
} timing_info_t;

/**
 * @brief DTMF tone detection result
 * 
 * Contains information about a detected tone frequency and its strength.
 */
typedef struct {
    double magnitude; ///< Signal magnitude/strength at this frequency
    double frequency; ///< Detected frequency in Hz
    int index;        ///< Index in frequency array (for mapping to DTMF key)
} tone_detection_t;

/**
 * @brief Complete DTMF detection result for one analysis frame
 * 
 * Contains the detected low and high frequency tones, confidence metrics,
 * the resulting DTMF key, and performance timing information.
 */
typedef struct {
    tone_detection_t low_tone;   ///< Detected low frequency tone (row)
    tone_detection_t high_tone;  ///< Detected high frequency tone (column)
    char detected_key;           ///< Decoded DTMF key ('0'-'9', '*', '#', 'A'-'D', or '\0')
    double low_confidence;       ///< Confidence ratio for low frequency detection
    double high_confidence;      ///< Confidence ratio for high frequency detection
    timing_info_t timing;        ///< Algorithm execution timing
    const char *window_name;     ///< Name of windowing function used (FFT only)
} dtmf_analysis_result_t;

// =============================================================================
// AUDIO QUALITY STRUCTURES
// =============================================================================

/**
 * @brief Audio quality assessment metrics
 * 
 * Contains various audio quality indicators used to validate input signal
 * and potentially apply preprocessing corrections.
 */
typedef struct {
    double dc_offset;    ///< DC component offset from zero
    double rms_level;    ///< Root Mean Square signal level
    double peak_level;   ///< Maximum absolute sample value
    double snr_estimate; ///< Estimated Signal-to-Noise Ratio in dB
    bool is_clipped;     ///< True if clipping/saturation detected
    bool is_too_quiet;   ///< True if signal level too low for reliable detection
} audio_quality_t;

// =============================================================================
// MEMORY MANAGEMENT
// =============================================================================

/**
 * @brief Memory pool for efficient allocation
 * 
 * Pre-allocated memory pool to reduce malloc/free overhead during real-time processing.
 */
typedef struct {
    void *pool;    ///< Pointer to memory pool
    size_t size;   ///< Total pool size in bytes
    size_t used;   ///< Currently used bytes
    size_t block_count; ///< Number of allocated blocks
} memory_pool_t;

// =============================================================================
// ERROR CONTEXT
// =============================================================================

/**
 * @brief Error context for detailed error reporting
 * 
 * Captures the location and details of errors for debugging purposes.
 */
typedef struct {
    const char *function; ///< Function where error occurred
    const char *file;     ///< Source file where error occurred
    int line;             ///< Line number where error occurred
    dtmf_result_t error_code; ///< Error code
    char message[256];    ///< Additional error message
} error_context_t;

/** @brief Macro to create error context with current location */
#define DTMF_ERROR_CTX(code) \
    ((error_context_t){__func__, __FILE__, __LINE__, (code), ""})

/** @brief Macro to create error context with custom message */
#define DTMF_ERROR_CTX_MSG(code, msg) \
    ((error_context_t){__func__, __FILE__, __LINE__, (code), msg})

// =============================================================================
// CONFIGURATION STRUCTURES
// =============================================================================

/**
 * @brief DTMF frequency configuration
 * 
 * Contains the frequency tables and key mapping used for DTMF detection.
 * Supports both standard telephony and extended DTMF frequencies.
 */
typedef struct {
    const double *low_frequencies;  ///< Array of low (row) frequencies in Hz
    const double *high_frequencies; ///< Array of high (column) frequencies in Hz
    const char (*key_map)[4];       ///< 2D array mapping frequency indices to DTMF keys
    size_t num_low_freqs;           ///< Number of low frequencies
    size_t num_high_freqs;          ///< Number of high frequencies
} dtmf_config_t;

/**
 * @brief Algorithm configuration and thresholds
 * 
 * Contains all configurable parameters for DTMF detection algorithms,
 * including detection thresholds, algorithm selection, and processing options.
 */
typedef struct {
    // Detection thresholds
    double min_confidence_ratio;  ///< Minimum confidence ratio for valid detection
    double energy_threshold;      ///< Minimum energy threshold for signal detection
    double twist_tolerance_db;    ///< Maximum twist (level difference) in dB
    double dc_offset_threshold;   ///< DC offset detection threshold
    double clip_threshold;        ///< Clipping detection threshold (0.0-1.0)
    double min_snr_db;           ///< Minimum SNR in dB for valid signal
    
    // Processing parameters
    size_t frame_size_ms;        ///< Analysis frame size in milliseconds
    size_t sample_rate;          ///< Audio sample rate in Hz
    size_t fft_size;            ///< FFT size for frequency analysis
    
    // Algorithm selection flags
    bool enable_fft;             ///< Enable FFT-based analysis
    bool enable_goertzel;        ///< Enable standard Goertzel algorithm
    bool enable_goertzel_twist;  ///< Enable Goertzel with twist compensation
    bool enable_filterbank;      ///< Enable filter bank approach
    bool enable_filterbank_iir;  ///< Enable IIR filter bank approach
    bool enable_correlation;     ///< Enable correlation-based detection
    bool enable_music;           ///< Enable MUSIC algorithm
    
    // Performance tuning options
    size_t memory_pool_size;     ///< Memory pool size in bytes
    bool use_lookup_tables;      ///< Use precomputed trigonometric tables
    bool enable_audio_validation; ///< Enable input audio quality checks
    bool enable_dc_removal;      ///< Enable DC offset removal
    bool enable_agc;            ///< Enable Automatic Gain Control
    
    // Logging configuration
    log_level_t log_level;       ///< Logging verbosity level
    bool log_to_stderr;          ///< Log to stderr instead of file
    const char *log_file;        ///< Log file path (if not stderr)
} dtmf_config_advanced_t;

// =============================================================================
// AUDIO CONTEXT WITH OPTIMIZATIONS
// =============================================================================

/**
 * @brief Complete audio processing context
 * 
 * Contains all data and resources needed for efficient DTMF detection,
 * including input samples, pre-allocated buffers, lookup tables, and configuration.
 * This structure is designed to minimize memory allocations during processing.
 */
typedef struct {
    // Sample data
    int16_t *samples;            ///< Input audio samples (16-bit PCM)
    size_t sample_count;         ///< Number of samples in current frame
    int sample_rate;             ///< Audio sample rate in Hz
    int window_ms;               ///< Window duration in milliseconds
    int fft_size;               ///< FFT size for frequency analysis
    
    // Pre-allocated buffers for performance
    complex_t *fft_buffer;       ///< Buffer for FFT input/output
    double *windowed_buffer;     ///< Buffer for windowed samples
    double *cos_table;           ///< Precomputed cosine lookup table
    double *sin_table;           ///< Precomputed sine lookup table
    
    // Memory pool
    memory_pool_t *mem_pool;     ///< Memory pool for efficient allocation
    
    // Audio quality info
    audio_quality_t quality;     ///< Audio quality assessment results
    
    // Configuration
    dtmf_config_advanced_t *config; ///< Algorithm configuration
} audio_context_t;

// =============================================================================
// WINDOWING FUNCTION TYPES
// =============================================================================

/**
 * @brief Function pointer type for windowing functions
 * 
 * @param input_samples Input audio samples
 * @param sample_count Number of samples
 * @param windowed_samples_out Output windowed samples (allocated by function)
 * @return DTMF_SUCCESS on success, error code otherwise
 */
typedef dtmf_result_t (*window_func_t)(const int16_t *input_samples,
                                       size_t sample_count,
                                       double **windowed_samples_out);

/**
 * @brief Windowing function strategy
 * 
 * Combines a windowing function with its descriptive name.
 */
typedef struct {
    const char *name;    ///< Human-readable name of the windowing function
    window_func_t apply; ///< Function pointer to windowing implementation
} window_strategy_t;

// =============================================================================
// ANALYSIS FUNCTION TYPES
// =============================================================================

/**
 * @brief Function pointer type for DTMF analysis algorithms
 * 
 * @param ctx Audio context containing samples and configuration
 * @param config DTMF frequency configuration
 * @param window_strategy Windowing strategy (may be NULL for non-FFT algorithms)
 * @param result Output structure for detection results
 * @return DTMF_SUCCESS on success, error code otherwise
 */
typedef dtmf_result_t (*dtmf_analysis_func_t)(
    const audio_context_t *ctx,
    const dtmf_config_t *config,
    const window_strategy_t *window_strategy,
    dtmf_analysis_result_t *result
);

// =============================================================================
// STREAM PROCESSING CONTEXT
// =============================================================================

/**
 * @brief Context for streaming audio processing
 * 
 * Maintains state for continuous DTMF detection from audio streams,
 * including file handles, buffers, and processing statistics.
 */
typedef struct {
    FILE *input;                               ///< Input file handle (or stdin)
    bool is_stdin;                             ///< True if reading from stdin
    bool header_printed;                       ///< True if CSV header was output
    size_t frame_count;                        ///< Number of processed frames
    double start_time;                         ///< Processing start timestamp
    int16_t frame_buffer[SAMPLES_PER_FRAME];   ///< Frame buffer for audio samples
    
    // Enhanced context
    audio_context_t *audio_ctx;                ///< Audio processing context
    dtmf_config_advanced_t *config;            ///< Algorithm configuration
    error_context_t last_error;                ///< Last error that occurred
} stream_context_t;

// =============================================================================
// MATRIX OPERATIONS (for MUSIC algorithm)
// =============================================================================

/**
 * @brief Matrix structure for linear algebra operations
 * 
 * Used primarily by the MUSIC algorithm for covariance matrix operations
 * and eigendecomposition.
 */
typedef struct {
    double **data; ///< 2D array of matrix elements
    int rows;      ///< Number of rows
    int cols;      ///< Number of columns
} matrix_t;

// =============================================================================
// ALGORITHM-SPECIFIC STRUCTURES
// =============================================================================

/**
 * @brief Configuration for Goertzel algorithm with twist detection
 * 
 * The Goertzel algorithm is optimized for detecting specific frequencies.
 * This structure contains precomputed coefficients and parameters for
 * efficient DTMF frequency detection with ITU-T twist tolerance checking.
 */
typedef struct {
    int fs;             ///< Sample rate in Hz
    int N;              ///< Frame length in samples
    int k[8];           ///< Goertzel bin indices for 8 DTMF frequencies
    double coeff[8];    ///< Precomputed coefficients: 2*cos(2*pi*k/N)
    double freqs[8];    ///< DTMF frequency list in Hz
} dtmf_goertzel_twist_t;

/**
 * @brief Result from Goertzel twist detection algorithm
 * 
 * Contains the detected DTMF digit and the measured twist value.
 * Twist is the power ratio between high and low frequency components.
 */
typedef struct {
    int valid;          ///< 1 if valid DTMF digit detected, 0 otherwise
    char digit;         ///< Detected DTMF digit ('0'-'9', '*', '#', 'A'-'D')
    double twist_db;    ///< Twist in dB: 10*log10(P_high/P_low)
    double power_low;   ///< Selected low frequency band power
    double power_high;  ///< Selected high frequency band power
    double energy_ratio; ///< (P_low + P_high) / total_energy ratio
} dtmf_twist_result_t;

/**
 * @brief IIR (Infinite Impulse Response) filter structure
 * 
 * Used by the filter bank algorithm to implement frequency-selective
 * bandpass filters for each DTMF frequency component.
 */
typedef struct {
    double *coeffs_b;  ///< Numerator (feedforward) coefficients
    double *coeffs_a;  ///< Denominator (feedback) coefficients  
    double *delay_x;   ///< Input sample delay line
    double *delay_y;   ///< Output sample delay line
    int order;         ///< Filter order (number of poles/zeros)
} iir_filter_t;

#endif // DTMF_TYPES_H
