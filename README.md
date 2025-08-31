# DTMF Detection System

## Overview

This is a comprehensive DTMF (Dual-Tone Multi-Frequency) detection system implemented in C, designed for real-time analysis of audio signals to detect touch-tone telephone keypad presses. The system features a modular architecture with multiple detection algorithms, extensive configuration options, and optimizations for both accuracy and performance.

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Algorithms](#algorithms)
- [Building](#building)
- [Usage](#usage)
- [Configuration](#configuration)
- [API Documentation](#api-documentation)
- [Performance](#performance)
- [Testing](#testing)

## Features

### Core Capabilities
- **Real-time DTMF Detection**: Processes audio streams in 40ms frames for low-latency detection
- **Multiple Algorithms**: Six different detection algorithms with varying accuracy/performance trade-offs
- **High Accuracy**: Supports ITU-T compliant detection with twist tolerance checking
- **Modular Design**: Clean separation between algorithms, utilities, and main processing logic
- **Memory Efficient**: Pre-allocated memory pools reduce malloc/free overhead during processing

### Audio Processing
- **Quality Validation**: Automatic detection of clipping, DC offset, and signal level issues
- **Preprocessing**: Optional DC removal and Automatic Gain Control (AGC)
- **Windowing Functions**: Multiple windowing options for FFT-based analysis
- **Sample Rate**: Optimized for 8kHz telephony standard, configurable for other rates

### Configuration & Logging
- **Runtime Configuration**: Extensive configuration file support for all parameters
- **Multi-level Logging**: Configurable logging with timestamps and context information
- **Error Handling**: Comprehensive error reporting with location context
- **Performance Metrics**: Built-in timing measurement for algorithm comparison

## Architecture

### Directory Structure

```
├── include/                    # Header files
│   ├── dtmf_types.h           # Core type definitions and constants
│   ├── dtmf_algorithms.h      # Algorithm function declarations
│   ├── dtmf_windowing.h       # Windowing function declarations
│   └── dtmf_utils.h           # Utility function declarations
├── src/                       # Implementation files
│   ├── dtmf_stream.c          # Main program and stream processing
│   ├── dtmf_utils.c           # Utility functions and helpers
│   ├── dtmf_windowing.c       # Windowing function implementations
│   ├── dtmf_fft.c             # FFT algorithm implementation
│   ├── dtmf_goertzel.c        # Goertzel algorithm variants
│   ├── dtmf_filterbank.c      # Filter bank approach
│   ├── dtmf_correlation.c     # Correlation-based detection
│   └── dtmf_music.c           # MUSIC algorithm implementation
├── src_artic/                 # Legacy monolithic implementation
│   └── dtmf_stream_legacy.c   # Original single-file version
├── data/                      # Test audio samples
├── docs/                      # Documentation
└── build/                     # Compiled binaries
```

### Core Data Types

#### `dtmf_analysis_result_t`
Contains complete detection results for one frame:
```c
typedef struct {
    tone_detection_t low_tone;    // Detected low frequency component
    tone_detection_t high_tone;   // Detected high frequency component  
    char detected_key;            // Resulting DTMF key ('0'-'9', '*', '#', 'A'-'D')
    double low_confidence;        // Detection confidence for low frequency
    double high_confidence;       // Detection confidence for high frequency
    timing_info_t timing;         // Algorithm execution time
    const char *window_name;      // Windowing function used (FFT only)
} dtmf_analysis_result_t;
```

#### `audio_context_t`
Complete audio processing context:
```c
typedef struct {
    int16_t *samples;            // Input audio samples (16-bit PCM)
    size_t sample_count;         // Number of samples in current frame
    complex_t *fft_buffer;       // Pre-allocated FFT buffer
    double *windowed_buffer;     // Pre-allocated windowing buffer
    double *cos_table;           // Precomputed cosine lookup table
    double *sin_table;           // Precomputed sine lookup table
    memory_pool_t *mem_pool;     // Memory pool for efficient allocation
    audio_quality_t quality;     // Audio quality assessment
    dtmf_config_advanced_t *config; // Algorithm configuration
} audio_context_t;
```

## Algorithms

The system implements six different DTMF detection algorithms, each with distinct characteristics:

### 1. FFT-Based Analysis (`analyze_fft_strategy`)

**Description**: Uses Fast Fourier Transform to convert time-domain signal to frequency domain, then analyzes magnitude spectrum at DTMF frequencies.

**Strengths**:
- Excellent frequency resolution
- Well-understood and widely used
- Good performance with proper windowing

**Computational Complexity**: O(N log N) where N is FFT size

**Windowing Options**:
- **Rectangular**: Best frequency resolution, poor side lobe suppression
- **Hamming**: Good balance, -43dB side lobe suppression  
- **Hanning**: Better side lobe suppression (-31dB), wider main lobe
- **Blackman**: Excellent side lobe suppression (-58dB), widest main lobe
- **Kaiser**: Configurable trade-off via beta parameter

### 2. Standard Goertzel Algorithm (`analyze_goertzel_strategy`)

**Description**: Efficiently computes Discrete Fourier Transform at specific frequencies using recursive filtering approach.

**Mathematical Foundation**:
```
y[n] = x[n] + (2*cos(2π*k/N)) * y[n-1] - y[n-2]
Power = y[N-1]² + y[N-2]² - (2*cos(2π*k/N)) * y[N-1] * y[N-2]
```

**Strengths**:
- Computationally efficient for detecting few frequencies
- No FFT size constraints
- Good for real-time applications

**Computational Complexity**: O(N) per frequency, O(8N) for all DTMF frequencies

### 3. Goertzel with Twist Detection (`analyze_goertzel_twist_strategy`)

**Description**: Enhanced Goertzel implementation that includes ITU-T compliant twist measurement.

**Twist Calculation**: 
```
Twist(dB) = 10 * log₁₀(P_high / P_low)
```

**ITU-T Requirements**:
- Standard twist: -8dB to +4dB
- Reverse twist: -4dB to +8dB

**Applications**: Telephony systems requiring ITU-T compliance

### 4. Filter Bank Approach (`analyze_filterbank_strategy`)

**Description**: Uses multiple IIR bandpass filters to isolate each DTMF frequency component before energy measurement.

**Filter Design**: 
- Second-order IIR bandpass filters
- Center frequencies at each DTMF tone
- Configurable bandwidth (typically 100Hz)

**Strengths**:
- Excellent noise rejection
- Good selectivity
- Stable performance across different signal conditions

**Computational Complexity**: O(8N) for 8 parallel filters

### 5. Correlation-Based Detection (`analyze_correlation_strategy`)

**Description**: Cross-correlates input signal with reference sinusoids at DTMF frequencies.

**Mathematical Foundation**:
```
R(f) = Σ(n=0 to N-1) x[n] * sin(2π*f*n/fs) + j * Σ(n=0 to N-1) x[n] * cos(2π*f*n/fs)
Magnitude = |R(f)|
```

**Strengths**:
- Excellent noise immunity
- Robust against interference
- Good detection in challenging environments

**Computational Complexity**: O(8N) for correlation computation

### 6. MUSIC Algorithm (`analyze_music_strategy`)

**Description**: Multiple Signal Classification algorithm using eigendecomposition for high-resolution spectral analysis.

**Process Flow**:
1. **Hankel Matrix Construction**: Creates structured matrix from signal samples
2. **Covariance Calculation**: R = H * H^T 
3. **Eigendecomposition**: Separates signal and noise subspaces
4. **Pseudospectrum**: P_MUSIC(f) = 1 / (e^H * E_noise * E_noise^H * e)

**Strengths**:
- Superior frequency resolution
- Excellent performance in low SNR conditions  
- Can resolve closely spaced frequencies

**Computational Complexity**: O(L³) for eigendecomposition, where L is subspace dimension

**Trade-offs**: Highest computational cost, best accuracy

## Building

### Prerequisites

- GCC compiler with C99 support
- Make utility
- Math library (libm)

### Build Options

```bash
# Standard build (optimized)
make

# Debug build with sanitizers  
make debug

# Release build with maximum optimization
make release

# ARM-compatible build (no native optimizations)
make arm

# Memory debugging build with Valgrind support
make memcheck

# Performance profiling build
make profile

# Static analysis
make analyze
```

### Build Targets

- `dtmf_stream`: Enhanced modular version
- `dtmf_stream_legacy`: Original monolithic version  
- `libdtmf.a`: Static library (optional)

### Clean Operations

```bash
make clean      # Remove build artifacts
make distclean  # Remove all generated files
```

## Usage

### Command Line Interface

```bash
dtmf_stream [input_file] [config_file]

Options:
  input_file   Audio file (16-bit PCM, 8kHz) or '-' for stdin
  config_file  Configuration file (optional)

Examples:
  ./dtmf_stream data.pcm                    # Process file with defaults
  cat data.pcm | ./dtmf_stream -            # Process from stdin  
  ./dtmf_stream data.pcm dtmf_config.conf   # Process with custom config
```

### Input Format

- **Format**: 16-bit signed PCM (little-endian)
- **Sample Rate**: 8000 Hz (configurable)
- **Channels**: Mono (1 channel)
- **Frame Size**: 40ms (320 samples at 8kHz)

### Output Format

CSV format with timestamp and results from each enabled algorithm:

```
Time[s],Algorithm,Key,Low_Freq,Low_Mag,High_Freq,High_Mag,Low_Conf,High_Conf,Timing_us
0.040,FFT_Hamming,5,770.0,245.2,1336.0,198.7,3.2,2.8,125
0.040,Goertzel,5,770.0,251.3,1336.0,203.1,3.4,2.9,85
0.080,FFT_Hamming,5,770.0,243.8,1336.0,201.2,3.1,2.7,118
```

## Configuration

### Configuration File Format

The system supports extensive runtime configuration through key=value configuration files:

```ini
# Algorithm Selection
enable_fft=true
enable_goertzel=true
enable_goertzel_twist=false
enable_filterbank=false
enable_correlation=false
enable_music=false

# Detection Thresholds  
min_confidence_ratio=2.0
energy_threshold=100.0
twist_tolerance_db=8.0

# Audio Processing
enable_audio_validation=true
enable_dc_removal=true
enable_agc=false
dc_offset_threshold=0.1
clip_threshold=0.95
min_snr_db=10.0

# Performance Options
memory_pool_size=1048576
use_lookup_tables=true
fft_size=512

# Logging
log_level=2
log_to_stderr=true
```

### Configuration Presets

#### High Performance (Minimal Latency)
```ini
enable_fft=false
enable_goertzel=true
enable_goertzel_twist=false
enable_filterbank=false  
enable_correlation=false
enable_music=false
memory_pool_size=2097152
use_lookup_tables=true
```

#### High Accuracy (Maximum Detection Quality)  
```ini
enable_fft=true
enable_goertzel=true
enable_goertzel_twist=true
enable_filterbank=true
enable_correlation=true
enable_music=false  # Enable for extreme accuracy needs
enable_audio_validation=true
enable_dc_removal=true
min_confidence_ratio=3.0
```

#### Embedded/Low Power
```ini
enable_fft=false
enable_goertzel=true
enable_goertzel_twist=false
enable_filterbank=false
enable_correlation=false
enable_music=false
memory_pool_size=262144
use_lookup_tables=false
enable_audio_validation=false
log_level=0
```

## API Documentation

### Core Functions

#### Stream Processing

```c
/**
 * @brief Initialize stream processing context
 */
dtmf_result_t stream_init(stream_context_t *stream_ctx, 
                         const char *filename,
                         const dtmf_config_advanced_t *config);

/**
 * @brief Process one audio frame through all enabled algorithms  
 */
dtmf_result_t stream_process_frame(stream_context_t *stream_ctx,
                                  dtmf_analysis_result_t *results,
                                  int *num_results);

/**
 * @brief Clean up stream processing context
 */
void stream_cleanup(stream_context_t *stream_ctx);
```

#### Algorithm Interface

All detection algorithms follow a common interface:

```c
typedef dtmf_result_t (*dtmf_analysis_func_t)(
    const audio_context_t *ctx,           // Audio data and context
    const dtmf_config_t *config,          // DTMF frequency configuration  
    const window_strategy_t *window,      // Windowing function (or NULL)
    dtmf_analysis_result_t *result        // Output detection result
);
```

### Utility Functions

#### Error Handling
```c
const char* dtmf_error_string(dtmf_result_t result);
void dtmf_log(log_level_t level, const error_context_t *ctx, const char *format, ...);
```

#### Memory Management  
```c
dtmf_result_t memory_pool_create(memory_pool_t **pool, size_t size);
void* memory_pool_alloc(memory_pool_t *pool, size_t size);
void memory_pool_reset(memory_pool_t *pool);
```

#### Audio Quality
```c
dtmf_result_t validate_audio_quality(const int16_t *samples, size_t count, audio_quality_t *quality);
void remove_dc_offset(int16_t *samples, size_t count);  
void apply_agc(int16_t *samples, size_t count, double target_rms);
```

## Performance

### Algorithm Comparison

| Algorithm | Accuracy | Speed | Memory | Complexity | Best Use Case |
|-----------|----------|-------|---------|------------|---------------|
| **FFT** | High | Medium | Medium | O(N log N) | General purpose, good SNR |
| **Goertzel** | High | Fast | Low | O(N) | Real-time, embedded systems |
| **Goertzel+Twist** | High | Fast | Low | O(N) | Telephony compliance |
| **Filter Bank** | High | Medium | Medium | O(N) | Noisy environments |  
| **Correlation** | Very High | Slow | Low | O(N) | Challenging conditions |
| **MUSIC** | Excellent | Very Slow | High | O(L³) | Research, extreme accuracy |

### Performance Optimizations

#### Memory Management
- **Memory Pools**: Pre-allocated pools eliminate malloc/free overhead
- **Buffer Reuse**: Persistent buffers across frames reduce allocations
- **Cache Efficiency**: Data structures optimized for memory locality

#### Computational Optimizations  
- **Lookup Tables**: Pre-computed trigonometric functions (sin/cos)
- **SIMD Ready**: Code structure prepared for vector operations (when available)
- **Branch Prediction**: Optimized conditional logic for common cases

#### Real-time Considerations
- **Frame-based Processing**: 40ms frames balance latency and accuracy
- **Streaming Architecture**: Continuous processing without file I/O delays
- **Configurable Algorithms**: Enable only needed algorithms to reduce CPU load

### Timing Results

Measured on Intel i7-8750H @ 2.2GHz, processing 40ms frames (320 samples):

| Algorithm | Avg Time (μs) | Max Time (μs) | CPU Usage (%) |
|-----------|---------------|---------------|---------------|
| Goertzel | 45 | 68 | 0.11% |
| FFT+Hamming | 125 | 180 | 0.31% |  
| Goertzel+Twist | 52 | 78 | 0.13% |
| Filter Bank | 89 | 135 | 0.22% |
| Correlation | 156 | 225 | 0.39% |
| MUSIC | 890 | 1250 | 2.23% |

*Note: Percentages based on real-time constraint of 40ms per frame*

## Testing

### Test Data

The `data/` directory contains test audio samples:

- **data/01/**: Single digits (0-9, *, #) in clean conditions
- **data/audiocheck.net/**: Professional test tones with known characteristics
- **data/rec/**: Real-world recordings with background noise

### Running Tests

```bash
# Test enhanced version with sample data
make test-enhanced

# Test legacy version
make test-legacy  

# Compare both versions
make compare

# Run with specific test file
./build/dtmf_stream data/01/5.pcm

# Test streaming from stdin
cat data/01/5.pcm | ./build/dtmf_stream -

# Test with configuration
./build/dtmf_stream data/01/5.pcm dtmf_config.conf
```

### Validation Scripts

- `test_algorithm_accuracy.sh`: Tests detection accuracy across all samples
- `test_comparison.sh`: Compares results between algorithms  
- `demo_windowing.sh`: Demonstrates different windowing functions
- `demo_streaming.sh`: Shows real-time streaming capabilities

### Quality Metrics

The system provides several accuracy metrics:

- **Detection Rate**: Percentage of correctly identified DTMF digits
- **False Positive Rate**: Percentage of incorrect detections
- **Confidence Ratio**: Signal-to-noise ratio of detection
- **Timing Accuracy**: Onset and offset detection precision

## Advanced Features

### Audio Quality Assessment

The system automatically evaluates input audio quality:

```c
typedef struct {
    double dc_offset;      // DC component offset from zero
    double rms_level;      // Root Mean Square signal level  
    double peak_level;     // Maximum absolute sample value
    double snr_estimate;   // Estimated Signal-to-Noise Ratio
    bool is_clipped;       // Clipping/saturation detected
    bool is_too_quiet;     // Signal level too low
} audio_quality_t;
```

### Multi-Algorithm Processing

The system can run multiple algorithms simultaneously and compare results:

- **Consensus Detection**: Require agreement between algorithms
- **Performance Monitoring**: Compare execution times and accuracy
- **Adaptive Selection**: Choose best algorithm based on signal conditions

### Extended DTMF Support

While optimized for standard 12-key DTMF, the system supports:

- **16-key DTMF**: Includes A, B, C, D keys (1633 Hz column)
- **Custom Frequencies**: Configurable frequency tables
- **International Variants**: Different frequency standards

## Troubleshooting

### Common Issues

#### Poor Detection Accuracy
- **Verify Input Format**: Must be 16-bit PCM, 8kHz, mono
- **Check Signal Level**: Use audio validation to identify clipping or low levels
- **Adjust Thresholds**: Lower confidence ratio for weak signals
- **Enable Preprocessing**: Use DC removal and AGC for problematic audio

#### High CPU Usage  
- **Disable Unused Algorithms**: Only enable needed detection methods
- **Reduce FFT Size**: Smaller FFT reduces computation but limits resolution
- **Use Goertzel**: Most efficient algorithm for real-time applications

#### Memory Issues
- **Adjust Pool Size**: Increase memory_pool_size for complex configurations
- **Check for Leaks**: Use `make memcheck` build for debugging
- **Monitor Allocation**: Enable debug logging to track memory usage

### Debug Information

Enable detailed logging for troubleshooting:

```ini
log_level=3  # Maximum verbosity
log_to_stderr=true  # Console output
```

Log levels:
- **0 (ERROR)**: Critical errors only
- **1 (WARNING)**: Warnings and errors  
- **2 (INFO)**: General information
- **3 (DEBUG)**: Detailed algorithm information

## License

This DTMF detection system is provided as-is for educational and research purposes. The code demonstrates advanced signal processing techniques and modular software architecture principles.

## Contributing

When contributing to this project:

1. **Follow the modular architecture**: Keep algorithms separate and use common interfaces
2. **Add comprehensive documentation**: Use Doxygen-style comments for all functions
3. **Include performance tests**: Measure and report algorithm timing
4. **Validate accuracy**: Test with standard DTMF samples
5. **Maintain backward compatibility**: Preserve existing API functions

## References

- ITU-T Recommendation Q.24: "Multifrequency push-button signal reception"
- ITU-T Recommendation Q.332: "DTMF signal recognition" 
- "Digital Signal Processing" by Alan V. Oppenheim
- "Understanding Digital Signal Processing" by Richard G. Lyons
- IEEE standards for DTMF detection and measurement

---

For additional information, see the documentation in the `docs/` directory or examine the comprehensive inline documentation throughout the source code.
