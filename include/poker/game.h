#pragma once

// Auction Hold'em (MIT Pokerbots 2024 variant) game engine.
//
// Heads-up no-limit hold'em, 400-chip stacks reset every hand, blinds 1/2,
// 1000 hands per match, seats swap every hand. Immediately after the flop is
// dealt both players submit a sealed bid (0..stack) for a third hole card:
// second-price - the higher bidder gets the card and pays the *lower* bid into
// the pot; on a tie both players pay their bid and both get a card. At
// showdown each player uses the best five of their hole cards + board.
//
// The betting logic mirrors the official engine's RoundState exactly
// (`button` counter, "raise to" amounts, min/max raise rules, split-pot
// deltas), so a bot developed here behaves identically under engine.py.
// One deliberate difference: bids are truly sealed (the official engine
// echoes the first bid to the second bidder before they act).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "poker/cards.h"

namespace poker {

constexpr int kStartingStack = 400;
constexpr int kBigBlind = 2;
constexpr int kSmallBlind = 1;
constexpr int kNumRounds = 1000;

enum class ActionType { Fold, Call, Check, Raise, Bid };

struct Action {
  ActionType type = ActionType::Check;
  int amount = 0;  // Raise: total pip after the raise ("raise to"); Bid: bid size

  static Action fold() { return {ActionType::Fold, 0}; }
  static Action call() { return {ActionType::Call, 0}; }
  static Action check() { return {ActionType::Check, 0}; }
  static Action raise(int to) { return {ActionType::Raise, to}; }
  static Action bid(int amt) { return {ActionType::Bid, amt}; }
};

std::string toString(const Action& a);

// Streets: 0 preflop, 3 flop, 4 turn, 5 river (number of visible board cards).
struct ActionRecord {
  int seat;
  int street;
  Action action;
  int pot_before;  // total chips in the pot before the action
};

struct LegalActions {
  bool fold = false, call = false, check = false, raise = false, bid = false;
};

class RoundState {
 public:
  // Deck order: hands = d[0..1], d[2..3]; board = d[4..8]; auction cards d[51], d[50].
  explicit RoundState(const std::array<Card, 52>& deck);

  int active() const { return button_ % 2; }
  bool terminal() const { return terminal_; }
  int street() const { return street_; }
  bool inAuction() const { return auction_; }
  int pot() const { return 2 * kStartingStack - stacks_[0] - stacks_[1]; }
  const std::array<int, 2>& pips() const { return pips_; }
  const std::array<int, 2>& stacks() const { return stacks_; }
  const std::array<int, 2>& bids() const { return bids_; }  // -1 = not submitted
  const std::vector<Card>& hand(int seat) const { return hands_[seat]; }
  std::vector<Card> board() const;
  const std::array<int, 2>& deltas() const { return deltas_; }
  bool showdown() const { return showdown_; }
  const std::vector<ActionRecord>& history() const { return history_; }
  int continueCost() const { return pips_[1 - active()] - pips_[active()]; }

  LegalActions legalActions() const;
  std::pair<int, int> raiseBounds() const;  // "raise to" bounds
  std::pair<int, int> bidBounds() const;

  // Client-side use (engine adapter): replace the cards a seat holds and the
  // visible board with what the server revealed. Betting logic is unaffected.
  void overrideCards(int seat, const std::vector<Card>& hand, const std::vector<Card>& board);

  // Applies an action for the active player. Illegal actions are coerced the
  // way the official engine does (check if legal, else fold; bid 0).
  void apply(Action a);

 private:
  void proceedStreet();
  void finishShowdown();
  void finishFold(int folder);

  std::array<Card, 52> deck_;
  int button_ = 0;
  int street_ = 0;
  bool auction_ = false;
  bool terminal_ = false;
  bool showdown_ = false;
  std::array<int, 2> bids_{-1, -1};
  std::array<int, 2> pips_{kSmallBlind, kBigBlind};
  std::array<int, 2> stacks_{kStartingStack - kSmallBlind, kStartingStack - kBigBlind};
  std::array<std::vector<Card>, 2> hands_;
  std::array<int, 2> deltas_{0, 0};
  std::vector<ActionRecord> history_;
};

// ------------------------------------------------------------------ bots

// What a bot sees when asked to act.
struct BotView {
  int seat;                       // 0 = small blind (acts first preflop)
  int round_num;                  // 1-based
  double game_clock;              // seconds left (engine-enforced in matches)
  int street;
  bool auction;
  std::vector<Card> hand;         // 2 or 3 cards
  std::vector<Card> board;
  std::array<int, 2> pips, stacks, bids;
  int pot;
  int continue_cost;
  LegalActions legal;
  std::pair<int, int> raise_bounds, bid_bounds;
  const std::vector<ActionRecord>* history;  // this hand, both players
};

struct HandResult {
  int seat;
  int round_num;
  int delta;                       // chips won (+) / lost (-) this hand
  bool showdown;
  std::vector<Card> my_hand;
  std::vector<Card> opp_hand;      // empty unless showdown
  std::vector<Card> board;         // cards dealt so far
  std::array<int, 2> bids;         // -1 if no auction happened
  const std::vector<ActionRecord>* history;
};

class Bot {
 public:
  virtual ~Bot() = default;
  virtual std::string name() const = 0;
  virtual void onMatchStart() {}
  virtual void onHandStart(int /*seat*/, int /*round_num*/, const std::vector<Card>& /*hand*/) {}
  virtual Action act(const BotView& view) = 0;
  virtual void onHandEnd(const HandResult& /*result*/) {}
};

struct MatchResult {
  std::array<long long, 2> bankroll{0, 0};  // indexed by bot (not seat)
  std::array<double, 2> time_used{0, 0};    // seconds spent inside act()
  std::array<int, 2> timeouts{0, 0};
  int hands = 0;
};

struct MatchOptions {
  int rounds = kNumRounds;
  uint64_t seed = 1;
  double game_clock = 30.0;  // per bot; a bot out of time checks/folds/bids 0
  bool enforce_clock = true;
};

// Plays a full match. Bot `a` sits in seat 0 on odd rounds, like the official
// engine's alternating seats.
MatchResult playMatch(Bot& a, Bot& b, const MatchOptions& opts);

// Duplicate match: plays `rounds` decks twice with seats swapped, so card luck
// cancels out. Returns bankroll per bot over 2*rounds hands.
MatchResult playDuplicateMatch(Bot& a, Bot& b, const MatchOptions& opts);

}  // namespace poker
