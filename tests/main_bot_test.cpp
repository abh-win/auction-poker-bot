#include <gtest/gtest.h>

#include "poker/main_bot.h"

using namespace poker;

namespace {
BotView auctionView(const std::string& hand, const std::string& board, std::vector<ActionRecord>* hist) {
  BotView v{};
  v.seat = 1;
  v.round_num = 1;
  v.game_clock = 30;
  v.street = 3;
  v.auction = true;
  v.hand = parseCards(hand);
  v.board = parseCards(board);
  v.pips = {0, 0};
  v.stacks = {390, 390};
  v.bids = {-1, -1};
  v.pot = 20;
  v.continue_cost = 0;
  v.legal.bid = true;
  v.bid_bounds = {0, 390};
  v.history = hist;
  return v;
}
}  // namespace

TEST(BoardTexture, DryVsWet) {
  auto dry = analyzeBoard(parseCards("Kd,7c,2h"));
  auto wet = analyzeBoard(parseCards("9h,Th,Jh"));
  EXPECT_LT(dry.wetness, 0.3);
  EXPECT_GT(wet.wetness, 0.8);
  EXPECT_TRUE(analyzeBoard(parseCards("8d,8c,3s")).paired);
}

TEST(Auction, TruthfulBidTracksTheCardsValue) {
  std::vector<ActionRecord> hist;
  MainBotConfig c;
  c.auction_ev = false;
  MainBot bot(c);
  bot.onMatchStart();
  // Strong draw: a third card adds a lot of equity. Near-nuts: it adds little.
  int draw_bid = bot.chooseBid(auctionView("8c,9c", "Tc,Jd,2c", &hist));
  int nuts_bid = bot.chooseBid(auctionView("Ah,Ad", "As,Kd,2c", &hist));
  EXPECT_GT(draw_bid, nuts_bid);
}

TEST(Auction, PushesThePriceOnlyWhenItOwnsThePot) {
  // Second-price + payments into the pot: bidding high is cheap when our
  // equity is high (a losing bid still feeds a pot we mostly win) and costly
  // when it is low.
  std::vector<ActionRecord> hist;
  MainBot bot;
  bot.onMatchStart();
  int strong = bot.chooseBid(auctionView("Kh,Qd", "Kc,8s,3d", &hist));
  int trash = bot.chooseBid(auctionView("7d,2s", "Ac,Kh,Qc", &hist));
  EXPECT_GT(strong, trash);
}

TEST(Auction, BidsWithinStack) {
  std::vector<ActionRecord> hist;
  MainBot bot;
  bot.onMatchStart();
  auto v = auctionView("8c,9c", "Tc,Jd,2c", &hist);
  v.stacks = {390, 3};
  v.bid_bounds = {0, 3};
  EXPECT_LE(bot.chooseBid(v), 3);
}

TEST(MainBot, DecisionsAreSubMillisecondOnAverage) {
  MainBot bot;
  MainBot other;
  MatchOptions o;
  o.rounds = 200;
  playMatch(bot, other, o);
  const auto& us = bot.decisionMicros();
  ASSERT_FALSE(us.empty());
  double s = 0;
  for (double x : us) s += x;
  EXPECT_LT(s / us.size(), 1000.0);
}
