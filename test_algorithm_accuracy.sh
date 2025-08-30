#!/bin/bash

# DTMF Algorithm Accuracy Comparison Script
# Tests all algorithms against all digit samples and compares expected vs actual results

echo "======================================"
echo "DTMF Algorithm Accuracy Comparison"
echo "======================================"
echo ""

# Define algorithms to test
algorithms=("FFT" "Goertzel" "GoertzelTwist" "FilterBank" "Correlation" "MUSIC")

# Define digits to test
digits=(1 2 3 4 5 6 7 8 9 pound star)

# Initialize counters
declare -A correct_count
declare -A total_count
declare -A accuracy

# Initialize counters for each algorithm
for alg in "${algorithms[@]}"; do
    correct_count[$alg]=0
    total_count[$alg]=0
done

# Function to extract detected digit from algorithm output
extract_digit() {
    local algorithm=$1
    local output=$2
    echo "$output" | grep "$algorithm" | head -1 | awk -F'|' '{print $4}' | awk '{print $1}' | tr -d ' '
}

# Function to convert digit name to expected symbol
get_expected_symbol() {
    local digit=$1
    case "$digit" in
        "pound") echo "#" ;;
        "star") echo "*" ;;
        *) echo "$digit" ;;
    esac
}

echo "Digit | Algorithm      | Expected | Result | Status"
echo "------|----------------|----------|--------|--------"

# Test each digit with each algorithm
for digit in "${digits[@]}"; do
    # Get the output from dtmf_stream for this digit
    output=$(cat "data/$digit.pcm" | ./build/dtmf_stream - 2>/dev/null)
    
    for algorithm in "${algorithms[@]}"; do
        expected=$(get_expected_symbol "$digit")
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
        printf "%5s | %-14s | %8s | %6s | %s\n" "$digit" "$algorithm" "$expected" "$result" "$status"
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
echo "- Each algorithm tested on 11 digit samples (0-9, *, #)"
echo "======================================"
