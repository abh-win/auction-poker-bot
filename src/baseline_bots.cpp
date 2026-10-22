#include "poker/baseline_bots.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "poker/evaluator.h"

namespace poker {
namespace {

Action potRaise(const BotView& v, double frac) {
  double target = v.pips[v.seat] + v.continue_cost + frac * (v.pot + v.continue_cost);
  int to = std::clamp(static_cast<int>(std::lround(target)), v.raise_bounds.first, v.raise_bounds.second);
  return Action::raise(to);
}

Action passive(const BotView& v) { return v.legal.check ? Action::check() : Action::call(); }

// Made-hand category using hole cards + board.
HandCategory madeHand(const BotView& v) {
  return categoryOf(evaluate(maskOf(v.hand) | maskOf(v.board)));
}

}  // namespace

Action RandomBot::act(const BotView& v) {
  if (v.auction) return Action::bid(static_cast<int>(rng_.below(static_cast<uint32_t>(std::min(v.stacks[v.seat], v.pot) + 1))));
  double u = rng_.uniform();
  if (v.legal.raise && u < 0.2) {
    auto [lo, hi] = v.raise_bounds;
    return Action::raise(lo + static_cast<int>(rng_.below(static_cast<uint32_t>(std::min(hi - lo, v.pot) + 1))));
  }
  if (v.legal.fold && u < 0.45) return Action::fold();
  return passive(v);
}

Action CallingStationBot::act(const BotView& v) {
  if (v.auction) return Action::bid(std::min(v.stacks[v.seat], v.pot / 10));
  return passive(v);
}

Action NitBot::act(const BotView& v) {
  if (v.auction) return Action::bid(std::min(v.stacks[v.seat], v.pot / 5));
  if (v.street == 0) {
    double pct = PreflopTable::instance().percentile(v.hand[0], v.hand[1]);
    if (pct <= 0.05 && v.legal.raise) return potRaise(v, 1.0);
    if (pct <= 0.15) return passive(v);
    return v.legal.check ? Action::check() : Action::fold();
  }
  HandCategory c = madeHand(v);
  if (c >= kTwoPair && v.legal.raise && rng_.uniform() < 0.7) return potRaise(v, 0.6);
  if (c >= kPair) return passive(v);
  return v.legal.check ? Action::check() : Action::fold();
}

Action ManiacBot::act(const BotView& v) {
  if (v.auction) return Action::bid(std::min(v.stacks[v.seat], v.pot));
  if (v.legal.raise && rng_.uniform() < (v.street == 0 ? 0.75 : 0.6)) return potRaise(v, 1.0);
  if (v.legal.fold && v.street > 0 && madeHand(v) == kHighCard && rng_.uniform() < 0.15) return Action::fold();
  return passive(v);
}

Action TagBot::act(const BotView& v) {
  if (v.auction) {
    // Third cards matter more with draws/weak holdings; ~30% of the pot.
    return Action::bid(std::min(v.stacks[v.seat], static_cast<int>(0.3 * v.pot)));
  }
  if (v.street == 0) {
    double pct = PreflopTable::instance().percentile(v.hand[0], v.hand[1]);
    if (v.continue_cost <= kSmallBlind && v.pips[v.seat] < kBigBlind) {  // SB open
      if (pct <= 0.55 && v.legal.raise) return potRaise(v, 1.0);
      return Action::fold();
    }
    if (pct <= 0.12 && v.legal.raise) return potRaise(v, 1.0);
    if (pct <= 0.45 || v.continue_cost == 0) return passive(v);
    return Action::fold();
  }
  EquityRequest q;
  q.hero = v.hand;
  q.board = v.board;
  q.opp_cards = 2;
  q.max_samples = 400;
  double e = estimateEquity(q, rng_).equity;
  if (v.continue_cost == 0) {
    if (e > 0.65 && v.legal.raise) return potRaise(v, 0.66);
    return Action::check();
  }
  double po = static_cast<double>(v.continue_cost) / (v.pot + v.continue_cost);
  if (e > 0.8 && v.legal.raise) return potRaise(v, 0.8);
  if (e > po + 0.05) return Action::call();
  return Action::fold();
}

Action AllInBidderBot::act(const BotView& v) {
  if (v.auction) return Action::bid(v.stacks[v.seat]);
  return tag_.act(v);
}

std::unique_ptr<Bot> makeBaseline(const std::string& name, uint64_t seed) {
  if (name == "random") return std::make_unique<RandomBot>(seed);
  if (name == "station") return std::make_unique<CallingStationBot>();
  if (name == "nit") return std::make_unique<NitBot>(seed);
  if (name == "maniac") return std::make_unique<ManiacBot>(seed);
  if (name == "tag") return std::make_unique<TagBot>(seed);
  if (name == "allin-bidder") return std::make_unique<AllInBidderBot>();
  throw std::invalid_argument("unknown baseline bot: " + name);
}

}  // namespace poker
