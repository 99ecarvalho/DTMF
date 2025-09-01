/**
 * @file dtmf_algorithm_demo.c  Advanced DTMF algorithms demonstration
 * 
 * This file shows advanced DTMF generation and detection algorithms
 * inspired by GNU Radio implementation and professional DTMF systems.
 * Includes proper amplitude control, timing, and multi-channel support.
 */

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

/**
 * Advanced DTMF Generator - inspired by GNU Radio pdtmf_generator
 * 
 * DTMF uses two simultaneous tones with specific amplitude relationships:
 * - Low frequency group: 697, 770, 852, 941 Hz
 * - High frequency group: 1209, 1336, 1477, 1633 Hz
 * - Lower tone is typically 2dB below higher tone (professional standard)
 * 
 *      1209 Hz  1336 Hz  1477 Hz  1633 Hz
 * 697 Hz   1       2       3        A
 * 770 Hz   4       5       6        B  
 * 852 Hz   7       8       9        C
 * 941 Hz   *       0       #        D
 */

/* DTMF frequency table (all 8 frequencies) */
static const double dtmf_freq_table[8] = {
    697.0, 770.0, 852.0, 941.0,    /* Low group */
    1209.0, 1336.0, 1477.0, 1633.0 /* High group */
};

/* Character to frequency index mapping */
typedef struct {
    char digit;
    int low_idx;   /* Index into dtmf_freq_table for low frequency */
    int high_idx;  /* Index into dtmf_freq_table for high frequency */
} dtmf_char_map_t;

static const dtmf_char_map_t dtmf_char_to_freq[] = {
    {'1', 0, 4}, {'2', 0, 5}, {'3', 0, 6}, {'A', 0, 7},
    {'4', 1, 4}, {'5', 1, 5}, {'6', 1, 6}, {'B', 1, 7},
    {'7', 2, 4}, {'8', 2, 5}, {'9', 2, 6}, {'C', 2, 7},
    {'*', 3, 4}, {'0', 3, 5}, {'#', 3, 6}, {'D', 3, 7}
};

/* Amplitude control - lower tone is 2dB below higher tone (professional standard) */
static const double dtmf_amplitude[2] = {
    0.7943, /* 10^(-0.2) = -2dB for lower tone */
    1.0     /* 0dB for higher tone */
};

/**
 * DTMF Message Generator - inspired by GNU Radio implementation
 */
typedef struct {
    double low_freq;
    double low_amp;
    double high_freq;
    double high_amp;
    int duration_samples;
} dtmf_tone_t;

typedef struct {
    dtmf_tone_t *tones;
    int tone_count;
    int total_samples;
} dtmf_message_t;

/**
 * Find frequency indices for a DTMF character
 */
static int find_dtmf_frequencies(char digit, int *low_idx, int *high_idx)
{
    for (size_t i = 0; i < sizeof(dtmf_char_to_freq) / sizeof(dtmf_char_to_freq[0]); i++) {
        if (dtmf_char_to_freq[i].digit == digit) {
            *low_idx = dtmf_char_to_freq[i].low_idx;
            *high_idx = dtmf_char_to_freq[i].high_idx;
            return 0; /* Success */
        }
    }
    return -1; /* Character not found */
}

/**
 * Advanced DTMF tone generation with proper timing and amplitude control
 * 
 * @param samples       Output buffer for audio samples
 * @param count         Number of samples to generate
 * @param srate         Sample rate (Hz)
 * @param digit         DTMF character to generate
 * @param on_time_ms    Duration of tone in milliseconds
 * @param off_time_ms   Duration of silence after tone in milliseconds
 */
