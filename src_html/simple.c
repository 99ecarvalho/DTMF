/**
 * @file simple.c
 * @brief Simple DTMF decoder that reads PCM data from stdin
 * 
 * This program implements a simplified version of the DTMF decoder,
 * reading raw PCM audio data from stdin and outputting detected DTMF digits.
 * 
 * Usage: cat audio.pcm | ./simple [threshold]
 * 
 * The program uses the Goertzel algorithm for efficient frequency detection,
 * processing audio in 320-sample frames (40ms at 8kHz sample rate).
 * 
 * @author Eduardo Correia <ecorreia@apliant.com.br>
 * @date September 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 8000
#define BUFFER_SIZE 320  // 40ms at 8kHz
#define DEFAULT_THRESHOLD 0.050
#define NUM_LOW_FREQS 4
#define NUM_HIGH_FREQS 4

// DTMF frequency definitions
static const int low_freqs[] = {697, 770, 852, 941};
static const int high_freqs[] = {1209, 1336, 1477, 1633};

// DTMF character mapping
static const char dtmf_table[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

/**
 * @brief Goertzel algorithm state for a single frequency
 */
typedef struct {
    double coeff;
    double q1, q2;
    double total_power;
    int sample_count;
} goertzel_state_t;

/**
 * @brief DTMF decoder context
 */
typedef struct {
    goertzel_state_t low_freq_states[NUM_LOW_FREQS];
    goertzel_state_t high_freq_states[NUM_HIGH_FREQS];
    double energy_threshold;
    int sample_rate;
} dtmf_decoder_t;

/**
 * @brief Apply exact Blackman window to a sample
 * 
 * @param sample The input sample value
 * @param index Sample index in the buffer
 * @param buffer_size Total buffer size
 * @return Windowed sample value
 */
static double exact_blackman_window(double sample, int index, int buffer_size) {
    return sample * (
        0.426591 -
        (0.496561 * cos((2.0 * M_PI * index) / buffer_size)) +
        (0.076849 * cos((4.0 * M_PI * index) / buffer_size))
    );
}

/**
 * @brief Initialize Goertzel algorithm state for a specific frequency
 * 
 * @param state Pointer to Goertzel state structure
 * @param frequency Target frequency in Hz
 * @param sample_rate Sample rate in Hz
 */
static void init_goertzel(goertzel_state_t *state, int frequency, int sample_rate) {
    double normalized_freq = (double)frequency / sample_rate;
    double omega = 2.0 * M_PI * normalized_freq;
    state->coeff = 2.0 * cos(omega);
    state->q1 = 0.0;
    state->q2 = 0.0;
    state->total_power = 0.0;
    state->sample_count = 0;
}

/**
 * @brief Process a single sample through Goertzel algorithm
 * 
 * @param state Pointer to Goertzel state structure
 * @param sample Input sample value
 */
static void process_goertzel_sample(goertzel_state_t *state, double sample) {
    double q0 = sample + (state->coeff * state->q1) - state->q2;
    state->q2 = state->q1;
    state->q1 = q0;
    state->total_power += sample * sample;
    state->sample_count++;
}

/**
 * @brief Get energy result from Goertzel algorithm
 * 
 * @param state Pointer to Goertzel state structure
 * @return Normalized energy value
 */
static double get_goertzel_energy(goertzel_state_t *state) {
    if (state->total_power == 0.0) {
        state->total_power = 1.0;  // Avoid division by zero
    }
    
    double power = (state->q2 * state->q2) + (state->q1 * state->q1) - 
                   (state->coeff * state->q1 * state->q2);
    
    return power / state->total_power / state->sample_count;
}

/**
 * @brief Reset Goertzel state for next frame
 * 
 * @param state Pointer to Goertzel state structure
 */
static void reset_goertzel(goertzel_state_t *state) {
    state->q1 = 0.0;
    state->q2 = 0.0;
    state->total_power = 0.0;
    state->sample_count = 0;
}

/**
 * @brief Initialize DTMF decoder
 * 
 * @param decoder Pointer to decoder structure
 * @param threshold Energy threshold for detection
 * @param sample_rate Audio sample rate
 */
static void init_dtmf_decoder(dtmf_decoder_t *decoder, double threshold, int sample_rate) {
    decoder->energy_threshold = threshold;
    decoder->sample_rate = sample_rate;
    
    // Initialize low frequency Goertzel filters
    for (int i = 0; i < NUM_LOW_FREQS; i++) {
        init_goertzel(&decoder->low_freq_states[i], low_freqs[i], sample_rate);
    }
    
    // Initialize high frequency Goertzel filters
    for (int i = 0; i < NUM_HIGH_FREQS; i++) {
        init_goertzel(&decoder->high_freq_states[i], high_freqs[i], sample_rate);
    }
}

/**
 * @brief Process a buffer of samples and detect DTMF character
 * 
 * @param decoder Pointer to initialized decoder
 * @param buffer Array of audio samples (16-bit PCM)
 * @param buffer_size Number of samples in buffer
 * @return Detected DTMF character or '\0' if none detected
 */
