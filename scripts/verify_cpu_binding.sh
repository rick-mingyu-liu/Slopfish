#!/bin/bash

PROCESS_NAME=${1:-slopfish}
REFRESH_INTERVAL=${2:-1}  # Refresh every N seconds (default 1)

# Detect OS
if [ "$(uname)" = "Linux" ]; then
    OS="Linux"
elif [ "$(uname)" = "Darwin" ]; then
    OS="macOS"
else
    OS="Unknown"
fi

find_process() {
    PID=$(pgrep -f "$PROCESS_NAME" | head -1)
    if [ -z "$PID" ]; then
        return 1
    fi
    return 0
}

display_linux_info() {
    clear
    echo "=========================================="
    echo "Live CPU Binding Monitor for $PROCESS_NAME (Linux)"
    echo "Process ID: $PID | Refresh: ${REFRESH_INTERVAL}s | Press Ctrl+C to exit"
    echo "=========================================="
    echo ""
    
    # Check if /proc filesystem is available
    if [ ! -d "/proc/$PID" ]; then
        echo "Error: Process $PID not found in /proc"
        return 1
    fi
    
    # Get main process CPU affinity using taskset if available
    if command -v taskset >/dev/null 2>&1; then
        echo "Main Process CPU Affinity:"
        taskset -cp $PID 2>/dev/null | sed 's/^/  /'
        echo ""
    fi
    
    # Get thread CPU assignments
    echo "Thread CPU Assignments (psr = processor/core):"
    echo "  TID    CPU  Thread Name"
    echo "  -----  ---  -----------"
    
    # Use ps to show thread info with CPU assignment
    ps -eLo pid,tid,psr,comm 2>/dev/null | awk -v pid="$PID" '
        $1 == pid {
            if (NR > 1) {
                printf "  %-5s  %-3s  %s\n", $2, $3, $4
                if ($3 == "0") cpus[0]++
                else other_cpus[$3]++
            }
        }
    ' | head -50
    
    echo ""
    
    # Count threads on each CPU
    if command -v awk >/dev/null 2>&1; then
        CPU_COUNTS=$(ps -eLo pid,psr 2>/dev/null | awk -v pid="$PID" '$1 == pid && NR > 1 {print $2}' | sort | uniq -c)
        echo "Thread Distribution:"
        echo "$CPU_COUNTS" | awk '{printf "  CPU %s: %s threads\n", $2, $1}'
        echo ""
        
        # Summary
        THREADS_ON_CPU0=$(echo "$CPU_COUNTS" | awk '$2 == "0" {print $1}' | head -1)
        TOTAL_THREADS=$(ps -eLo pid 2>/dev/null | awk -v pid="$PID" '$1 == pid' | wc -l | tr -d ' ')
        OTHER_THREADS=$((TOTAL_THREADS - ${THREADS_ON_CPU0:-0}))
        
        echo "Summary:"
        echo "  Total threads: $TOTAL_THREADS"
        echo "  Threads on CPU 0: ${THREADS_ON_CPU0:-0}"
        echo "  Threads on other CPUs: $OTHER_THREADS"
        
        if [ "$OTHER_THREADS" -gt 0 ]; then
            echo ""
            echo "  ⚠ WARNING: Some threads are NOT on CPU 0!"
        else
            echo ""
            echo "  ✓ All threads are on CPU 0"
        fi
    fi
    
    echo ""
    echo "Last updated: $(date '+%Y-%m-%d %H:%M:%S')"
}

display_macos_info() {
    clear
    echo "=========================================="
    echo "Live Thread Monitor for $PROCESS_NAME (macOS)"
    echo "Process ID: $PID | Refresh: ${REFRESH_INTERVAL}s | Press Ctrl+C to exit"
    echo "=========================================="
    echo ""
    
    # Show thread count
    THREAD_COUNT=$(ps -M -p $PID 2>/dev/null | tail -n +2 | wc -l | tr -d ' ')
    echo "Total threads: $THREAD_COUNT"
    echo ""
    
    # Show recent thread list (first 20)
    echo "Thread list (showing first 20):"
    ps -M -p $PID 2>/dev/null | head -21
    
    echo ""
    echo "=========================================="
    echo "Note: On macOS, CPU core assignment cannot be verified"
    echo "      CPU binding (sched_setaffinity) only works on Linux"
    echo "      For CPU binding verification, test on Ubuntu/Linux system"
    echo ""
    echo "Last updated: $(date '+%Y-%m-%d %H:%M:%S')"
}

# Main loop
while true; do
    if ! find_process; then
        clear
        echo "=========================================="
        echo "Waiting for $PROCESS_NAME process..."
        echo "Press Ctrl+C to exit"
        echo "=========================================="
        sleep $REFRESH_INTERVAL
        continue
    fi
    
    case "$OS" in
        Linux)
            display_linux_info
            ;;
        macOS)
            display_macos_info
            ;;
        *)
            echo "Unsupported OS: $OS"
            exit 1
            ;;
    esac
    
    sleep $REFRESH_INTERVAL
done
