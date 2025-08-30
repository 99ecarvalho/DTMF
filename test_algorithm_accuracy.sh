#!/bin/bash

# DTMF Algorithm Accuracy Comparison Script
# Tests all algorithms against all digit samples and compares expected vs actual results
# 
# Usage: 
#   ./test_algorithm_accuracy.sh [dataset]           # Run tests on dataset
#   ./test_algorithm_accuracy.sh --generate [dir]   # Generate data.txt for directory
# 
# Default dataset: data/01
# 
# Dataset structure:
# data/
#   01/
#     data.txt          # Contains: filename_without_extension expected_symbols
#     file1.pcm         # PCM audio files
#     file1.wav         # WAV audio files (automatically converted to PCM)
#     ...

# Function to generate data.txt for a directory
generate_data_file() {
    local target_dir="$1"
    
    if [[ ! -d "$target_dir" ]]; then
        echo "Error: Directory '$target_dir' not found!"
        return 1
    fi
    
    local data_file="$target_dir/data.txt"
    
    if [[ -f "$data_file" ]]; then
        read -p "data.txt already exists in $target_dir. Overwrite? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "Cancelled."
            return 1
        fi
    fi
    
    echo "# DTMF Test Dataset - Auto-generated" > "$data_file"
    echo "# Generated on $(date)" >> "$data_file"
    echo "# Format: filename_without_extension expected_symbol_sequence" >> "$data_file"
    echo "#" >> "$data_file"
    
# Function to map filename to DTMF symbol(s)
map_filename_to_symbols() {
    local filename="$1"
    local symbols=""
    
    # First, check for specific word mappings (override sequence detection)
    case "$filename" in
        # Numbers (words)
        zero) symbols="0" ;;
        one) symbols="1" ;;
        two) symbols="2" ;;
        three) symbols="3" ;;
        four) symbols="4" ;;
        five) symbols="5" ;;
        six) symbols="6" ;;
        seven) symbols="7" ;;
        eight) symbols="8" ;;
        nine) symbols="9" ;;
        # Special symbols (words)
        star|asterisk) symbols="*" ;;
        pound|hash|sharp) symbols="#" ;;
        # DTMF letters (words)
        # Check for sequence patterns (only if not a word)
        *)
            # Handle sequences by processing each character if filename is numeric/symbolic
            if [[ "$filename" =~ ^[0-9hsHSaAbBcCdD_-]+$ ]] && [[ ${#filename} -gt 1 ]]; then
                # Look for sequence patterns like "112", "123", etc.
                local sequence=""
                local i
                for ((i=0; i<${#filename}; i++)); do
                    local char="${filename:$i:1}"
                    case "$char" in
                        [0-9]) sequence="$sequence $char" ;;
                        h|H) sequence="$sequence #" ;;
                        s|S) sequence="$sequence *" ;;
                        a|A) sequence="$sequence A" ;;
                        b|B) sequence="$sequence B" ;;
                        c|C) sequence="$sequence C" ;;
                        d|D) sequence="$sequence D" ;;
                        # Skip separators (underscores, dashes, etc.)
                        [_-]) ;;
                        # Unknown character, break sequence
                        *) sequence=""; break ;;
                    esac
                done
                
                # If we found a valid sequence, use it
                if [[ -n "$sequence" ]]; then
                    symbols=$(echo "$sequence" | xargs)  # Remove leading space and clean up
                    echo "$symbols"
                    return
                fi
            fi
            
            # Fall back to single symbol mapping and pattern matching
            case "$filename" in
                # Single digit/symbol patterns
                0|*[_-]0) symbols="0" ;;
                1|*[_-]1) symbols="1" ;;
                2|*[_-]2) symbols="2" ;;
                3|*[_-]3) symbols="3" ;;
                4|*[_-]4) symbols="4" ;;
                5|*[_-]5) symbols="5" ;;
                6|*[_-]6) symbols="6" ;;
                7|*[_-]7) symbols="7" ;;
                8|*[_-]8) symbols="8" ;;
                9|*[_-]9) symbols="9" ;;
                # DTMF letters
                a|A) symbols="A" ;;
                b|B) symbols="B" ;;
                c|C) symbols="C" ;;
                d|D) symbols="D" ;;
                # Pattern matching for common formats
                *star*|*asterisk*) symbols="*" ;;
                *pound*|*hash*) symbols="#" ;;
                # Default: use filename as-is and let user edit
                *) symbols="?" ;;
            esac
            ;;
    esac
    
    echo "$symbols"
}

