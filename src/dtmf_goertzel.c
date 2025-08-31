/**
 * @file dtmf_goertzel.c
 * @brief Implementation of Goertzel algorithm for selective DTMF frequency detection
 * 
 * This file contains implementations of the Goertzel algorithm, a computationally
 * efficient method for detecting specific frequencies in a signal. Unlike FFT
 * which computes the entire frequency spectrum, Goertzel computes the DFT for
 * only the frequencies of interest, making it ideal for DTMF detection.
 * 
 * Mathematical foundation:
 * The Goertzel algorithm implements a second-order IIR filter tuned to the
 * target frequency k:
 * 
 * H(z) = 1 / (1 - e^(j*2π*k/N) * z^(-1))
 * 
 * The algorithm uses the recurrence relation:
 * y[n] = x[n] + 2*cos(2π*k/N)*y[n-1] - y[n-2]
 * 
 * After processing all samples, the magnitude is computed as:
 * |X[k]|² = y[N-1]² + y[N-2]² - 2*cos(2π*k/N)*y[N-1]*y[N-2]
 *
 * @author Eduardo Correia <ecorreia@apliant.com.br>
 * @date August 2025
 */

#include "dtmf_algorithms.h"
#include "dtmf_utils.h"

// =============================================================================
// GOERTZEL ALGORITHM CONSTANTS
// =============================================================================

/** @brief DTMF frequency mapping for twist-aware detection */
static const double DTMF_TWIST_FREQS[8] = {697.0, 770.0, 852.0, 941.0, 1209.0, 1336.0, 1477.0, 1633.0};

/** @brief DTMF keypad mapping for twist-aware detection */
static const char DTMF_TWIST_TABLE[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'}, 
    {'7','8','9','C'},
    {'*','0','#','D'}
};

// =============================================================================
// BASIC GOERTZEL IMPLEMENTATION
// =============================================================================

/**
 * @brief Compute magnitude of specific frequency using Goertzel algorithm
 * 
 * Implements the classic Goertzel algorithm to detect the magnitude of a
 * target frequency in a signal. Uses a second-order IIR filter structure
 * that resonates at the target frequency, providing efficient detection
 * without computing the full frequency spectrum.
 * 
 * Algorithm steps:
 * 1. Calculate normalized frequency k and resonant coefficient
 * 2. Apply recursive filter: y[n] = coeff*y[n-1] - y[n-2] + x[n]
 * 3. Compute complex magnitude from final filter states
 * 
 * Computational complexity: O(N) vs O(N log N) for FFT
 * Memory complexity: O(1) vs O(N) for FFT
 * 
 * @param samples Input audio samples (16-bit signed integers)
 * @param sample_count Number of samples to process
 * @param sample_rate Sampling rate in Hz
 * @param target_frequency Frequency to detect in Hz
 * @return Magnitude of the target frequency component, 0.0 on error
 */
double goertzel_magnitude(const int16_t *samples, 
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
// GOERTZEL WITH TWIST DETECTION
// =============================================================================

/**
 * @brief Find nearest DFT bin for Goertzel algorithm
 * 
 * Calculates the nearest DFT bin index for a target frequency to minimize
 * spectral leakage in Goertzel detection. Uses rounding to find the closest
 * integer frequency bin.
 * 
 * @param N Frame length (number of samples)
 * @param fs Sampling rate in Hz
 * @param frequency Target frequency in Hz
 * @return Nearest DFT bin index
 */
static int nearest_goertzel_bin(int N, int fs, double frequency) {
    double k_exact = (double)N * frequency / (double)fs;
    return (int)round(k_exact);
}

/**
 * @brief Initialize Goertzel twist detector with DTMF frequencies
 * 
 * Sets up a Goertzel detector optimized for DTMF detection with twist
 * measurement capabilities. Pre-computes coefficients for all 8 DTMF
 * frequencies to enable efficient batch processing and twist analysis.
 * 
 * The twist detector enables measurement of amplitude differences between
 * row and column frequencies, which is important for telephony standards
 * compliance and noise robustness.
 * 
 * @param detector Twist detector structure to initialize
 * @param sample_rate Audio sampling rate in Hz
 * @param frame_length Number of samples per analysis frame
 * @return DTMF_SUCCESS on success, DTMF_ERROR_INVALID_PARAM on invalid input
 */
dtmf_result_t init_goertzel_twist(dtmf_goertzel_twist_t *detector, 
                                 int sample_rate, int frame_length) {
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

/**
 * @brief Compute power using Goertzel algorithm
 * 
 * Calculates the power (magnitude squared) at a specific frequency using
 * the Goertzel algorithm. Returns power instead of magnitude to avoid
 * expensive square root operation when only relative comparisons are needed.
 * 
 * @param samples Input audio samples
 * @param N Number of samples
 * @param coeff Pre-computed Goertzel coefficient (2*cos(2π*k/N))
 * @return Power (magnitude squared) at the target frequency
 */
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

/**
 * @brief Calculate total frame energy for normalization
 * 
 * Computes the total energy (sum of squares) of the input frame for
 * signal normalization and threshold comparison. Used to determine
 * if the signal level is sufficient for reliable DTMF detection.
 * 
 * @param samples Input audio samples
 * @param N Number of samples
 * @return Total energy of the frame
 */
static double calculate_frame_energy(const int16_t *samples, int N) {
    double energy = 0.0;
    for (int i = 0; i < N; i++) {
        double sample = (double)samples[i];
        energy += sample * sample;
    }
    return energy;
}

/**
 * @brief Process audio frame with twist analysis using Goertzel algorithm
 * 
 * Performs comprehensive DTMF detection using Goertzel algorithm with twist
 * measurement. Computes power for all 8 DTMF frequencies, identifies the
 * strongest row and column frequencies, measures twist (amplitude ratio),
 * and validates detection criteria.
 * 
 * Twist measurement is important for telephony compliance:
 * - Normal twist: column > row (typical in phone systems)
 * - Reverse twist: row > column (less common)
 * - Standard allows up to 8 dB twist in either direction
 * 
 * @param detector Initialized Goertzel twist detector
 * @param samples Input audio samples for one frame
 * @return Structure containing detected key, twist measurements, and powers
 */
dtmf_twist_result_t process_frame_with_twist(const dtmf_goertzel_twist_t *detector, 
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
// GOERTZEL ANALYSIS STRATEGIES
// =============================================================================

dtmf_result_t analyze_goertzel_strategy(const audio_context_t *ctx,
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

dtmf_result_t analyze_goertzel_twist_strategy(const audio_context_t *ctx,
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
            
            // Calculate confidence ratios based on signal strength (energy ratio)
            // Both confidence values now represent signal strength aspects, scaled like other algorithms
            
            // Use power ratios directly like other algorithms do
            // Convert power to magnitude and scale appropriately
            double low_mag = sqrt(twist_result.power_low);
            double high_mag = sqrt(twist_result.power_high);
            
            // Scale to be similar to other algorithms (typically 1-20 range)
            // Use a more dynamic scaling based on actual signal levels
            result->low_confidence = fmax(1.0, low_mag / 10000.0);
            result->high_confidence = fmax(1.0, high_mag / 10000.0);
            
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
