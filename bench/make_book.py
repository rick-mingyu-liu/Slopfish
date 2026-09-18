#!/usr/bin/env python3
"""Generate variant-legal opening positions for forced-capture chess.

A standard chess book is INVALID here: it contains positions reached by
declining a capture, which are unreachable under the variant rule. We
generate positions by random legal *variant* play instead.
"""
import chess, random, argparse

def variant_legal_moves(board):
    """Legal moves under 'captures are forced'."""
    caps = [m for m in board.legal_moves if board.is_capture(m)]
    return caps if caps else list(board.legal_moves)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plies", type=int, default=8)
    ap.add_argument("--count", type=int, default=500)
    ap.add_argument("--seed",  type=int, default=20251215)
    ap.add_argument("--out",   default="openings.moves")
    a = ap.parse_args()

    random.seed(a.seed)
    seen, out = set(), []
    attempts = 0
    while len(out) < a.count and attempts < a.count * 200:
        attempts += 1
        b = chess.Board()
        ok = True
        for _ in range(a.plies):
            mv = variant_legal_moves(b)
            if not mv:
                ok = False
                break
            b.push(random.choice(mv))
        if not ok or b.is_game_over(claim_draw=True):
            continue
        # Reject positions already decided or with a forced capture pending:
        # starting a game mid-forced-sequence biases the first move.
        if any(b.is_capture(m) for m in b.legal_moves):
            continue
        line = " ".join(m.uci() for m in b.move_stack)
        key = b.epd()
        if key in seen:
            continue
        seen.add(key)
        out.append(line)

    with open(a.out, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {len(out)} unique variant-legal opening LINES ({a.plies} plies) -> {a.out}")
    print(f"attempts={attempts}")

if __name__ == "__main__":
    main()