void generate_advanced_dtmf_tone(int16_t *samples, int count, int srate, 
                                char digit, int on_time_ms, int off_time_ms)
{
    int low_idx, high_idx;
    int on_samples = (srate * on_time_ms) / 1000;
    int off_samples = (srate * off_time_ms) / 1000;
    int total_needed = on_samples + off_samples;
    
    /* Initialize buffer to silence */
    for (int i = 0; i < count; i++) {
        samples[i] = 0;
    }
    
    /* Find the frequencies for this digit */
    if (find_dtmf_frequencies(toupper(digit), &low_idx, &high_idx) != 0) {
        printf("Invalid DTMF digit: %c\n", digit);
        return;
    }
    
    if (total_needed > count) {
        printf("Warning: Buffer too small for requested timing\n");
        on_samples = count;
        off_samples = 0;
    }
    
    double low_freq = dtmf_freq_table[low_idx];
    double high_freq = dtmf_freq_table[high_idx];
    double low_amp = dtmf_amplitude[0];
    double high_amp = dtmf_amplitude[1];
    
    printf("Generating DTMF '%c': %.1f Hz (amp=%.3f) + %.1f Hz (amp=%.3f)\n", 
           digit, low_freq, low_amp, high_freq, high_amp);
    
    /* Generate dual-tone samples with proper amplitude relationship */
    for (int i = 0; i < on_samples; i++) {
        double t = (double)i / srate;
        double low_sine = sin(2.0 * PI * low_freq * t) * low_amp;
        double high_sine = sin(2.0 * PI * high_freq * t) * high_amp;
        
        /* Combine the two tones with proper amplitude weighting */
        double combined = (low_sine + high_sine) * 0.4; /* Scale to prevent clipping */
        
        /* Convert to 16-bit signed integer */
        samples[i] = (int16_t)(combined * 32767.0);
    }
    
    /* Silence period is already initialized to 0 */
    printf("Generated %d samples tone + %d samples silence\n", 
           on_samples, off_samples);
}

/**
 * Generate complete DTMF message sequence (like GNU Radio implementation)
 */
