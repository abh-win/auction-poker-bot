# Algorithmic Poker Bot: Auction Hold'em

A C++17 bot for **MIT Pokerbots 2024 "Auction Hold'em"**. The game is heads-up no-limit hold'em
with 400-chip stacks that reset every hand, 1/2 blinds, 1,000 hands per match and a 30-second
total clock. Right after the flop, both players submit a sealed bid for a **third hole card**. The
auction is second-price: the winner pays the *loser's* bid into the pot, and on a tie both players
pay and both get a card.

The bot has three parts:

- **Exploitative profiling.** It tracks VPIP/PFR and other stats with Bayesian smoothing,
  classifies the opponent's archetype and recalibrates its game-theoretic betting ranges against it.
- **Bounded Monte Carlo equity.** Postflop equity is estimated by Monte Carlo against the
  opponent's estimated range, and decisions factor in SPR and board texture. Latency is
  sub-millisecond.
- **Marginal-EV auction.** The bid maximises expected value against the opponent's empirical bid
  distribution, using the second-price rule and the asymmetric payoff of paying into a shared pot.

The project includes a full rules-exact game engine and an adapter that plays through the
**official `engine.py`**.

## Components

| | file | notes |
|---|---|---|
| Hand evaluator | `src/evaluator.cpp` | Best 5 of 5–8 cards (three hole cards + board) using per-suit rank bitmasks, no lookup tables. **19 ns/hand.** Verified against all 133,784,560 seven-card hands (exact category counts, `eval_enumerate`) and against an independent brute-force evaluator. |
| Game engine | `src/game.cpp` | Mirrors the official `RoundState`: button counter, "raise to" bounds, BB option, auction resolution, split-pot deltas with unequal auction payments, and illegal-action coercion. Matches alternate seats, and **duplicate matches** replay every deck with seats swapped to cancel card luck. Bids are truly sealed. (The official engine echoes the first bid to the second bidder; the bot does not rely on that.) |
| Equity | `src/equity.cpp` | 169-class preflop table (equity vs random, combo-weighted percentiles) and **bounded Monte Carlo**: opponent hands are drawn from a weighted range by rejection sampling, and sampling stops at N samples or when the time budget runs out (checked every 64 samples). Uses a xoshiro256** RNG. |
| Opponent model | `src/opponent_model.cpp` | Beta-smoothed VPIP, PFR, 3-bet, aggression frequency, fold-to-bet, WTSD and an empirical bid-size distribution with recency weighting. Classifies the opponent as **Nit / TAG / LAG / Calling station / Maniac**, and maps the stats to range width (`top x%`). |
| Strategy | `src/main_bot.cpp` | See below. |
| Baselines | `src/baseline_bots.cpp` | Random, CallingStation, Nit, Maniac, TAG (equity thresholds) and AllInBidder. |
| Engine adapter | `adapter/mit_adapter.cpp` | Socket client for the official engine protocol (`T P H F C K R A N B O D Q`). It mirrors the round with the same `RoundState` and checks its stacks against the server's after every auction. |

### Strategy

**Preflop.** Hands are ranked by heads-up equity percentile. The defaults are game-theory-inspired
heads-up ranges: the SB opens about 80%, 3-bets with more than 60% equity against the raiser's
range, and defends by pot odds with a position adjustment. The profile then recalibrates them:

| archetype | adjustment |
|---|---|
| Nit | open 90% (steal) and give their bets more credit |
| Calling station | open 55% with bigger sizing, value-bet thinner, never bluff |
| Maniac | open 55%, trap, call down lighter |
| LAG | open 70% |

**Postflop.** Equity comes from Monte Carlo against the opponent's range: their preflop range
(from PFR, VPIP or 3-bet frequency), narrowed for each postflop bet or raise, and narrowed less for
high-aggression players.

- **Value bets:** above an equity threshold; sized up on wet boards (texture = flush/straight
  coordination, pairing). It shoves when SPR is below 1.5, or below 3 with very high equity.
- **Bluffs and semi-bluffs:** frequency scales with the opponent's fold-to-bet; there is a c-bet
  on dry flops after raising preflop.
