#!/usr/bin/env python3
"""
youtube_stream.py - Stream YouTube audio or local video files for DTMF analysis

This script extracts audio from a YouTube video URL or local video file and streams it to stdout
in a format suitable for piping to dtmf_stream (8kHz mono signed 16-bit PCM).

Usage:
    python3 youtube_stream.py <youtube_url> | ./build/dtmf_stream
    python3 youtube_stream.py --local-file <video_path> | ./build/dtmf_stream

Requirements:
    pip install yt-dlp

The script uses yt-dlp to extract the audio stream URL and ffmpeg to convert
the audio to the required format without downloading the entire video.
For local files, it directly processes the video file with ffmpeg.
"""

import sys
import subprocess
import json
import re
import os
from urllib.parse import urlparse, parse_qs


class YouTubeStreamError(Exception):
    """Custom exception for YouTube streaming errors."""
    pass


class AudioProcessingError(Exception):
    """Custom exception for audio processing errors."""
    pass


# Constants for bot detection
BOT_DETECTION_SIGN_IN = "Sign in to confirm"
BOT_DETECTION_BOT = "bot"

# Constants for format selection
AUDIO_FORMAT_PRIMARY = "bestaudio/best"
AUDIO_FORMAT_FALLBACK = "worst/bestaudio"

def is_youtube_url(url):
    """Check if the URL is a valid YouTube URL."""
    youtube_patterns = [
        r'(https?://)?(www\.)?(youtube\.com/watch\?v=|youtu\.be/|youtube\.com/embed/)',
        r'(https?://)?(www\.)?youtube\.com/.*[?&]v='
    ]
    
    for pattern in youtube_patterns:
        if re.match(pattern, url):
            return True
    return False

def get_audio_stream_url(youtube_url, cookies_file=None):
    """Extract the direct audio stream URL using yt-dlp."""
    try:
        # Use yt-dlp to get the best audio stream URL with additional options
        # to help bypass bot detection
        cmd = [
            'yt-dlp',
            '--format', AUDIO_FORMAT_PRIMARY,
            '--get-url',
            '--no-warnings',
            '--user-agent', 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
            '--extractor-args', 'youtube:player_client=web,mweb',
            '--no-check-certificates',
            youtube_url
        ]
        
        # Add cookies file if provided
        if cookies_file:
            cmd.extend(['--cookies', cookies_file])
        
        # Add timeout to prevent hanging
        result = subprocess.run(cmd, capture_output=True, text=True, check=True, timeout=30)
        audio_url = result.stdout.strip()
        
        if not audio_url:
            raise YouTubeStreamError("No audio stream URL found")
            
        return audio_url
        
    except subprocess.TimeoutExpired:
        # Try fallback method if timeout occurs
        sys.stderr.write("Primary extraction timed out, trying fallback method...\n")
        return get_audio_stream_url_fallback(youtube_url, cookies_file)
    except subprocess.CalledProcessError as e:
        # Try with different extraction strategy if first attempt fails
        if BOT_DETECTION_SIGN_IN in e.stderr or BOT_DETECTION_BOT in e.stderr:
            return get_audio_stream_url_fallback(youtube_url, cookies_file)
        else:
            raise YouTubeStreamError(f"yt-dlp failed: {e.stderr}")
    except FileNotFoundError:
        raise YouTubeStreamError("yt-dlp not found. Install with: pip install yt-dlp")


def get_audio_stream_url_fallback(youtube_url, cookies_file=None):
    """Fallback method with different yt-dlp options for bot detection issues."""
    try:
        # Alternative approach with different player client and options
        cmd = [
            'yt-dlp',
            '--format', AUDIO_FORMAT_FALLBACK,
            '--get-url',
            '--no-warnings',
            '--user-agent', 'Mozilla/5.0 (Linux; Android 10; SM-G981B) AppleWebKit/537.36',
            '--extractor-args', 'youtube:player_client=android',
            '--no-check-certificates',
            '--ignore-errors',
            youtube_url
        ]
        
        # Add cookies file if provided
        if cookies_file:
            cmd.extend(['--cookies', cookies_file])
        
        result = subprocess.run(cmd, capture_output=True, text=True, check=True, timeout=20)
        audio_url = result.stdout.strip()
        
        if not audio_url:
            # Try one more fallback with embedded player
            return get_audio_stream_url_embedded(youtube_url, cookies_file)
            
        return audio_url
        
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return get_audio_stream_url_embedded(youtube_url, cookies_file)


