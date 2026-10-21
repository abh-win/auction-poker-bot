#pragma once

// The competition bot.
//
// Preflop   Hands are ranked by heads-up equity percentile. Opening, 3-bet and
//           defend ranges start from game-theory-inspired heads-up defaults
//           (wide SB opens, pot-odds defence) and are re-calibrated against
//           the opponent's archetype from VPIP/PFR profiling.
// Auction   Equity is estimated for the three outcomes (we win the card, they
//           win it, both get one). The bid maximises expected value against
//           the empirical distribution of the opponent's past bids. It
//           accounts for the second-price rule (the winner pays the loser's
//           bid) and for the payoff asymmetry: auction payments go into a pot
//           we part-own, so bidding up a likely winner is not free for them.
// Postflop  Bounded Monte Carlo equity against the opponent's estimated range
//           (narrowed by their aggression), with decisions driven by pot odds,
//           stack-to-pot ratio and board texture (bet sizing, bluff and
//           semi-bluff frequencies scaled by the opponent's fold-to-bet).

#include <string>
#include <vector>

#include "poker/equity.h"
#include "poker/game.h"
#include "poker/opponent_model.h"

namespace poker {

struct MainBotConfig {
  std::string name = "MainBot";
  bool profiling = true;       // exploit opponent statistics (else fixed priors)
  bool auction_ev = true;      // EV-maximising bid (else bid the card's equity value)
  int mc_samples = 1500;       // per equity estimate
  double mc_budget_us = 500;   // per equity estimate, also capped by the game clock
  uint64_t seed = 7;
};

struct BoardTexture {
  bool paired = false;
  int max_suit = 0;          // most cards of one suit on board
  int connected = 0;         // most board ranks inside any 5-rank window
  double wetness = 0;        // 0 dry .. 1 very coordinated
};
BoardTexture analyzeBoard(const std::vector<Card>& board);

class MainBot final : public Bot {
 public:
  explicit MainBot(MainBotConfig cfg = {});
  std::string name() const override { return cfg_.name; }
  void onMatchStart() override;
  void onHandStart(int seat, int round_num, const std::vector<Card>& hand) override;
  Action act(const BotView& v) override;
  void onHandEnd(const HandResult& r) override;

  const OpponentModel& opponent() const { return opp_; }
  const std::vector<double>& decisionMicros() const { return decision_us_; }

  // Exposed for tests/benchmarks.
  int chooseBid(const BotView& v);

 private:
  Action preflop(const BotView& v);
  Action postflop(const BotView& v);
  Range opponentRange(const BotView& v) const;
  int oppHoleCards(const BotView& v) const;
  double equity(const BotView& v, const Range& r, int hero_extra, int opp_cards, int samples,
                double budget_share = 1.0);
  double budgetUs(const BotView& v) const;
  Action raiseTo(const BotView& v, double pot_fraction) const;

  MainBotConfig cfg_;
  OpponentModel opp_;
  Rng rng_;
  std::vector<double> decision_us_;
  bool we_raised_preflop_ = false;
};

}  // namespace poker
