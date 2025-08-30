#!/usr/bin/env python3
"""
test_youtube_alternatives.py - Test script for YouTube streaming alternatives

This script demonstrates alternative approaches when direct YouTube streaming fails.
"""

import sys
import subprocess
import os

def test_yt_dlp_basic():
    """Test basic yt-dlp functionality."""
    try:
        # Test with a simple command
        cmd = ['yt-dlp', '--version']
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        print(f"yt-dlp version: {result.stdout.strip()}")
        return True
    except Exception as e:
        print(f"yt-dlp test failed: {e}")
        return False

def create_test_audio():
    """Create a test DTMF audio file for demonstration."""
    print("Creating test DTMF audio file...")
    
    # Generate a simple DTMF tone using ffmpeg
    cmd = [
        'ffmpeg', '-f', 'lavfi', 
        '-i', 'sine=frequency=697:duration=1,sine=frequency=1209:duration=1',
        '-filter_complex', '[0:0][1:0]amix=inputs=2',
        '-f', 's16le', '-ac', '1', '-ar', '8000',
        '-y', 'test_dtmf.pcm'
    ]
    
    try:
        subprocess.run(cmd, check=True, capture_output=True)
        print("Created test_dtmf.pcm (DTMF tone '1')")
        return True
    except subprocess.CalledProcessError as e:
        print(f"Failed to create test audio: {e}")
        return False

def test_dtmf_stream():
    """Test the DTMF stream analyzer with test file."""
    if not os.path.exists('test_dtmf.pcm'):
        if not create_test_audio():
            return False
    
    print("\nTesting DTMF stream analyzer with generated tone...")
    cmd = ['./build/dtmf_stream', 'test_dtmf.pcm']
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        print("DTMF Analysis Output:")
        print(result.stdout)
        return True
    except Exception as e:
        print(f"DTMF stream test failed: {e}")
        return False

def suggest_youtube_alternatives():
    """Suggest alternative approaches for YouTube analysis."""
    print("""
=== YouTube Audio Analysis Alternatives ===

When direct streaming fails due to bot detection, try these approaches:

1. DOWNLOAD FIRST METHOD:
   # Download audio first, then convert and analyze
   yt-dlp -x --audio-format wav "YOUTUBE_URL"
   ffmpeg -i "downloaded_file.wav" -f s16le -ac 1 -ar 8000 converted.pcm
   ./build/dtmf_stream converted.pcm

2. USE DIFFERENT VIDEO:
   # Try with a different, less restricted video
   # Educational or CC-licensed videos often work better

3. VPN/PROXY METHOD:
   # Use a VPN to change your IP address
   # Sometimes geo-restrictions cause the issue

4. LOCAL FILE METHOD:
   # If you have the audio file locally
   ffmpeg -i your_audio.mp3 -f s16le -ac 1 -ar 8000 your_audio.pcm
   ./build/dtmf_stream your_audio.pcm

5. BROWSER EXTENSION:
   # Use browser extensions to download audio
   # Then convert and analyze locally

6. DIFFERENT EXTRACTOR:
   # Try with youtube-dl instead of yt-dlp
   pip install youtube-dl
   # Modify script to use youtube-dl

7. WAIT AND RETRY:
   # YouTube's bot detection is often temporary
   # Try again in a few minutes/hours

=== Testing Your Setup ===

Run this script to test your DTMF analysis setup:
python3 test_youtube_alternatives.py
""")

def main():
    print("=== YouTube Streaming Alternatives Test ===\n")
    
    # Test basic functionality
    print("1. Testing yt-dlp installation...")
    if test_yt_dlp_basic():
        print("✓ yt-dlp is working\n")
    else:
        print("✗ yt-dlp test failed\n")
    
    # Create and test with generated audio
    print("2. Testing DTMF stream analyzer...")
    if test_dtmf_stream():
        print("✓ DTMF stream analyzer is working\n")
    else:
        print("✗ DTMF stream analyzer test failed\n")
    
    # Show alternatives
    print("3. Showing alternative approaches...")
    suggest_youtube_alternatives()

if __name__ == "__main__":
    main()
