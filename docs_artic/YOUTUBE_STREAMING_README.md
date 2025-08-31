# YouTube Audio Streaming for DTMF Analysis

The `youtube_stream.py` script allows you to stream audio directly from YouTube videos and pipe it to the DTMF analyzer without downloading the entire video.

## Prerequisites

Install the required dependency:
```bash
pip install yt-dlp
```

Make sure you have `ffmpeg` installed (usually available in most Linux distributions):
```bash
# Ubuntu/Debian
sudo apt install ffmpeg

# CentOS/RHEL/Fedora
sudo yum install ffmpeg  # or dnf install ffmpeg
```

## Usage

### Basic Usage
```bash
python3 youtube_stream.py "<youtube_url>" | ./build/dtmf_stream
```

### Examples

**Stream a YouTube video for DTMF analysis:**
```bash
python3 youtube_stream.py "https://www.youtube.com/watch?v=dQw4w9WgXcQ" | ./build/dtmf_stream
```

**Stream with shortened YouTube URL:**
```bash
python3 youtube_stream.py "https://youtu.be/dQw4w9WgXcQ" | ./build/dtmf_stream
```

**Stream and save results to a file:**
```bash
python3 youtube_stream.py "https://www.youtube.com/watch?v=dQw4w9WgXcQ" | ./build/dtmf_stream > analysis.txt
```

**Stream only the first 30 seconds (using timeout):**
```bash
timeout 30s python3 youtube_stream.py "https://www.youtube.com/watch?v=dQw4w9WgXcQ" | ./build/dtmf_stream
```

## How It Works

1. **URL Extraction**: Uses `yt-dlp` to extract the direct audio stream URL from the YouTube video
2. **Audio Conversion**: Uses `ffmpeg` to:
   - Stream the audio without downloading the entire file
   - Convert to 8kHz mono signed 16-bit PCM format
   - Output to stdout in real-time
3. **DTMF Analysis**: The PCM audio stream is piped directly to `dtmf_stream` for real-time analysis

## Format Details

The script converts the audio to the exact format required by `dtmf_stream`:
- **Sample Rate**: 8kHz
- **Channels**: Mono (1 channel)
- **Format**: Signed 16-bit little-endian PCM
- **Streaming**: Real-time, no temporary files

## Error Handling

The script handles various error conditions:
- Invalid YouTube URLs
- Network connectivity issues
- Missing dependencies (`yt-dlp` or `ffmpeg`)
- Audio stream extraction failures
- Broken pipe scenarios (when `dtmf_stream` exits early)

## Performance

- **Memory Efficient**: No temporary file storage, everything is streamed
- **Low Latency**: 1KB buffer chunks for responsive streaming
- **Network Optimized**: Only downloads audio data as needed
- **CPU Efficient**: Leverages hardware-accelerated audio decoding when available

## Troubleshooting

**"yt-dlp not found" error:**
```bash
pip install yt-dlp
```

**"ffmpeg not found" error:**
```bash
sudo apt install ffmpeg  # Ubuntu/Debian
```

**Network or video access issues:**
- Check your internet connection
- Verify the YouTube URL is valid and accessible
- Some videos may be geo-restricted or have access limitations

**Stream interruption:**
- Use Ctrl+C to stop streaming gracefully
- The script handles broken pipes when `dtmf_stream` exits early
