/**
 * @file dtmf_stream.c
 * @brief Enhanced real-time streaming DTMF tone detection system
 * 
 * This is the main program for DTMF (Dual-Tone Multi-Frequency) detection
 * from audio streams. It provides a modular architecture supporting multiple
 * detection algorithms with enhanced error handling, performance optimization,
 * and audio quality validation.
 * 
 * ## Supported Algorithms:
 * - FFT-based analysis with multiple windowing functions
 * - Standard Goertzel algorithm
 * - Goertzel with ITU-T twist detection  
 * - Filter bank approach
 * - Cross-correlation detection
 * - MUSIC (Multiple Signal Classification) algorithm
 * 
 * ## Features:
 * - Modular architecture with separate algorithm implementations
 * - Enhanced error handling and comprehensive logging
 * - Memory pool allocation for reduced latency
 * - Audio quality validation and preprocessing
 * - Runtime configuration management
 * - Performance optimization with lookup tables and buffer reuse
 * 
 * ## Usage Examples:
 * ```bash
 * # Process from file:
 * ./dtmf_stream input.pcm
 * 
 * # Process from stdin (streaming):
 * cat audio.pcm | ./dtmf_stream -
 * arecord -f S16_LE -r 8000 -c 1 | ./dtmf_stream -
 * 
 * # With custom configuration:
 * ./dtmf_stream input.pcm config.conf
 * ```
 *
 * @author Eduardo Correia <ecorreia@apliant.com.br>
 * @date August 2025
 */

#define _POSIX_C_SOURCE 200809L

#include "dtmf_types.h"
#include "dtmf_utils.h"
#include "dtmf_windowing.h"
#include "dtmf_algorithms.h"

// =============================================================================
// STREAM PROCESSING
// =============================================================================

/**
 * @brief Initialize stream processing context
 * 
 * Sets up the stream context for processing audio from either a file
 * or standard input. Allocates necessary resources and initializes
 * the audio processing context.
 * 
 * @param stream_ctx Stream context to initialize
 * @param filename Input filename or "-" for stdin, NULL for stdin
 * @param config Algorithm configuration to use
 * @return DTMF_SUCCESS on success, error code on failure
 */
static dtmf_result_t stream_init(stream_context_t *stream_ctx, 
                                const char *filename,
                                const dtmf_config_advanced_t *config) {
    if (!stream_ctx || !config) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    memset(stream_ctx, 0, sizeof(stream_context_t));
    
    if (filename && strcmp(filename, "-") != 0) {
        stream_ctx->input = fopen(filename, "rb");
        if (!stream_ctx->input) {
            dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(DTMF_ERROR_FILE_IO), 
                    "Error opening file: %s", filename);
            return DTMF_ERROR_FILE_IO;
        }
        stream_ctx->is_stdin = false;
    } else {
        stream_ctx->input = stdin;
        stream_ctx->is_stdin = true;
    }
    
    // Initialize audio context
    stream_ctx->audio_ctx = malloc(sizeof(audio_context_t));
    if (!stream_ctx->audio_ctx) {
        if (!stream_ctx->is_stdin) fclose(stream_ctx->input);
        return DTMF_ERROR_MEMORY;
    }
    
    dtmf_result_t result = audio_context_init(stream_ctx->audio_ctx, config);
    if (result != DTMF_SUCCESS) {
        free(stream_ctx->audio_ctx);
        if (!stream_ctx->is_stdin) fclose(stream_ctx->input);
        return result;
    }
    
    // Set up audio context sample pointer
    stream_ctx->audio_ctx->samples = stream_ctx->frame_buffer;
    
    // Copy configuration
    stream_ctx->config = malloc(sizeof(dtmf_config_advanced_t));
    if (!stream_ctx->config) {
        audio_context_cleanup(stream_ctx->audio_ctx);
        free(stream_ctx->audio_ctx);
        if (!stream_ctx->is_stdin) fclose(stream_ctx->input);
        return DTMF_ERROR_MEMORY;
    }
    *stream_ctx->config = *config;
    
    stream_ctx->start_time = get_current_timestamp();
    
    dtmf_log(LOG_INFO, NULL, "Stream initialized successfully");
    return DTMF_SUCCESS;
}

