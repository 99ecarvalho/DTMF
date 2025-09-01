/**
 * @file example01.c
 * @brief Comprehensive DTMF Library Usage Example
 * 
 * This example demonstrates how to use the DTMF detection library for processing
 * audio data and detecting DTMF tones. It covers basic usage patterns, different
 * algorithms, and typical library workflows.
 * 
 * The example shows:
 * - Basic library initialization
 * - Processing pre-recorded DTMF audio files
 * - Using different detection algorithms (FFT, Goertzel, Filter Bank, etc.)
 * - Handling results and error conditions
 * - Memory management and cleanup
 * 
 * @author Eduardo Correia <ecorreia@apliant.com.br>
 * @date August 2025
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "dtmf_types.h"
#include "dtmf_utils.h"
#include "dtmf_algorithms.h"
#include "dtmf_windowing.h"

/**
 * @brief Example 1: Basic DTMF detection from a file using FFT algorithm
 * 
 * This is the most straightforward way to detect DTMF tones from an audio file.
 * Uses the FFT-based algorithm which is good for general-purpose detection.
 */
static dtmf_result_t example_basic_file_processing(const char *filename) {
    printf("\n=== Example 1: Basic File Processing with FFT ===\n");
    
    // Initialize default configuration
    dtmf_config_advanced_t config;
    get_default_config(&config);  // Use the proper initialization function
    config.energy_threshold = 1000.0;  // Minimum energy threshold
    
    // Initialize audio processing context
    audio_context_t audio_ctx;
    dtmf_result_t result = audio_context_init(&audio_ctx, &config);
    if (result != DTMF_SUCCESS) {
        printf("Failed to initialize audio context: %s\n", dtmf_error_string(result));
        return result;
    }
    
    // Allocate samples buffer manually since audio_context_init doesn't do it
    audio_ctx.samples = malloc(SAMPLES_PER_FRAME * sizeof(int16_t));
    if (!audio_ctx.samples) {
        printf("Failed to allocate samples buffer\n");
        audio_context_cleanup(&audio_ctx);
        return DTMF_ERROR_MEMORY;
    }
    
    // Debug: Check audio context initialization
    printf("Audio context initialized:\n");
    printf("  samples pointer: %p\n", (void*)audio_ctx.samples);
    printf("  sample_count: %zu\n", audio_ctx.sample_count);
    printf("  sample_rate: %d\n", audio_ctx.sample_rate);
    printf("  window_ms: %d\n", audio_ctx.window_ms);
    printf("  fft_size: %d\n", audio_ctx.fft_size);
    
    // Open audio file
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filename);
        // Free manually allocated samples buffer
        if (audio_ctx.samples) {
            free(audio_ctx.samples);
            audio_ctx.samples = NULL;
        }
        audio_context_cleanup(&audio_ctx);
        return DTMF_ERROR_FILE_IO;
    }
    
    // Process audio in frames
    int16_t samples[SAMPLES_PER_FRAME];
    int frame_number = 0;
    
    printf("Processing audio file: %s\n", filename);
    printf("Frame size: %d samples (%.1f ms at %d Hz)\n", 
           SAMPLES_PER_FRAME, (float)FRAME_MS, SAMPLE_RATE);
    
    while (fread(samples, sizeof(int16_t), SAMPLES_PER_FRAME, file) == SAMPLES_PER_FRAME) {
        // Safety check: verify audio context samples buffer is allocated
        if (audio_ctx.samples == NULL) {
            printf("Error: audio_ctx.samples is NULL - context not properly initialized\n");
            break;
        }
        
        // Safety check: verify sample count is sufficient
        if (audio_ctx.sample_count < SAMPLES_PER_FRAME) {
            printf("Error: audio_ctx.sample_count (%zu) < SAMPLES_PER_FRAME (%d)\n", 
                   audio_ctx.sample_count, SAMPLES_PER_FRAME);
            break;
        }
        
        // Copy samples to audio context - use the safer approach
        for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
            audio_ctx.samples[i] = samples[i];
        }
        
        // Update actual sample count in context
        audio_ctx.sample_count = SAMPLES_PER_FRAME;
        
        // Analyze frame using FFT algorithm with Hamming window
        dtmf_analysis_result_t analysis_result;
        result = analyze_fft_strategy(&audio_ctx, &DEFAULT_DTMF_CONFIG, 
                                    &WINDOW_STRATEGIES[1], &analysis_result); // Use Hamming window (index 1)
        
        if (result == DTMF_SUCCESS) {
            if (analysis_result.detected_key != '\0') {
                printf("Frame %d: Detected DTMF key '%c' (%.1f Hz + %.1f Hz) "
                       "Confidence: L=%.2f H=%.2f Time: %.3f ms Magnitude: L=%.2e H=%.2e LSB\n",
                       frame_number,
                       analysis_result.detected_key,
                       analysis_result.low_tone.frequency,
                       analysis_result.high_tone.frequency,
                       analysis_result.low_confidence,
                       analysis_result.high_confidence,
                       analysis_result.timing.wall_time_s * 1000,
                       analysis_result.low_tone.magnitude,
                       analysis_result.high_tone.magnitude);
            } else {
                // Still show energy levels even when no key is detected
                printf("Frame %d: No DTMF detected - Magnitude: L=%.2e H=%.2e LSB\n",
                       frame_number,
                       analysis_result.low_tone.magnitude,
                       analysis_result.high_tone.magnitude);
            }
        } else {
            printf("Frame %d: Analysis failed: %s\n", frame_number, dtmf_error_string(result));
        }
        
        frame_number++;
        
        // Limit processing for initial testing
        if (frame_number >= 3) {
            printf("Limiting to %d frames for initial testing\n", frame_number);
            break;
        }
    }
    
    printf("Processed %d frames total\n", frame_number);
    
    // Cleanup
    fclose(file);
    
    // Free manually allocated samples buffer
    if (audio_ctx.samples) {
        free(audio_ctx.samples);
        audio_ctx.samples = NULL;
    }
    
    audio_context_cleanup(&audio_ctx);
    
    return DTMF_SUCCESS;
}

