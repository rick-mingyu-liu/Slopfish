#!/usr/bin/env bash

# Script to test different thread counts and find optimal performance
# Usage: ./scripts/test_thread_counts.sh [path_to_slopfish_executable]

EXEC=${1:-./slopfish}

if [ ! -f "$EXEC" ]; then
    # Try in src directory if not in current directory
    if [ -f "src/slopfish" ]; then
        EXEC="./src/slopfish"
    elif [ -f "src/stockfish" ]; then
        EXEC="./src/stockfish"
    else
        echo "Error: Executable not found at $EXEC"
        echo "Please compile the program first or provide the path:"
        echo "Usage: $0 [path_to_slopfish_executable]"
        exit 1
    fi
fi

echo "=========================================="
echo "Testing thread count performance"
echo "All threads will be bound to CPU core 0"
echo "=========================================="
echo ""

# Thread counts to test - focusing on hardware_concurrency values and optimal range
# hardware_concurrency = 11, so 4x = 44, and formula gives max(1024, 44) = 1024
# Testing: actual cores, 4x cores, optimal range, and formula result
THREAD_COUNTS=(1 4 8 11 16 22 32 44 64 96 128 192 256 384 512 768 1024)

# Results arrays (using parallel arrays since associative arrays may not be supported)
declare -a RESULT_THREADS
declare -a RESULT_NPS

# Run benchmark for each thread count
for threads in "${THREAD_COUNTS[@]}"; do
    echo "Testing with $threads thread(s)..."
    echo "----------------------------------------"
    
    # Run benchmark and capture output
    # Using a shorter benchmark for faster results (depth 12 instead of default 13)
    OUTPUT=$($EXEC bench 64 $threads 12 2>&1)
    
    # Extract nodes/second from output (macOS-compatible grep)
    NPS=$(echo "$OUTPUT" | grep "Nodes/second" | awk '{print $NF}' || echo "0")
    
    # Remove any non-numeric characters
    NPS=$(echo "$NPS" | tr -d ',' | tr -d ' ')
    
    RESULT_THREADS+=($threads)
    RESULT_NPS+=($NPS)
    echo "Result: $NPS nodes/second"
    echo ""
done

# Display summary
echo "=========================================="
echo "Performance Summary"
echo "=========================================="
printf "%-10s %-20s %-15s\n" "Threads" "Nodes/Second" "Performance"
echo "----------------------------------------"

BEST_THREADS=1
BEST_NPS=0

# Find best first
for i in "${!RESULT_THREADS[@]}"; do
    threads=${RESULT_THREADS[$i]}
    nps=${RESULT_NPS[$i]}
    if [ -n "$nps" ] && [ "$nps" != "0" ] && [ "$nps" -gt "$BEST_NPS" ]; then
        BEST_NPS=$nps
        BEST_THREADS=$threads
    fi
done

# Display results with percentages
for i in "${!RESULT_THREADS[@]}"; do
    threads=${RESULT_THREADS[$i]}
    nps=${RESULT_NPS[$i]}
    if [ -z "$nps" ] || [ "$nps" = "0" ]; then
        perf="N/A"
    else
        # Calculate percentage of best
        if [ "$BEST_NPS" != "0" ] && [ "$BEST_NPS" -gt 0 ]; then
            percent=$(( (nps * 100) / BEST_NPS ))
        else
            percent=100
        fi
        perf="${percent}%"
    fi
    
    printf "%-10s %-20s %-15s\n" "$threads" "$nps" "$perf"
done

echo "----------------------------------------"
if [ "$BEST_NPS" != "0" ]; then
    echo "Optimal thread count: $BEST_THREADS threads ($BEST_NPS nodes/second)"
    echo ""
    echo "Recommendation: Use $BEST_THREADS threads for best performance"
    echo "  (set with: setoption name Threads value $BEST_THREADS)"
else
    echo "Warning: Could not determine optimal thread count from results"
fi

