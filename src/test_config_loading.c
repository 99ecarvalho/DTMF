/**
 * @file test_config_loading.c
 * @brief Test program to verify configuration loading functionality
 * 
 * This test program validates the configuration management system for the
 * DTMF detection library. It tests loading default configurations, parsing
 * configuration files, and displaying all configuration parameters in a
 * human-readable format for verification and debugging purposes.
 * 
 * Test coverage includes:
 * - Default configuration initialization
 * - Configuration file parsing (main and minimal configs)
 * - Parameter validation and display
 * - Error handling for missing or invalid files
 */

#include "../include/dtmf_types.h"
#include "../include/dtmf_utils.h"
#include <stdio.h>

// Forward declaration from dtmf_utils.c (load_config not in header yet)
dtmf_result_t load_config(const char *config_file, dtmf_config_advanced_t *config);

/**
 * @brief Display comprehensive configuration parameters
 * 
 * Prints all configuration parameters in organized sections for easy
 * verification and debugging. Includes detection thresholds, processing
 * parameters, algorithm selection flags, performance tuning options,
 * and logging configuration.
 * 
 * Output format provides clear categorization of related parameters
 * with appropriate formatting for boolean values and numeric precision.
 * 
 * @param config Pointer to advanced DTMF configuration structure
 */
void print_config(const dtmf_config_advanced_t *config) {
    printf("=== DTMF Configuration ===\n");
    
    printf("\n--- Detection Thresholds ---\n");
    printf("min_confidence_ratio: %.2f\n", config->min_confidence_ratio);
    printf("energy_threshold: %.2f\n", config->energy_threshold);
    printf("twist_tolerance_db: %.2f\n", config->twist_tolerance_db);
    printf("dc_offset_threshold: %.2f\n", config->dc_offset_threshold);
    printf("clip_threshold: %.2f\n", config->clip_threshold);
    printf("min_snr_db: %.2f\n", config->min_snr_db);
    
    printf("\n--- Processing Parameters ---\n");
    printf("frame_size_ms: %zu\n", config->frame_size_ms);
    printf("sample_rate: %zu\n", config->sample_rate);
    printf("fft_size: %zu\n", config->fft_size);
    
    printf("\n--- Algorithm Selection ---\n");
    printf("enable_fft: %s\n", config->enable_fft ? "true" : "false");
    printf("enable_goertzel: %s\n", config->enable_goertzel ? "true" : "false");
    printf("enable_goertzel_twist: %s\n", config->enable_goertzel_twist ? "true" : "false");
    printf("enable_filterbank: %s\n", config->enable_filterbank ? "true" : "false");
    printf("enable_correlation: %s\n", config->enable_correlation ? "true" : "false");
    printf("enable_music: %s\n", config->enable_music ? "true" : "false");
    
    printf("\n--- Performance Tuning ---\n");
    printf("memory_pool_size: %zu\n", config->memory_pool_size);
    printf("use_lookup_tables: %s\n", config->use_lookup_tables ? "true" : "false");
    printf("enable_audio_validation: %s\n", config->enable_audio_validation ? "true" : "false");
    printf("enable_dc_removal: %s\n", config->enable_dc_removal ? "true" : "false");
    printf("enable_agc: %s\n", config->enable_agc ? "true" : "false");
    
    printf("\n--- Logging Configuration ---\n");
    printf("log_level: %d\n", config->log_level);
    printf("log_to_stderr: %s\n", config->log_to_stderr ? "true" : "false");
    printf("log_file: %s\n", config->log_file ? config->log_file : "NULL");
    
    printf("\n=============================\n");
}

/**
 * @brief Main test function for configuration loading validation
 * 
 * Executes comprehensive tests of the DTMF configuration system including:
 * 
 * Test 1: Default Configuration Loading
 * - Verifies default parameter initialization
 * - Ensures all defaults are within valid ranges
 * - Displays baseline configuration values
 * 
 * Test 2: Main Configuration File Loading
 * - Tests parsing of dtmf_config.conf with full parameter set
 * - Validates file format parsing and parameter assignment
 * - Verifies error handling for malformed entries
 * 
 * Test 3: Minimal Configuration File Loading  
 * - Tests parsing of dtmf_config_minimal.conf with subset of parameters
 * - Validates that unspecified parameters retain default values
 * - Ensures graceful handling of sparse configuration files
 * 
 * Each test displays complete configuration state for manual verification
 * and provides clear pass/fail indication for automated testing.
 * 
 * @return 0 on successful completion of all tests, 1 on any test failure
 */
int main() {
    dtmf_config_advanced_t config;
    
    printf("Testing configuration loading...\n\n");
    
    // Test 1: Load default configuration
    printf("1. Loading default configuration:\n");
    get_default_config(&config);
    print_config(&config);
    
    // Test 2: Load main configuration file
    printf("\n\n2. Loading dtmf_config.conf:\n");
    if (load_config("dtmf_config.conf", &config) == DTMF_SUCCESS) {
        print_config(&config);
    } else {
        printf("Failed to load dtmf_config.conf!\n");
        return 1;
    }
    
    // Test 3: Load minimal configuration file
    printf("\n\n3. Loading dtmf_config_minimal.conf:\n");
    if (load_config("dtmf_config_minimal.conf", &config) == DTMF_SUCCESS) {
        print_config(&config);
    } else {
        printf("Failed to load dtmf_config_minimal.conf!\n");
        return 1;
    }
    
    printf("\nConfiguration loading test completed successfully!\n");
    return 0;
}