/**
 * @brief Example 2: Compare multiple detection algorithms
 * 
 * This example demonstrates how to use different detection algorithms
 * and compare their results on the same audio data.
 */
static dtmf_result_t example_algorithm_comparison(const char *filename) {
    printf("\n=== Example 2: Algorithm Comparison ===\n");
    
    // Define algorithms to test with their function pointers
    struct {
        const char *name;
        dtmf_result_t (*analyze_func)(const audio_context_t *, const dtmf_config_t *, const window_strategy_t *, dtmf_analysis_result_t *);
    } algorithms[] = {
        {"FFT", analyze_fft_strategy},
        {"Goertzel", analyze_goertzel_strategy},
        {"GoertzelTwist", analyze_goertzel_twist_strategy},
        {"FilterBank", analyze_filterbank_strategy},
        {"Correlation", analyze_correlation_strategy}
    };
    
    const int num_algorithms = sizeof(algorithms) / sizeof(algorithms[0]);
    
    // Initialize configuration
    dtmf_config_advanced_t adv_config;
    get_default_config(&adv_config);
    adv_config.energy_threshold = 800.0;
    
    // Read test audio file into memory
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filename);
        return DTMF_ERROR_FILE_IO;
    }
    
    // Read one frame for testing
    int16_t test_samples[SAMPLES_PER_FRAME];
    if (fread(test_samples, sizeof(int16_t), SAMPLES_PER_FRAME, file) != SAMPLES_PER_FRAME) {
        printf("Error: Could not read test frame from file\n");
        fclose(file);
        return DTMF_ERROR_INSUFFICIENT_DATA;
    }
    fclose(file);
    
    printf("Comparing algorithms on single frame from: %s\n", filename);
    printf("%-15s %-8s %-12s %-12s %-10s %-10s %-12s %-12s %-10s\n", 
           "Algorithm", "Key", "Low Freq", "High Freq", "L-Conf", "H-Conf", "L-Magnitude", "H-Magnitude", "Time(μs)");
    printf("%-15s %-8s %-12s %-12s %-10s %-10s %-12s %-12s %-10s\n", 
           "---------------", "--------", "------------", "------------", "----------", "----------", "------------", "------------", "----------");
    
    // Test each algorithm
    for (int i = 0; i < num_algorithms; i++) {
        // Initialize context for this algorithm
        audio_context_t audio_ctx;
        
        dtmf_result_t result = audio_context_init(&audio_ctx, &adv_config);
        if (result != DTMF_SUCCESS) {
            printf("%-15s Failed to initialize\n", algorithms[i].name);
            continue;
        }
        
        // Allocate samples buffer manually
        audio_ctx.samples = malloc(SAMPLES_PER_FRAME * sizeof(int16_t));
        if (!audio_ctx.samples) {
            printf("%-15s Failed to allocate samples buffer\n", algorithms[i].name);
            audio_context_cleanup(&audio_ctx);
            continue;
        }
        
        // Copy test samples
        memcpy(audio_ctx.samples, test_samples, sizeof(test_samples));
        
        // Analyze with current algorithm (using rectangular window - index 0)
        dtmf_analysis_result_t analysis_result;
        result = algorithms[i].analyze_func(&audio_ctx, &DEFAULT_DTMF_CONFIG, 
                                          &WINDOW_STRATEGIES[0], &analysis_result);
        
        if (result == DTMF_SUCCESS) {
            printf("%-15s %-8c %-12.1f %-12.1f %-10.2f %-10.2f %-12.2e %-12.2e %-10.1f\n",
                   algorithms[i].name,
                   analysis_result.detected_key ? analysis_result.detected_key : '-',
                   analysis_result.low_tone.frequency,
                   analysis_result.high_tone.frequency,
                   analysis_result.low_confidence,
                   analysis_result.high_confidence,
                   analysis_result.low_tone.magnitude,
                   analysis_result.high_tone.magnitude,
                   analysis_result.timing.wall_time_s * 1000000);  // Convert to microseconds
        } else {
            printf("%-15s Error: %s\n", algorithms[i].name, dtmf_error_string(result));
        }
        
        // Free manually allocated samples buffer
        if (audio_ctx.samples) {
            free(audio_ctx.samples);
            audio_ctx.samples = NULL;
        }
        
        audio_context_cleanup(&audio_ctx);
    }
    
    return DTMF_SUCCESS;
}

