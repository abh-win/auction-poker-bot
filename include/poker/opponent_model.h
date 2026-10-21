#pragma once

// Online opponent profiling from the hand history the engine provides.
//
// Frequencies are Beta-smoothed toward a population prior so that early in a
// match (few observations) the bot plays a sound default strategy, and moves
// toward exploitation as evidence accumulates:  p = (k + a) / (n + a + b).
//
// Tracked
//   VPIP   voluntarily put chips in preflop (call/raise; BB checking doesn't count)
//   PFR    raised preflop
//   3-bet  re-raised preflop when facing a raise
//   AF     postflop aggression frequency: (bets + raises) / (bets + raises + calls + checks)
//   FtB    folds when facing a postflop bet/raise
//   WTSD   went to showdown after seeing the flop
//   bids   auction bids as a fraction of the pot (empirical distribution)
//
// Archetypes (VPIP, PFR/VPIP, AF), calibrated for heads-up where solid
// players already enter ~50% of pots:
//   Nit             VPIP < 0.30                              - folds too much
//   Calling station VPIP > 0.55, PFR/VPIP < 0.35             - loose-passive
//   Maniac          VPIP > 0.70, PFR/VPIP > 0.60, AF > 0.35  - hyper-aggressive
//   LAG             VPIP > 0.60, PFR/VPIP > 0.45
//   TAG             otherwise (balanced; play close to the default strategy)

#include <array>
#include <string>
#include <vector>

#include "poker/game.h"

namespace poker {

enum class Archetype { Unknown, Nit, TAG, LAG, CallingStation, Maniac };
const char* toString(Archetype a);

struct BetaStat {
  double k = 0, n = 0;
  double a, b;  // prior pseudo-counts
  BetaStat(double prior_mean, double strength) : a(prior_mean * strength), b((1 - prior_mean) * strength) {}
  void add(bool hit) {
    k += hit;
    n += 1;
  }
  double mean() const { return (k + a) / (n + a + b); }
};

class OpponentModel {
 public:
  OpponentModel();
  void reset() { *this = OpponentModel(); }

  // Updates from a finished hand (we are `result.seat`).
  void observeHand(const HandResult& result);

  double vpip() const { return vpip_.mean(); }
  double pfr() const { return pfr_.mean(); }
  double threeBet() const { return three_bet_.mean(); }
  double aggression() const { return agg_.mean(); }
  double foldToBet() const { return fold_to_bet_.mean(); }
  double wtsd() const { return wtsd_.mean(); }
  int handsObserved() const { return hands_; }

  Archetype archetype() const;

  // Estimated preflop range width (fraction of hands) given how the opponent
  // entered the pot this hand.
  double rangeWidth(bool opp_raised_preflop, bool opp_reraised) const;

  // Opponent bids as pot fractions: observed values plus a weak prior.
  // Returns (fraction, weight) support points.
  std::vector<std::pair<double, double>> bidDistribution() const;
  double meanBidFraction() const;

 private:
  BetaStat vpip_, pfr_, three_bet_, agg_, fold_to_bet_, wtsd_;
  std::vector<double> bid_fracs_;
  int hands_ = 0;
};

}  // namespace poker