- **Facing bets:** call when equity ≥ pot odds + margin. The margin is set from the opponent's
  aggression frequency (passive players' bets are stronger) and archetype. It raises for value
  above 75–80% equity.

**Auction.** The bot estimates its equity for each outcome: winning the card (3 v 2 cards), losing
it (2 v 3) and tying (3 v 3). It models its final stack change as
`e·(pot + payments + 2F) − (own contribution + own payment + F)`, where F is expected future
betting capped by the effective stack. It evaluates candidate bids (pot fractions, plus ±1 around
each observed opponent bid) against the opponent's bid distribution:

- **Win** (b > o): pay o.
- **Lose** (b < o): the opponent pays *b* into the pot.
- **Tie:** both pay.

Because payments land in a pot the bot partly owns, a losing bid is not free for the winner. With
high equity the bot pushes the price up on aggressive bidders; with low equity it bids only the
card's value. The ablation **`main-truthful`** instead bids the card's equity value, the dominant
strategy in a plain Vickrey auction.

**Time management.** Each decision's budget comes from the remaining game clock
(`clock / (hands_left · 4) · 0.6`), capped at 500 µs per estimate. Auction decisions split the
budget across their three estimates.

## Build & run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/poker_tests                  # 25 GoogleTest cases
./build/eval_enumerate               # exhaustive 7-card evaluator check
./build/latency_bench                # evaluator + equity latency
./build/arena --matches 20           # MainBot vs all baselines (duplicate matches)
./build/arena --bot main --opps main-basic,main-noprofile,main-truthful --matches 30

# Official engine (needs Python + eval7: python -m venv .venv && .venv/bin/pip install eval7)
scripts/run_mit_engine.sh self       # self-play through engine.py
scripts/run_mit_engine.sh caller     # vs a minimal Python socket bot
```

## Results

The figures below come from `results/arena.txt`. Each cell covers 20 duplicate matches of
2 × 1,000 hands (40,000 hands), reported in big blinds per 100 hands with a 95% CI. In
`main-noprofile` the opponent model never updates (fixed priors); `main-truthful` bids the card's
equity value; `main-basic` has both features off.

| opponent | **main** | main-noprofile | main-truthful | main-basic |
|---|---|---|---|---|
| TAG (equity thresholds) | **+602 ± 45** | +597 ± 58 | +594 ± 48 | +615 ± 57 |
| Calling station | **+2861 ± 58** | +1008 ± 15 | +2852 ± 61 | +1006 ± 15 |
| Nit | +209 ± 21 | +226 ± 21 | +198 ± 21 | +232 ± 24 |
| Maniac | **+4016 ± 107** | +4130 ± 105 | +945 ± 68 | +737 ± 57 |
| Random | **+1552 ± 29** | +1478 ± 50 | +1067 ± 41 | +979 ± 29 |
| All-in bidder | **+2002 ± 58** | +61 ± 36 | +39 ± 25 | +14 ± 28 |

The full bot won 120 of 120 matches against the baselines.

- **Profiling** almost triples the win rate against the calling station (value-heavy ranges,
  bigger sizing, no bluffs). Combined with the EV auction, it turns the all-in bidder from about
  break-even into +2,000 bb/100: once the bot learns that the opponent always bids its stack, it
  bids just below it whenever its equity justifies forcing them to pay.
- **The EV auction** is worth +3,000 bb/100 against the maniac (who bids the pot) and +500 against
  random bidding.
- **Head-to-head** (30 duplicate matches, 60,000 hands each), the full bot beats `main-basic`
  by +145 ± 25, `main-noprofile` by +132 ± 29 (28/30) and `main-truthful` by +203 ± 34 (30/30).
- **Neutral cases:** profiling makes no measurable difference against the nit and the TAG bot;
  the confidence intervals overlap.

**Latency** (`latency_bench`, single thread, i5-1235U):

| operation | mean | p99 |
|---|---|---|
| 7-card evaluation | 31 ns (random-access benchmark), 19 ns (enumeration) | |
| Postflop equity, 1,500 samples | 168 µs | 193 µs |
| Postflop equity, 1,500 samples, 3 hole cards | 173 µs | 201 µs |
| Full bot decision (arena) | 160–235 µs | ~0.5–0.8 ms single-threaded (up to 1.8 ms with 12 matches on 2P+8E cores) |

**Official engine:** 1,000 hands with 0 state mismatches, about 1.6 s of the 30 s clock used, and
+54,458 chips against the Python calling station.

## Layout

```
include/poker/  cards.h evaluator.h game.h equity.h opponent_model.h main_bot.h baseline_bots.h
src/            implementation
adapter/        mit_adapter.cpp (pokerbot binary for engine.py)
tools/          arena.cpp latency_bench.cpp eval_enumerate.cpp
tests/          evaluator, equity, game rules, opponent model, bot/auction tests
scripts/        run_mit_engine.sh caller_bot.py
```

## Limitations

- The baselines are fixed-strategy bots. A strong adaptive opponent would also read the bot's
  bids, and the bid choice does not yet account for the information it reveals.
- Ranges are modelled preflop only, narrowed by aggression. There is no full postflop range
  update by board interaction.