def get_audio_stream_url_embedded(youtube_url, cookies_file=None):
    """Final fallback using embedded player approach."""
    try:
        cmd = [
            'yt-dlp',
            '--format', AUDIO_FORMAT_PRIMARY,
            '--get-url',
            '--no-warnings',
            '--extractor-args', 'youtube:player_client=embed',
            '--user-agent', 'Mozilla/5.0 (compatible; Googlebot/2.1)',
            youtube_url
        ]
        
        # Add cookies file if provided
        if cookies_file:
            cmd.extend(['--cookies', cookies_file])
        
        result = subprocess.run(cmd, capture_output=True, text=True, check=True, timeout=15)
        audio_url = result.stdout.strip()
        
        if not audio_url:
            raise YouTubeStreamError("All extraction methods failed. The video may be restricted or unavailable.")
            
        return audio_url
        
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
        error_msg = f"All extraction methods failed. Error: {getattr(e, 'stderr', 'timeout or unknown error')}"
        raise YouTubeStreamError(error_msg)
        
        
def suggest_alternative_approach():
    """Suggest alternative approaches when YouTube extraction fails."""
    return """
Alternative approaches when YouTube extraction fails:

1. Try a different video that's not restricted
2. Use a local audio file instead:
   ./build/dtmf_stream your_audio.pcm

3. Convert existing audio file to PCM format:
   ffmpeg -i your_audio.wav -f s16le -ac 1 -ar 8000 your_audio.pcm
   ./build/dtmf_stream your_audio.pcm

4. Use a VPN to change your IP address if geo-restricted

5. Wait and try again later (YouTube's bot detection is temporary)

6. Try downloading the audio first, then analyze:
   yt-dlp -x --audio-format wav "YOUR_YOUTUBE_URL"
   ffmpeg -i "downloaded_file.wav" -f s16le -ac 1 -ar 8000 converted.pcm
   ./build/dtmf_stream converted.pcm

7. Check available formats:
   yt-dlp --list-formats "YOUR_YOUTUBE_URL"
"""


def check_available_formats(youtube_url, cookies_file=None):
    """Check what formats are available for the YouTube video."""
    try:
        cmd = [
            'yt-dlp',
            '--list-formats',
            '--no-warnings',
            youtube_url
        ]
        
        if cookies_file:
            cmd.extend(['--cookies', cookies_file])
            
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        return result.stdout
        
    except Exception:
        return "Could not retrieve format information"

def stream_audio_to_stdout(audio_url):
    """Stream and convert audio to 8kHz mono PCM format."""
    try:
        # FFmpeg command to stream and convert audio
        # -i: input URL
        # -f s16le: output format signed 16-bit little-endian PCM
        # -ac 1: mono (1 channel)
        # -ar 8000: 8kHz sample rate
        # -acodec pcm_s16le: PCM signed 16-bit little-endian codec
        # -loglevel error: suppress non-error output
        # pipe:1: output to stdout
        ffmpeg_cmd = [
            'ffmpeg',
            '-i', audio_url,
            '-f', 's16le',
            '-ac', '1',
            '-ar', '8000',
            '-acodec', 'pcm_s16le',
            '-loglevel', 'error',
            'pipe:1'
        ]
        
        # Start ffmpeg process
        process = subprocess.Popen(
            ffmpeg_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0  # Unbuffered for real-time streaming
        )
        
        # Stream data to stdout in chunks
        chunk_size = 1024  # 1KB chunks for responsive streaming
        
        try:
            while True:
                if process.stdout is None:
                    break
                chunk = process.stdout.read(chunk_size)
                if not chunk:
                    break
                    
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                
        except BrokenPipeError:
            # Handle case where the receiving process (dtmf_stream) closes the pipe
            pass
        except KeyboardInterrupt:
            # Handle Ctrl+C gracefully
            process.terminate()
            sys.stderr.write("\nStreaming interrupted by user\n")
            sys.exit(1)
        
        # Wait for ffmpeg to complete and check for errors
        process.wait()
        
        if process.returncode != 0:
            if process.stderr is not None:
                error_output = process.stderr.read().decode('utf-8', errors='ignore')
            else:
                error_output = "Unknown error"
            raise AudioProcessingError(f"FFmpeg failed (code {process.returncode}): {error_output}")
            
    except FileNotFoundError:
        raise AudioProcessingError("ffmpeg not found. Please install ffmpeg")

def process_local_video_file(video_path):
    """Process local video file and stream audio to stdout."""
    # Check if file exists
    if not os.path.exists(video_path):
        raise AudioProcessingError(f"Video file not found: {video_path}")
    
    # Check if file is readable
    if not os.access(video_path, os.R_OK):
        raise AudioProcessingError(f"Cannot read video file: {video_path}")
    
    # Use the existing stream_audio_to_stdout function with the local file path
    sys.stderr.write(f"Processing local video file: {video_path}\n")
    sys.stderr.write("Converting to 8kHz mono PCM...\n")
    stream_audio_to_stdout(video_path)

