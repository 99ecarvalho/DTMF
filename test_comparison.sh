#!/bin/bash

# DTMF Analyzer Comparison Test Script
# Tests both original and refactored versions for consistency

echo "🧪 DTMF Analyzer Comparison Test"
echo "=================================="
echo

# Check if both executables exist
if [[ ! -f "./dtmf_compare" ]]; then
    echo "❌ Original dtmf_compare not found. Run: make dtmf_compare"
    exit 1
fi

if [[ ! -f "./dtmf_compare_refactored" ]]; then
    echo "❌ Refactored dtmf_compare_refactored not found. Run: make dtmf_compare_refactored"
    exit 1
fi

echo "✅ Both executables found"
echo

# Test files
test_files=("0.pcm" "1.pcm" "2.pcm" "3.pcm" "4.pcm" "5.pcm" "6.pcm" "7.pcm" "8.pcm" "9.pcm" "star.pcm" "pound.pcm")
passed_tests=0
total_tests=0

printf "%-12s %-10s %-10s %-8s\n" "File" "Original" "Refactored" "Status"
printf "%-12s %-10s %-10s %-8s\n" "----" "--------" "----------" "------"

for file in "${test_files[@]}"; do
    if [[ -f "data/$file" ]]; then
        total_tests=$((total_tests + 1))
        
        # Get results from both versions
        original_fft=$(./dtmf_compare "data/$file" 2>/dev/null | grep "detected key (FFT)" | cut -d'=' -f2 | tr -d ' ')
        original_goertzel=$(./dtmf_compare "data/$file" 2>/dev/null | grep "detected key (Goertzel)" | cut -d'=' -f2 | tr -d ' ')
        
        refactored_results=$(./dtmf_compare_refactored "data/$file" 2>/dev/null | grep "Detected Key:" | cut -d':' -f2 | tr -d ' ')
        refactored_fft=$(echo "$refactored_results" | sed -n '1p')
        refactored_goertzel=$(echo "$refactored_results" | sed -n '2p')
        
        # Compare results
        if [[ "$original_fft" == "$refactored_fft" ]] && [[ "$original_goertzel" == "$refactored_goertzel" ]]; then
            status="✅ PASS"
            passed_tests=$((passed_tests + 1))
        else
            status="❌ FAIL"
        fi
        
        printf "%-12s %-10s %-10s %-8s\n" "$file" "$original_fft/$original_goertzel" "$refactored_fft/$refactored_goertzel" "$status"
    fi
done

echo
echo "📊 Test Results:"
echo "  Total tests: $total_tests"
echo "  Passed: $passed_tests"
echo "  Failed: $((total_tests - passed_tests))"

if [[ $passed_tests -eq $total_tests ]]; then
    echo "  Success Rate: 100% ✅"
    echo
    echo "🎉 All tests passed! Both versions produce identical results."
else
    echo "  Success Rate: $((passed_tests * 100 / total_tests))% ❌"
    echo
    echo "⚠️  Some tests failed. Check implementation differences."
fi

echo
echo "🔍 Performance Comparison (using data/0.pcm):"
echo "Original version timing:"
./dtmf_compare data/0.pcm | grep -E "(FFT time|Goertzel time)"

echo
echo "Refactored version timing:"
./dtmf_compare_refactored data/0.pcm | grep -E "Timing:"

echo
echo "✅ Test completed!"
