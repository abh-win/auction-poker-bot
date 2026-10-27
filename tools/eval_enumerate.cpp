// Enumerates all C(52,7) = 133,784,560 seven-card hands and checks the hand
// category distribution against the known exact counts.
#include <chrono>
#include <cstdio>

#include "poker/evaluator.h"

using namespace poker;

int main() {
  const long long expected[9] = {23294460, 58627800, 31433400, 6461620, 6180020, 4047644, 3473184, 224848, 41584};
  long long counts[9] = {0};
  auto t0 = std::chrono::steady_clock::now();
  for (int a = 0; a < 52; ++a)
    for (int b = a + 1; b < 52; ++b)
      for (int c = b + 1; c < 52; ++c)
        for (int d = c + 1; d < 52; ++d)
          for (int e = d + 1; e < 52; ++e)
            for (int f = e + 1; f < 52; ++f) {
              CardMask base = bit(a) | bit(b) | bit(c) | bit(d) | bit(e) | bit(f);
              for (int g = f + 1; g < 52; ++g) counts[categoryOf(evaluate(base | bit(g)))]++;
            }
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  bool ok = true;
  long long total = 0;
  for (int i = 0; i < 9; ++i) {
    std::printf("%-15s %10lld  expected %10lld  %s\n", categoryName(static_cast<HandCategory>(i)), counts[i], expected[i],
                counts[i] == expected[i] ? "ok" : "MISMATCH");
    ok = ok && counts[i] == expected[i];
    total += counts[i];
  }
  std::printf("%lld hands in %.2f s (%.1f ns/hand)\n", total, s, s * 1e9 / total);
  return ok ? 0 : 1;
}
