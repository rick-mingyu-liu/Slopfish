# Team: Slopfish
- Rick
- Evan
- Rohit
  
## Slopfish is based on the original Stockfish Program: 
We used the Stockfish program as a foundation and changed the evaluation-related files to achieve the variant of captures are forced.

https://github.com/official-stockfish/Stockfish

## Links for explaining the original program (Stockfish)
https://medium.com/data-science/dissecting-stockfish-part-1-in-depth-look-at-a-chess-engine-7fddd1d83579

https://medium.com/data-science/dissecting-stockfish-part-2-in-depth-look-at-a-chess-engine-2643cdc35c9a

https://medium.com/data-science/dissecting-stockfish-part-3-in-depth-look-at-a-chess-engine-51b59e532bb4

## Compile Command
```bash
cd src
make build ARCH=native
```

## Xboard Engine Load:
After compile the program, then edit the engine list with the actual location of the folder (we name it slopfish), below is an example of how to load the engine with polyglot
```
"Slopfish" -fcp "polyglot" -fd "/Users/rickliu/Desktop/slopfish"
```
