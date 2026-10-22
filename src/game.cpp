#include "poker/game.h"

#include <algorithm>
#include <chrono>
#include <numeric>

#include "poker/evaluator.h"

namespace poker {

std::string toString(const Action& a) {
  switch (a.type) {
    case ActionType::Fold: return "F";
    case ActionType::Call: return "C";
    case ActionType::Check: return "K";
    case ActionType::Raise: return "R" + std::to_string(a.amount);
    case ActionType::Bid: return "A" + std::to_string(a.amount);
  }
  return "?";
}

RoundState::RoundState(const std::array<Card, 52>& deck) : deck_(deck) {
  hands_[0] = {deck_[0], deck_[1]};
  hands_[1] = {deck_[2], deck_[3]};
}

std::vector<Card> RoundState::board() const {
  return std::vector<Card>(deck_.begin() + 4, deck_.begin() + 4 + street_);
}

void RoundState::overrideCards(int seat, const std::vector<Card>& hand, const std::vector<Card>& board) {
  if (!hand.empty()) hands_[seat] = hand;
  for (size_t i = 0; i < board.size() && i < 5; ++i) deck_[4 + i] = board[i];
}

LegalActions RoundState::legalActions() const {
  LegalActions la;
  if (terminal_) return la;
  if (auction_) {
    la.bid = true;
    return la;
  }
  const int a = active();
  const int cost = continueCost();
  if (cost == 0) {
    la.check = true;
    la.raise = !(stacks_[0] == 0 || stacks_[1] == 0);
    return la;
  }
  la.fold = la.call = true;
  la.raise = !(cost >= stacks_[a] || stacks_[1 - a] == 0);
  return la;
}

std::pair<int, int> RoundState::raiseBounds() const {
  const int a = active();
  const int cost = continueCost();
  const int max_contribution = std::min(stacks_[a], stacks_[1 - a] + cost);
  const int min_contribution = std::min(max_contribution, cost + std::max(cost, kBigBlind));
  return {pips_[a] + min_contribution, pips_[a] + max_contribution};
}

std::pair<int, int> RoundState::bidBounds() const { return {0, stacks_[active()]}; }

void RoundState::finishFold(int folder) {
  terminal_ = true;
  int delta0 = folder == 0 ? stacks_[0] - kStartingStack : kStartingStack - stacks_[1];
  deltas_ = {delta0, -delta0};
}

void RoundState::finishShowdown() {
  terminal_ = true;
  showdown_ = true;
  street_ = 5;
  CardMask board = maskOf(this->board());
  uint32_t s0 = evaluate(board | maskOf(hands_[0]));
  uint32_t s1 = evaluate(board | maskOf(hands_[1]));
  int delta0;
  if (s0 > s1) delta0 = kStartingStack - stacks_[1];
  else if (s0 < s1) delta0 = stacks_[0] - kStartingStack;
  else delta0 = (stacks_[0] - stacks_[1]) / 2;  // split (auction payments may differ)
  deltas_ = {delta0, -delta0};
}

void RoundState::proceedStreet() {
  if (street_ == 5) {
    finishShowdown();
    return;
  }
  pips_ = {0, 0};
  button_ = 1;
  if (street_ == 0) {
    street_ = 3;
    auction_ = true;  // flop dealt, auction before flop betting
    return;
  }
  street_ += 1;
}

void RoundState::apply(Action act) {
  if (terminal_) return;
  const LegalActions la = legalActions();
  const int a = active();

  // Coerce illegal actions like engine.py.
  bool legal = (act.type == ActionType::Fold && la.fold) || (act.type == ActionType::Call && la.call) ||
               (act.type == ActionType::Check && la.check) || (act.type == ActionType::Raise && la.raise) ||
               (act.type == ActionType::Bid && la.bid);
  if (legal && act.type == ActionType::Raise) {
    auto [lo, hi] = raiseBounds();
    legal = act.amount >= lo && act.amount <= hi;
  }
  if (legal && act.type == ActionType::Bid) {
    auto [lo, hi] = bidBounds();
    legal = act.amount >= lo && act.amount <= hi;
  }
  if (!legal) {
    if (la.bid) act = Action::bid(0);
    else act = la.check ? Action::check() : Action::fold();
  }
  history_.push_back({a, street_, act, pot()});

  switch (act.type) {
    case ActionType::Fold:
      finishFold(a);
      return;
    case ActionType::Call: {
      if (button_ == 0) {  // small blind completes preflop; big blind keeps the option
        pips_ = {kBigBlind, kBigBlind};
        stacks_ = {kStartingStack - kBigBlind, kStartingStack - kBigBlind};
        button_ = 1;
        return;
      }
      int contribution = pips_[1 - a] - pips_[a];
      stacks_[a] -= contribution;
      pips_[a] += contribution;
      button_ += 1;
      proceedStreet();
      return;
    }
    case ActionType::Check:
      if ((street_ == 0 && button_ > 0) || button_ > 1) {
        proceedStreet();
      } else {
        button_ += 1;
      }
      return;
    case ActionType::Raise: {
      int contribution = act.amount - pips_[a];
      stacks_[a] -= contribution;
      pips_[a] += contribution;
      button_ += 1;
      return;
    }
    case ActionType::Bid: {
      bids_[a] = act.amount;
      if (bids_[0] < 0 || bids_[1] < 0) {
        button_ += 1;
        return;
      }
      if (bids_[0] == bids_[1]) {
        hands_[0].push_back(deck_[51]);
        hands_[1].push_back(deck_[50]);
        stacks_[0] -= bids_[0];
        stacks_[1] -= bids_[1];
      } else {
        int winner = bids_[0] > bids_[1] ? 0 : 1;
        hands_[winner].push_back(deck_[51]);
        stacks_[winner] -= bids_[1 - winner];
      }
      auction_ = false;
      button_ = 1;
      return;
    }
  }
}

// ------------------------------------------------------------------ matches

namespace {

using Clock = std::chrono::steady_clock;

std::array<Card, 52> shuffledDeck(Rng& rng) {
  std::array<Card, 52> d;
  std::iota(d.begin(), d.end(), 0);
  for (int i = 51; i > 0; --i) std::swap(d[i], d[rng.below(static_cast<uint32_t>(i + 1))]);
  return d;
}

// Plays one hand; bots[s] sits in seat s. Returns seat deltas.
std::array<int, 2> playHand(std::array<Bot*, 2> bots, const std::array<Card, 52>& deck, int round_num,
                            std::array<double, 2>& clock, std::array<double, 2>& used,
                            std::array<int, 2>& timeouts, bool enforce) {
  RoundState st(deck);
  for (int s = 0; s < 2; ++s) bots[s]->onHandStart(s, round_num, st.hand(s));
  while (!st.terminal()) {
    const int a = st.active();
    BotView v;
    v.seat = a;
    v.round_num = round_num;
    v.game_clock = clock[a];
    v.street = st.street();
    v.auction = st.inAuction();
    v.hand = st.hand(a);
    v.board = st.board();
    v.pips = st.pips();
    v.stacks = st.stacks();
    v.bids = st.bids();
    if (v.auction) v.bids = {-1, -1};  // sealed: never reveal the other bid early
    v.pot = st.pot();
    v.continue_cost = st.continueCost();
    v.legal = st.legalActions();
    v.raise_bounds = st.raiseBounds();
    v.bid_bounds = st.bidBounds();
    v.history = &st.history();

    Action act;
    if (enforce && clock[a] <= 0) {
      act = v.legal.bid ? Action::bid(0) : (v.legal.check ? Action::check() : Action::fold());
    } else {
      auto t0 = Clock::now();
      act = bots[a]->act(v);
      double dt = std::chrono::duration<double>(Clock::now() - t0).count();
      clock[a] -= dt;
      used[a] += dt;
      if (enforce && clock[a] <= 0) timeouts[a]++;
    }
    st.apply(act);
  }
  for (int s = 0; s < 2; ++s) {
    HandResult r;
    r.seat = s;
    r.round_num = round_num;
    r.delta = st.deltas()[s];
    r.showdown = st.showdown();
    r.my_hand = st.hand(s);
    if (st.showdown()) r.opp_hand = st.hand(1 - s);
    r.board = st.board();
    r.bids = st.bids();
    r.history = &st.history();
    bots[s]->onHandEnd(r);
  }
  return st.deltas();
}

}  // namespace

MatchResult playMatch(Bot& a, Bot& b, const MatchOptions& opts) {
  MatchResult m;
  Rng rng(opts.seed);
  std::array<double, 2> clock{opts.game_clock, opts.game_clock};  // by bot
  a.onMatchStart();
  b.onMatchStart();
  for (int r = 1; r <= opts.rounds; ++r) {
    auto deck = shuffledDeck(rng);
    bool a_first = (r % 2) == 1;
    std::array<Bot*, 2> seats = a_first ? std::array<Bot*, 2>{&a, &b} : std::array<Bot*, 2>{&b, &a};
    std::array<double, 2> seat_clock = a_first ? clock : std::array<double, 2>{clock[1], clock[0]};
    std::array<double, 2> seat_used{0, 0};
    std::array<int, 2> seat_to{0, 0};
    auto d = playHand(seats, deck, r, seat_clock, seat_used, seat_to, opts.enforce_clock);
    int ia = a_first ? 0 : 1;
    m.bankroll[0] += d[ia];
    m.bankroll[1] += d[1 - ia];
    clock = a_first ? seat_clock : std::array<double, 2>{seat_clock[1], seat_clock[0]};
    m.time_used[0] += seat_used[ia];
    m.time_used[1] += seat_used[1 - ia];
    m.timeouts[0] += seat_to[ia];
    m.timeouts[1] += seat_to[1 - ia];
    m.hands++;
  }
  return m;
}

MatchResult playDuplicateMatch(Bot& a, Bot& b, const MatchOptions& opts) {
  MatchResult first = playMatch(a, b, opts);
  MatchResult second = playMatch(b, a, opts);  // same seed => same decks, seats swapped
  MatchResult m;
  m.bankroll = {first.bankroll[0] + second.bankroll[1], first.bankroll[1] + second.bankroll[0]};
  m.time_used = {first.time_used[0] + second.time_used[1], first.time_used[1] + second.time_used[0]};
  m.timeouts = {first.timeouts[0] + second.timeouts[1], first.timeouts[1] + second.timeouts[0]};
  m.hands = first.hands + second.hands;
  return m;
}

}  // namespace poker