/**
 * @brief Clean up stream processing context and free all resources
 * 
 * Properly closes files, frees allocated memory, and cleans up the
 * audio processing context. Safe to call multiple times.
 * 
 * @param stream_ctx Stream context to clean up (can be NULL)
 */
static void stream_cleanup(stream_context_t *stream_ctx) {
    if (!stream_ctx) return;
    
    if (stream_ctx->input && !stream_ctx->is_stdin) {
        fclose(stream_ctx->input);
    }
    
    if (stream_ctx->audio_ctx) {
        audio_context_cleanup(stream_ctx->audio_ctx);
        free(stream_ctx->audio_ctx);
    }
    
    free(stream_ctx->config);
    
    dtmf_log(LOG_INFO, NULL, "Stream cleanup completed");
}

/**
 * @brief Read one frame of audio samples from input stream
 * 
 * Reads exactly SAMPLES_PER_FRAME samples from the input stream.
 * Handles both file input and stdin streams. Updates the audio
 * context with the current sample count.
 * 
 * @param stream_ctx Stream context with initialized input
 * @return DTMF_SUCCESS on successful read, DTMF_ERROR_END_OF_STREAM when done,
 *         DTMF_ERROR_INSUFFICIENT_DATA for partial frames
 */
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
        dtmf_log(LOG_DEBUG, NULL, "Partial frame read: %zu/%d samples", 
                samples_read, SAMPLES_PER_FRAME);
    }
    
    // Audio quality validation if enabled
    if (stream_ctx->config->enable_audio_validation) {
        dtmf_result_t quality_result = validate_audio_quality(
            stream_ctx->frame_buffer, SAMPLES_PER_FRAME, 
            &stream_ctx->audio_ctx->quality);
            
        if (quality_result != DTMF_SUCCESS) {
            dtmf_log(LOG_WARNING, &DTMF_ERROR_CTX(quality_result), 
                    "Audio quality validation failed");
        } else {
            // Check quality thresholds
            if (stream_ctx->audio_ctx->quality.is_clipped) {
                dtmf_log(LOG_WARNING, NULL, "Audio clipping detected");
            }
            if (stream_ctx->audio_ctx->quality.is_too_quiet) {
                dtmf_log(LOG_DEBUG, NULL, "Audio level too low");
            }
        }
    }
    
    // DC offset removal if enabled
    if (stream_ctx->config->enable_dc_removal) {
        remove_dc_offset(stream_ctx->frame_buffer, SAMPLES_PER_FRAME);
    }
    
    // Automatic gain control if enabled
    if (stream_ctx->config->enable_agc) {
        apply_agc(stream_ctx->frame_buffer, SAMPLES_PER_FRAME, 5000.0); // Target RMS
    }
    
    stream_ctx->frame_count++;
    return DTMF_SUCCESS;
}

/**
 * @brief Print CSV header with system information
 * 
 * Outputs a descriptive header for the CSV results including system
 * capabilities and column definitions. Called once at program start.
 */
static void print_header() {
    printf("# Enhanced DTMF Stream Analysis Results\n");
    printf("# Frame: 40ms (320 samples @ 8kHz)\n");
    printf("# Features: Audio validation, DC removal, Enhanced logging\n");
    printf("#\n");
    printf("%-10s | %-15s | %-11s | %-4s | %-8s %-8s | %-10s\n", 
           "Timestamp", "Algorithm", "Window", "DTMF", "Low_Conf", "High_Conf", "Time_ms.us");
    printf("%-10s-+-%-15s-+-%-11s-+-%-4s-+-%-8s-%-8s-+-%-10s\n", 
           "----------", "---------------", "-----------", "----", "--------", "--------", "----------");
}

// =============================================================================
// MAIN PROCESSING LOOP
// =============================================================================

