#pragma once

// Fixed-strategy opponents used to evaluate the main bot. Each one embodies a
// classic player archetype (and a characteristic auction behaviour).

#include <memory>
#include <string>

#include "poker/equity.h"
#include "poker/game.h"

namespace poker {

// Uniformly random legal actions; random bids up to the pot.
class RandomBot final : public Bot {
 public:
  explicit RandomBot(uint64_t seed = 1) : rng_(seed) {}
  std::string name() const override { return "Random"; }
  Action act(const BotView& v) override;

 private:
  Rng rng_;
};

// Loose-passive: calls everything, never raises, bids a little.
class CallingStationBot final : public Bot {
 public:
  std::string name() const override { return "CallingStation"; }
  Action act(const BotView& v) override;
};

// Tight-passive: plays the top 15% preflop, continues only with strong made
// hands, bids conservatively.
class NitBot final : public Bot {
 public:
  explicit NitBot(uint64_t seed = 2) : rng_(seed) {}
  std::string name() const override { return "Nit"; }
  Action act(const BotView& v) override;

 private:
  Rng rng_;
};

// Hyper-aggressive: raises most hands, bets pot relentlessly, bids big.
class ManiacBot final : public Bot {
 public:
  explicit ManiacBot(uint64_t seed = 3) : rng_(seed) {}
  std::string name() const override { return "Maniac"; }
  Action act(const BotView& v) override;

 private:
  Rng rng_;
};

// Solid equity-threshold player (TAG) without opponent modelling. Bids a fixed
// fraction of the pot scaled by how much a third card helps.
class TagBot final : public Bot {
 public:
  explicit TagBot(uint64_t seed = 4) : rng_(seed) {}
  std::string name() const override { return "TAG"; }
  Action act(const BotView& v) override;

 private:
  Rng rng_;
};

// TAG betting, but always bids its entire stack in the auction (tests the
// second-price logic: it always wins the card but pays our bid).
class AllInBidderBot final : public Bot {
 public:
  std::string name() const override { return "AllInBidder"; }
  Action act(const BotView& v) override;

 private:
  TagBot tag_{5};
};

std::unique_ptr<Bot> makeBaseline(const std::string& name, uint64_t seed);

}  // namespace poker
