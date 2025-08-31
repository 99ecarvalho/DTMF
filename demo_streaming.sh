#!/bin/bash

# demo_streaming.sh
# Demonstration of the streaming DTMF analyzer capabilities
# 
# Usage: 
#   ./demo_streaming.sh [dataset]    # Run demo on dataset
# 
# Default dataset: data/01

# Default dataset
DATASET_DIR="${1:-data/01}"

# Normalize path - remove trailing slashes
DATASET_DIR="${DATASET_DIR%/}"

# Check if dataset directory exists
if [[ ! -d "$DATASET_DIR" ]]; then
    echo "Error: Dataset directory '$DATASET_DIR' not found!"
    echo "Usage: $0 [dataset_dir]"
    echo "Example: $0 data/01"
    echo "Available datasets:"
    ls -d data/*/ 2>/dev/null | sed 's|/$||' | sed 's|^|  |'
    exit 1
fi

# Check if data.txt exists
DATA_FILE="$DATASET_DIR/data.txt"
if [[ ! -f "$DATA_FILE" ]]; then
    echo "Error: Data file '$DATA_FILE' not found!"
    echo "Each dataset directory must contain a 'data.txt' file with the format:"
    echo "filename_without_extension expected_symbol_sequence"
    exit 1
fi

echo "🎵 DTMF Streaming Analyzer Demonstration"
echo "Dataset: $DATASET_DIR"
echo "========================================"
echo

# Build if needed
if [ ! -f "build/dtmf_stream" ]; then
    echo "📦 Building streaming analyzer..."
    make dtmf_stream
    echo
fi

echo "🔍 Testing DTMF digits from dataset..."

# Arrays to store test data
declare -a test_files
declare -a expected_symbols

# Read data.txt and prepare test files
echo "Loading test data from $DATA_FILE..."
while IFS= read -r line; do
    # Skip empty lines and comments
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
    
    # Parse filename and expected symbols
    filename=$(echo "$line" | awk '{print $1}')
    expected_sequence=$(echo "$line" | cut -d' ' -f2-)
    
    # Check if PCM file exists
    pcm_file="$DATASET_DIR/$filename.pcm"
    if [[ -f "$pcm_file" ]]; then
        test_files+=("$filename")
        expected_symbols+=("$expected_sequence")
        echo "Added test: $filename -> '$expected_sequence'"
    else
        echo "Warning: Could not find $pcm_file"
    fi
done < "$DATA_FILE"

# Check if we have any test files
if [[ ${#test_files[@]} -eq 0 ]]; then
    echo "Error: No valid test files found in dataset!"
    exit 1
fi

echo "Found ${#test_files[@]} test files"
echo

for i in "${!test_files[@]}"; do
    filename="${test_files[$i]}"
    expected="${expected_symbols[$i]}"
    
    echo "🎯 Processing $DATASET_DIR/${filename}.pcm:"
    echo "   Expected: $expected"
    
    # Get frames starting from 0.040 and extract the detected digits
    result=$(./build/dtmf_stream "$DATASET_DIR/${filename}.pcm" | grep -v "^#" | grep -v "^Timestamp" | grep -v "^-" | awk '$1 >= 0.040' | head -5 | awk -F'|' '{print $4}' | awk '{print $1}' | sort | uniq -c | sort -nr | head -1)
    
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

echo "🎯 Testing with streaming analyzer (first frame, FFT only):"
# Use the first available test file
if [[ ${#test_files[@]} -gt 0 ]]; then
    test_file="${test_files[0]}"
    ./build/dtmf_stream "$DATASET_DIR/$test_file.pcm" | awk '$1 >= 0.040' | grep -E "FFT.*Rectangular" | head -1
else
    echo "   No test files available"
fi

echo
echo "🌊 Streaming from stdin demonstration:"
if [[ ${#test_files[@]} -gt 0 ]]; then
    test_file="${test_files[0]}"
    echo "   (Processing frames from 0.040s onward of $DATASET_DIR/$test_file.pcm through pipe)"
    echo
    head -c 3840 "$DATASET_DIR/$test_file.pcm" | ./build/dtmf_stream | grep -v "^#" | grep -v "^Timestamp" | grep -v "^-" | awk '$1 >= 0.040'
else
    echo "   No test files available for stdin demo"
fi

echo
echo "✅ Streaming analyzer features:"
echo "   • Processes 40ms frames (320 samples @ 8kHz)"
echo "   • Works with files or stdin streaming"
echo "   • Low latency for real-time processing"
echo "   • Multi-line format: one line per algorithm/window combination"
echo "   • Fixed column widths optimized for up to 1 hour"
echo "   • Processing time tracking per algorithm"
echo "   • FFT with 5 windowing functions: Rectangular, Hamming, Hanning, Blackman, Kaiser"
echo "   • All 6 algorithms: FFT, Goertzel, Goertzel+Twist, FilterBank, Correlation, MUSIC"
echo "   • Feature parity with original dtmf.c implementation"
echo "   • Suitable for: arecord -f S16_LE -r 8000 -c 1 | ./dtmf_stream"
echo

echo "🚀 Ready for real-time streaming!"
