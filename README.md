
# DTMF Compare

This project analyzes DTMF (Dual-tone multi-frequency) signals from PCM files using FFT and Goertzel algorithms.

## Requirements

- GCC (for building)
- sox (for audio conversion)
- PulseAudio (optional, for playback)
- aplay (for playback)

## Usage

### 1. Convert WAV to PCM

```sh
sox input.wav -t raw -r 8000 -e signed -b 16 -c 1 output.pcm
```


### 2. Build the Program with CMake

```sh
cmake -S . -B build
cmake --build build
```

The executable will be located in `build/dtmf_compare`.

### 3. Run the DTMF Compare Tool

```sh
./build/dtmf_compare sample.pcm 8000 40 1024
```
- `sample.pcm`: Input PCM file
- `8000`: Sample rate (Hz)
- `40`: Window size (ms)
- `1024`: FFT size

### 4. (Optional) Play Audio

Set up PulseAudio and play a WAV file:
```sh
export PULSE_SERVER=tcp:127.0.0.1:4713
aplay file.wav
```

## References

- [PulseAudio Windows Support](https://www.freedesktop.org/wiki/Software/PulseAudio/Ports/Windows/Support/)
- [PulseAudio 1.1 Download](http://bosmans.ch/pulseaudio/pulseaudio-1.1.zip)