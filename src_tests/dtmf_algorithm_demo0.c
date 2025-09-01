/**
 * @file dtmf_algorithm_demo.c  Demonstration of DTMF algorithms
 * 
 * This file shows the theoretical implementation of DTMF generation and detection
 * algorithms similar to what would be used in the libre/rem libraries.
 */

#include <stdio.h>
#include <math.h>
#include <stdint.h>

#define PI 3.14159265358979323846

/**
 * DTMF Frequency Table
 * 
 * DTMF uses two simultaneous tones:
 * - Low frequency group: 697, 770, 852, 941 Hz
 * - High frequency group: 1209, 1336, 1477, 1633 Hz
 * 
 *      1209 Hz  1336 Hz  1477 Hz  1633 Hz
 * 697 Hz   1       2       3        A
 * 770 Hz   4       5       6        B  
 * 852 Hz   7       8       9        C
 * 941 Hz   *       0       #        D
 */

/* DTMF frequency table */
static const int dtmf_low_freq[] = {697, 770, 852, 941};
static const int dtmf_high_freq[] = {1209, 1336, 1477, 1633};

/* DTMF character mapping */
static const char dtmf_chars[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

/**
 * Generate DTMF tone samples for a given character
 * 
 * @param samples   Output buffer for audio samples
 * @param count     Number of samples to generate
 * @param srate     Sample rate (Hz)
 * @param digit     DTMF character to generate
 * @param amplitude Amplitude (0.0 to 1.0)
 * 
 * This is conceptually similar to autone_dtmf() from rem library
 */
void generate_dtmf_tone(int16_t *samples, int count, int srate, char digit, float amplitude)
{
    int low_freq = 0, high_freq = 0;
    int row, col;
    
    /* Find the frequencies for this digit */
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            if (dtmf_chars[row][col] == digit) {
                low_freq = dtmf_low_freq[row];
                high_freq = dtmf_high_freq[col];
                goto found;
            }
        }
    }
    
found:
    if (low_freq == 0 || high_freq == 0) {
        /* Invalid digit - generate silence */
        for (int i = 0; i < count; i++) {
            samples[i] = 0;
        }
        return;
    }
    
    /* Generate dual-tone samples */
    for (int i = 0; i < count; i++) {
        double t = (double)i / srate;
        double low_sine = sin(2.0 * PI * low_freq * t);
        double high_sine = sin(2.0 * PI * high_freq * t);
        
        /* Combine the two tones */
        double combined = (low_sine + high_sine) * amplitude * 0.5;
        
        /* Convert to 16-bit signed integer */
        samples[i] = (int16_t)(combined * 32767.0);
    }
}

/**
 * Simple DTMF detector using Goertzel algorithm
 * 
 * This is a simplified version of what dtmf_dec_probe() would do.
 * Real implementations use more sophisticated filtering and thresholding.
 */
typedef struct {
    double coeff;
    double q1, q2;
} goertzel_state_t;

void goertzel_init(goertzel_state_t *state, int freq, int srate, int N)
{
    double omega = 2.0 * PI * freq / srate;
    state->coeff = 2.0 * cos(omega);
    state->q1 = state->q2 = 0.0;
}

void goertzel_process(goertzel_state_t *state, double sample)
{
    double q0 = state->coeff * state->q1 - state->q2 + sample;
    state->q2 = state->q1;
    state->q1 = q0;
}

double goertzel_magnitude(goertzel_state_t *state)
{
    double magnitude = sqrt(state->q1 * state->q1 + state->q2 * state->q2 - 
                           state->q1 * state->q2 * state->coeff);
    return magnitude;
}

/**
 * Detect DTMF digit in audio samples
 * 
 * @param samples   Input audio samples
 * @param count     Number of samples
 * @param srate     Sample rate
 * @return          Detected DTMF character or 0 if none detected
 */
char detect_dtmf_digit(const int16_t *samples, int count, int srate)
{
    goertzel_state_t low_detectors[4];
    goertzel_state_t high_detectors[4];
    double low_magnitudes[4];
    double high_magnitudes[4];
    
    /* Initialize Goertzel detectors for all DTMF frequencies */
    for (int i = 0; i < 4; i++) {
        goertzel_init(&low_detectors[i], dtmf_low_freq[i], srate, count);
        goertzel_init(&high_detectors[i], dtmf_high_freq[i], srate, count);
    }
    
    /* Process all samples */
    for (int n = 0; n < count; n++) {
        double sample = samples[n] / 32768.0; /* Normalize to [-1, 1] */
        
        for (int i = 0; i < 4; i++) {
            goertzel_process(&low_detectors[i], sample);
            goertzel_process(&high_detectors[i], sample);
        }
    }
    
    /* Calculate magnitudes */
    for (int i = 0; i < 4; i++) {
        low_magnitudes[i] = goertzel_magnitude(&low_detectors[i]);
        high_magnitudes[i] = goertzel_magnitude(&high_detectors[i]);
    }
    
    /* Find the strongest low and high frequency components */
    int best_low = 0, best_high = 0;
    double max_low = low_magnitudes[0];
    double max_high = high_magnitudes[0];
    
    for (int i = 1; i < 4; i++) {
        if (low_magnitudes[i] > max_low) {
            max_low = low_magnitudes[i];
            best_low = i;
        }
        if (high_magnitudes[i] > max_high) {
            max_high = high_magnitudes[i];
            best_high = i;
        }
    }
    
    /* Simple threshold check (in real implementation, this would be more sophisticated) */
    const double threshold = 50.0; /* Adjust based on signal level */
    
    printf("Low freq magnitudes: [%.1f, %.1f, %.1f, %.1f], best=%d (%.1f)\n", 
           low_magnitudes[0], low_magnitudes[1], low_magnitudes[2], low_magnitudes[3], 
           best_low, max_low);
    printf("High freq magnitudes: [%.1f, %.1f, %.1f, %.1f], best=%d (%.1f)\n", 
           high_magnitudes[0], high_magnitudes[1], high_magnitudes[2], high_magnitudes[3], 
           best_high, max_high);
    
    if (max_low > threshold && max_high > threshold) {
        return dtmf_chars[best_low][best_high];
    }
    
    return 0; /* No DTMF detected */
}

int main(void)
{
    const int srate = 8000;
    const int duration_ms = 100;
    const int sample_count = (srate * duration_ms) / 1000;
    
    int16_t samples[sample_count];
    
    printf("DTMF Algorithm Demo\n");
    printf("===================\n\n");
    
    /* Generate and test DTMF tone for digit '5' */
    char test_digit = '5';
    printf("Generating DTMF tone for digit '%c'\n", test_digit);
    
    generate_dtmf_tone(samples, sample_count, srate, test_digit, 0.5);
    
    printf("Generated %d samples at %d Hz\n", sample_count, srate);
    printf("Expected frequencies: 770 Hz (low) + 1336 Hz (high)\n\n");
    
    /* Detect the DTMF digit */
    char detected = detect_dtmf_digit(samples, sample_count, srate);
    
    printf("Detection result: ");
    if (detected) {
        printf("'%c'\n", detected);
        if (detected == test_digit) {
            printf("OK   Detection successful!\n");
        } else {
            printf("FAIL Detection mismatch!\n");
        }
    } else {
        printf("No DTMF detected\n");
    }
    
    return 0;
}
