#include <gtest/gtest.h>

#include "poker/equity.h"

using namespace poker;

TEST(Preflop, ClassesAndOrdering) {
  EXPECT_EQ(handClass(parseCard("As"), parseCard("Ad")), 12);
  EXPECT_EQ(handClass(parseCard("2s"), parseCard("2d")), 0);
  EXPECT_NE(handClass(parseCard("As"), parseCard("Ks")), handClass(parseCard("As"), parseCard("Kd")));
  int total = 0;
  for (int c = 0; c < 169; ++c) total += classCombos(c);
  EXPECT_EQ(total, 1326);
  const auto& t = PreflopTable::instance();
  int aa = handClass(parseCard("As"), parseCard("Ad"));
  int seven_deuce = handClass(parseCard("7s"), parseCard("2d"));
  EXPECT_NEAR(t.equityVsRandom(aa), 0.852, 0.015);          // known: 85.2%
  EXPECT_NEAR(t.equityVsRandom(seven_deuce), 0.346, 0.02);  // known: ~34.6%
  EXPECT_LT(t.percentile(aa), 0.01);
  EXPECT_GT(t.percentile(seven_deuce), 0.95);
}

TEST(Equity, KnownMatchupsAndExactRiver) {
  Rng rng(1);
  EquityRequest q;
  q.hero = parseCards("As,Ah");
  q.max_samples = 40000;
  EXPECT_NEAR(estimateEquity(q, rng).equity, 0.852, 0.01);

  // River: nut flush vs random two cards, MC agrees with exact enumeration.
  q.hero = parseCards("Ah,Kd");
  q.board = parseCards("2h,7h,9h,Jh,3c");
  double exact = exactRiverEquity(q.hero, q.board);
  EXPECT_NEAR(estimateEquity(q, rng).equity, exact, 0.01);
  EXPECT_GT(exact, 0.95);
}

TEST(Equity, ThirdCardHelpsAndRangesBite) {
  Rng rng(2);
  EquityRequest q;
  q.hero = parseCards("8c,9c");
  q.board = parseCards("Tc,Jd,2s");
  q.max_samples = 20000;
  double two = estimateEquity(q, rng).equity;
  q.hero_extra = 1;
  double three = estimateEquity(q, rng).equity;
  EXPECT_GT(three, two + 0.05);  // an extra hole card is worth a lot

  // Against a tight range a medium hand does worse than against random.
  q.hero = parseCards("Td,9s");
  q.board.clear();
  q.hero_extra = 0;
  double vs_random = estimateEquity(q, rng).equity;
  Range tight = Range::top(0.08);
  q.opp_range = &tight;
  double vs_tight = estimateEquity(q, rng).equity;
  EXPECT_LT(vs_tight, vs_random - 0.08);
}

TEST(Equity, RespectsTimeBudget) {
  Rng rng(3);
  EquityRequest q;
  q.hero = parseCards("As,Kd");
  q.board = parseCards("2h,7h,9h");
  q.max_samples = 10'000'000;
  q.time_budget_us = 300;
  auto r = estimateEquity(q, rng);
  EXPECT_LT(r.micros, 1500);
  EXPECT_GT(r.samples, 100);
}
