#include "poker/main_bot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace poker {

BoardTexture analyzeBoard(const std::vector<Card>& board) {
  BoardTexture t;
  int suit[4] = {0, 0, 0, 0}, rank_count[13] = {0};
  uint32_t ranks = 0;
  for (Card c : board) {
    suit[suitOf(c)]++;
    rank_count[rankOf(c)]++;
    ranks |= 1u << rankOf(c);
  }
  for (int s : suit) t.max_suit = std::max(t.max_suit, s);
  for (int r : rank_count) t.paired = t.paired || r >= 2;
  uint32_t ext = (ranks << 1) | (ranks >> 12 & 1);  // ace also low
  for (int lo = 0; lo <= 9; ++lo) t.connected = std::max(t.connected, __builtin_popcount((ext >> lo) & 0x1F));
  double flushy = t.max_suit >= 3 ? 1.0 : (t.max_suit == 2 ? 0.5 : 0.0);
  double straighty = t.connected >= 3 ? 1.0 : (t.connected == 2 ? 0.4 : 0.0);
  t.wetness = std::clamp(0.5 * flushy + 0.4 * straighty - (t.paired ? 0.15 : 0.0), 0.0, 1.0);
  return t;
}

MainBot::MainBot(MainBotConfig cfg) : cfg_(std::move(cfg)), rng_(cfg_.seed) {
  PreflopTable::instance();  // build tables before the clock starts
}

void MainBot::onMatchStart() {
  opp_.reset();
  rng_.reseed(cfg_.seed);
  decision_us_.clear();
}

void MainBot::onHandStart(int, int, const std::vector<Card>&) { we_raised_preflop_ = false; }

void MainBot::onHandEnd(const HandResult& r) {
  if (cfg_.profiling) opp_.observeHand(r);
}

Action MainBot::act(const BotView& v) {
  auto t0 = std::chrono::steady_clock::now();
  Action a;
  if (v.auction) a = Action::bid(chooseBid(v));
  else if (v.street == 0) a = preflop(v);
  else a = postflop(v);
  decision_us_.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
  return a;
}

// ------------------------------------------------------------------ helpers

double MainBot::budgetUs(const BotView& v) const {
  // ~4 decisions per hand on average; keep a 40% safety margin.
  double hands_left = std::max(1, kNumRounds + 1 - v.round_num);
  double per_decision = v.game_clock / (hands_left * 4.0) * 0.6 * 1e6;
  return std::clamp(per_decision, 50.0, cfg_.mc_budget_us);
}

int MainBot::oppHoleCards(const BotView& v) const {
  const int me = v.seat, opp = 1 - v.seat;
  if (v.street >= 3 && !v.auction && v.bids[opp] >= 0 && v.bids[opp] >= v.bids[me]) return 3;
  return 2;
}

Range MainBot::opponentRange(const BotView& v) const {
  const int opp = 1 - v.seat;
  int opp_raises_pre = 0, opp_aggr_post = 0;
  bool we_raised_before_opp_raise = false, our_raise_seen = false;
  for (const auto& a : *v.history) {
    if (a.action.type != ActionType::Raise) continue;
    if (a.street == 0) {
      if (a.seat == opp) {
        ++opp_raises_pre;
        if (our_raise_seen) we_raised_before_opp_raise = true;
      } else {
        our_raise_seen = true;
      }
    } else if (a.seat == opp) {
      ++opp_aggr_post;
    }
  }
  bool reraised = opp_raises_pre >= 2 || we_raised_before_opp_raise;
  double width = cfg_.profiling ? opp_.rangeWidth(opp_raises_pre > 0, reraised)
                                : (reraised ? 0.12 : (opp_raises_pre > 0 ? 0.45 : 0.7));
  // Each postflop bet/raise narrows the range - less so for aggressive players.
  double af = cfg_.profiling ? opp_.aggression() : 0.4;
  for (int i = 0; i < opp_aggr_post; ++i) width *= 0.70 + 0.3 * af;
  return Range::top(std::clamp(width, 0.05, 1.0), 0.08);
}

double MainBot::equity(const BotView& v, const Range& r, int hero_extra, int opp_cards, int samples,
                       double budget_share) {
  EquityRequest q;
  q.hero = v.hand;
  q.board = v.board;
  q.opp_cards = opp_cards;
  q.hero_extra = hero_extra;
  q.opp_range = &r;
  q.max_samples = samples;
  q.time_budget_us = budgetUs(v) * budget_share;
  return estimateEquity(q, rng_).equity;
}

