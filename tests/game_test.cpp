#include <gtest/gtest.h>

#include <numeric>

#include "poker/baseline_bots.h"
#include "poker/game.h"

using namespace poker;

namespace {
std::array<Card, 52> deckWith(const std::vector<std::string>& first) {
  std::array<Card, 52> d;
  std::vector<bool> used(52, false);
  size_t i = 0;
  for (; i < first.size(); ++i) {
    d[i] = parseCard(first[i]);
    used[d[i]] = true;
  }
  for (Card c = 0; c < 52; ++c)
    if (!used[c]) d[i++] = c;
  return d;
}
}  // namespace

TEST(Game, BlindsAndPreflopFold) {
  RoundState st(deckWith({}));
  EXPECT_EQ(st.active(), 0);
  EXPECT_EQ(st.pot(), 3);
  st.apply(Action::fold());
  ASSERT_TRUE(st.terminal());
  EXPECT_EQ(st.deltas()[0], -1);
  EXPECT_EQ(st.deltas()[1], 1);
}

TEST(Game, LimpGivesBigBlindOptionThenAuction) {
  RoundState st(deckWith({}));
  st.apply(Action::call());
  EXPECT_EQ(st.active(), 1);  // BB option
  EXPECT_TRUE(st.legalActions().check);
  st.apply(Action::check());
  EXPECT_TRUE(st.inAuction());
  EXPECT_EQ(st.street(), 3);
  EXPECT_EQ(st.active(), 1);  // BB bids first
  EXPECT_TRUE(st.legalActions().bid);
}

TEST(Game, RaiseBoundsMatchEngine) {
  RoundState st(deckWith({}));
  // SB facing 1 more: min raise to 4 (call 1 + raise by 2), max = all-in 400.
  auto [lo, hi] = st.raiseBounds();
  EXPECT_EQ(lo, 4);
  EXPECT_EQ(hi, 400);
  st.apply(Action::raise(10));
  auto [lo2, hi2] = st.raiseBounds();
  EXPECT_EQ(lo2, 18);  // BB: call 8 + raise by 8 -> to 18
  EXPECT_EQ(hi2, 400);
}

TEST(Game, SecondPriceAuctionAndThirdCard) {
  RoundState st(deckWith({}));
  st.apply(Action::call());
  st.apply(Action::check());
  st.apply(Action::bid(30));  // seat 1
  st.apply(Action::bid(12));  // seat 0
  EXPECT_FALSE(st.inAuction());
  EXPECT_EQ(st.hand(1).size(), 3u);  // higher bid wins the card
  EXPECT_EQ(st.hand(0).size(), 2u);
  EXPECT_EQ(st.stacks()[1], 398 - 12);  // pays the loser's bid
  EXPECT_EQ(st.stacks()[0], 398);
  EXPECT_EQ(st.pot(), 4 + 12);
  EXPECT_EQ(st.active(), 1);  // BB acts first on the flop
}

TEST(Game, TiedBidsBothPayBothGetCards) {
  RoundState st(deckWith({}));
  st.apply(Action::call());
  st.apply(Action::check());
  st.apply(Action::bid(7));
  st.apply(Action::bid(7));
  EXPECT_EQ(st.hand(0).size(), 3u);
  EXPECT_EQ(st.hand(1).size(), 3u);
  EXPECT_EQ(st.pot(), 4 + 14);
}

TEST(Game, ShowdownSplitWithUnequalAuctionPayments) {
  // Both play the board (royal flush on board) -> split. Seat 1 paid 5 more in the auction.
  RoundState st(deckWith({"2c", "3d", "2d", "3c", "As", "Ks", "Qs", "Js", "Ts"}));
  st.apply(Action::call());
  st.apply(Action::check());
  st.apply(Action::bid(9));
  st.apply(Action::bid(5));
  for (int i = 0; i < 6 && !st.terminal(); ++i) st.apply(Action::check());
  ASSERT_TRUE(st.terminal());
  EXPECT_TRUE(st.showdown());
  // contributions: seat0 2, seat1 2+5 -> pot 9; each gets 4.5 -> delta0 = (398-393)/2 = 2
  EXPECT_EQ(st.deltas()[0], 2);
  EXPECT_EQ(st.deltas()[1], -2);
}

TEST(Game, IllegalActionsAreCoerced) {
  RoundState st(deckWith({}));
  st.apply(Action::raise(1000));  // out of bounds -> fold (check not legal for SB)
  EXPECT_TRUE(st.terminal());
}

TEST(Match, ZeroSumAndDuplicateCancelsLuckForIdenticalBots) {
  CallingStationBot a, b;
  MatchOptions o;
  o.rounds = 200;
  auto r = playMatch(a, b, o);
  EXPECT_EQ(r.bankroll[0] + r.bankroll[1], 0);
  auto d = playDuplicateMatch(a, b, o);
  EXPECT_EQ(d.bankroll[0], 0);  // identical deterministic bots: perfectly cancelled
  EXPECT_EQ(d.hands, 400);
}