# Function to process audio files and add to data.txt
process_audio_files() {
    local target_dir="$1"
    local data_file="$2"
    local count=0
    
    # Create a list of all audio files (both PCM and WAV)
    local -a all_files
    
    # Add PCM files
    for file in "$target_dir"/*.pcm; do
        [[ -f "$file" ]] && all_files+=("$file")
    done
    
    # Add WAV files (only if no corresponding PCM exists)
    for file in "$target_dir"/*.wav; do
        if [[ -f "$file" ]]; then
            local basename_file=$(basename "$file")
            local filename="${basename_file%.*}"
            
            # Skip if corresponding PCM file already exists
            [[ -f "$target_dir/$filename.pcm" ]] || all_files+=("$file")
        fi
    done
    
    # Process all files
    for file in "${all_files[@]}"; do
        local basename_file=$(basename "$file")
        local filename="${basename_file%.*}"
        local symbols
        
        symbols=$(map_filename_to_symbols "$filename")
        
        echo "$filename $symbols" >> "$data_file"
        ((count++))
        echo "Added: $filename -> $symbols"
    done
    
    echo "$count"
}

    # Now process all audio files and generate data.txt entries
    local count
    count=$(process_audio_files "$target_dir" "$data_file")
    
    echo ""
    echo "Generated data.txt with $count entries in: $data_file"
    echo ""
    echo "Please review and edit the file to ensure correct symbol mappings."
    echo "Entries marked with '?' need manual correction."
    echo ""
    return 0
}

# Check for --generate flag
if [[ "$1" == "--generate" ]]; then
    if [[ -z "$2" ]]; then
        echo "Error: Please specify directory for data.txt generation"
        echo "Usage: $0 --generate <directory>"
        exit 1
    fi
    generate_data_file "$2"
    exit $?
fi

# Default dataset
DATASET_DIR="${1:-data/01}"

# Normalize path - remove trailing slashes
DATASET_DIR="${DATASET_DIR%/}"

# Check if dataset directory exists
if [[ ! -d "$DATASET_DIR" ]]; then
    echo "Error: Dataset directory '$DATASET_DIR' not found!"
    echo "Usage: $0 [dataset_dir]"
    echo "Example: $0 data/01"
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

echo "======================================"
echo "DTMF Algorithm Accuracy Comparison"
echo "Dataset: $DATASET_DIR"
echo "======================================"
echo ""

# Define algorithms to test
algorithms=("FFT" "Goertzel" "GoertzelTwist" "FilterBank" "Correlation" "MUSIC")

# Initialize counters
declare -A correct_count
declare -A total_count
declare -A accuracy
declare -a test_files
declare -a expected_symbols

# Initialize counters for each algorithm
for alg in "${algorithms[@]}"; do
    correct_count[$alg]=0
    total_count[$alg]=0
done

# Function to convert WAV to PCM if needed
convert_wav_to_pcm() {
    local filename=$1
    local wav_file="$DATASET_DIR/$filename.wav"
    local pcm_file="$DATASET_DIR/$filename.pcm"
    
    # If PCM already exists, don't overwrite
    if [[ -f "$pcm_file" ]]; then
        return 0
    fi
    
    # If WAV exists, convert it
    if [[ -f "$wav_file" ]]; then
        echo "Converting $wav_file to PCM..."
        if command -v sox >/dev/null 2>&1; then
            sox "$wav_file" -t raw -r 8000 -c 1 -b 16 -e signed-integer "$pcm_file"
            if [[ $? -eq 0 ]]; then
                echo "Successfully converted $wav_file to $pcm_file"
                return 0
            else
                echo "Error converting $wav_file to PCM"
                return 1
            fi
        else
            echo "Error: sox not found. Please install sox to convert WAV files."
            return 1
        fi
    fi
    
    return 1
}

# Read data.txt and prepare test files
echo "Loading test data from $DATA_FILE..."
while IFS= read -r line; do
    # Skip empty lines and comments
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
    
    # Parse filename and expected symbols
    filename=$(echo "$line" | awk '{print $1}')
    expected_sequence=$(echo "$line" | cut -d' ' -f2-)
    
    # Check if PCM file exists, or try to convert from WAV
    pcm_file="$DATASET_DIR/$filename.pcm"
    if [[ -f "$pcm_file" ]] || convert_wav_to_pcm "$filename"; then
        test_files+=("$filename")
        expected_symbols+=("$expected_sequence")
        echo "Added test: $filename -> '$expected_sequence'"
    else
        echo "Warning: Could not find $pcm_file or $filename.wav"
    fi
done < "$DATA_FILE"

# Check if we have any test files
if [[ ${#test_files[@]} -eq 0 ]]; then
    echo "Error: No valid test files found in dataset!"
    exit 1
fi

echo ""
echo "Found ${#test_files[@]} test files"
echo ""

# Function to extract detected digit from algorithm output
extract_digit() {
    local algorithm=$1
    local output=$2
    echo "$output" | grep "$algorithm" | head -1 | awk -F'|' '{print $4}' | awk '{print $1}' | tr -d ' '
}

echo "Filename | Algorithm      | Expected | Result | Status"
echo "---------|----------------|----------|--------|--------"

# Test each file with each algorithm
for i in "${!test_files[@]}"; do
    filename="${test_files[$i]}"
    expected_sequence="${expected_symbols[$i]}"
    pcm_file="$DATASET_DIR/$filename.pcm"
    
    # Get the output from dtmf_stream for this file
    if [[ ! -f "$pcm_file" ]]; then
        echo "Error: $pcm_file not found!"
        continue
    fi
    
    output=$(cat "$pcm_file" | ./build/dtmf_stream - 2>/dev/null)
    
    for algorithm in "${algorithms[@]}"; do
        # For now, assume single symbol detection (take first symbol if sequence)
        expected=$(echo "$expected_sequence" | awk '{print $1}')
        result=$(extract_digit "$algorithm" "$output")
        
        # Handle case where result is empty or invalid
        if [[ -z "$result" || "$result" == "?" ]]; then
            result="FAIL"
            status="❌"
        elif [[ "$result" == "$expected" ]]; then
            status="✅"
            ((correct_count[$algorithm]++))
        else
            status="❌"
        fi
        
        ((total_count[$algorithm]++))
        
        # Format output with proper alignment
        printf "%8s | %-14s | %8s | %6s | %s\n" "$filename" "$algorithm" "$expected" "$result" "$status"
    done
    echo ""
done

echo "======================================"
echo "ACCURACY SUMMARY"
echo "======================================"
echo ""
echo "Algorithm      | Correct | Total | Accuracy"
echo "---------------|---------|-------|----------"

for algorithm in "${algorithms[@]}"; do
    correct=${correct_count[$algorithm]}
    total=${total_count[$algorithm]}
    if [[ $total -gt 0 ]]; then
        accuracy[$algorithm]=$(echo "scale=1; $correct * 100 / $total" | bc -l)
    else
        accuracy[$algorithm]=0.0
    fi
    
    printf "%-14s | %7d | %5d | %6.1f%%\n" "$algorithm" "$correct" "$total" "${accuracy[$algorithm]}"
done

echo ""
echo "======================================"
echo "ALGORITHM RANKING (by accuracy)"
echo "======================================"

# Create array for sorting
sort_array=()
for algorithm in "${algorithms[@]}"; do
    sort_array+=("${accuracy[$algorithm]}:$algorithm")
done

# Sort by accuracy (descending)
IFS=$'\n' sorted=($(sort -rn <<<"${sort_array[*]}"))
unset IFS

echo ""
echo "Rank | Algorithm      | Accuracy"
echo "-----|----------------|----------"

rank=1
for item in "${sorted[@]}"; do
    acc=$(echo "$item" | cut -d':' -f1)
    alg=$(echo "$item" | cut -d':' -f2)
    printf "%4d | %-14s | %6.1f%%\n" "$rank" "$alg" "$acc"
    ((rank++))
done

echo ""
echo "======================================"
echo "NOTES:"
echo "- ✅ = Correct detection"
echo "- ❌ = Incorrect detection or failure"
echo "- FAIL = No detection or invalid result"
echo "- Each algorithm tested on ${#test_files[@]} samples from dataset: $DATASET_DIR"
echo "======================================"
