/**
 * @file simple.c
 * @brief Simple DTMF decoder that reads PCM data from stdin
 * 
 * This program implements a simplified version of the HTML/JavaScript DTMF decoder,
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
static const int LOW_FREQS[] = {697, 770, 852, 941};
static const int HIGH_FREQS[] = {1209, 1336, 1477, 1633};

// DTMF character mapping
static const char DTMF_TABLE[4][4] = {
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
    double totalPower;
    int sampleCount;
} GoertzelState;

/**
 * @brief DTMF decoder context
 */
typedef struct {
    GoertzelState lowFreqStates[NUM_LOW_FREQS];
    GoertzelState highFreqStates[NUM_HIGH_FREQS];
    double energyThreshold;
    int sampleRate;
} DTMFDecoder;

/**
 * @brief Apply exact Blackman window to a sample
 * 
 * @param sample The input sample value
 * @param index Sample index in the buffer
 * @param bufferSize Total buffer size
 * @return Windowed sample value
 */
static double exactBlackmanWindow(double sample, int index, int bufferSize) {
    return sample * (
        0.426591 -
        (0.496561 * cos((2.0 * M_PI * index) / bufferSize)) +
        (0.076849 * cos((4.0 * M_PI * index) / bufferSize))
    );
}

/**
 * @brief Initialize Goertzel algorithm state for a specific frequency
 * 
 * @param state Pointer to Goertzel state structure
 * @param frequency Target frequency in Hz
 * @param sampleRate Sample rate in Hz
 */
static void initGoertzel(GoertzelState *state, int frequency, int sampleRate) {
    double normalizedFreq = (double)frequency / sampleRate;
    double omega = 2.0 * M_PI * normalizedFreq;
    state->coeff = 2.0 * cos(omega);
    state->q1 = 0.0;
    state->q2 = 0.0;
    state->totalPower = 0.0;
    state->sampleCount = 0;
}

/**
 * @brief Process a single sample through Goertzel algorithm
 * 
 * @param state Pointer to Goertzel state structure
 * @param sample Input sample value
 */
static void processGoertzelSample(GoertzelState *state, double sample) {
    double q0 = sample + (state->coeff * state->q1) - state->q2;
    state->q2 = state->q1;
    state->q1 = q0;
    state->totalPower += sample * sample;
    state->sampleCount++;
}

/**
 * @brief Get energy result from Goertzel algorithm
 * 
 * @param state Pointer to Goertzel state structure
 * @return Normalized energy value
 */
static double getGoertzelEnergy(GoertzelState *state) {
    if (state->totalPower == 0.0) {
        state->totalPower = 1.0;  // Avoid division by zero
    }
    
    double power = (state->q2 * state->q2) + (state->q1 * state->q1) - 
                   (state->coeff * state->q1 * state->q2);
    
    return power / state->totalPower / state->sampleCount;
}

/**
 * @brief Reset Goertzel state for next frame
 * 
 * @param state Pointer to Goertzel state structure
 */
static void resetGoertzel(GoertzelState *state) {
    state->q1 = 0.0;
    state->q2 = 0.0;
    state->totalPower = 0.0;
    state->sampleCount = 0;
}

/**
 * @brief Initialize DTMF decoder
 * 
 * @param decoder Pointer to decoder structure
 * @param threshold Energy threshold for detection
 * @param sampleRate Audio sample rate
 */
static void initDTMFDecoder(DTMFDecoder *decoder, double threshold, int sampleRate) {
    decoder->energyThreshold = threshold;
    decoder->sampleRate = sampleRate;
    
    // Initialize low frequency Goertzel filters
    for (int i = 0; i < NUM_LOW_FREQS; i++) {
        initGoertzel(&decoder->lowFreqStates[i], LOW_FREQS[i], sampleRate);
    }
    
    // Initialize high frequency Goertzel filters
    for (int i = 0; i < NUM_HIGH_FREQS; i++) {
        initGoertzel(&decoder->highFreqStates[i], HIGH_FREQS[i], sampleRate);
    }
}

/**
 * @brief Process a buffer of samples and detect DTMF character
 * 
 * @param decoder Pointer to initialized decoder
 * @param buffer Array of audio samples (16-bit PCM)
 * @param bufferSize Number of samples in buffer
 * @return Detected DTMF character or '\0' if none detected
 */
