#!/bin/bash

# demo_streaming.sh
# Demonstration of the streaming DTMF analyzer capabilities

echo "🎵 DTMF Streaming Analyzer Demonstration"
echo "========================================"
echo

# Build if needed
if [ ! -f "build/dtmf_stream" ]; then
    echo "📦 Building streaming analyzer..."
    make stream
    echo
fi

echo "🔍 Testing different DTMF digits..."
echo

# Test different digits
for digit in 1 2 5 star; do
    echo "🎯 Processing data/${digit}.pcm:"
    echo "   Expected: $(case $digit in 
        1) echo "1" ;; 
        2) echo "2" ;; 
        5) echo "5" ;; 
        star) echo "*" ;; 
    esac)"
    
    # Get first few frames and extract the detected digits
    result=$(./build/dtmf_stream data/${digit}.pcm | grep -v "^#" | grep -v "^Timestamp" | head -5 | awk '{print $3}' | sort | uniq -c | sort -nr | head -1)
    
    if [ -n "$result" ]; then
        detected=$(echo $result | awk '{print $2}')
        count=$(echo $result | awk '{print $1}')
        echo "   Detected: $detected (in $count/5 frames)"
    fi
    echo
done

echo "📊 Streaming vs File Processing Comparison"
echo "==========================================="
echo

echo "🎯 Testing with 5.pcm using original analyzer:"
./build/dtmf data/5.pcm | grep "Detected" | head -1

echo
echo "🎯 Testing with 5.pcm using streaming analyzer (first frame):"
./build/dtmf_stream data/5.pcm | grep -E "^\s*0\.000" | head -3

echo
echo "🌊 Streaming from stdin demonstration:"
echo "   (Processing first 3 frames of data/5.pcm through pipe)"
echo
head -c 1920 data/5.pcm | ./build/dtmf_stream | grep -v "^#" | grep -v "^Timestamp" | grep -v "^-"

echo
echo "✅ Streaming analyzer features:"
echo "   • Processes 40ms frames (320 samples @ 8kHz)"
echo "   • Works with files or stdin streaming"
echo "   • Low latency for real-time processing"
echo "   • Multi-line format: one line per algorithm per frame"
echo "   • Fixed column widths optimized for up to 1 hour"
echo "   • Processing time tracking per algorithm"
echo "   • All 6 algorithms: FFT, Goertzel, Goertzel+Twist, FilterBank, Correlation, MUSIC"
echo "   • Suitable for: arecord -f S16_LE -r 8000 -c 1 | ./dtmf_stream"
echo

echo "🚀 Ready for real-time streaming!"
