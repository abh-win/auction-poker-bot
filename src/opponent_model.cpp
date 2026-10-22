#include "poker/opponent_model.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace poker {

const char* toString(Archetype a) {
  switch (a) {
    case Archetype::Unknown: return "unknown";
    case Archetype::Nit: return "nit";
    case Archetype::TAG: return "TAG";
    case Archetype::LAG: return "LAG";
    case Archetype::CallingStation: return "calling-station";
    case Archetype::Maniac: return "maniac";
  }
  return "?";
}

// Priors roughly match a competent heads-up player.
OpponentModel::OpponentModel()
    : vpip_(0.60, 20), pfr_(0.40, 20), three_bet_(0.15, 10), agg_(0.40, 20), fold_to_bet_(0.40, 15),
      wtsd_(0.35, 10) {}

void OpponentModel::observeHand(const HandResult& r) {
  const int me = r.seat, opp = 1 - r.seat;
  const auto& h = *r.history;
  ++hands_;

  bool vpip = false, pfr = false, acted_pre = false;
  bool faced_raise_pre = false, three_bet = false;
  bool saw_flop = false;
  int last_raiser_pre = -1;
  for (size_t i = 0; i < h.size(); ++i) {
    const ActionRecord& a = h[i];
    if (a.street == 0) {
      if (a.seat == opp) {
        acted_pre = true;
        if (last_raiser_pre == me) {
          faced_raise_pre = true;
          if (a.action.type == ActionType::Raise) three_bet = true;
        }
        if (a.action.type == ActionType::Call || a.action.type == ActionType::Raise) vpip = true;
        if (a.action.type == ActionType::Raise) pfr = true;
      }
      if (a.action.type == ActionType::Raise) last_raiser_pre = a.seat;
    } else {
      saw_flop = true;
      if (a.seat != opp || a.action.type == ActionType::Bid) continue;
      agg_.add(a.action.type == ActionType::Raise);
      // Facing a bet: our most recent action this street was a bet/raise.
      for (size_t j = i; j-- > 0;) {
        if (h[j].street != a.street || h[j].action.type == ActionType::Bid) break;
        if (h[j].seat == me) {
          if (h[j].action.type == ActionType::Raise) fold_to_bet_.add(a.action.type == ActionType::Fold);
          break;
        }
      }
    }
  }
  if (acted_pre) {
    vpip_.add(vpip);
    pfr_.add(pfr);
  }
  if (faced_raise_pre) three_bet_.add(three_bet);
  if (saw_flop) wtsd_.add(r.showdown);

  if (r.bids[opp] >= 0) {
    // Pot at the time of the auction = pot before the first bid record.
    for (const auto& a : h) {
      if (a.action.type == ActionType::Bid) {
        int pot = std::max(1, a.pot_before);
        bid_fracs_.push_back(static_cast<double>(r.bids[opp]) / pot);
        break;
      }
    }
  }
}

Archetype OpponentModel::archetype() const {
  if (hands_ < 15) return Archetype::Unknown;
  double v = vpip(), p = pfr(), af = aggression();
  double ratio = v > 0 ? p / v : 0;
  if (v < 0.30) return Archetype::Nit;
  if (v > 0.70 && ratio > 0.60 && af > 0.35) return Archetype::Maniac;
  if (v > 0.55 && ratio < 0.35) return Archetype::CallingStation;
  if (v > 0.60 && ratio > 0.45) return Archetype::LAG;
  return Archetype::TAG;
}

double OpponentModel::rangeWidth(bool opp_raised_preflop, bool opp_reraised) const {
  double w = opp_reraised ? threeBet() * vpip() : (opp_raised_preflop ? pfr() : vpip());
  return std::clamp(w, 0.04, 1.0);
}

std::vector<std::pair<double, double>> OpponentModel::bidDistribution() const {
  // Weak prior spread over plausible bid sizes; total prior weight 4.
  std::vector<std::pair<double, double>> d = {{0.0, 0.8}, {0.15, 0.8}, {0.35, 0.8}, {0.7, 0.8}, {1.5, 0.8}};
  // Recent behaviour matters more (bots adapt): exponential recency weights.
  const size_t n = bid_fracs_.size();
  for (size_t i = 0; i < n; ++i) {
    double age = static_cast<double>(n - 1 - i);
    d.emplace_back(bid_fracs_[i], std::max(0.2, std::pow(0.995, age)));
  }
  return d;
}

double OpponentModel::meanBidFraction() const {
  auto d = bidDistribution();
  double s = 0, w = 0;
  for (auto [f, wt] : d) {
    s += f * wt;
    w += wt;
  }
  return s / w;
}

}  // namespace poker
