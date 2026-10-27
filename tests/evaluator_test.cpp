#include <gtest/gtest.h>

#include "poker/evaluator.h"

using namespace poker;

namespace {
uint32_t ev(const std::string& s) { return evaluate(maskOf(parseCards(s, ' '))); }
}  // namespace

TEST(Evaluator, Categories) {
  EXPECT_EQ(categoryOf(ev("As Ks Qs Js Ts 2d 3c")), kStraightFlush);
  EXPECT_EQ(categoryOf(ev("5h 4h 3h 2h Ah Kd Kc")), kStraightFlush);  // steel wheel
  EXPECT_EQ(categoryOf(ev("9c 9d 9h 9s 2d 3c 4h")), kQuads);
  EXPECT_EQ(categoryOf(ev("9c 9d 9h 2s 2d 3c 4h")), kFullHouse);
  EXPECT_EQ(categoryOf(ev("9c 9d 9h 2s 2d 2c 4h")), kFullHouse);  // two sets
  EXPECT_EQ(categoryOf(ev("Ac 9c 7c 4c 2c Kd Qh")), kFlush);
  EXPECT_EQ(categoryOf(ev("5d 4c 3h 2s Ad Kd Qh")), kStraight);  // wheel
  EXPECT_EQ(categoryOf(ev("Td 9c 8h 7s 6d 2d 2h")), kStraight);
  EXPECT_EQ(categoryOf(ev("Td Tc Th 7s 6d 2d 3h")), kTrips);
  EXPECT_EQ(categoryOf(ev("Td Tc 7h 7s 6d 6c 3h")), kTwoPair);
  EXPECT_EQ(categoryOf(ev("Td Tc 8h 7s 5d 4c 2h")), kPair);
  EXPECT_EQ(categoryOf(ev("Ad Jc 8h 7s 5d 4c 2h")), kHighCard);
}

TEST(Evaluator, Ordering) {
  EXPECT_GT(ev("As Ad Kc Kd 2h 3h 9s"), ev("As Ad Qc Qd Jh Th 9s"));  // AAKK > AAQQ
  EXPECT_GT(ev("6d 5c 4h 3s 2d Kd Qh"), ev("5d 4c 3h 2s Ad Kd Qh"));  // 6-high > wheel
  EXPECT_EQ(ev("Ah Kh 9c 8d 4s 3c 2c"), ev("As Kd 9h 8c 4d 3s 2d"));  // identical ranks tie
  EXPECT_GT(ev("Td Tc 8h 7s 5d 4c Ah"), ev("Td Tc 8h 7s 5d 4c Kh"));  // kicker
}

TEST(Evaluator, EightCardsFlushVsFullHouse) {
  // 5 hearts + a full house among 8 cards: full house wins.
  EXPECT_EQ(categoryOf(ev("2h 5h 9h Jh Kh Kd Kc 9s")), kFullHouse);
  EXPECT_EQ(categoryOf(ev("2h 5h 9h Jh Kh Kd Kc Ks")), kQuads);
}

TEST(Evaluator, MatchesBruteForceOnRandomHands) {
  Rng rng(5);
  for (int n : {5, 6, 7, 8}) {
    for (int i = 0; i < 20000; ++i) {
      CardMask m = 0;
      for (int k = 0; k < n; ++k) drawCard(rng, m);
      ASSERT_EQ(evaluate(m), evaluateSlow(m)) << n << " cards, mask " << m;
    }
  }
}

TEST(Evaluator, ComparisonsAgreeWithBruteForce) {
  Rng rng(6);
  for (int i = 0; i < 20000; ++i) {
    CardMask a = 0, b = 0, dead = 0;
    for (int k = 0; k < 7; ++k) a |= bit(drawCard(rng, dead));
    for (int k = 0; k < 7; ++k) b |= bit(drawCard(rng, dead));
    int fast = (evaluate(a) > evaluate(b)) - (evaluate(a) < evaluate(b));
    int slow = (evaluateSlow(a) > evaluateSlow(b)) - (evaluateSlow(a) < evaluateSlow(b));
    ASSERT_EQ(fast, slow);
  }
}
