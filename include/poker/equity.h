#pragma once

// Equity estimation.
//
// * Preflop: the 169 starting-hand classes are ranked once by heads-up equity
//   vs a random hand; a "range" is then a weight per class (e.g. the top 35%
//   of hands by that ranking, weighted by combo count).
// * Postflop: bounded Monte Carlo. Opponent holdings are sampled from their
//   estimated range by rejection; the remaining board is completed at random.
//   Sampling stops at `max_samples` or when the time budget runs out, which
//   keeps a decision well under a millisecond.

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

#include "poker/cards.h"

namespace poker {

// 0..168: pairs 0..12 (22..AA), then suited, then offsuit combinations.
int handClass(Card a, Card b);
int classCombos(int cls);  // 6 pairs, 4 suited, 12 offsuit

class PreflopTable {
 public:
  static const PreflopTable& instance();  // computed once (~50 ms, fixed seed)

  double equityVsRandom(int cls) const { return equity_[cls]; }
  // Fraction of all combos that are at least as strong (0 = AA, ~1 = 72o).
  double percentile(int cls) const { return percentile_[cls]; }
  double percentile(Card a, Card b) const { return percentile_[handClass(a, b)]; }

 private:
  PreflopTable();
  std::array<double, 169> equity_{};
  std::array<double, 169> percentile_{};
};

// Probability weight per starting-hand class.
struct Range {
  std::array<float, 169> w;
  static Range all() {
    Range r;
    r.w.fill(1.0f);
    return r;
  }
  // Top `frac` of hands by preflop strength, with a soft edge of width `soft`.
  static Range top(double frac, double soft = 0.05);
  // Hands between percentiles lo..hi (e.g. a calling range that is capped).
  static Range band(double lo, double hi, double soft = 0.05);
  bool isUniform() const;
};

struct EquityRequest {
  std::vector<Card> hero;       // 2 or 3 cards
  std::vector<Card> board;      // 0..5 cards
  int opp_cards = 2;            // opponent hole-card count (2 or 3)
  int hero_extra = 0;           // extra random hole cards to add to hero (auction what-ifs)
  const Range* opp_range = nullptr;  // nullptr = uniform
  int max_samples = 2000;
  double time_budget_us = 1e9;  // stop early when exceeded (checked every 64 samples)
};

struct EquityResult {
  double equity = 0;  // P(win) + P(tie)/2
  int samples = 0;
  double micros = 0;
};

EquityResult estimateEquity(const EquityRequest& req, Rng& rng);

// Exact river equity vs a uniform two-card opponent (enumerates all combos).
double exactRiverEquity(const std::vector<Card>& hero, const std::vector<Card>& board);

}  // namespace poker
