## Run overview

- Symbols: SYNTH1, SYNTH2, SYNTH3, SYNTH4
- Bands (bps): 1, 3.5, 10, 25.5
- Repeats per combination: 3
- Run length: 604-605 s
- **All runs were ended by the script's `CLOSE` timeout** rather than by the CSV finishing, so they are partial sessions.
- Fixed for all runs: participation cap = 0.15, VWAP window (ms) = 30000, max quantity = 500

## Per-symbol market data

| Symbol | Market qty | Session VWAP | Runs |
|---|---|---|---|
| SYNTH1 | 15750 | 101.2577 | 12 |
| SYNTH2 | 5670 | 249.1509 | 12 |
| SYNTH3 | 342 | 87.4198 | 12 |
| SYNTH4 | 43925 | 34.0289 | 12 |

## Results by symbol and band

Cells show the median over repeats, with the min-max range in parentheses when repeats differ. Participation is executed qty / market qty. Slippage is vs the session VWAP: negative is favourable.

| Symbol | Band (bps) | Executed qty | Participation % | Slippage (bps) | Runs |
|---|---|---|---|---|---|
| SYNTH1 | 1 | 2190 | 13.90 | -3.87 | 3 |
| SYNTH1 | 3.5 | 1745 | 11.08 | -4.29 | 3 |
| SYNTH1 | 10 | 0 | 0.00 | n/a | 3 |
| SYNTH1 | 25.5 | 0 | 0.00 | n/a | 3 |
| SYNTH2 | 1 | 795 | 14.02 | -1.15 | 3 |
| SYNTH2 | 3.5 | 310 | 5.47 | 9.44 | 3 |
| SYNTH2 | 10 | 0 | 0.00 | n/a | 3 |
| SYNTH2 | 25.5 | 0 | 0.00 | n/a | 3 |
| SYNTH3 | 1 | 23 (23 to 44) | 6.73 (6.73 to 12.87) | -6.33 (-12.58 to -6.33) | 3 |
| SYNTH3 | 3.5 | 5 | 1.46 | -24.92 | 3 |
| SYNTH3 | 10 | 0 | 0.00 | n/a | 3 |
| SYNTH3 | 25.5 | 0 | 0.00 | n/a | 3 |
| SYNTH4 | 1 | 6375 (6375 to 6450) | 14.51 (14.51 to 14.68) | -0.12 (-1.13 to -0.12) | 3 |
| SYNTH4 | 3.5 | 6375 | 14.51 | -1.96 | 3 |
| SYNTH4 | 10 | 6275 | 14.29 | -6.22 | 3 |
| SYNTH4 | 25.5 | 1300 | 2.96 | -31.62 | 3 |

No orders were accepted (executed qty 0) for: SYNTH1 at 10, 25.5 bps; SYNTH2 at 10, 25.5 bps; SYNTH3 at 10, 25.5 bps.
Widest band tested that still produced accepted orders: SYNTH1 = 3.5 bps, SYNTH2 = 3.5 bps, SYNTH3 = 3.5 bps, SYNTH4 = 25.5 bps.

## `tick-to-order` latency

Per-run percentiles pooled by taking the median across runs, and the worst run for the tail. Percentiles cannot be averaged, so this is not a pooled percentile over all samples. Times are microseconds, from `recv()` returning to just before `sendAll`.

| Symbol | Runs | p50 (median, range) | p99 median | p99 worst run | p99.9 median | p99.9 worst run |
|---|---|---|---|---|---|---|
| SYNTH1 | 12 | 2.02 (1.89 to 2.62) | 3.38 | 41.67 | 5.68 | 87.53 |
| SYNTH2 | 12 | 2.10 (1.94 to 2.56) | 4.01 | 6.54 | 6.04 | 7.36 |
| SYNTH3 | 12 | 2.25 (1.94 to 2.68) | 5.64 | 6.56 | 5.64 | 6.56 |
| SYNTH4 | 12 | 2.01 (1.90 to 2.72) | 7.57 | 37.78 | 30.19 | 40.42 |
| **all** | 48 | 2.05 (1.89 to 2.72) | 5.06 | 41.67 | 6.07 | 87.53 |

## Groups where repeats disagreed

2 of 16 groups differed between repeats, which matches the known quote/trade interleaving effect:

- SYNTH3 at 1 bps: executed qty 23, 23, 44 (rep 1, 2, ...)
- SYNTH4 at 1 bps: executed qty 6450, 6375, 6375 (rep 1, 2, ...)
