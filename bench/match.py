#!/usr/bin/env python3
"""Head-to-head match runner for forced-capture chess, with Elo + error bars.

The ARBITER enforces the variant rule -- it does not trust the engines.
An engine that returns a non-capture while a capture is available has made
an illegal move and forfeits. That makes this a correctness test as well
as a strength test.

Usage:
  python3 match.py --a /path/engineA --b /path/engineB \
      --name-a full --name-b no-qsearch-fix \
      --book openings.epd --rounds 200 --tc 10+0.1 --concurrency 3
"""
import argparse, math, random, sys, time, json
import chess, chess.engine
from concurrent.futures import ProcessPoolExecutor, as_completed


def variant_legal(board):
    caps = [m for m in board.legal_moves if board.is_capture(m)]
    return caps if caps else list(board.legal_moves)


def parse_tc(s):
    if "+" in s:
        base, inc = s.split("+")
    else:
        base, inc = s, "0"
    return float(base), float(inc)


def play_game(job):
    """Return (result_for_white, plies, reason). result: 1.0 / 0.5 / 0.0"""
    line, white_path, black_path, tc, gid = job
    base, inc = parse_tc(tc)
    wc = bc = base
    board = chess.Board()
    for u in line.split():
        board.push(chess.Move.from_uci(u))   # keeps move_stack -> "position startpos moves ..."

    we = be = None
    try:
        we = chess.engine.SimpleEngine.popen_uci(white_path)
        be = chess.engine.SimpleEngine.popen_uci(black_path)
        for e in (we, be):
            try:
                e.configure({"Threads": 1, "Hash": 64})
            except Exception:
                pass  # rejected by the min-8 clamp; recorded by caller

        plies = 0
        while plies < 600:
            legal = variant_legal(board)
            if not legal:
                break
            if board.is_game_over(claim_draw=True):
                break

            eng = we if board.turn == chess.WHITE else be
            limit = chess.engine.Limit(white_clock=wc, black_clock=bc,
                                       white_inc=inc, black_inc=inc)
            t0 = time.perf_counter()
            try:
                res = eng.play(board, limit)
            except Exception as ex:
                loser_white = board.turn == chess.WHITE
                return (0.0 if loser_white else 1.0, plies, f"crash:{type(ex).__name__}")
            elapsed = time.perf_counter() - t0

            if board.turn == chess.WHITE:
                wc -= elapsed - inc
                if wc <= 0:
                    return (0.0, plies, "time-forfeit-white")
            else:
                bc -= elapsed - inc
                if bc <= 0:
                    return (1.0, plies, "time-forfeit-black")

            mv = res.move
            if mv is None or mv not in legal:
                # ILLEGAL under the variant rule -> forfeit
                loser_white = board.turn == chess.WHITE
                why = "illegal-declined-capture" if (mv in board.legal_moves) else "illegal-move"
                try:
                    with open("illegal_cases.log", "a") as fh:
                        fh.write(f"{why}\tply={plies}\tplayed={mv}\t"
                                 f"engine={'W:'+white_path.split('-')[-1] if loser_white else 'B:'+black_path.split('-')[-1]}\t"
                                 f"fen={board.fen()}\tlegal={[str(x) for x in legal]}\n")
                except Exception:
                    pass
                return (0.0 if loser_white else 1.0, plies, why)

            board.push(mv)
            plies += 1

        outcome = board.outcome(claim_draw=True)
        if outcome is None:
            return (0.5, plies, "max-plies")
        if outcome.winner is None:
            return (0.5, plies, outcome.termination.name)
        return (1.0 if outcome.winner == chess.WHITE else 0.0, plies,
                outcome.termination.name)
    finally:
        for e in (we, be):
            if e is not None:
                try:
                    e.quit()
                except Exception:
                    pass


def elo_with_ci(w, d, l):
    n = w + d + l
    if n == 0:
        return 0.0, 0.0, 0.5
    s = (w + 0.5 * d) / n
    if s <= 0 or s >= 1:
        return (float("inf") if s >= 1 else float("-inf")), 0.0, s
    elo = -400.0 * math.log10(1.0 / s - 1.0)
    var = (w * (1 - s) ** 2 + d * (0.5 - s) ** 2 + l * (0 - s) ** 2) / n
    se = math.sqrt(var / n)
    grad = 400.0 / (math.log(10) * s * (1 - s))
    return elo, 1.96 * se * grad, s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True); ap.add_argument("--b", required=True)
    ap.add_argument("--name-a", default="A"); ap.add_argument("--name-b", default="B")
    ap.add_argument("--book", default="openings.moves")
    ap.add_argument("--rounds", type=int, default=200,
                    help="openings used; each is played twice (colors reversed)")
    ap.add_argument("--tc", default="10+0.1")
    ap.add_argument("--concurrency", type=int, default=3)
    ap.add_argument("--out", default="match_result.json")
    args = ap.parse_args()

    book = [l.strip() for l in open(args.book) if l.strip()]
    random.seed(7); random.shuffle(book)
    book = book[:args.rounds]

    jobs = []
    for i, epd in enumerate(book):
        jobs.append((epd, args.a, args.b, args.tc, f"{i}w"))   # A white
        jobs.append((epd, args.b, args.a, args.tc, f"{i}b"))   # A black

    w = d = l = 0
    reasons, plies_all = {}, []
    t0 = time.time()
    done = 0
    with ProcessPoolExecutor(max_workers=args.concurrency) as ex:
        futs = {ex.submit(play_game, j): j for j in jobs}
        for f in as_completed(futs):
            job = futs[f]
            a_is_white = job[4].endswith("w")
            try:
                res_white, plies, why = f.result()
            except Exception as e:
                reasons[f"harness:{type(e).__name__}"] = reasons.get(f"harness:{type(e).__name__}", 0) + 1
                continue
            a_score = res_white if a_is_white else 1.0 - res_white
            if   a_score == 1.0: w += 1
            elif a_score == 0.0: l += 1
            else:                d += 1
            reasons[why] = reasons.get(why, 0) + 1
            plies_all.append(plies)
            done += 1
            if done % 10 == 0 or done == len(jobs):
                elo, ci, s = elo_with_ci(w, d, l)
                rate = done / max(1e-9, time.time() - t0)
                eta = (len(jobs) - done) / max(1e-9, rate)
                print(f"[{done}/{len(jobs)}] {args.name_a} +{w} ={d} -{l}  "
                      f"score={s:.3f}  Elo={elo:+.1f} ± {ci:.1f}  "
                      f"({rate*3600:.0f} games/h, ETA {eta/60:.0f} min)", flush=True)

    elo, ci, s = elo_with_ci(w, d, l)
    summary = {
        "engine_a": args.name_a, "engine_b": args.name_b, "tc": args.tc,
        "games": w + d + l, "wins": w, "draws": d, "losses": l,
        "score": s, "elo": elo, "elo_95ci": ci,
        "mean_plies": sum(plies_all) / max(1, len(plies_all)),
        "termination_reasons": reasons,
        "wall_seconds": time.time() - t0,
    }
    print("\n" + json.dumps(summary, indent=2))
    json.dump(summary, open(args.out, "w"), indent=2)
    print(f"\n{args.name_a} vs {args.name_b}: {elo:+.1f} ± {ci:.1f} Elo over {w+d+l} games")


if __name__ == "__main__":
    main()
