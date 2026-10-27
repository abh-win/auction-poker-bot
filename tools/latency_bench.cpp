// Decision-latency benchmark for the equity engine and full bot decisions.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "poker/equity.h"
#include "poker/evaluator.h"
#include "poker/main_bot.h"

using namespace poker;
using Clock = std::chrono::steady_clock;

static void report(const char* name, std::vector<double>& us) {
  std::sort(us.begin(), us.end());
  double s = 0;
  for (double x : us) s += x;
  std::printf("%-44s mean %7.1f us  p50 %7.1f  p99 %7.1f  max %7.1f\n", name, s / us.size(), us[us.size() / 2],
              us[us.size() * 99 / 100], us.back());
}

int main() {
  Rng rng(99);
  // Raw evaluator throughput.
  {
    std::vector<CardMask> hands(1 << 20);
    for (auto& h : hands) {
      CardMask d = 0;
      for (int k = 0; k < 7; ++k) drawCard(rng, d);
      h = d;
    }
    auto t0 = Clock::now();
    uint64_t acc = 0;
    for (int rep = 0; rep < 10; ++rep)
      for (CardMask h : hands) acc += evaluate(h);
    double ns = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / (10.0 * hands.size());
    std::printf("evaluate(7 cards): %.1f ns/hand (checksum %llu)\n", ns, static_cast<unsigned long long>(acc & 0xffff));
  }
  PreflopTable::instance();
  Range wide = Range::top(0.6);
  for (int samples : {500, 1000, 1500, 3000}) {
    for (int hero_cards : {2, 3}) {
      std::vector<double> us;
      for (int i = 0; i < 2000; ++i) {
        CardMask dead = 0;
        EquityRequest q;
        for (int k = 0; k < hero_cards; ++k) q.hero.push_back(drawCard(rng, dead));
        int board_n = 3 + static_cast<int>(rng.below(3));
        for (int k = 0; k < board_n; ++k) q.board.push_back(drawCard(rng, dead));
        q.opp_range = &wide;
        q.max_samples = samples;
        us.push_back(estimateEquity(q, rng).micros);
      }
      char name[96];
      std::snprintf(name, sizeof name, "postflop equity, %d samples, %d hole cards", samples, hero_cards);
      report(name, us);
    }
  }
  return 0;
}
