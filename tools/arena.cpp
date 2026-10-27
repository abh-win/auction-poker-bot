// arena: evaluate bots with duplicate matches.
//
//   arena [--bot main] [--opps tag,station,nit,maniac,random,allin-bidder]
//         [--matches 20] [--rounds 1000] [--seed 1] [--threads 8] [--csv out.csv]
//
// Bot names: main | main-noprofile | main-truthful | main-basic (both off), or any
// baseline (random, station, nit, maniac, tag, allin-bidder).
// Each duplicate match plays `rounds` decks twice with seats swapped (2*rounds
// hands), cancelling card luck. Reported win rate is in big blinds per 100
// hands with a 95% confidence interval across matches.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "poker/baseline_bots.h"
#include "poker/main_bot.h"

using namespace poker;

namespace {

std::unique_ptr<Bot> makeBot(const std::string& name, uint64_t seed) {
  MainBotConfig c;
  c.seed = seed;
  if (name == "main") return std::make_unique<MainBot>(c);
  if (name == "main-noprofile") { c.name = name; c.profiling = false; return std::make_unique<MainBot>(c); }
  if (name == "main-truthful") { c.name = name; c.auction_ev = false; return std::make_unique<MainBot>(c); }
  if (name == "main-basic") { c.name = name; c.profiling = false; c.auction_ev = false; return std::make_unique<MainBot>(c); }
  return makeBaseline(name, seed);
}

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  size_t a = 0;
  while (a <= s.size()) {
    size_t b = s.find(',', a);
    if (b == std::string::npos) b = s.size();
    if (b > a) out.push_back(s.substr(a, b - a));
    a = b + 1;
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::string bot = "main", opps = "tag,station,nit,maniac,random,allin-bidder", csv;
  int matches = 20, rounds = kNumRounds, threads = static_cast<int>(std::thread::hardware_concurrency());
  uint64_t seed = 1;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&] { return std::string(i + 1 < argc ? argv[++i] : ""); };
    if (a == "--bot") bot = next();
    else if (a == "--opps") opps = next();
    else if (a == "--matches") matches = std::stoi(next());
    else if (a == "--rounds") rounds = std::stoi(next());
    else if (a == "--seed") seed = std::stoull(next());
    else if (a == "--threads") threads = std::stoi(next());
    else if (a == "--csv") csv = next();
    else { std::fprintf(stderr, "unknown flag %s\n", a.c_str()); return 2; }
  }
  std::unique_ptr<std::ofstream> out;
  if (!csv.empty()) {
    out = std::make_unique<std::ofstream>(csv);
    *out << "bot,opponent,matches,hands,bb_per_100,ci95,win_matches,us_per_decision,p99_us,timeouts\n";
  }
  std::printf("%-15s vs %-15s %8s %16s %8s %12s %10s %9s\n", "bot", "opponent", "hands", "bb/100 (95% CI)", "won", "us/decision", "p99 us", "timeouts");

  for (const auto& opp : split(opps)) {
    std::vector<double> bb100(matches);
    std::vector<double> mean_us(matches), p99_us(matches);
    std::vector<int> timeouts(matches);
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < std::max(1, threads); ++t) {
      pool.emplace_back([&] {
        for (int m; (m = next.fetch_add(1)) < matches;) {
          auto a = makeBot(bot, seed * 1000 + m);
          auto b = makeBot(opp, seed * 7919 + m);
          MatchOptions o;
          o.rounds = rounds;
          o.seed = seed * 104729 + m;
          MatchResult r = playDuplicateMatch(*a, *b, o);
          bb100[m] = 100.0 * r.bankroll[0] / kBigBlind / r.hands;
          timeouts[m] = r.timeouts[0];
          if (auto* mb = dynamic_cast<MainBot*>(a.get())) {
            auto us = mb->decisionMicros();  // second leg only (reset at match start)
            std::sort(us.begin(), us.end());
            double s = 0;
            for (double x : us) s += x;
            mean_us[m] = us.empty() ? 0 : s / us.size();
            p99_us[m] = us.empty() ? 0 : us[std::min(us.size() - 1, us.size() * 99 / 100)];
          }
        }
      });
    }
    for (auto& th : pool) th.join();
    double mean = 0, var = 0, mus = 0, mp99 = 0;
    int wins = 0, to = 0;
    for (int m = 0; m < matches; ++m) {
      mean += bb100[m];
      wins += bb100[m] > 0;
      mus += mean_us[m];
      mp99 = std::max(mp99, p99_us[m]);
      to += timeouts[m];
    }
    mean /= matches;
    for (double x : bb100) var += (x - mean) * (x - mean);
    double sd = matches > 1 ? std::sqrt(var / (matches - 1)) : 0;
    double ci = 1.96 * sd / std::sqrt(static_cast<double>(matches));
    mus /= matches;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%+.1f ± %.1f", mean, ci);
    std::printf("%-15s vs %-15s %8d %16s %5d/%-3d %12.1f %10.1f %9d\n", bot.c_str(), opp.c_str(),
                matches * rounds * 2, buf, wins, matches, mus, mp99, to);
    std::fflush(stdout);
    if (out)
      *out << bot << "," << opp << "," << matches << "," << matches * rounds * 2 << "," << mean << "," << ci << ","
           << wins << "," << mus << "," << mp99 << "," << to << "\n";
  }
  return 0;
}
