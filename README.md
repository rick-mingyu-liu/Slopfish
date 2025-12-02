# Team: Slopfish
- Evan:
- Rick:
- Rohit:

## Forced Capture Variant Implementation

This engine implements **Chess with Forced Captures**: if a player has any legal capture move, they **must** make a capture move. This is a mandatory capture variant where all standard chess rules apply, except captures are forced when available.

### Key Implementation Details

- **Root Level**: In `thread.cpp`, root moves are filtered to only include captures if any exist
- **Search**: The `MovePicker` class enforces forced captures during search
- **Quiescence**: QSearch respects forced capture rules - stand-pat is not allowed if captures exist
- **Check Evasions**: When in check, if capture evasions exist, only those are considered

### Strategic Differences from Standard Chess

1. **Piece Sacrifices**: More common and strategically valuable as they force opponent captures
2. **Material Imbalances**: Matter less than tactical opportunities
3. **King Safety**: Even more critical due to forced sequences
4. **Forced Sequences**: More common, making deeper search more valuable
5. **Tactical Play**: The variant encourages tactical play over positional maneuvering

### Testing

Test positions should include:
- Positions with only captures available
- Positions with both captures and quiet moves
- Check positions with capture evasions
- Edge cases: en passant, castling when captures exist

## Links for explaining the original program
https://medium.com/data-science/dissecting-stockfish-part-1-in-depth-look-at-a-chess-engine-7fddd1d83579

https://medium.com/data-science/dissecting-stockfish-part-2-in-depth-look-at-a-chess-engine-2643cdc35c9a

https://medium.com/data-science/dissecting-stockfish-part-3-in-depth-look-at-a-chess-engine-51b59e532bb4

## Compile Command
```bash
make build ARCH=native
```

## Xboard Engine Load:
```
"Slopfish" -fcp "polyglot" -fd "/Users/rickliu/Desktop/slopfish"
```
