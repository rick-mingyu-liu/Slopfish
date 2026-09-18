#!/usr/bin/env python3
"""Regression test: the engine must never return a non-capture when a capture exists.

Includes the two positions the match arbiter caught forfeiting, fed the same way
that triggered them (position fen ..., which resets the engine's game_ply() to 0
and makes the ply<=7 opening table fire).

Usage: python3 test_variant_invariant.py <engine> [<engine> ...]
"""
import sys, chess, chess.engine

CASES = [
    # (fen, note)
    ("r1bqkbnr/ppp1pp1p/n7/3p2P1/P7/5N1P/1PPPP1P1/RNBQKB1R b KQkq - 1 1",
     "Nf3 on board -> trap pattern 4 wants ...g5, but g7 is EMPTY and c8h3 is forced"),
    ("rnbq1kn1/ppppbppr/8/4p3/2P1P3/5P2/PP1P2PP/RNB1KBNR b KQ - 0 1",
     "e4 on board -> trap pattern 2 wants ...h5, but h7h2 is a forced capture"),
    ("rnbqkbn1/1p1p1ppr/4p3/p1p5/8/2P2P1N/PP1PP1PP/RNB1KB1R b KQq - 0 1",
     "Nh3 on board -> trap pattern 4 wants ...g5, but h7h3 is a forced capture"),
]

def variant_legal(b):
    caps = [m for m in b.legal_moves if b.is_capture(m)]
    return caps if caps else list(b.legal_moves)

def run(path):
    print(f"\n=== {path}")
    eng = chess.engine.SimpleEngine.popen_uci(path)
    try:
        try: eng.configure({"Threads": 1, "Hash": 64})
        except Exception: pass
        failures = 0
        for fen, note in CASES:
            b = chess.Board(fen)
            legal = variant_legal(b)
            try:
                mv = eng.play(b, chess.engine.Limit(time=0.5)).move
            except Exception as ex:
                print(f"  FAIL  {type(ex).__name__}: {ex}")
                print(f"        {note}")
                failures += 1
                continue
            ok = mv in legal
            print(f"  {'PASS ' if ok else 'FAIL '} played={mv}  "
                  f"variant-legal={[str(m) for m in legal][:4]}")
            if not ok:
                print(f"        {note}")
                failures += 1
        print(f"  -> {len(CASES)-failures}/{len(CASES)} passed")
        return failures
    finally:
        try: eng.quit()
        except Exception: pass

if __name__ == "__main__":
    total = sum(run(p) for p in sys.argv[1:])
    sys.exit(1 if total else 0)