Action MainBot::raiseTo(const BotView& v, double pot_fraction) const {
  const int me = v.seat;
  // Bet `pot_fraction` of the pot after calling.
  double target = v.pips[me] + v.continue_cost + pot_fraction * (v.pot + v.continue_cost);
  int to = static_cast<int>(std::lround(target));
  to = std::clamp(to, v.raise_bounds.first, v.raise_bounds.second);
  return Action::raise(to);
}

// ------------------------------------------------------------------ preflop

Action MainBot::preflop(const BotView& v) {
  const auto& table = PreflopTable::instance();
  const double pct = table.percentile(v.hand[0], v.hand[1]);  // 0 = best
  const Archetype arch = cfg_.profiling ? opp_.archetype() : Archetype::Unknown;
  const int me = v.seat;
  const bool sb_open = me == 0 && v.pips[0] == kSmallBlind && v.pips[1] == kBigBlind;
  auto check_or_fold = [&] { return v.legal.check ? Action::check() : Action::fold(); };

  if (sb_open) {
    double open = 0.80;  // heads-up SB opens wide
    switch (arch) {
      case Archetype::Nit: open = 0.90; break;             // steal relentlessly
      case Archetype::CallingStation: open = 0.55; break;  // they call: value only
      case Archetype::Maniac: open = 0.55; break;          // they 3-bet light: tighten, trap
      case Archetype::LAG: open = 0.70; break;
      default: break;
    }
    if (pct <= open && v.legal.raise) {
      int size = arch == Archetype::CallingStation ? 4 : 3;  // bigger vs stations (they pay)
      int to = std::clamp(size * kBigBlind, v.raise_bounds.first, v.raise_bounds.second);
      we_raised_preflop_ = true;
      return Action::raise(to);
    }
    // Complete with speculative hands vs passive players (cheap flops).
    if (pct <= open + 0.12 && arch != Archetype::Maniac) return Action::call();
    return Action::fold();
  }

  if (v.continue_cost == 0) {  // BB after a limp
    double raise_thr = arch == Archetype::CallingStation ? 0.40 : 0.30;
    if (pct <= raise_thr && v.legal.raise) {
      we_raised_preflop_ = true;
      return raiseTo(v, 1.0);
    }
    return Action::check();
  }

  // Facing a raise.
  Range r = opponentRange(v);
  double e = equity(v, r, 0, 2, 800);
  double po = static_cast<double>(v.continue_cost) / (v.pot + v.continue_cost);
  double margin = me == 0 ? -0.02 : 0.02;  // SB is in position postflop
  if (arch == Archetype::Maniac) margin -= 0.04;
  if (arch == Archetype::Nit) margin += 0.04;
  int eff = std::min(v.stacks[0] + v.pips[0], v.stacks[1] + v.pips[1]);

  if ((e > 0.60 || pct <= 0.03) && v.legal.raise) {
    int opp_pip = v.pips[1 - me];
    int to = 3 * opp_pip;
    if (to * 3 > eff) to = v.raise_bounds.second;  // low SPR after a 3-bet: just get it in
    we_raised_preflop_ = true;
    return Action::raise(std::clamp(to, v.raise_bounds.first, v.raise_bounds.second));
  }
  if (e >= po + margin) return v.legal.call ? Action::call() : check_or_fold();
  return check_or_fold();
}

// ------------------------------------------------------------------ auction

