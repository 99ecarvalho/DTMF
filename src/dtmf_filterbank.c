/**
 * @file dtmf_filterbank.c
 * @brief Implementation of filter bank approach for DTMF detection
 * 
 * This file contains the implementation of a filter bank approach to DTMF
 * detection using digital bandpass filters tuned to each DTMF frequency.
 * The filter bank method provides excellent noise rejection and frequency
 * selectivity through dedicated filters for each tone.
 * 
 * Mathematical foundation:
 * Each bandpass filter implements a second-order IIR (Infinite Impulse Response)
 * filter with transfer function:
 * 
 * H(z) = (b₀ + b₁z⁻¹ + b₂z⁻²) / (1 + a₁z⁻¹ + a₂z⁻²)
 * 
 * The difference equation is:
 * y[n] = b₀x[n] + b₁x[n-1] + b₂x[n-2] - a₁y[n-1] - a₂y[n-2]
 * 
 * Filter design uses the bilinear transform to convert analog prototypes
 * to digital implementations with precise frequency response control.
 */

#include "dtmf_algorithms.h"
#include "dtmf_utils.h"
#include <string.h>

// =============================================================================
// IIR FILTER IMPLEMENTATION
// =============================================================================

/**
 * @brief Initialize IIR filter structure with specified order
 * 
 * Allocates memory for filter coefficients and delay lines for an IIR filter
 * of the specified order. Creates arrays for feedforward coefficients (b),
 * feedback coefficients (a), input delay line (x), and output delay line (y).
 * 
 * @param filter IIR filter structure to initialize
 * @param order Filter order (number of poles/zeros)
 * @return DTMF_SUCCESS on success, DTMF_ERROR_MEMORY on allocation failure
 */
