# Simple DTMF Decoder

This is a C port of the HTML/JavaScript DTMF decoder found in `scr_html/`. It reads raw PCM audio data from stdin and detects DTMF (Dual-Tone Multi-Frequency) tones, commonly used in telephone systems.

## Features

- Reads 16-bit PCM audio data from stdin
- Uses Goertzel algorithm for efficient frequency detection
- Applies Blackman windowing for improved accuracy
- Configurable energy threshold for sensitivity adjustment
- Real-time output display similar to the web version
- Decodes all 16 standard DTMF digits: 0-9, A-D, *, #

## Usage

```bash
# Basic usage
cat audio.pcm | ./build/simple

# With custom threshold (0.0 to 1.0, default 0.050)
cat audio.pcm | ./build/simple 0.01

# Test with sample data
make test-simple
```

## Audio Format

The program expects:
- 16-bit signed PCM samples (little-endian)
- 8000 Hz sample rate
- Mono (single channel)

## Output Format

The program displays:
- Real-time detection with dots (.) for silence and characters for detected digits
- Timeline with second markers (1s, 2s, etc.)
- Final decoded sequence at the end

Example output:
```
1s .11..222..33..
2s ....

Decoded: 123
```

## Algorithm

The decoder uses:
1. **Goertzel Algorithm**: Efficient single-frequency DFT for detecting specific DTMF frequencies
2. **Blackman Window**: Applied to each 320-sample frame (40ms) for better frequency resolution
3. **Dual-tone Detection**: Requires both low and high frequency components above threshold
4. **Energy Normalization**: Normalizes energy by total signal power to handle varying amplitude

## Comparison with Original

This C implementation closely follows the JavaScript version's algorithm:
- Same 320-sample frame size (40ms at 8kHz)
- Same Blackman windowing function
- Same Goertzel algorithm implementation
- Same energy thresholding approach
- Same DTMF frequency table and character mapping

The main differences:
- Reads from stdin instead of audio files via Web Audio API
- Uses 16-bit PCM instead of floating-point samples
- Command-line threshold parameter instead of web UI
- Simple console output instead of HTML display

## Building

```bash
# Build just the simple decoder
make simple

# Build everything including simple
make all

# Clean build
make clean && make simple
```