int MainBot::chooseBid(const BotView& v) {
  const int me = v.seat, opp = 1 - v.seat;
  const int stack = v.stacks[me], opp_stack = v.stacks[opp];
  if (stack <= 0) return 0;
  Range r = opponentRange(v);
  const int n = std::max(200, cfg_.mc_samples / 3);
  // One decision, three estimates: each gets a third of the time budget.
  const double e_win = equity(v, r, 1, 2, n, 1.0 / 3);   // we hold 3 cards
  const double e_lose = equity(v, r, 0, 3, n, 1.0 / 3);  // they hold 3 cards
  const double e_tie = equity(v, r, 1, 3, n, 1.0 / 3);   // both hold 3 cards
  const double P = v.pot;
  const double c_us = P / 2.0;  // preflop pips are equal by the time of the auction

  // EV of our final stack change given auction payments and equity e. Future
  // betting adds F per player, capped by the effective stack.
  auto ev = [&](double paid_us, double paid_op, double e) {
    double pot_after = P + paid_us + paid_op;
    double eff_after = std::min(stack - paid_us, opp_stack - paid_op);
    double F = std::max(0.0, std::min(eff_after, 0.5 * pot_after));
    return e * (pot_after + 2 * F) - (c_us + paid_us + F);
  };

  if (!cfg_.auction_ev) {
    // Truthful baseline: bid the card's value (dominant strategy in a plain
    // Vickrey auction, ignoring that payments go into the pot).
    double value = ev(0, 0, e_win) - ev(0, 0, e_lose);
    return std::clamp(static_cast<int>(std::lround(value)), 0, stack);
  }

  auto dist = cfg_.profiling ? opp_.bidDistribution() : OpponentModel().bidDistribution();
  std::vector<std::pair<int, double>> opp_bids;
  for (auto [f, w] : dist) opp_bids.emplace_back(std::clamp(static_cast<int>(std::lround(f * P)), 0, opp_stack), w);

  std::set<int> cand;
  for (double f : {0.0, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.65, 0.8, 1.0, 1.25, 1.5, 2.0, 3.0})
    cand.insert(static_cast<int>(std::lround(f * P)));
  for (auto [o, w] : opp_bids) {
    for (int d : {-1, 0, 1}) cand.insert(o + d);
  }
  int best_bid = 0;
  double best_ev = -1e18;
  for (int b : cand) {
    if (b < 0 || b > stack) continue;
    double total = 0, wsum = 0;
    for (auto [o, w] : opp_bids) {
      double val;
      if (b > o) val = ev(o, 0, e_win);          // we win, pay their bid
      else if (b < o) val = ev(0, b, e_lose);    // they win, pay our bid into the pot
      else val = ev(b, b, e_tie);                // tie: both pay, both get a card
      total += w * val;
      wsum += w;
    }
    double e = total / wsum;
    if (e > best_ev + 1e-9) {
      best_ev = e;
      best_bid = b;
    }
  }
  return best_bid;
}

// ------------------------------------------------------------------ postflop

Action MainBot::postflop(const BotView& v) {
  const Archetype arch = cfg_.profiling ? opp_.archetype() : Archetype::Unknown;
  Range r = opponentRange(v);
  const double e = equity(v, r, 0, oppHoleCards(v), cfg_.mc_samples);
  const BoardTexture tex = analyzeBoard(v.board);
  const int eff = std::min(v.stacks[0], v.stacks[1]);
  const double spr = static_cast<double>(eff) / std::max(1, v.pot);
  const bool river = v.street == 5;
  const double ftb = cfg_.profiling ? opp_.foldToBet() : 0.4;
  const double af = cfg_.profiling ? opp_.aggression() : 0.4;

  if (v.continue_cost == 0) {
    if (!v.legal.raise) return Action::check();
    double value_thr = river ? 0.66 : 0.63;
    if (arch == Archetype::CallingStation) value_thr -= 0.06;  // thin value vs stations
    if (arch == Archetype::Nit) value_thr += 0.04;
    if (e >= value_thr) {
      if (spr < 1.5 || (e > 0.85 && spr < 3.0)) return Action::raise(v.raise_bounds.second);
      double size = river ? 0.75 : 0.55 + 0.25 * tex.wetness;  // protect on wet boards
      if (arch == Archetype::CallingStation) size += 0.25;
      return raiseTo(v, size);
    }
    // Bluffs and semi-bluffs: frequency follows the opponent's fold-to-bet.
    double bluff_p = std::clamp((ftb - 0.30) * 1.2 + 0.10, 0.0, 0.55);
    if (arch == Archetype::CallingStation) bluff_p = 0.0;
    bool cbet_spot = we_raised_preflop_ && v.street == 3 && tex.wetness < 0.4;
    if (cbet_spot && rng_.uniform() < std::min(0.85, bluff_p + 0.35)) return raiseTo(v, 0.33);
    if (!river && e >= 0.35 && rng_.uniform() < bluff_p) return raiseTo(v, 0.5 + 0.2 * tex.wetness);
    if (river && e < 0.25 && rng_.uniform() < bluff_p * 0.6) return raiseTo(v, 0.7);
    return Action::check();
  }

  // Facing a bet.
  const double po = static_cast<double>(v.continue_cost) / (v.pot + v.continue_cost);
  double margin = river ? 0.02 : 0.0;
  margin += (0.40 - af) * 0.20;  // passive players' bets are stronger
  if (arch == Archetype::Maniac) margin -= 0.05;
  if (arch == Archetype::Nit) margin += 0.05;
  const double raise_thr = river ? 0.80 : 0.75;
  if (e >= raise_thr && v.legal.raise) {
    if (spr < 2.0) return Action::raise(v.raise_bounds.second);
    return raiseTo(v, 0.8);
  }
  if (e >= po + margin) return Action::call();
  if (!river && v.legal.raise && ftb > 0.6 && e > 0.3 && rng_.uniform() < 0.15) return raiseTo(v, 0.8);
  return Action::fold();
}

}  // namespace poker