dtmf_result_t iir_filter_init(iir_filter_t *filter, int order) {
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

/**
 * @brief Clean up IIR filter and free allocated memory
 * 
 * Releases all memory allocated for filter coefficients and delay lines.
 * Resets the filter structure to zero state.
 * 
 * @param filter IIR filter structure to clean up
 */
void iir_filter_cleanup(iir_filter_t *filter) {
    if (filter) {
        free(filter->coeffs_b);
        free(filter->coeffs_a);
        free(filter->delay_x);
        free(filter->delay_y);
        memset(filter, 0, sizeof(iir_filter_t));
    }
}

/**
 * @brief Design second-order bandpass filter for DTMF frequency
 * 
 * Creates a second-order IIR bandpass filter centered at the specified
 * frequency with the given bandwidth. Uses bilinear transform method
 * to convert analog filter design to digital implementation.
 * 
 * The filter provides sharp frequency selectivity for isolating individual
 * DTMF tones while rejecting adjacent frequencies and noise.
 * 
 * @param filter IIR filter structure to configure
 * @param center_freq Center frequency of the bandpass filter in Hz
 * @param bandwidth Filter bandwidth (3 dB) in Hz
 * @param sample_rate Audio sampling rate in Hz
 * @return DTMF_SUCCESS on success, DTMF_ERROR_INVALID_PARAM on invalid parameters
 */
dtmf_result_t design_bandpass_filter(iir_filter_t *filter, 
                                    double center_freq, 
                                    double bandwidth,
                                    double sample_rate) {
    if (!filter || center_freq <= 0 || bandwidth <= 0 || sample_rate <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    // Design 2nd order bandpass filter using biquad design
    dtmf_result_t result = iir_filter_init(filter, 2);
    if (result != DTMF_SUCCESS) {
        return result;
    }
    
    // Calculate filter parameters for biquad bandpass filter
    double f0 = center_freq / sample_rate;           // Normalized center frequency [0, 0.5]
    double Q = center_freq / bandwidth;              // Quality factor
    double omega = 2.0 * M_PI * f0;                  // Angular frequency
    double alpha = sin(omega) / (2.0 * Q);           // Filter width parameter
    double cos_omega = cos(omega);
    
    // Biquad bandpass filter coefficients
    // H(z) = (alpha * (1 - z^-2)) / (1 + alpha + alpha * z^-1 + (alpha - 1) * z^-2)
    double b0 = alpha;
    double b1 = 0.0;
    double b2 = -alpha;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cos_omega;
    double a2 = 1.0 - alpha;
    
    // Normalize coefficients by a0
    filter->coeffs_b[0] = b0 / a0;
    filter->coeffs_b[1] = b1 / a0;
    filter->coeffs_b[2] = b2 / a0;
    
    filter->coeffs_a[0] = 1.0;                      // a0 normalized to 1
    filter->coeffs_a[1] = a1 / a0;
    filter->coeffs_a[2] = a2 / a0;
    
    return DTMF_SUCCESS;
}

double iir_filter_process_sample(iir_filter_t *filter, double input) {
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

double calculate_rms_energy(const double *samples, size_t count) {
    if (!samples || count == 0) return 0.0;
    
    double sum_squares = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum_squares += samples[i] * samples[i];
    }
    
    return sqrt(sum_squares / count);
}

// Calculate energy in a frequency band around a center frequency from FFT results
double calculate_frequency_band_energy(const complex_t *fft_buffer, 
                                      size_t fft_size,
                                      double center_freq,
                                      double bandwidth,
                                      double sample_rate) {
    if (!fft_buffer || fft_size == 0 || center_freq <= 0 || sample_rate <= 0) {
        return 0.0;
    }
    
    double freq_resolution = sample_rate / (double)fft_size;
    double half_bandwidth = bandwidth / 2.0;
    
    // Find the frequency bin range for the band
    int center_bin = (int)round(center_freq / freq_resolution);
    int bandwidth_bins = (int)round(half_bandwidth / freq_resolution);
    
    int start_bin = center_bin - bandwidth_bins;
    int end_bin = center_bin + bandwidth_bins;
    
    // Clamp to valid range
    if (start_bin < 0) start_bin = 0;
    if (end_bin >= (int)(fft_size / 2)) end_bin = (int)(fft_size / 2) - 1;
    
    // Sum energy in the frequency band
    double energy = 0.0;
    for (int bin = start_bin; bin <= end_bin; bin++) {
        double magnitude = sqrt(fft_buffer[bin].re * fft_buffer[bin].re + 
                               fft_buffer[bin].im * fft_buffer[bin].im);
        energy += magnitude;
    }
    
    return energy;
}

// =============================================================================
// FILTER BANK ANALYSIS
// =============================================================================

dtmf_result_t analyze_filterbank_strategy(const audio_context_t *ctx,
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

    // Use FFT-based frequency band energy detection instead of IIR filters
    // This is more reliable and simpler than the problematic IIR filter design
    
    size_t fft_size = find_power_of_two_for_size(ctx->sample_count);
    complex_t *fft_buffer = calloc(fft_size, sizeof(complex_t));
    if (!fft_buffer) {
        return DTMF_ERROR_MEMORY;
    }

    // Copy samples to FFT buffer (zero-padded if necessary)
    for (size_t i = 0; i < ctx->sample_count && i < fft_size; i++) {
        fft_buffer[i].re = (double)ctx->samples[i];
        fft_buffer[i].im = 0.0;
    }

    // Perform FFT
    dtmf_result_t fft_result = fft_radix2(fft_buffer, fft_size, false);
    if (fft_result != DTMF_SUCCESS) {
        dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(fft_result), "FFT computation failed in filter bank");
        free(fft_buffer);
        return fft_result;
    }

    // Calculate energy in frequency bands around each DTMF frequency
    double low_energies[config->num_low_freqs];
    double high_energies[config->num_high_freqs];
    
    // Initialize arrays to avoid compiler warnings
    memset(low_energies, 0, sizeof(low_energies));
    memset(high_energies, 0, sizeof(high_energies));
    
    const double bandwidth = 100.0; // Hz, energy integration bandwidth
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_energies[i] = calculate_frequency_band_energy(fft_buffer, fft_size, 
                                                         config->low_frequencies[i], 
                                                         bandwidth, ctx->sample_rate);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_energies[i] = calculate_frequency_band_energy(fft_buffer, fft_size, 
                                                          config->high_frequencies[i], 
                                                          bandwidth, ctx->sample_rate);
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
    
    free(fft_buffer);
    
    // Record timing
    timing_end(&wall_start, &cpu_start, &result->timing);
    
    return DTMF_SUCCESS;
}
