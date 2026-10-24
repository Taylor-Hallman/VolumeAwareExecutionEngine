# Slipstream: Volume-Aware Execution Engine

`slipstream` is a single-instrument execution checker written in C++23. It consumes a binary
market-data feed (quotes) and a binary trade feed over TCP, maintains an L1 book and a rolling
VWAP, and judges each historical trade against a VWAP band, a size ceiling and a participation
cap. Every judged trade gets a `NewOrder` reply marked Accepted (`A`) or Rejected (`R`). On exit it
prints an execution report, including `tick-to-order` latency percentiles.

Three executables are built:

| Executable   | Role |
|--------------|------|
| `slipstream` | The server. Listens on an MD port and an OE port, runs the decision engine, prints the report. |
| `md_client`  | Replays the **quotes** from a CSV over TCP to the MD port, paced by the CSV's timestamps. One-way. |
| `oe_client`  | Replays the **trades** from the same CSV over TCP to the OE port, and receives `NewOrder`, `Heartbeat` and `SessionControl` messages back. |

> **Status:** M1 (codec + replay clients), M2 (server, L1, rolling VWAP) and M3 (decision engine,
> order path, session control, heartbeat, execution report) are implemented. A second transport
> (M4, optional) is **not** implemented. See [Known issues](#known-issues-and-open-questions).

---

## Contents

1. [Build and run](#build-and-run)
2. [Architecture](#architecture)
3. [Wire protocol](#wire-protocol)
4. [Decision engine](#decision-engine)
5. [Execution report](#execution-report)
6. [Latency: `tick-to-order`](#latency-tick-to-order)
7. [Design decisions and tradeoffs](#design-decisions-and-tradeoffs)
8. [Assumptions](#assumptions)
9. [Known issues and open questions](#known-issues-and-open-questions)
10. [Testing](#testing)
11. [Reproducing the numbers](#reproducing-the-numbers)
12. [Transport comparison](#transport-comparison)

---

## Build and run

**Requirements:** Linux, CMake >= 4.3, a C++23 compiler (developed with `clang++-19`).
GoogleTest (v1.15.2) and CLI11 (v2.4.2) are fetched automatically with `FetchContent`. The core
(codec, parser, book, VWAP, decision engine) uses no third-party code. Only tests and CLI parsing do.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-19
cmake --build build
ctest --test-dir build --output-on-failure
```

**Run** (start the server first, then both clients):

```bash
# terminal 1
./build/server/slipstream \
    --symbol SYNTH4 --max-quantity 500 --participation-cap 0.15 \
    --vwap-window-ms 30000 --band-bps 25.5 \
    --md-host 127.0.0.1 --md-port 14200 --oe-host 127.0.0.1 --oe-port 14300 \
    --transport tcp

# terminal 2
./build/clients/md_client/md_client --host 127.0.0.1 --port 14200 --csv Quotes_and_Trades.csv

# terminal 3
./build/clients/oe_client/oe_client --host 127.0.0.1 --port 14300 --csv Quotes_and_Trades.csv
```

**Console commands** (typed into the server's terminal): `HALT` stops new `NewOrder` messages while
the VWAP keeps updating; `OPEN` resumes; `CLOSE` notifies the OE client, closes both connections
and prints the execution report.

**CLI notes.** `--band-bps` and `--participation-cap` are parsed from their decimal strings into
scaled integers (no floating point). More than four decimal places is rejected rather than
silently truncated. `--transport` currently only accepts `tcp`; `grpc` and `udp-multicast` are potential future work

---

## Architecture

```
  Quotes_and_Trades.csv                                   Quotes_and_Trades.csv
          |                                                         |
     [ md_client ]                                             [ oe_client ]
   replay, paced by ts                                      replay, paced by ts
          |  Quote (GCMD/1)                         Trade (GCMD/1) |   ^  NewOrder (GCOE/1)
          v                                                        v   |  Heartbeat, SessionControl
   +--------------------------- slipstream ---------------------------+
   |  MD thread                              OE thread                 |
   |  recv -> reassemble -> Decode           poll(5s) -> recv ->       |
   |  -> filter symbol -> L1State  ------->  reassemble -> Decode ->   |
   |     (mutex-guarded)       snapshot      filter symbol ->          |
   |                                         RollingVwap + totals ->   |
   |  console thread: HALT / OPEN / CLOSE    judgeTrade -> send        |
   +-------------------------------------------------------------------+
```

**Source layout** (`VAEE_lib` holds everything reusable; each executable is a thin `main`):

| Path | Contents |
|------|----------|
| `src/data/GCMD_1`, `src/data/GCOE_1` | Message structs (`Quote`, `Trade`, `Heartbeat`, `SessionControl`, `NewOrder`, `ExecReport`, `FrameHeader`) |
| `src/serialization` | `Encode` overloads and `Decode`; explicit little-endian field-by-field codec |
| `src/util` | CSV parser, fixed-point parser, stream reassembly (`processBytes`), socket helpers (`sendAll`, `createSocket`) |
| `src/decision_engine` | `judgeTrade`, `DecisionConfig`, `SessionState`, `L1Snapshot`; `RollingVwap` |
| `server/`, `clients/md_client/`, `clients/oe_client/` | The three entry points |
| `tests/` | GoogleTest suites |

**Threads in `slipstream`:** one thread per client connection (MD, OE), plus a console-input
thread. The decision engine runs inside the OE thread, so the path  from trade received to order 
sent has no hand-off between threads. Only L1 state crosses threads (MD writes, OE reads), so it 
is the only data guarded by a mutex. The rolling VWAP, session totals and order-id counter are 
touched only by the OE thread and need no locking.

**Session lifecycle.** The server waits until both clients are connected before processing.
`CLOSE` (or a client disconnecting) leads to an orderly shutdown and the report.

---

## Wire protocol

`GCMD/1` (Quote, Trade, Heartbeat, SessionControl) and `GCOE/1`
(NewOrder, ExecReport). Every message is a 4-byte header (`body_len` u16, `msg_type` u8, `version`
u8) followed by the body. All integers are little-endian. Prices are signed 64-bit fixed point
scaled by 10,000.

Choices worth knowing:

- **No struct-memcpy.** Structs are never copied onto the wire, so padding and host endianness
  cannot leak into the format. Each field is written and read individually by `append_le` /
  `read_le`, which shift and mask on the *value*. The output is identical on any host.
- **Encode writes into a caller-provided buffer** and returns the bytes written, so the hot path
  allocates nothing. The decoder returns `{optional<Message>, bytesConsumed}`, where `Message` is a
  `std::variant` of all structs that are sent over the wire.
- **`Decode` is incremental.** It returns "incomplete" (nothing consumed) for a partial frame,
  decodes one message and reports `bytesConsumed` when a full frame is present, and leaves trailing
  bytes for the next call. The server and client keep a per-connection accumulation buffer and loop
  on `Decode`, so split frames and multiple frames per `recv()` are both handled.
- **Malformed input.** A `body_len` that disagrees with the message type's fixed size is rejected
  (the fixed size, not the wire value, drives `bytesConsumed`, so a corrupt length cannot
  desynchronise the stream). An unknown `msg_type` throws.
- **`Trade` travels on the OE connection** even though its layout is defined in the `GCMD/1`
  section: the OE client sends trades to be judged, and `NewOrder.trade_id` ties each reply back to
  the trade it judges.
- **Symbols** are 12-byte zero-padded arrays and are compared as arrays, never as C strings.
- **`send` is looped** until the whole frame is written (`MSG_NOSIGNAL`, `EINTR` retried), and
  `recv` output goes through reassembly.

---

## Decision engine

For each decoded `Trade` on the symbol the server is configured for:

1. **Update the books.** Add the trade to the rolling VWAP window and to the session totals
   (market volume and notional). This happens even during warm-up, so warm-up trades still count
   as market volume and still feed the VWAP.
2. **Warm-up gate.** If fewer than 10 quotes have been observed, or less than one full
   `--vwap-window-ms` has elapsed since both clients connected, **no `NewOrder` is sent**.
3. **Judge** (`judgeTrade`, a pure function of its inputs, with no clock, locking or socket):
    1. Reject if `trade.qty > --max-quantity`.
    2. Reject if `executed + trade.qty` would exceed `cap * market volume`.
    3. Buy trigger: `best_ask <= V * (1 - B)`. Sell trigger: `best_bid >= V * (1 + B)`, where
       `V` is the rolling VWAP and `B = band_bps / 10,000`. Reject if neither fires.
    4. Otherwise accept, with side `B` if the buy trigger fired and `S` otherwise.
4. **Respond.** Send the `NewOrder` unless the session is halted.

**No floating point in decisions.** `--band-bps` is stored as an integer scaled by 10,000, so
`B = bandScaled / 10^8`. The triggers are evaluated by cross-multiplication in `__int128`:

```
buy:   ask * 10^8 <= vwap * (10^8 - bandScaled)
sell:  bid * 10^8 >= vwap * (10^8 + bandScaled)
```

The participation cap is likewise `10,000 * (executed + qty) <= capScaled * marketVolume`, with
no division. Every product is widened to `__int128` before it is multiplied.

**L1 state** is a four-field struct (best bid/ask price and quantity) that each matching `Quote`
overwrites. A `Quote` is already a top-of-book snapshot, so no price levels or order matching are
involved. The OE thread copies all fields under one lock acquisition so a judgement always sees one
consistent quote. Bid and ask quantities are stored but not used in any decision.

---

## Execution report

Printed on exit, after the connections are closed:

| Line | Definition |
|------|-----------|
| market qty | Total quantity of all trades seen for the symbol (including during warm-up) |
| executed qty (%) | Total quantity of accepted orders, and its share of market qty |
| avg fill price | Quantity-weighted average of the **trade's own price** over accepted orders |
| session VWAP | Whole-session `sum(px*qty)/sum(qty)` over all trades, kept separately from the rolling window |
| slippage vs VWAP | See below |
| participation (cap) | Executed qty / market qty, next to the configured cap |
| tick-to-order p50 / p99 / p99.9 | Latency percentiles, and the number of samples |

**Slippage** is computed per side so buys and sells do not cancel each other. Relative to the
session VWAP `V`, a buy's cost is `px - V` and a sell's cost is `V - px`:

```
cost         = (buy_notional - V * buy_qty) + (V * sell_qty - sell_notional)
slippage_bps = cost * 10,000 / (V * (buy_qty + sell_qty))
```

Negative is favourable, positive is unfavourable. A value of `n/a` is printed when nothing was
accepted. All accumulation is in integers (`__int128` for notionals). A `double` is used only
inside the print statements, and prices are divided by 10,000 only there.

---

## Latency: `tick-to-order`

**Definition.** One sample per `NewOrder` sent. The clock starts when `recv()` returns the bytes
containing the trade (so decoding is included) and stops immediately **before** `sendAll`, so
the cost of the `send()` syscall is excluded. Both readings are `steady_clock` values in the same
process, so no cross-process clock synchronization is involved. The cost of `send()` is recorded
separately. Trades that produce no order (warm-up, halted) have no sample.

**Percentiles** use the nearest-rank method on the sorted nanosecond samples, in integer
arithmetic, computed once at shutdown (not on the hot path). With fewer than 1,000 samples, p99.9 is
simply the maximum, so the sample count is printed next to the percentiles.
<details>
<summary><strong>Results (click to expand)</strong></summary>

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
</details>

---

**Candidate explanations for the tail** (p99 about 3x the median) <!-- TODO: confirm with the stage breakdown -->
- Cold caches and branch predictors: trades arrive milliseconds to seconds apart, so the path runs
  cold each time.
- CPU power states between events (mitigated by the `performance` governor).
- The first trade after a quiet period, and one-time page faults on first touch of memory.
- Occasional scheduler preemption, and contention on the L1 mutex if the MD thread holds it.

**Allocation discipline on the measured path.** Decode returns values (no heap), the
accumulation buffer is reserved up front, `NewOrder` is encoded into a fixed-size buffer, the
latency-sample vector is preallocated, and logging is outside the interval. <!-- TODO: verify against the final code. -->

---

## Design decisions and tradeoffs

- **Rolling VWAP: fixed-capacity ring buffer with O(1) amortised updates.** Capacity is a
  template parameter (a power of two, enforced by `static_assert`, so wraparound is a bit mask),
  which also lets tests overflow a 4-slot buffer cheaply. Running sums are updated on insert and
  eviction, so reading the VWAP is O(1) with no scan. Eviction is a *rolling* window (entries leave
  as they age past `now - window`), not a tumbling one. A tick exactly `window` old is kept.
    - **Overflow policy:** if the buffer fills before the oldest tick is old enough to expire, the
      oldest tick is overwritten and its contribution is subtracted from the running sums, so the
      window becomes "the most recent N ticks within the time window". With this project's data
      volume it should never trigger. It is covered by tests.
    - **Accumulators are `__int128`**, because `sum(px * qty)` can overflow 64 bits. This is a
      GCC/Clang extension and is not portable to MSVC. 
    - **Rounding:** VWAP is rounded to the nearest fixed-point unit in integer arithmetic
      (`(num + den/2) / den`). Decisions use the rounded VWAP, so the comparison error is at most half
      a tick.
- **Blocking sockets, thread per connection, `poll()` for the heartbeat timer.** A single-threaded 
  event loop (with poll or epoll) would remove the L1 mutex, the atomic `open` flag and stop-token 
  plumbing, and thread-start coordination. It was not adopted because it requires non-blocking I/O 
  throughout, and any slow operation on the loop would delay both connections. epoll’s advantage over 
  poll is scaling to thousands of descriptors, which isn’t relevant at about five.
- **Decision engine as plain functions.** `judgeTrade` takes its inputs by value or const
  reference (trade, quote snapshot, session state, config, VWAP) and has no clock or lock, so it is
  unit-testable without sockets.
- **Mutex, not lock-free, for L1 state.** `L1State` is 24 bytes, larger than any hardware atomic.
  A lock-free double buffer would work, but quotes arrive milliseconds apart and an uncontended
  mutex costs tens of nanoseconds, so the extra complexity buys nothing here.
- **Per-order callback from `processTradeBytes`.** A single recv can contain several trades. A
  callback sends each verdict as soon as it is made, which avoids losing earlier verdicts and
  avoids a returned container allocating on the hot path.
- **Error handling.** Startup failures (bind, bad CLI value, unreadable CSV) throw and end the
  program. Expected network events (peer closed, reset, partial frames) are reported by return
  value and are not exceptions. Each thread body is wrapped so an error triggers an orderly shutdown
  instead of `std::terminate`. <!-- TODO: confirm all thread bodies have a try/catch. -->
- **CSV parsing** uses `mmap` with `string_view` fields and `from_chars`. Parsing happens once at
  startup, so this is not on the measured path. It was done for practice. 

---

## Assumptions

1. **Timestamps are replay-relative.** CSV `HH:MM:SS.mmm` values become nanoseconds since the
   09:30:00 anchor, not Unix epoch time.
2. **Each `Trade` is judged on arrival** against the latest quote seen on the MD connection and
   the current rolling VWAP (which includes that trade). The trade is counted into market volume
   *before* it is judged, so the first trades in a session are rejected on participation until
   enough volume has accumulated.
3. **Warm-up: no order is sent** before the gate opens. The alternative reading (send `R`) is
   isolated in one check. "One full window" is measured as wall-clock time since both clients
   connected.
4. **`limit_px`** is the quote price that triggered the order (best ask for a buy, best bid for a
   sell). The report's fill price uses the **trade's own price**, held in the session totals and
   not on the wire. Rejected orders carry `limit_px = 0`, and their `side` is not meaningful.
5. **`aggressor` is `'?'`** for every trade: the CSV has no side information and nothing in the
   decision rules reads it.
6. **Quote sizes are stored but unused.** No spec rule depends on `bid_qty` or `ask_qty`.
7. **Participation** is cumulative since the start of the session, not windowed.
8. **Server-originated timestamps** (`Heartbeat`, `SessionControl`) are nanoseconds since both
   clients connected. `NewOrder.ts_ns` is the trade's own timestamp plus processing time, which keeps
   it in the replay-relative domain.
9. **Heartbeats** are sent each time 5 seconds pass with no bytes received from the OE client,
   and only bytes from the OE connection reset that timer.
10. **`HALT`** suppresses `NewOrder` messages only. Trades are still decoded and still feed the
    VWAP and the market totals.
11. **Prices are positive**, which the VWAP rounding assumes. **Input timestamps are
    non-decreasing**, which window eviction assumes. Both hold for the sorted replay CSV.

---

### Behaviour that is expected, not a bug
- **Results are not bit-for-bit reproducible between runs.** The two clients start their replay
  clocks independently, so the same trade can be judged against a different "latest quote" each
  run. Verdicts near a threshold (small `--band-bps` values especially) can flip, and the
  participation cap carries one flip forward through later trades. The rolling VWAP per trade ID
  is identical across runs, which pins the variation to quote/trade interleaving. Starting both
  clients together from one script reduces it. Judging against the quote as of the trade's feed
  timestamp would remove it, at the cost of waiting for the MD stream inside the measured path.
- **At the spec's example band (25.5 bps), the sample data triggers few or no orders.** With a
  tight spread and a mean-reverting price, quotes rarely sit that far from a 30-second VWAP.
  Rejecting nearly everything is correct output for that input. Runs at smaller bands are included
  in the experiment results. TODO: cite the band at which each symbol first triggers.
- **Slippage can be unfavourable even though every accepted order beat the rolling VWAP when it
  was decided.** The report benchmarks against the session VWAP (with hindsight), and the fill
  price is the trade's price, not the quote that triggered the order.

### Not implemented
- Second transport (gRPC, UDP multicast) and the transport interface extraction.
- `ExecReport` messages, order state tracking, `in_flight` accounting and the 250 ms timeout.
- All stretch goals (A/B arbitration, gap recovery, multi-symbol sharding, pre-trade risk gate).

### Limitations
- Only one symbol per session, by design of the spec.
- `__int128` ties the build to GCC or Clang.
- A corrupt stream (unknown `msg_type`) closes that connection. There is no resynchronisation.

---

## Testing

GoogleTest, run with `ctest --test-dir build --output-on-failure`. Set a per-test timeout so a
hang fails instead of blocking, for example
`gtest_discover_tests(VAEE_tests PROPERTIES TIMEOUT 10)`.

| Area | What is covered |
|------|-----------------|
| Codec | Round trip for each of the six message types. Exact byte-sequence checks for each field width (so a codec that is wrong in the same way on both sides still fails). Zero/max/negative boundary values. Symbols shorter than and exactly 12 characters. |
| Streaming | Every truncation length returns "incomplete". Byte-at-a-time growth. Several messages in one buffer. A message plus a partial next message. Mismatched `body_len`, unknown `msg_type`, empty and short buffers. |
| Parsing | `parseFixedPoint` shapes (integer, decimal, over-long, malformed). Timestamp conversion and anchor boundary. CSV end to end. |
| Rolling VWAP | Rounding policy, window boundary (kept at exactly `window`, evicted one nanosecond later), capacity overflow and index wraparound, empty state, `__int128` safety with near-limit values, and a differential test against a brute-force reference on seeded random input. |
| Decision engine | Accepted buy and sell, each rejection reason, exact threshold boundaries (`<=` / `>=`), a large band that would break a narrow integer type, quantity and participation boundaries, warm-up gate boundaries, symbol filtering, non-trade messages ignored, fragmented trades, and several trades in one chunk delivering every verdict in order. |
| Server loop | Reassembly and filtering through `processQuoteBytes` / `processTradeBytes` without a socket. |

Code that touches sockets and the full three-process flow are tested end to end through the
experiment script, not unit tests.

---

## Reproducing the numbers

1. Build in Release (see above) and close other CPU-heavy programs.
2. Pin the CPU governor: `sudo cpupower frequency-set -g performance`.
3. Run `./run_experiments.sh`. Parameters are at the top of the script: symbols, band values,
   participation cap, window, max quantity and repeats. Each combination runs `REPEATS` times
   because runs differ. A run ends when the CSV finishes or after `MAX_RUN_SECS`, when the script
   types `CLOSE` into the server.
4. Output is in `results/<timestamp>/`:
    - `env.txt`: CPU, governor, build type, git commit.
    - `index.csv`: parameters and exit codes per run. Check this first. A nonzero server exit or
      `has_report=0` marks a run to inspect.
    - `summary.txt`: every execution report, labelled by run.
    - one directory per run with the server and client logs and `report.txt`.

---

## Transport comparison

Only raw blocking TCP is implemented. `epoll` was considered and not adopted, as described under 
[Design decisions](#design-decisions-and-tradeoffs). gRPC and UDP multicast were out of scope.