/**
 * @brief Example 3: Using Goertzel algorithm with twist detection
 * 
 * This example shows how to use the Goertzel algorithm specifically,
 * which is efficient for detecting specific frequencies and includes
 * ITU-T twist measurement for telephony compliance.
 */
static dtmf_result_t example_goertzel_twist(const char *filename) {
    printf("\n=== Example 3: Goertzel Algorithm with Twist Detection ===\n");
    
    // Initialize Goertzel twist detector
    dtmf_goertzel_twist_t detector;
    dtmf_result_t result = init_goertzel_twist(&detector, SAMPLE_RATE, SAMPLES_PER_FRAME);
    if (result != DTMF_SUCCESS) {
        printf("Failed to initialize Goertzel twist detector: %s\n", dtmf_error_string(result));
        return result;
    }
    
    // Open file
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filename);
        return DTMF_ERROR_FILE_IO;
    }
    
    printf("Processing with Goertzel twist detection...\n");
    printf("ITU-T Q.24 compliant twist measurement (-8dB to +4dB)\n\n");
    
    int16_t samples[SAMPLES_PER_FRAME];
    int frame_number = 0;
    
    while (fread(samples, sizeof(int16_t), SAMPLES_PER_FRAME, file) == SAMPLES_PER_FRAME) {
        // Process frame with twist analysis
        dtmf_twist_result_t twist_result = process_frame_with_twist(&detector, samples);
        
        if (twist_result.valid && twist_result.digit != '\0') {
            printf("Frame %d: Key='%c' Powers=[%.1e %.1e] Twist=%.1fdB Energy=%.1e\n",
                   frame_number,
                   twist_result.digit,
                   twist_result.power_low,
                   twist_result.power_high,
                   twist_result.twist_db,
                   twist_result.energy_ratio);
        }
        
        frame_number++;
    }
    
    printf("Processed %d frames with Goertzel twist analysis\n", frame_number);
    
    fclose(file);
    
    return DTMF_SUCCESS;
}

/**
 * @brief Example 4: Real-time processing simulation
 * 
 * This example demonstrates how you might process audio in a real-time
 * scenario, maintaining state between frames and implementing a simple
 * key debouncing mechanism.
 */