/**
 * @brief Display command-line usage information
 * 
 * Shows all available command-line options and usage examples
 * for the DTMF detection program.
 * 
 * @param program_name Name of the program executable
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [input_file] [config_file]\n", program_name);
    printf("\nOptions:\n");
    printf("  input_file   Audio file (PCM, 16-bit, 8kHz) or '-' for stdin\n");
    printf("  config_file  Configuration file (optional)\n");
    printf("\nExamples:\n");
    printf("  %s data.pcm              # Process file\n", program_name);
    printf("  cat data.pcm | %s -      # Process from stdin\n", program_name);
    printf("  %s data.pcm config.conf  # Process with config\n", program_name);
}

/**
 * @brief Main program entry point
 * 
 * Coordinates the entire DTMF detection process from initialization through
 * cleanup. Handles command-line arguments, configuration loading, stream
 * processing, and result output.
 * 
 * ## Process Flow:
 * 1. Parse command-line arguments and handle help requests
 * 2. Load configuration from file or use defaults
 * 3. Initialize stream processing and audio context
 * 4. Process audio frames in continuous loop
 * 5. Run enabled detection algorithms on each frame
 * 6. Output results in CSV format with timing information
 * 7. Clean up all resources before exit
 * 
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings
 * @return 0 on successful completion, non-zero on error
 */
