# bench — self-play evaluation harness

Measures engine strength and enforces the forced-capture rule independently of
the engine, so a rule violation is caught instead of being scored as a loss.

## Why the arbiter matters

`match.py` validates every move the engine returns against the variant rule.
An engine that plays a non-capture while a capture is available forfeits with
`illegal-declined-capture`. This is what surfaced the `capture_stage()` bug
fixed in 09bf2db — a conventional match runner would have recorded those 25
games as ordinary losses.

## Usage

Engines must accept `Threads=1`. The shipped `engine.cpp` declares
`Option(22, 8, MaxThreads, ...)`, and Stockfish **silently discards** out-of-range
option values, so a benchmark build must restore `Option(1, 1, MaxThreads, ...)`
or every engine quietly runs 22 threads and the comparison measures nothing.

```bash
# 1. Opening book (regenerate only if you want different openings)
python3 make_book.py --plies 8 --count 500 --out openings.moves

# 2. Head-to-head match
python3 match.py --a /path/engineA --b /path/engineB \
    --name-a full --name-b noqs --book openings.moves \
    --rounds 500 --tc 5+0.05 --concurrency 3 --out result.json

# 3. Rule-invariant regression test
python3 test_variant_invariant.py /path/engine
```

`--concurrency` must not exceed `performance_cores / 2` (3 on an M3 Pro). Each
game runs two engines; oversubscribing the CPU makes the engines miss their
clocks and invalidates every time-based result.

The book must be generated, not downloaded. A standard chess opening book is
invalid here — it is full of positions reached by *declining* a capture, which
are unreachable under this variant's rule.

## Results on record

| File | Result |
|---|---|
| `elo_full_vs_noqs.json` | +4.5 ± 14.4 Elo, 1000 games — quiescence stand-pat fix costs no measurable strength |
| `verify_fixed.json` | 400 games, 0 rule violations (9.0 expected at the pre-fix rate, p ≈ 1e-4) |
