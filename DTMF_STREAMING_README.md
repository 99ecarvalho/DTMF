# DTMF Streaming Analyzer

A real-time streaming DTMF (Dual-Tone Multi-Frequency) tone detection system that processes PCM audio in 40ms frames and outputs consolidated results in table format.

## Features

- **Real-time streaming**: Processes audio from stdin or static files
- **Low latency**: 40ms frame processing (320 samples at 8kHz) 
- **Multiple algorithms**: FFT, Goertzel, Goertzel+Twist, FilterBank, Correlation, MUSIC
- **Consolidated output**: Single row per frame with timestamp and all algorithm results
- **Robust streaming**: Handles continuous input without buffering entire files

## Usage

### Build
```bash
make stream
# or
make all  # builds both original and streaming versions
```

### Static File Processing
```bash
./build/dtmf_stream input.pcm
```

### Streaming from stdin
```bash
# From a file
cat audio.pcm | ./build/dtmf_stream

# From live audio recording (Linux ALSA)
arecord -f S16_LE -r 8000 -c 1 | ./build/dtmf_stream

# From pipe
head -c 12800 data/5.pcm | ./build/dtmf_stream
```

## Input Requirements

- **Format**: PCM 16-bit signed little-endian
- **Sample Rate**: 8000 Hz
- **Channels**: 1 (mono)
- **Frame Size**: 40ms (320 samples)

## Output Format

The analyzer outputs multiple lines per processed frame, with one line for each algorithm:

```
Timestamp  | Algorithm       | DTMF | Low_Conf High_Conf | Time_ms 
-----------+-----------------+------+-------------------+---------
     0.000 | FFT             | 5    |    2.989    4.469 |    0.017
     0.000 | Goertzel        | 5    |    2.398    5.991 |    0.006
     0.000 | GoertzelTwist   | 5    |    2.398    5.991 |    0.005
     0.000 | FilterBank      | 5    |    2.398    5.991 |    0.005
     0.000 | Correlation     | 5    |    2.440    4.120 |    0.024
     0.000 | MUSIC           | 5    |    2.989    4.469 |    0.006

     0.040 | FFT             | 5    |    8.663   17.156 |    0.006
     0.040 | Goertzel        | 5    |    7.870   12.034 |    0.005
```

### Columns

- **Timestamp**: Time in seconds from start of stream (format: SSSS.SSS, up to 1 hour)
- **Algorithm**: Analysis algorithm name (15 chars width)
- **DTMF**: Detected DTMF character ('0'-'9', '*', '#', 'A'-'D', or '?' for uncertain)
- **Low_Conf**: Low frequency confidence ratio (8.3 format)
- **High_Conf**: High frequency confidence ratio (8.3 format)  
- **Time_ms**: Processing time for this algorithm in milliseconds (8.3 format)

Each timestamp group is separated by a blank line for readability.

## Algorithms

1. **FFT with Zero-Padding**: Frequency domain analysis using 512-point FFT
2. **Goertzel Algorithm**: Efficient single-frequency DFT computation
3. **Goertzel with Twist Check**: Enhanced Goertzel with amplitude ratio validation
4. **Filter Bank + Energy Detection**: Bandpass filtering with energy measurement
5. **Correlation-Based Detection**: Normalized cross-correlation with reference tones
6. **MUSIC Algorithm**: Multiple Signal Classification (simplified for streaming)

## Performance

- **Latency**: ~40ms per frame (real-time capable)
- **Throughput**: Processes 8000 samples/second input
- **Memory**: Low memory footprint, no full-file buffering
- **CPU**: Optimized for continuous operation

## Testing

```bash
# Test with sample files
make test-stream

# Test streaming from stdin
make test-stream-stdin

# Run full demonstration
make demo-stream
./demo_streaming.sh
```

## Real-world Examples

### Live Audio Recording
```bash
# Linux (ALSA)
arecord -f S16_LE -r 8000 -c 1 -t raw | ./build/dtmf_stream

# macOS (with sox)
sox -t coreaudio -r 8000 -c 1 -b 16 default -t raw - | ./build/dtmf_stream

# Windows (with ffmpeg)
ffmpeg -f dshow -i audio="Microphone" -ar 8000 -ac 1 -f s16le - | dtmf_stream.exe
```

### Network Streaming
```bash
# Receiver side
nc -l 8080 | ./build/dtmf_stream

# Sender side  
cat audio.pcm | nc target_host 8080
```

### File Processing with Progress
```bash
# Process large file with progress indication
pv large_audio.pcm | ./build/dtmf_stream > results.txt
```

## Integration

The streaming analyzer can be integrated into:

- **Telephony systems**: Real-time DTMF detection in VoIP applications
- **Audio processing pipelines**: As a component in larger audio analysis systems
- **Monitoring systems**: Continuous DTMF tone detection and logging
- **Interactive applications**: Real-time user input via audio tones

## Comparison with Original

| Feature | Original `dtmf.c` | Streaming `dtmf_stream.c` |
|---------|------------------|---------------------------|
| Input | Static file only | File or stdin streaming |
| Processing | Entire file at once | Frame-by-frame (40ms) |
| Output | Per-algorithm breakdown | Consolidated table format |
| Latency | High (entire file) | Low (40ms frames) |
| Memory | Loads full file | Constant small buffer |
| Real-time | No | Yes |
| Windowing | 5 window types | Rectangular (streaming optimized) |

## Architecture

The streaming version maintains the same core algorithms as the original but adapts them for continuous operation:

- **Frame-based processing**: 320-sample frames with zero-padding for short frames
- **Persistent state**: Minimal state between frames for efficiency  
- **Streaming I/O**: Non-blocking stdin handling for continuous operation
- **Consolidated output**: Single-line results for easy parsing/monitoring

## Dependencies

- **Standard C99**: No external DSP libraries required
- **Math library**: Link with `-lm`
- **POSIX**: For timing and I/O operations

Built and tested on Linux, should work on macOS and Windows with appropriate toolchain.
