# Team: Slopfish
- Evan:
- Rick:
- Rohit:

## Forced Capture Variant Implementation

This engine implements **Chess with Forced Captures**: if a player has any legal capture move, they **must** make a capture move.

### Key Features

1. **Forced Capture Enforcement** - At all search levels (root, main search, quiescence, check evasions)
2. **Opening Cache** - Learns and remembers best moves for faster play
3. **Instant First Move** - White plays e4 instantly in starting position
4. **Strategic Evaluation** - Bonus for positions where opponent must capture
5. **Move Ordering** - Prioritizes checking captures

### Opening Cache

The engine includes a learning cache that:
- Stores position → best move mappings
- Only caches deeply-searched moves (depth ≥ 8)
- Respects forced capture rules
- Saves to `slopfish_opening_cache.bin` on exit
- Loads automatically on startup
- Improves over time as more games are played

### Files Modified

| File | Changes |
|------|---------|
| `opening_cache.h` | New - Opening cache implementation |
| `thread.cpp` | Cache lookup, forced captures at root, instant e4 |
| `search.cpp` | Cache storage, quiescence forced captures |
| `movepick.cpp` | Check evasion forced captures, move ordering |
| `evaluate.cpp` | Evaluation bonus for forced capture positions |
| `position.h/cpp` | `has_forced_captures()` helper function |
| `engine.cpp/h` | Cache load/save on startup/exit |

## Links for explaining the original program
https://medium.com/data-science/dissecting-stockfish-part-1-in-depth-look-at-a-chess-engine-7fddd1d83579

https://medium.com/data-science/dissecting-stockfish-part-2-in-depth-look-at-a-chess-engine-2643cdc35c9a

https://medium.com/data-science/dissecting-stockfish-part-3-in-depth-look-at-a-chess-engine-51b59e532bb4

## Compile Command
```bash
cd src
make build ARCH=native
```

## Xboard Engine Load:
```
"Slopfish" -fcp "polyglot" -fd "/Users/rickliu/Desktop/slopfish"
```