static dtmf_result_t example_realtime_simulation(const char *filename) {
    printf("\n=== Example 4: Real-time Processing Simulation ===\n");
    
    // Initialize configuration for real-time processing
    dtmf_config_advanced_t config;
    get_default_config(&config);  
    config.energy_threshold = 1200.0;  // Higher threshold for robustness
    
    audio_context_t audio_ctx;
    dtmf_result_t result = audio_context_init(&audio_ctx, &config);
    if (result != DTMF_SUCCESS) {
        printf("Failed to initialize audio context: %s\n", dtmf_error_string(result));
        return result;
    }
    
    // Allocate samples buffer manually
    audio_ctx.samples = malloc(SAMPLES_PER_FRAME * sizeof(int16_t));
    if (!audio_ctx.samples) {
        printf("Failed to allocate samples buffer\n");
        audio_context_cleanup(&audio_ctx);
        return DTMF_ERROR_MEMORY;
    }
    
    // State for key debouncing
    char last_key = '\0';
    int key_count = 0;
    const int DEBOUNCE_FRAMES = 3;  // Require 3 consecutive detections

    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filename);
        // Free manually allocated samples buffer
        if (audio_ctx.samples) {
            free(audio_ctx.samples);
            audio_ctx.samples = NULL;
        }
        audio_context_cleanup(&audio_ctx);
        return DTMF_ERROR_FILE_IO;
    }
    
    printf("Simulating real-time processing with key debouncing...\n");
    printf("Debounce threshold: %d consecutive frames\n\n", DEBOUNCE_FRAMES);
    
    int16_t samples[SAMPLES_PER_FRAME];
    int frame_number = 0;
    
    while (fread(samples, sizeof(int16_t), SAMPLES_PER_FRAME, file) == SAMPLES_PER_FRAME) {
        // Copy samples to context
        memcpy(audio_ctx.samples, samples, sizeof(samples));
        
        // Calculate frame time in milliseconds for audio editor correlation
        float frame_time_ms = (float)frame_number * FRAME_MS;
        
        // Analyze current frame using Goertzel (efficient for real-time)
        dtmf_analysis_result_t analysis_result;
        result = analyze_goertzel_strategy(&audio_ctx, &DEFAULT_DTMF_CONFIG, 
                                         &WINDOW_STRATEGIES[0], &analysis_result);
        
        if (result == DTMF_SUCCESS) {
            char current_key = analysis_result.detected_key;
            
            // Always show energy levels for debugging with frame time
            printf("Frame %d (%.1fms): Magnitude L=%.2e H=%.2e LSB ", 
                   frame_number,
                   frame_time_ms,
                   analysis_result.low_tone.magnitude,
                   analysis_result.high_tone.magnitude);
            
            if (current_key != '\0') {
                printf("Key='%c' (%.1f Hz + %.1f Hz)\n", 
                       current_key,
                       analysis_result.low_tone.frequency,
                       analysis_result.high_tone.frequency);
            } else {
                printf("No key detected\n");
            }
            
            // Implement simple debouncing
            if (current_key == last_key) {
                if (current_key != '\0') {
                    key_count++;
                    if (key_count == DEBOUNCE_FRAMES) {
                        printf("Frame %d (%.1fms): CONFIRMED DTMF key '%c' (debounced after %d frames)\n",
                               frame_number, frame_time_ms, current_key, DEBOUNCE_FRAMES);
                    }
                }
            } else {
                // Key changed, reset counter
                if (last_key != '\0' && key_count > 0 && key_count < DEBOUNCE_FRAMES) {
                    printf("Frame %d (%.1fms): REJECTED key '%c' (only %d/%d frames)\n",
                           frame_number, frame_time_ms, last_key, key_count, DEBOUNCE_FRAMES);
                }
                last_key = current_key;
                key_count = (current_key != '\0') ? 1 : 0;
            }
        }
        
        frame_number++;
        
        // Simulate real-time processing delay (optional)
        // usleep(FRAME_MS * 1000);  // Sleep for frame duration
    }
    
    printf("\nSimulation completed after %d frames\n", frame_number);
    
    fclose(file);
    
    // Free manually allocated samples buffer
    if (audio_ctx.samples) {
        free(audio_ctx.samples);
        audio_ctx.samples = NULL;
    }
    
    audio_context_cleanup(&audio_ctx);
    
    return DTMF_SUCCESS;
}

/**
 * @brief Example 5: Audio quality assessment
 * 
 * This example shows how to assess audio quality and handle poor quality signals.
 */
