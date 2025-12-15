#!/bin/bash

PROCESS_NAME=${1:-slopfish}
PID=$(pgrep -f "$PROCESS_NAME" | head -1)

if [ -z "$PID" ]; then
    echo "Error: $PROCESS_NAME process not found"
    exit 1
fi

echo "=========================================="
echo "Thread Information for $PROCESS_NAME (macOS)"
echo "Process ID: $PID"
echo "=========================================="
echo ""

# Show thread count
THREAD_COUNT=$(ps -M -p $PID 2>/dev/null | tail -n +2 | wc -l | tr -d ' ')
echo "Total threads: $THREAD_COUNT"
echo ""
echo "Thread list:"
ps -M -p $PID 2>/dev/null

echo ""
echo "=========================================="
echo "Note: On macOS, CPU core assignment cannot be verified"
echo "      CPU binding (sched_setaffinity) only works on Linux"
echo "      For CPU binding verification, test on Ubuntu/Linux system"
