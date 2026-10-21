#pragma once

// Hand evaluator for 5..8 cards (best five-card hand among all of them, as in
// the auction variant where a player may hold three hole cards).
//
// Works directly on per-suit 13-bit rank masks with bit tricks - no lookup
// tables, ~15-25 ns per 7/8-card hand. Returned values compare like hands:
// larger is better, equal means a tie.
//
//   bits 26..29  category (0 high card .. 8 straight flush)
//   bits 13..25  primary rank mask (quad / trip / pair ranks, straight top)
//   bits  0..12  kicker / secondary rank mask

#include <cstdint>

#include "poker/cards.h"

namespace poker {

enum HandCategory : uint32_t {
  kHighCard = 0,
  kPair,
  kTwoPair,
  kTrips,
  kStraight,
  kFlush,
  kFullHouse,
  kQuads,
  kStraightFlush,
};

uint32_t evaluate(CardMask cards);

inline HandCategory categoryOf(uint32_t value) { return static_cast<HandCategory>(value >> 26); }
const char* categoryName(HandCategory c);

// Reference implementation (enumerates every 5-card subset); used by tests.
uint32_t evaluateSlow(CardMask cards);

}  // namespace poker