static char processBuffer(DTMFDecoder *decoder, const int16_t *buffer, int bufferSize) {
    // Reset all Goertzel states
    for (int i = 0; i < NUM_LOW_FREQS; i++) {
        resetGoertzel(&decoder->lowFreqStates[i]);
    }
    for (int i = 0; i < NUM_HIGH_FREQS; i++) {
        resetGoertzel(&decoder->highFreqStates[i]);
    }
    
    // Process each sample through all filters
    for (int sampleIdx = 0; sampleIdx < bufferSize; sampleIdx++) {
        // Convert to normalized float and apply windowing
        double sample = (double)buffer[sampleIdx] / 32768.0;
        double windowedSample = exactBlackmanWindow(sample, sampleIdx, bufferSize);
        
        // Process through low frequency filters
        for (int i = 0; i < NUM_LOW_FREQS; i++) {
            processGoertzelSample(&decoder->lowFreqStates[i], windowedSample);
        }
        
        // Process through high frequency filters
        for (int i = 0; i < NUM_HIGH_FREQS; i++) {
            processGoertzelSample(&decoder->highFreqStates[i], windowedSample);
        }
    }
    
    // Find strongest low frequency
    int bestLowIdx = -1;
    double maxLowEnergy = 0.0;
    for (int i = 0; i < NUM_LOW_FREQS; i++) {
        double energy = getGoertzelEnergy(&decoder->lowFreqStates[i]);
        if (energy > maxLowEnergy && energy >= decoder->energyThreshold) {
            maxLowEnergy = energy;
            bestLowIdx = i;
        }
    }
    
    if (bestLowIdx == -1) {
        return '\0';  // No low frequency detected
    }
    
    // Find strongest high frequency
    int bestHighIdx = -1;
    double maxHighEnergy = 0.0;
    for (int i = 0; i < NUM_HIGH_FREQS; i++) {
        double energy = getGoertzelEnergy(&decoder->highFreqStates[i]);
        if (energy > maxHighEnergy && energy >= decoder->energyThreshold) {
            maxHighEnergy = energy;
            bestHighIdx = i;
        }
    }
    
    if (bestHighIdx == -1) {
        return '\0';  // No high frequency detected
    }
    
    return DTMF_TABLE[bestLowIdx][bestHighIdx];
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
    printf("Sample rate: %d Hz, Buffer size: %d samples, Threshold: %.3f\n", 
           SAMPLE_RATE, BUFFER_SIZE, threshold);
    printf("Press Ctrl+C to stop\n\n");
    
    DTMFDecoder decoder;
    initDTMFDecoder(&decoder, threshold, SAMPLE_RATE);
    
    int16_t buffer[BUFFER_SIZE];
    char prevChar = '\0';
    char decoded[1024] = "";
    int decodedLen = 0;
    int frameCount = 0;
    int secondCount = 1;
    
    printf("1s ");
    
    while (1) {
        // Read one frame of PCM data
        size_t bytesRead = fread(buffer, sizeof(int16_t), BUFFER_SIZE, stdin);
        
        if (bytesRead == 0) {
            if (feof(stdin)) {
                break;  // End of input
            }
            continue;  // Keep trying
        }
        
        if (bytesRead < BUFFER_SIZE) {
            // Pad with zeros if incomplete frame
            memset(&buffer[bytesRead], 0, (BUFFER_SIZE - bytesRead) * sizeof(int16_t));
        }
        
        // Process the buffer
        char currentChar = processBuffer(&decoder, buffer, BUFFER_SIZE);
        
        // Output detection result
        if (currentChar != '\0') {
            printf("%c", currentChar);
            
            // Add to decoded string if different from previous
            if (currentChar != prevChar && decodedLen < (int)(sizeof(decoded) - 1)) {
                decoded[decodedLen++] = currentChar;
                decoded[decodedLen] = '\0';
            }
        } else {
            printf(".");
        }
        
        prevChar = currentChar;
        frameCount++;
        
        // Print newline every second (25 frames at 40ms each = 1 second)
        if (frameCount % 25 == 0) {
            secondCount++;
            printf("\n%ds ", secondCount);
        }
        
        fflush(stdout);
    }
    
    printf("\n\nDecoded: %s\n", decoded);
    
    return 0;
}
