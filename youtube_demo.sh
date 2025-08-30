#!/bin/bash

# youtube_demo.sh - Demonstration script for YouTube DTMF analysis alternatives

echo "=== YouTube DTMF Analysis Demo ==="
echo ""

echo "Due to YouTube's bot detection, direct streaming may fail."
echo "Here are the working alternatives:"
echo ""

echo "1. TESTING WITH EXISTING SAMPLES:"
echo "   Using our existing DTMF sample files..."
echo ""
echo "   Analysis of digit '5':"
head -c 3200 data/5.pcm | ./build/dtmf_stream | head -15

echo ""
echo "2. MANUAL YOUTUBE DOWNLOAD METHOD:"
echo "   When streaming fails, use this approach:"
echo ""
echo "   Step 1: Download audio"
echo "   yt-dlp -x --audio-format wav 'https://youtube.com/watch?v=VIDEO_ID'"
echo ""
echo "   Step 2: Convert format"
echo "   ffmpeg -i downloaded_file.wav -f s16le -ac 1 -ar 8000 converted.pcm"
echo ""
echo "   Step 3: Analyze"
echo "   ./build/dtmf_stream converted.pcm"
echo ""

echo "3. SIMULATED YOUTUBE STREAM:"
echo "   Simulating what YouTube streaming would look like..."
echo "   (Using concatenated sample data)"
echo ""

# Create a simulated "stream" by concatenating different DTMF samples
{
    cat data/1.pcm data/2.pcm data/3.pcm
} | head -c 9600 | ./build/dtmf_stream | head -20

echo ""
echo "=== Bot Detection Solutions ==="
echo ""
echo "If you get 'Sign in to confirm you're not a bot' errors:"
echo "- Wait 1 hour and try again"
echo "- Use a VPN to change your IP"
echo "- Try different videos (educational content works better)"
echo "- Use the download-first method shown above"
echo ""
echo "The youtube_stream.py script includes automatic fallbacks,"
echo "but YouTube's restrictions may still prevent direct streaming."