static char process_buffer(dtmf_decoder_t *decoder, const int16_t *buffer, int buffer_size) {
    // Reset all Goertzel states
    for (int i = 0; i < NUM_LOW_FREQS; i++) {
        reset_goertzel(&decoder->low_freq_states[i]);
    }
    for (int i = 0; i < NUM_HIGH_FREQS; i++) {
        reset_goertzel(&decoder->high_freq_states[i]);
    }
    
    // Process each sample through all filters
    for (int sample_idx = 0; sample_idx < buffer_size; sample_idx++) {
        // Convert to normalized float and apply windowing
        double sample = (double)buffer[sample_idx] / 32768.0;
        double windowed_sample = exact_blackman_window(sample, sample_idx, buffer_size);
        
        // Process through low frequency filters
        for (int i = 0; i < NUM_LOW_FREQS; i++) {
            process_goertzel_sample(&decoder->low_freq_states[i], windowed_sample);
        }
        
        // Process through high frequency filters
        for (int i = 0; i < NUM_HIGH_FREQS; i++) {
            process_goertzel_sample(&decoder->high_freq_states[i], windowed_sample);
        }
    }
    
    // Find strongest low frequency
    int best_low_idx = -1;
    double max_low_energy = 0.0;
    for (int i = 0; i < NUM_LOW_FREQS; i++) {
        double energy = get_goertzel_energy(&decoder->low_freq_states[i]);
        if (energy > max_low_energy && energy >= decoder->energy_threshold) {
            max_low_energy = energy;
            best_low_idx = i;
        }
    }
    
    if (best_low_idx == -1) {
        return '\0';  // No low frequency detected
    }
    
    // Find strongest high frequency
    int best_high_idx = -1;
    double max_high_energy = 0.0;
    for (int i = 0; i < NUM_HIGH_FREQS; i++) {
        double energy = get_goertzel_energy(&decoder->high_freq_states[i]);
        if (energy > max_high_energy && energy >= decoder->energy_threshold) {
            max_high_energy = energy;
            best_high_idx = i;
        }
    }
    
    if (best_high_idx == -1) {
        return '\0';  // No high frequency detected
    }
    
    return dtmf_table[best_low_idx][best_high_idx];
}

/**
 * @brief Main function - reads PCM from stdin and detects DTMF
 */
int main(int argc, char *argv[]) {
    double threshold = DEFAULT_THRESHOLD;
    
    // Parse command line threshold argument
    if (argc > 1) {
        threshold = atof(argv[1]);
        if (threshold <= 0.0 || threshold > 1.0) {
            fprintf(stderr, "Warning: threshold should be between 0 and 1, using default %.3f\n", 
                    DEFAULT_THRESHOLD);
            threshold = DEFAULT_THRESHOLD;
        }
    }
    
    printf("DTMF Decoder - reading PCM from stdin\n");
    printf("(C) 2025 Eduardo Correia\n");
    printf("Sample rate: %d Hz, Buffer size: %d samples, Threshold: %.3f\n", 
           SAMPLE_RATE, BUFFER_SIZE, threshold);
    printf("Press Ctrl+C to stop\n\n");
    
    dtmf_decoder_t decoder;
    init_dtmf_decoder(&decoder, threshold, SAMPLE_RATE);
    
    int16_t buffer[BUFFER_SIZE];
    char prev_char = '\0';
    char decoded[1024] = "";
    int decoded_len = 0;
    int frame_count = 0;
    int second_count = 1;
    
    printf("1s ");
    
    while (1) {
        // Read one frame of PCM data
        size_t bytes_read = fread(buffer, sizeof(int16_t), BUFFER_SIZE, stdin);
        
        if (bytes_read == 0) {
            if (feof(stdin)) {
                break;  // End of input
            }
            continue;  // Keep trying
        }
        
        if (bytes_read < BUFFER_SIZE) {
            // Pad with zeros if incomplete frame
            memset(&buffer[bytes_read], 0, (BUFFER_SIZE - bytes_read) * sizeof(int16_t));
        }
        
        // Process the buffer
        char current_char = process_buffer(&decoder, buffer, BUFFER_SIZE);
        
        // Output detection result
        if (current_char != '\0') {
            printf("%c", current_char);
            
            // Add to decoded string if different from previous
            if (current_char != prev_char && decoded_len < (int)(sizeof(decoded) - 1)) {
                decoded[decoded_len++] = current_char;
                decoded[decoded_len] = '\0';
            }
        } else {
            printf(".");
        }
        
        prev_char = current_char;
        frame_count++;
        
        // Print newline every second (25 frames at 40ms each = 1 second)
        if (frame_count % 25 == 0) {
            second_count++;
            printf("\n%ds ", second_count);
        }
        
        fflush(stdout);
    }
    
    printf("\n\nDecoded: %s\n", decoded);
    
    return 0;
}