def main():
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Stream YouTube audio or local video files for DTMF analysis',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s "https://www.youtube.com/watch?v=dQw4w9WgXcQ" | ./build/dtmf_stream
  %(prog)s --cookies cookies.txt "https://www.youtube.com/watch?v=dQw4w9WgXcQ" | ./build/dtmf_stream
  %(prog)s --local-file /path/to/video.mp4 | ./build/dtmf_stream

Cookie Export Instructions:
  1. Install browser extension "Get cookies.txt LOCALLY" or similar
  2. Go to YouTube.com while logged in
  3. Export cookies to a .txt file (Netscape format)
  4. Use --cookies parameter to specify the file
        ''')
    
    # Make youtube_url optional when using local file
    parser.add_argument('youtube_url', nargs='?', help='YouTube video URL')
    parser.add_argument('--local-file', '-f', 
                       help='Path to local video file to process')
    parser.add_argument('--cookies', '-c', 
                       help='Path to cookies.txt file (Netscape format) for authentication')
    parser.add_argument('--list-formats', '-l', action='store_true',
                       help='List available formats for the video and exit')
    
    args = parser.parse_args()
    
    youtube_url = args.youtube_url
    local_file = args.local_file
    cookies_file = args.cookies
    list_formats = args.list_formats
    
    # Validate that either YouTube URL or local file is provided
    if not youtube_url and not local_file:
        print("Error: Either provide a YouTube URL or use --local-file option", file=sys.stderr)
        parser.print_help()
        sys.exit(1)
    
    if youtube_url and local_file:
        print("Error: Cannot use both YouTube URL and --local-file option simultaneously", file=sys.stderr)
        sys.exit(1)
    
    # Handle local file processing
    if local_file:
        try:
            process_local_video_file(local_file)
            return
        except AudioProcessingError as e:
            print(f"Audio processing error: {e}", file=sys.stderr)
            sys.exit(1)
        except Exception as e:
            print(f"Unexpected error: {e}", file=sys.stderr)
            sys.exit(1)
    
    # Handle YouTube URL processing (existing logic)
    # Validate YouTube URL
    if not is_youtube_url(youtube_url):
        print(f"Error: '{youtube_url}' does not appear to be a valid YouTube URL", file=sys.stderr)
        sys.exit(1)
    
    # If user wants to list formats, do that and exit
    if list_formats:
        print(f"Available formats for: {youtube_url}")
        formats = check_available_formats(youtube_url, cookies_file)
        print(formats)
        sys.exit(0)
    
    # Validate cookies file if provided
    if cookies_file and not os.path.exists(cookies_file):
        print(f"Error: Cookies file '{cookies_file}' not found", file=sys.stderr)
        sys.exit(1)
    
    try:
        # Step 1: Get the direct audio stream URL
        if cookies_file:
            sys.stderr.write(f"Using cookies from: {cookies_file}\n")
        sys.stderr.write(f"Extracting audio stream URL from: {youtube_url}\n")
        try:
            audio_url = get_audio_stream_url(youtube_url, cookies_file)
            sys.stderr.write("Found audio stream, starting conversion to 8kHz mono PCM...\n")
        except YouTubeStreamError as e:
            if BOT_DETECTION_SIGN_IN in str(e) or BOT_DETECTION_BOT in str(e):
                sys.stderr.write("YouTube bot detection encountered. Trying alternative methods...\n")
                # The fallback methods are already called within get_audio_stream_url
                raise
            else:
                raise
        
        # Step 2: Stream and convert audio
        stream_audio_to_stdout(audio_url)
        
    except YouTubeStreamError as e:
        print(f"YouTube extraction error: {e}", file=sys.stderr)
        
        # Check if it's a format issue
        if "Requested format is not available" in str(e):
            print("\n🔍 Checking available formats...", file=sys.stderr)
            formats = check_available_formats(youtube_url, cookies_file)
            print("Available formats:", file=sys.stderr)
            print(formats, file=sys.stderr)
            
        if BOT_DETECTION_SIGN_IN in str(e) or BOT_DETECTION_BOT in str(e) or "All extraction methods failed" in str(e):
            if not cookies_file:
                print("\nTip: Try using browser cookies to authenticate:", file=sys.stderr)
                print("  1. Export cookies from your browser while logged into YouTube", file=sys.stderr)
                print("  2. Use: python3 youtube_stream.py --cookies cookies.txt 'URL'", file=sys.stderr)
            print(suggest_alternative_approach(), file=sys.stderr)
        sys.exit(1)
    except AudioProcessingError as e:
        print(f"Audio processing error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
