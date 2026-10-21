#pragma once

// Cards are integers 0..51: card = rank * 4 + suit, rank 0 = deuce .. 12 = ace,
// suit 0..3 = c, d, h, s. Sets of cards are 64-bit masks (bit = card id).

#include <array>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace poker {

using Card = int;
using CardMask = uint64_t;

constexpr int kNumCards = 52;
constexpr const char* kRanks = "23456789TJQKA";
constexpr const char* kSuits = "cdhs";

inline int rankOf(Card c) { return c >> 2; }
inline int suitOf(Card c) { return c & 3; }
inline CardMask bit(Card c) { return CardMask{1} << c; }

inline CardMask maskOf(const std::vector<Card>& cards) {
  CardMask m = 0;
  for (Card c : cards) m |= bit(c);
  return m;
}

inline Card parseCard(const std::string& s) {
  if (s.size() != 2) throw std::invalid_argument("bad card: " + s);
  int r = -1, su = -1;
  for (int i = 0; i < 13; ++i)
    if (kRanks[i] == s[0]) r = i;
  for (int i = 0; i < 4; ++i)
    if (kSuits[i] == s[1]) su = i;
  if (r < 0 || su < 0) throw std::invalid_argument("bad card: " + s);
  return r * 4 + su;
}

inline std::string cardString(Card c) { return {kRanks[rankOf(c)], kSuits[suitOf(c)]}; }

inline std::vector<Card> parseCards(const std::string& s, char sep = ',') {
  std::vector<Card> out;
  size_t start = 0;
  while (start < s.size()) {
    size_t e = s.find(sep, start);
    if (e == std::string::npos) e = s.size();
    if (e > start) out.push_back(parseCard(s.substr(start, e - start)));
    start = e + 1;
  }
  return out;
}

inline std::string cardsString(const std::vector<Card>& cs) {
  std::string out;
  for (size_t i = 0; i < cs.size(); ++i) out += (i ? " " : "") + cardString(cs[i]);
  return out;
}

// xoshiro256** - fast, high-quality PRNG for Monte Carlo.
class Rng {
 public:
  explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) { reseed(seed); }
  void reseed(uint64_t seed) {
    for (auto& x : s_) {
      seed += 0x9E3779B97F4A7C15ull;
      uint64_t z = seed;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      x = z ^ (z >> 31);
    }
  }
  uint64_t next() {
    uint64_t r = rotl(s_[1] * 5, 7) * 9, t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return r;
  }
  // Uniform integer in [0, n) (Lemire's multiply-shift; bias negligible for n <= 52).
  uint32_t below(uint32_t n) { return static_cast<uint32_t>((static_cast<__uint128_t>(next()) * n) >> 64); }
  double uniform() { return (next() >> 11) * 0x1.0p-53; }

  using result_type = uint64_t;
  static constexpr uint64_t min() { return 0; }
  static constexpr uint64_t max() { return ~uint64_t{0}; }
  uint64_t operator()() { return next(); }

 private:
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  uint64_t s_[4];
};

// Draws a uniformly random card not in `dead` (rejection sampling).
inline Card drawCard(Rng& rng, CardMask& dead) {
  while (true) {
    Card c = static_cast<Card>(rng.below(52));
    if (!(dead & bit(c))) {
      dead |= bit(c);
      return c;
    }
  }
}

}  // namespace poker