static dtmf_result_t example_audio_quality_check(const char *filename) {
    printf("\n=== Example 5: Audio Quality Assessment ===\n");
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filename);
        return DTMF_ERROR_FILE_IO;
    }
    
    // Read a frame for quality assessment
    int16_t samples[SAMPLES_PER_FRAME];
    if (fread(samples, sizeof(int16_t), SAMPLES_PER_FRAME, file) != SAMPLES_PER_FRAME) {
        printf("Error reading audio data\n");
        fclose(file);
        return DTMF_ERROR_INSUFFICIENT_DATA;
    }
    
    // Assess audio quality
    audio_quality_t quality;
    dtmf_result_t quality_result = validate_audio_quality(samples, SAMPLES_PER_FRAME, &quality);
    
    if (quality_result != DTMF_SUCCESS) {
        printf("Warning: Audio quality validation failed: %s\n", dtmf_error_string(quality_result));
        // Continue with limited quality assessment
        memset(&quality, 0, sizeof(quality));
    }

    printf("Audio Quality Assessment for: %s\n", filename);
    printf("  DC Offset:     %.2f (LSB)\n", quality.dc_offset);
    printf("  RMS Level:     %.2f (LSB)\n", quality.rms_level);
    printf("  Peak Level:    %.2f (LSB)\n", quality.peak_level);
    printf("  SNR Estimate:  %.1f dB\n", quality.snr_estimate);
    printf("  Clipping:      %s\n", quality.is_clipped ? "Yes" : "No");
    printf("  Too Quiet:     %s\n", quality.is_too_quiet ? "Yes" : "No");
    
    // Quality recommendations
    printf("\nQuality Recommendations:\n");
    if (quality.is_clipped) {
        printf("  ⚠ Warning: Audio clipping detected - reduce input gain\n");
    }
    if (quality.is_too_quiet) {
        printf("  ⚠ Warning: Signal level too low - increase input gain\n");
    }
    if (quality.snr_estimate < 20.0) {
        printf("  ⚠ Warning: High noise level (SNR < 20dB) - may affect detection accuracy\n");
    }
    if (fabs(quality.dc_offset) > 100.0) {
        printf("  ⚠ Warning: Significant DC offset - consider high-pass filtering\n");
    }
    if (!quality.is_clipped && !quality.is_too_quiet && quality.snr_estimate >= 20.0) {
        printf("  ✓ Audio quality appears good for DTMF detection\n");
    }
    
    fclose(file);
    return DTMF_SUCCESS;
}

/**
 * @brief Print usage information
 */
static void print_usage(const char *program_name) {
    printf("DTMF Library Usage Example\n");
    printf("==========================\n\n");
    printf("Usage: %s <audio_file.pcm>\n\n", program_name);
    printf("This example demonstrates various ways to use the DTMF detection library:\n");
    printf("  1. Basic file processing with FFT algorithm\n");
    printf("  2. Comparison of different detection algorithms\n");
    printf("  3. Goertzel algorithm with twist detection\n");
    printf("  4. Real-time processing simulation with debouncing\n");
    printf("  5. Audio quality assessment\n\n");
    printf("Audio file requirements:\n");
    printf("  - Format: 16-bit PCM, mono\n");
    printf("  - Sample rate: 8000 Hz\n");
    printf("  - Contains DTMF tones\n\n");
    printf("Example files available in data/ directory:\n");
    printf("  data/01/*.pcm - Individual digit samples\n");
    printf("  data/audiocheck.net/*.pcm - Test tone samples\n");
    printf("  data/rec/*.pcm - Recorded sequences\n");
}

/**
 * @brief Main function - demonstrate library usage
 */
int main(int argc, char *argv[]) {
    printf("DTMF Detection Library - Usage Examples\n");
    printf("========================================\n");
    
    // Set up logging
    dtmf_set_log_level(LOG_INFO);
    
    // Check command line arguments
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *audio_file = argv[1];
    
    // Verify file exists
    FILE *test_file = fopen(audio_file, "rb");
    if (!test_file) {
        printf("Error: Cannot open audio file '%s'\n", audio_file);
        printf("Please provide a valid 16-bit PCM audio file.\n");
        return 1;
    }
    fclose(test_file);
    
    printf("Using audio file: %s\n", audio_file);
    
    // Run all examples
    dtmf_result_t result;
    
    result = example_basic_file_processing(audio_file);
    if (result != DTMF_SUCCESS) {
        printf("Example 1 failed: %s\n", dtmf_error_string(result));
    }
    
    result = example_algorithm_comparison(audio_file);
    if (result != DTMF_SUCCESS) {
        printf("Example 2 failed: %s\n", dtmf_error_string(result));
    }
    
    result = example_goertzel_twist(audio_file);
    if (result != DTMF_SUCCESS) {
        printf("Example 3 failed: %s\n", dtmf_error_string(result));
    }
    
    result = example_realtime_simulation(audio_file);
    if (result != DTMF_SUCCESS) {
        printf("Example 4 failed: %s\n", dtmf_error_string(result));
    }
    
    result = example_audio_quality_check(audio_file);
    if (result != DTMF_SUCCESS) {
        printf("Example 5 failed: %s\n", dtmf_error_string(result));
    }
    
    printf("\n=== All Examples Completed ===\n");
    printf("This demonstrates the main ways to use the DTMF detection library.\n");
    printf("You can adapt these patterns for your own applications.\n");
    
    return 0;
}