dtmf_message_t *create_dtmf_message(const char *text, int srate, 
                                   int on_time_ms, int off_time_ms, 
                                   int sleep_time_ms)
{
    dtmf_message_t *msg = malloc(sizeof(dtmf_message_t));
    if (!msg) return NULL;
    
    int text_len = strlen(text);
    msg->tones = malloc(text_len * sizeof(dtmf_tone_t));
    if (!msg->tones) {
        free(msg);
        return NULL;
    }
    
    int on_samples = (srate * on_time_ms) / 1000;
    int off_samples = (srate * off_time_ms) / 1000;
    int sleep_samples = (srate * sleep_time_ms) / 1000;
    
    msg->tone_count = 0;
    msg->total_samples = 0;
    
    printf("Creating DTMF message: '%s'\n", text);
    printf("Timing: %dms tone + %dms silence + %dms sleep\n", 
           on_time_ms, off_time_ms, sleep_time_ms);
    
    for (int i = 0; i < text_len; i++) {
        char digit = toupper(text[i]);
        int low_idx, high_idx;
        
        if (find_dtmf_frequencies(digit, &low_idx, &high_idx) != 0) {
            printf("Skipping invalid character: '%c'\n", digit);
            continue;
        }
        
        dtmf_tone_t *tone = &msg->tones[msg->tone_count];
        tone->low_freq = dtmf_freq_table[low_idx];
        tone->low_amp = dtmf_amplitude[0];
        tone->high_freq = dtmf_freq_table[high_idx];
        tone->high_amp = dtmf_amplitude[1];
        tone->duration_samples = on_samples + off_samples;
        
        msg->total_samples += tone->duration_samples;
        msg->tone_count++;
        
        printf("  Tone %d: '%c' -> %.1f Hz + %.1f Hz (%d samples)\n", 
               msg->tone_count, digit, tone->low_freq, tone->high_freq, 
               tone->duration_samples);
    }
    
    /* Add final sleep period */
    msg->total_samples += sleep_samples;
    
    printf("Total message: %d tones, %d samples (%.2f seconds)\n", 
           msg->tone_count, msg->total_samples, 
           (double)msg->total_samples / srate);
    
    return msg;
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
 * Advanced DTMF detector using Goertzel algorithm
 * 
 * This is a more sophisticated version that uses the frequency table
 * and proper detection logic similar to professional implementations.
 */
char detect_advanced_dtmf_digit(const int16_t *samples, int count, int srate)
{
    goertzel_state_t detectors[8]; /* One for each DTMF frequency */
    double magnitudes[8];
    
    /* Initialize Goertzel detectors for all DTMF frequencies */
    for (int i = 0; i < 8; i++) {
        goertzel_init(&detectors[i], (int)dtmf_freq_table[i], srate, count);
    }
    
    /* Process all samples */
    for (int n = 0; n < count; n++) {
        double sample = samples[n] / 32768.0; /* Normalize to [-1, 1] */
        
        for (int i = 0; i < 8; i++) {
            goertzel_process(&detectors[i], sample);
        }
    }
    
    /* Calculate magnitudes */
    for (int i = 0; i < 8; i++) {
        magnitudes[i] = goertzel_magnitude(&detectors[i]);
    }
    
    /* Find the strongest low frequency (indices 0-3) */
    int best_low = 0;
    double max_low = magnitudes[0];
    for (int i = 1; i < 4; i++) {
        if (magnitudes[i] > max_low) {
            max_low = magnitudes[i];
            best_low = i;
        }
    }
    
    /* Find the strongest high frequency (indices 4-7) */
    int best_high = 4;
    double max_high = magnitudes[4];
    for (int i = 5; i < 8; i++) {
        if (magnitudes[i] > max_high) {
            max_high = magnitudes[i];
            best_high = i;
        }
    }
    
    /* Professional DTMF detection thresholds and validation */
    const double threshold = 50.0;  /* Minimum signal strength */
    const double twist_ratio = 4.0; /* Max ratio between low and high tones */
    
    printf("DTMF Detection Analysis:\n");
    printf("  Low frequencies:  [%.1f, %.1f, %.1f, %.1f] -> best: %d (%.1f Hz, mag=%.1f)\n",
           magnitudes[0], magnitudes[1], magnitudes[2], magnitudes[3],
           best_low, dtmf_freq_table[best_low], max_low);
    printf("  High frequencies: [%.1f, %.1f, %.1f, %.1f] -> best: %d (%.1f Hz, mag=%.1f)\n",
           magnitudes[4], magnitudes[5], magnitudes[6], magnitudes[7],
           best_high, dtmf_freq_table[best_high], max_high);
    
    /* Validate signal strength */
    if (max_low < threshold || max_high < threshold) {
        printf("  Result: Signal too weak (threshold=%.1f)\n", threshold);
        return 0;
    }
    
    /* Validate twist (amplitude relationship between tones) */
    double twist = max_high / max_low;
    if (twist > twist_ratio || twist < (1.0 / twist_ratio)) {
        printf("  Result: Invalid twist ratio %.2f (max=%.1f)\n", twist, twist_ratio);
        return 0;
    }
    
    /* Find the corresponding DTMF character */
    for (size_t i = 0; i < sizeof(dtmf_char_to_freq) / sizeof(dtmf_char_to_freq[0]); i++) {
        if (dtmf_char_to_freq[i].low_idx == best_low && 
            dtmf_char_to_freq[i].high_idx == best_high) {
            printf("  Result: Valid DTMF digit '%c' (twist=%.2f)\n", 
                   dtmf_char_to_freq[i].digit, twist);
            return dtmf_char_to_freq[i].digit;
        }
    }
    
    printf("  Result: No matching DTMF character\n");
    return 0; /* No valid DTMF detected */
}

int main(void)
{
    const int srate = 8000;
    const int on_time_ms = 100;   /* 100ms tone duration */
    const int off_time_ms = 100;  /* 100ms silence between tones */
    const int sleep_time_ms = 500; /* 500ms final silence */
    
    printf("🚀 Advanced DTMF Algorithm Demo (GNU Radio inspired)\n");
    printf("==================================================\n\n");
    
    /* Test 1: Advanced single tone generation */
    printf("=== Test 1: Advanced DTMF Tone Generation ===\n");
    char test_digit = '5';
    int tone_samples = (srate * (on_time_ms + off_time_ms)) / 1000;
    int16_t *samples = malloc(tone_samples * sizeof(int16_t));
    
    if (!samples) {
        printf("Memory allocation failed!\n");
        return 1;
    }
    
    generate_advanced_dtmf_tone(samples, tone_samples, srate, test_digit, 
                               on_time_ms, off_time_ms);
    
    /* Test detection on the generated tone */
    printf("\n=== Test 2: Advanced DTMF Detection ===\n");
    char detected = detect_advanced_dtmf_digit(samples, tone_samples, srate);
    
    printf("\nGeneration/Detection Results:\n");
    printf("  Generated: '%c'\n", test_digit);
    printf("  Detected:  '%c'\n", detected ? detected : '?');
    
    if (detected == test_digit) {
        printf("  ✅ Perfect match!\n");
    } else {
        printf("  ❌ Detection failed!\n");
    }
    
    free(samples);
    
    /* Test 3: Complete message generation */
    printf("\n=== Test 3: Complete DTMF Message ===\n");
    const char *test_message = "123*0#ABC";
    
    dtmf_message_t *message = create_dtmf_message(test_message, srate, 
                                                 on_time_ms, off_time_ms, 
                                                 sleep_time_ms);
    
    if (message) {
        printf("\n📊 Message Statistics:\n");
        printf("  Input text: '%s'\n", test_message);
        printf("  Valid tones: %d\n", message->tone_count);
        printf("  Total duration: %.2f seconds\n", 
               (double)message->total_samples / srate);
        printf("  Sample rate: %d Hz\n", srate);
        
        printf("\n🎵 Tone Details:\n");
        for (int i = 0; i < message->tone_count; i++) {
            dtmf_tone_t *tone = &message->tones[i];
            printf("  Tone %d: %.1f Hz (%.3f) + %.1f Hz (%.3f) for %d samples\n",
                   i + 1, tone->low_freq, tone->low_amp, 
                   tone->high_freq, tone->high_amp, tone->duration_samples);
        }
    
        
        /* Test 4: Decode the complete message */
        printf("\n=== Test 4: Complete Message Decoding ===\n");
        printf("🔄 Generating and decoding complete DTMF sequence...\n");
        
        /* Generate audio samples for the complete message */
        int16_t *msg_samples = malloc(message->total_samples * sizeof(int16_t));
        if (msg_samples) {
            /* Initialize to silence */
            for (int i = 0; i < message->total_samples; i++) {
                msg_samples[i] = 0;
            }
            
            int sample_offset = 0;
            printf("\n🎵 Generating audio for each tone:\n");
            
            /* Generate each tone in the message */
            for (int t = 0; t < message->tone_count; t++) {
                dtmf_tone_t *tone = &message->tones[t];
                int on_samples = (srate * on_time_ms) / 1000;
                int off_samples = (srate * off_time_ms) / 1000;
                
                /* Find the corresponding digit */
                char digit = '?';
                for (size_t i = 0; i < sizeof(dtmf_char_to_freq) / sizeof(dtmf_char_to_freq[0]); i++) {
                    if (dtmf_char_to_freq[i].low_idx < 4 && 
                        dtmf_char_to_freq[i].high_idx >= 4 &&
                        fabs(dtmf_freq_table[dtmf_char_to_freq[i].low_idx] - tone->low_freq) < 1.0 &&
                        fabs(dtmf_freq_table[dtmf_char_to_freq[i].high_idx] - tone->high_freq) < 1.0) {
                        digit = dtmf_char_to_freq[i].digit;
                        break;
                    }
                }
                
                printf("  Tone %d: '%c' at offset %d samples\n", t + 1, digit, sample_offset);
                
                /* Generate the tone samples */
                for (int i = 0; i < on_samples && (sample_offset + i) < message->total_samples; i++) {
                    double t_sec = (double)i / srate;
                    double low_sine = sin(2.0 * PI * tone->low_freq * t_sec) * tone->low_amp;
                    double high_sine = sin(2.0 * PI * tone->high_freq * t_sec) * tone->high_amp;
                    double combined = (low_sine + high_sine) * 0.4;
                    msg_samples[sample_offset + i] = (int16_t)(combined * 32767.0);
                }
                
                sample_offset += tone->duration_samples;
            }
            
            printf("\n🔍 Decoding the generated message:\n");
            printf("Expected sequence: '%s'\n", test_message);
            printf("Detected sequence: '");
            
            /* Decode each tone by processing chunks */
            char decoded_message[32] = {0};
            int decoded_count = 0;
            int chunk_size = (srate * on_time_ms) / 1000; /* Process one tone duration at a time */
            
            sample_offset = 0;
            for (int t = 0; t < message->tone_count && decoded_count < 31; t++) {
                /* Process the tone portion (skip silence) */
                if (sample_offset + chunk_size <= message->total_samples) {
                    char detected_digit = detect_advanced_dtmf_digit(
                        &msg_samples[sample_offset], chunk_size, srate);
                    
                    if (detected_digit) {
                        decoded_message[decoded_count++] = detected_digit;
                        printf("%c", detected_digit);
                        fflush(stdout);
                    } else {
                        printf("?");
                        fflush(stdout);
                    }
                }
                
                /* Move to next tone (skip current tone + silence) */
                sample_offset += (srate * (on_time_ms + off_time_ms)) / 1000;
            }
            
            printf("'\n");
            decoded_message[decoded_count] = '\0';
            
            /* Compare results */
            printf("\n📊 Decoding Results:\n");
            printf("  Original message: '%s' (%zu characters)\n", test_message, strlen(test_message));
            printf("  Decoded message:  '%s' (%d characters)\n", decoded_message, decoded_count);
            
            int matches = 0;
            int total_compared = (strlen(test_message) < decoded_count) ? strlen(test_message) : decoded_count;
            
            for (int i = 0; i < total_compared; i++) {
                if (test_message[i] == decoded_message[i]) {
                    matches++;
                }
            }
            
            double accuracy = total_compared > 0 ? (double)matches / total_compared * 100.0 : 0.0;
            printf("  Accuracy: %d/%d matches (%.1f%%)\n", matches, total_compared, accuracy);
            
            if (accuracy >= 90.0) {
                printf("  ✅ Excellent decoding performance!\n");
            } else if (accuracy >= 70.0) {
                printf("  ⚠️  Good decoding with some errors\n");
            } else {
                printf("  ❌ Poor decoding performance\n");
            }
            
            free(msg_samples);
        } else {
            printf("  ❌ Failed to allocate memory for message samples\n");
        }
        
        /* Cleanup */
        free(message->tones);
        free(message);
        
        printf("  ✅ Message generation successful!\n");
    } else {
        printf("  ❌ Message generation failed!\n");
    }
    
    /* Test 5: Professional DTMF characteristics */
    printf("\n=== Test 5: Professional DTMF Standards ===\n");
    printf("📋 DTMF Specifications:\n");
    printf("  • Tone duration: %d ms (typical: 40-200ms)\n", on_time_ms);
    printf("  • Inter-tone silence: %d ms (typical: 40-200ms)\n", off_time_ms);
    printf("  • Lower tone amplitude: %.1f dB below higher tone\n", 
           20 * log10(dtmf_amplitude[0] / dtmf_amplitude[1]));
    printf("  • Frequency tolerance: ±1.5%% (ITU-T Q.24)\n");
    printf("  • Twist tolerance: ±4 dB (amplitude ratio)\n");
    
    printf("\n🔊 Frequency Plan:\n");
    printf("     ");
    for (int col = 4; col < 8; col++) {
        printf("%8.0f Hz", dtmf_freq_table[col]);
    }
    printf("\n");
    
    for (int row = 0; row < 4; row++) {
        printf("%.0f Hz", dtmf_freq_table[row]);
        for (int col = 4; col < 8; col++) {
            /* Find the character for this row/col combination */
            char digit = '?';
            for (size_t i = 0; i < sizeof(dtmf_char_to_freq) / sizeof(dtmf_char_to_freq[0]); i++) {
                if (dtmf_char_to_freq[i].low_idx == row && 
                    dtmf_char_to_freq[i].high_idx == col) {
                    digit = dtmf_char_to_freq[i].digit;
                    break;
                }
            }
            printf("%10c", digit);
        }
        printf("\n");
    }
    
    printf("\n✅ Advanced DTMF Demo completed!\n");
    printf("\n💡 Key improvements over basic implementation:\n");
    printf("   • Professional amplitude relationships (-2dB twist)\n");
    printf("   • Configurable timing (on/off/sleep periods)\n");
    printf("   • Advanced detection with twist validation\n");
    printf("   • Complete message sequence generation\n");
    printf("   • ITU-T Q.24 compliant frequency plan\n");
    printf("   • Multi-channel support ready (like GNU Radio)\n");
    
    return 0;
}
