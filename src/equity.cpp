#include "poker/equity.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "poker/evaluator.h"

namespace poker {

int handClass(Card a, Card b) {
  int ra = rankOf(a), rb = rankOf(b);
  if (ra < rb) std::swap(ra, rb);
  if (ra == rb) return ra;                        // 0..12
  int idx = ra * (ra - 1) / 2 + rb;               // 0..77 over (hi > lo)
  return suitOf(a) == suitOf(b) ? 13 + idx : 91 + idx;
}

int classCombos(int cls) { return cls < 13 ? 6 : (cls < 91 ? 4 : 12); }

PreflopTable::PreflopTable() {
  // Equity of one representative combo per class vs a random hand.
  Rng rng(12345);
  std::array<std::pair<Card, Card>, 169> rep{};
  for (Card a = 0; a < 52; ++a)
    for (Card b = 0; b < 52; ++b)
      if (a != b) rep[handClass(a, b)] = {a, b};
  constexpr int kSamples = 6000;
  for (int cls = 0; cls < 169; ++cls) {
    auto [a, b] = rep[cls];
    double win = 0;
    for (int s = 0; s < kSamples; ++s) {
      CardMask dead = bit(a) | bit(b);
      Card o1 = drawCard(rng, dead), o2 = drawCard(rng, dead);
      CardMask board = 0;
      for (int k = 0; k < 5; ++k) board |= bit(drawCard(rng, dead));
      uint32_t h = evaluate(board | bit(a) | bit(b)), o = evaluate(board | bit(o1) | bit(o2));
      win += h > o ? 1.0 : (h == o ? 0.5 : 0.0);
    }
    equity_[cls] = win / kSamples;
  }
  std::array<int, 169> order;
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int x, int y) { return equity_[x] > equity_[y]; });
  double cum = 0;
  for (int cls : order) {
    cum += classCombos(cls);
    percentile_[cls] = cum / 1326.0;
  }
}

const PreflopTable& PreflopTable::instance() {
  static const PreflopTable t;
  return t;
}

Range Range::top(double frac, double soft) { return band(0.0, frac, soft); }

Range Range::band(double lo, double hi, double soft) {
  const auto& t = PreflopTable::instance();
  Range r;
  for (int c = 0; c < 169; ++c) {
    double p = t.percentile(c);
    auto edge = [&](double x) { return std::clamp(x / std::max(soft, 1e-9) + 0.5, 0.0, 1.0); };
    double w = edge(hi - p) * (lo <= 0 ? 1.0 : edge(p - lo));
    r.w[c] = static_cast<float>(std::max(w, 0.02));  // never fully exclude a hand
  }
  return r;
}

bool Range::isUniform() const {
  return std::all_of(w.begin(), w.end(), [](float x) { return x >= 0.999f; });
}

EquityResult estimateEquity(const EquityRequest& req, Rng& rng) {
  using Clock = std::chrono::steady_clock;
  auto t0 = Clock::now();
  EquityResult res;
  const CardMask hero = maskOf(req.hero);
  const CardMask board = maskOf(req.board);
  const int board_missing = 5 - static_cast<int>(req.board.size());
  const bool weighted = req.opp_range != nullptr && !req.opp_range->isUniform();
  double score = 0;
  int n = 0;
  for (; n < req.max_samples; ++n) {
    if ((n & 63) == 63) {
      double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
      if (us > req.time_budget_us) break;
    }
    CardMask dead = hero | board;
    // Opponent's first two cards from their range (rejection sampling).
    Card o1, o2;
    for (int tries = 0;; ++tries) {
      CardMask d = dead;
      o1 = drawCard(rng, d);
      o2 = drawCard(rng, d);
      if (!weighted || tries > 50 || rng.uniform() < req.opp_range->w[handClass(o1, o2)]) {
        dead = d;
        break;
      }
    }
    CardMask opp = bit(o1) | bit(o2);
    for (int k = 2; k < req.opp_cards; ++k) opp |= bit(drawCard(rng, dead));
    CardMask h = hero;
    for (int k = 0; k < req.hero_extra; ++k) h |= bit(drawCard(rng, dead));
    CardMask b = board;
    for (int k = 0; k < board_missing; ++k) b |= bit(drawCard(rng, dead));
    uint32_t hv = evaluate(h | b), ov = evaluate(opp | b);
    score += hv > ov ? 1.0 : (hv == ov ? 0.5 : 0.0);
  }
  res.samples = n;
  res.equity = n ? score / n : 0.5;
  res.micros = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
  return res;
}

double exactRiverEquity(const std::vector<Card>& hero, const std::vector<Card>& board) {
  const CardMask hm = maskOf(hero), bm = maskOf(board);
  const uint32_t hv = evaluate(hm | bm);
  double score = 0;
  int n = 0;
  for (Card a = 0; a < 52; ++a) {
    if ((hm | bm) & bit(a)) continue;
    for (Card b = a + 1; b < 52; ++b) {
      if ((hm | bm) & bit(b)) continue;
      uint32_t ov = evaluate(bm | bit(a) | bit(b));
      score += hv > ov ? 1.0 : (hv == ov ? 0.5 : 0.0);
      ++n;
    }
  }
  return score / n;
}

}  // namespace poker
