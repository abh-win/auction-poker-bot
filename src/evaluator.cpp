#include "poker/evaluator.h"

#include <vector>

namespace poker {
namespace {

inline int popcount(uint32_t x) { return __builtin_popcount(x); }

// Keeps the k highest set bits of m.
inline uint32_t topBits(uint32_t m, int k) {
  while (popcount(m) > k) m &= m - 1;
  return m;
}

inline uint32_t highestBit(uint32_t m) { return m ? (1u << (31 - __builtin_clz(m))) : 0; }

// Returns a one-bit mask of the straight's top rank, or 0. Handles the wheel
// (A-2-3-4-5), whose top card is the five.
inline uint32_t straightTop(uint32_t ranks) {
  uint32_t r = ranks & (ranks << 1) & (ranks << 2) & (ranks << 3) & (ranks << 4);
  if (r) return highestBit(r);
  constexpr uint32_t kWheel = (1u << 12) | 0xF;  // A,2,3,4,5
  if ((ranks & kWheel) == kWheel) return 1u << 3;
  return 0;
}

inline uint32_t make(uint32_t cat, uint32_t primary, uint32_t kicker) {
  return (cat << 26) | (primary << 13) | kicker;
}

}  // namespace

uint32_t evaluate(CardMask cards) {
  uint32_t s[4] = {0, 0, 0, 0};
  for (CardMask m = cards; m; m &= m - 1) {
    int c = __builtin_ctzll(m);
    s[c & 3] |= 1u << (c >> 2);
  }
  const uint32_t ranks = s[0] | s[1] | s[2] | s[3];

  for (int su = 0; su < 4; ++su) {
    if (popcount(s[su]) >= 5) {
      if (uint32_t top = straightTop(s[su])) return make(kStraightFlush, top, 0);
      // A flush beats anything below; quads/full house need >= 4 non-suit
      // cards, impossible with <= 8 cards and 5 of one suit... except 8-card
      // hands (5 suited + 3 others) can hold quads or a full house.
      uint32_t quads = s[0] & s[1] & s[2] & s[3];
      if (quads) return make(kQuads, quads, highestBit(ranks & ~quads));
      uint32_t trips = (s[0] & s[1] & s[2]) | (s[0] & s[1] & s[3]) | (s[0] & s[2] & s[3]) | (s[1] & s[2] & s[3]);
      uint32_t pairs = (s[0] & s[1]) | (s[0] & s[2]) | (s[0] & s[3]) | (s[1] & s[2]) | (s[1] & s[3]) | (s[2] & s[3]);
      if (trips && popcount(pairs) >= 2) {
        uint32_t t = highestBit(trips);
        return make(kFullHouse, t, highestBit(pairs & ~t));
      }
      return make(kFlush, 0, topBits(s[su], 5));
    }
  }

  const uint32_t quads = s[0] & s[1] & s[2] & s[3];
  if (quads) return make(kQuads, quads, highestBit(ranks & ~quads));

  const uint32_t trips = (s[0] & s[1] & s[2]) | (s[0] & s[1] & s[3]) | (s[0] & s[2] & s[3]) | (s[1] & s[2] & s[3]);
  const uint32_t pairs = (s[0] & s[1]) | (s[0] & s[2]) | (s[0] & s[3]) | (s[1] & s[2]) | (s[1] & s[3]) | (s[2] & s[3]);

  if (trips) {
    uint32_t t = highestBit(trips);
    uint32_t rest_pairs = pairs & ~t;  // includes a second set of trips
    if (rest_pairs) return make(kFullHouse, t, highestBit(rest_pairs));
  }
  if (uint32_t top = straightTop(ranks)) return make(kStraight, top, 0);
  if (trips) {
    uint32_t t = highestBit(trips);
    return make(kTrips, t, topBits(ranks & ~t, 2));
  }
  if (popcount(pairs) >= 2) {
    uint32_t two = topBits(pairs, 2);
    return make(kTwoPair, two, highestBit(ranks & ~two));
  }
  if (pairs) return make(kPair, pairs, topBits(ranks & ~pairs, 3));
  return make(kHighCard, 0, topBits(ranks, 5));
}

const char* categoryName(HandCategory c) {
  static const char* kNames[] = {"high card", "pair",  "two pair",   "trips",         "straight",
                                 "flush",     "full house", "quads", "straight flush"};
  return kNames[c];
}

namespace {

// Independent textbook 5-card evaluator (rank counting + sorting).
uint32_t evaluate5Naive(const int* cs) {
  int counts[13] = {0};
  bool flush = true;
  uint32_t mask = 0;
  for (int i = 0; i < 5; ++i) {
    counts[rankOf(cs[i])]++;
    mask |= 1u << rankOf(cs[i]);
    if (suitOf(cs[i]) != suitOf(cs[0])) flush = false;
  }
  int distinct = __builtin_popcount(mask);
  int hi = 31 - __builtin_clz(mask), lo = __builtin_ctz(mask);
  int straight_top = -1;
  if (distinct == 5 && hi - lo == 4) straight_top = hi;
  if (mask == ((1u << 12) | 0xF)) straight_top = 3;  // wheel
  auto b = [](int r) { return 1u << r; };
  int quad = -1, trip = -1, pair_hi = -1, pair_lo = -1;
  for (int r = 12; r >= 0; --r) {
    if (counts[r] == 4) quad = r;
    else if (counts[r] == 3) trip = r;
    else if (counts[r] == 2) (pair_hi < 0 ? pair_hi : pair_lo) = r;
  }
  if (straight_top >= 0 && flush) return make(kStraightFlush, b(straight_top), 0);
  if (quad >= 0) return make(kQuads, b(quad), mask & ~b(quad));
  if (trip >= 0 && pair_hi >= 0) return make(kFullHouse, b(trip), b(pair_hi));
  if (flush) return make(kFlush, 0, mask);
  if (straight_top >= 0) return make(kStraight, b(straight_top), 0);
  if (trip >= 0) return make(kTrips, b(trip), mask & ~b(trip));
  if (pair_lo >= 0) return make(kTwoPair, b(pair_hi) | b(pair_lo), mask & ~(b(pair_hi) | b(pair_lo)));
  if (pair_hi >= 0) return make(kPair, b(pair_hi), mask & ~b(pair_hi));
  return make(kHighCard, 0, mask);
}

}  // namespace

uint32_t evaluateSlow(CardMask cards) {
  std::vector<int> cs;
  for (CardMask m = cards; m; m &= m - 1) cs.push_back(__builtin_ctzll(m));
  uint32_t best = 0;
  const int n = static_cast<int>(cs.size());
  for (int a = 0; a < n; ++a)
    for (int b = a + 1; b < n; ++b)
      for (int c = b + 1; c < n; ++c)
        for (int d = c + 1; d < n; ++d)
          for (int e = d + 1; e < n; ++e) {
            int five[5] = {cs[a], cs[b], cs[c], cs[d], cs[e]};
            uint32_t v = evaluate5Naive(five);
            if (v > best) best = v;
          }
  return best;
}

}  // namespace poker