int main(int argc, char **argv) {
    const char *filename = (argc > 1) ? argv[1] : "-";
    const char *config_file = (argc > 2) ? argv[2] : NULL;
    
    // Check for help request
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    
    // Initialize configuration
    dtmf_config_advanced_t config;
    get_default_config(&config);
    
    // Load configuration file if specified
    if (config_file) {
        dtmf_result_t config_result = load_config(config_file, &config);
        if (config_result != DTMF_SUCCESS) {
            fprintf(stderr, "Warning: Failed to load config file %s: %s\n", 
                   config_file, dtmf_error_string(config_result));
        }
    }
    
    // Set up logging
    dtmf_set_log_level(config.log_level);
    if (config.log_file) {
        dtmf_set_log_file(config.log_file);
    }
    
    dtmf_log(LOG_INFO, NULL, "Starting DTMF stream processing");
    
    // Initialize stream context
    stream_context_t stream_ctx;
    dtmf_result_t init_result = stream_init(&stream_ctx, filename, &config);
    if (init_result != DTMF_SUCCESS) {
        fprintf(stderr, "Failed to initialize stream: %s\n", dtmf_error_string(init_result));
        return 1;
    }
    
    // Define analysis strategies
    typedef struct {
        const char *name;
        dtmf_analysis_func_t analyze;
        bool enabled;
    } analysis_strategy_t;
    
    analysis_strategy_t strategies[] = {
        {"FFT", analyze_fft_strategy, config.enable_fft},
        {"Goertzel", analyze_goertzel_strategy, config.enable_goertzel},
        {"GoertzelTwist", analyze_goertzel_twist_strategy, config.enable_goertzel_twist},
        {"FilterBank", analyze_filterbank_strategy, config.enable_filterbank},
        {"FilterBankIIR", analyze_filterbank_iir_strategy, config.enable_filterbank_iir},
        {"Correlation", analyze_correlation_strategy, config.enable_correlation},
        {"MUSIC", analyze_music_strategy, config.enable_music}
    };
    
    const size_t num_strategies = sizeof(strategies) / sizeof(strategies[0]);
    
    // Count enabled strategies
    size_t enabled_strategies = 0;
    for (size_t i = 0; i < num_strategies; i++) {
        if (strategies[i].enabled) enabled_strategies++;
    }
    
    if (enabled_strategies == 0) {
        dtmf_log(LOG_ERROR, NULL, "No analysis algorithms enabled");
        stream_cleanup(&stream_ctx);
        return 1;
    }
    
    // Print header
    print_header();
    
    // Main processing loop
    dtmf_result_t frame_result;
    size_t total_frames = 0;
    double total_processing_time = 0.0;
    
    while ((frame_result = stream_read_frame(&stream_ctx)) == DTMF_SUCCESS) {
        // Calculate timestamp for this frame
        double frame_timestamp = (stream_ctx.frame_count - 1) * FRAME_MS / 1000.0;
        
        // Calculate total number of results (FFT with all windows + other algorithms)
        size_t fft_results = config.enable_fft ? NUM_WINDOW_STRATEGIES : 0;
        size_t other_results = enabled_strategies - (config.enable_fft ? 1 : 0);
        int total_results = fft_results + other_results;
        
        dtmf_analysis_result_t *results = calloc(total_results, sizeof(dtmf_analysis_result_t));
        if (!results) {
            dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(DTMF_ERROR_MEMORY), 
                    "Memory allocation failed for results");
            break;
        }
        
        int result_idx = 0;
        
        // Run FFT with all windows if enabled
        if (config.enable_fft) {
            for (size_t w = 0; w < NUM_WINDOW_STRATEGIES; w++) {
                dtmf_result_t status = analyze_fft_strategy(stream_ctx.audio_ctx, &DEFAULT_DTMF_CONFIG, 
                                                           &WINDOW_STRATEGIES[w], &results[result_idx]);
                if (status != DTMF_SUCCESS) {
                    dtmf_log(LOG_WARNING, &DTMF_ERROR_CTX(status), 
                            "FFT analysis failed with %s window", WINDOW_STRATEGIES[w].name);
                    // Fill with error indicators
                    results[result_idx].detected_key = '?';
                    results[result_idx].low_confidence = 0.0;
                    results[result_idx].high_confidence = 0.0;
                    results[result_idx].window_name = WINDOW_STRATEGIES[w].name;
                }
                total_processing_time += results[result_idx].timing.wall_time_s;
                result_idx++;
            }
        }
        
        // Run other enabled algorithms with rectangular window only
        for (size_t i = 1; i < num_strategies; i++) {
            if (!strategies[i].enabled) continue;
            
            dtmf_result_t status = strategies[i].analyze(stream_ctx.audio_ctx, &DEFAULT_DTMF_CONFIG,
                                                        &WINDOW_STRATEGIES[0], &results[result_idx]);
            if (status != DTMF_SUCCESS) {
                dtmf_log(LOG_WARNING, &DTMF_ERROR_CTX(status), 
                        "%s analysis failed", strategies[i].name);
                // Fill with error indicators
                results[result_idx].detected_key = '?';
                results[result_idx].low_confidence = 0.0;
                results[result_idx].high_confidence = 0.0;
                results[result_idx].window_name = "N/A";
            }
            total_processing_time += results[result_idx].timing.wall_time_s;
            result_idx++;
        }
        
        // Print results for this frame
        for (int i = 0; i < total_results; i++) {
            double timing_us = results[i].timing.wall_time_s * 1000000.0;  // Convert to microseconds
            int ms = (int)(timing_us / 1000.0);  // Extract milliseconds
            int us = (int)(timing_us) % 1000;    // Extract remaining microseconds
            const char *algorithm_name = "";
            const char *window_name;
            
            if (config.enable_fft && i < (int)NUM_WINDOW_STRATEGIES) {
                // FFT results
                algorithm_name = "FFT";
                window_name = WINDOW_STRATEGIES[i].name;
            } else {
                // Other algorithm results  
                int algo_idx = config.enable_fft ? (i - (int)NUM_WINDOW_STRATEGIES + 1) : i;
                
                // Find the enabled algorithm at this index
                int enabled_count = 0;
                for (size_t j = config.enable_fft ? 1 : 0; j < num_strategies; j++) {
                    if (strategies[j].enabled) {
                        if (enabled_count == algo_idx - (config.enable_fft ? 1 : 0)) {
                            algorithm_name = strategies[j].name;
                            break;
                        }
                        enabled_count++;
                    }
                }
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
        total_frames++;
        
        // Reset memory pool for next frame
        memory_pool_reset(stream_ctx.audio_ctx->mem_pool);
    }
    
    if (frame_result != DTMF_ERROR_END_OF_STREAM) {
        dtmf_log(LOG_ERROR, &DTMF_ERROR_CTX(frame_result), "Stream processing error");
        fprintf(stderr, "Stream processing error: %s\n", dtmf_error_string(frame_result));
    }
    
    // Print summary statistics
    dtmf_log(LOG_INFO, NULL, 
            "Processing completed: %zu frames, %.3f ms total processing time, %.3f ms avg per frame",
            total_frames, total_processing_time * 1000.0, 
            total_frames > 0 ? (total_processing_time * 1000.0 / total_frames) : 0.0);
    
    // Cleanup
    stream_cleanup(&stream_ctx);
    dtmf_set_log_file(NULL); // Close log file if opened
    
    return 0;
}
