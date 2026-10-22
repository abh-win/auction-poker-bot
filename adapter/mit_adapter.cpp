// Socket client for the official MIT Pokerbots 2024 engine (engine.py).
//
// The engine launches `pokerbot [--host H] PORT` and talks one line per
// request (clauses separated by spaces):
//   T<clock> P<seat> H<c,c> F C K R<to> A<bid> N<s0,s1>_<b0,b1>_<cards> B<board> O<opp> D<delta> Q
// We mirror the round locally with poker::RoundState (identical betting
// rules), feed MainBot the same BotView it sees in the in-process arena, and
// answer with F/C/K/R###/A### (or K to acknowledge the end of a round).

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>

#include "poker/main_bot.h"

using namespace poker;

namespace {

int connectTo(const std::string& host, const std::string& port) {
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) return -1;
  int fd = -1;
  for (addrinfo* p = res; p; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

class LineSocket {
 public:
  explicit LineSocket(int fd) : fd_(fd) {}
  bool readLine(std::string* line) {
    while (true) {
      auto nl = buf_.find('\n');
      if (nl != std::string::npos) {
        *line = buf_.substr(0, nl);
        buf_.erase(0, nl + 1);
        return true;
      }
      char tmp[4096];
      ssize_t n = recv(fd_, tmp, sizeof tmp, 0);
      if (n <= 0) return false;
      buf_.append(tmp, static_cast<size_t>(n));
    }
  }
  void writeLine(const std::string& s) {
    std::string out = s + "\n";
    send(fd_, out.data(), out.size(), 0);
  }

 private:
  int fd_;
  std::string buf_;
};

std::array<Card, 52> placeholderDeck() {
  std::array<Card, 52> d;
  std::iota(d.begin(), d.end(), 0);
  return d;
}

}  // namespace

int main(int argc, char** argv) {
  std::string host = "localhost", port;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) host = argv[++i];
    else port = a;
  }
  if (port.empty()) {
    std::fprintf(stderr, "usage: pokerbot [--host HOST] PORT\n");
    return 2;
  }
  int fd = connectTo(host, port);
  if (fd < 0) {
    std::fprintf(stderr, "could not connect to %s:%s\n", host.c_str(), port.c_str());
    return 1;
  }
  LineSocket sock(fd);
  MainBot bot;
  bot.onMatchStart();

  std::unique_ptr<RoundState> st;
  int seat = 0, round_num = 0;
  double clock = 30.0;
  std::vector<Card> hand, board, opp_hand;
  bool round_over = false;
  int delta = 0;
  int mismatches = 0;

  std::string line;
  while (sock.readLine(&line)) {
    std::istringstream in(line);
    std::string clause;
    bool quit = false;
    while (in >> clause) {
      char c = clause[0];
      std::string arg = clause.substr(1);
      switch (c) {
        case 'T': clock = std::stod(arg); break;
        case 'P': seat = std::stoi(arg); break;
        case 'H':
          hand = parseCards(arg);
          board.clear();
          opp_hand.clear();
          round_over = false;
          ++round_num;
          st = std::make_unique<RoundState>(placeholderDeck());
          st->overrideCards(seat, hand, {});
          st->overrideCards(1 - seat, {51, 50}, {});
          bot.onHandStart(seat, round_num, hand);
          break;
        case 'F': st->apply(Action::fold()); break;
        case 'C': st->apply(Action::call()); break;
        case 'K': st->apply(Action::check()); break;
        case 'R': st->apply(Action::raise(std::stoi(arg))); break;
        case 'A': st->apply(Action::bid(std::stoi(arg))); break;
        case 'N': {  // N<s0,s1>_<b0,b1>_<cards>: our hand after the auction
          auto last = arg.rfind('_');
          hand = parseCards(arg.substr(last + 1));
          // Consistency check: our mirrored state must match the server's.
          int s0 = 0, s1 = 0;
          if (std::sscanf(arg.c_str(), "%d,%d", &s0, &s1) == 2 &&
              (s0 != st->stacks()[0] || s1 != st->stacks()[1])) {
            std::fprintf(stderr, "STATE MISMATCH round %d: server stacks %d,%d local %d,%d\n", round_num, s0,
                         s1, st->stacks()[0], st->stacks()[1]);
            ++mismatches;
          }
          st->overrideCards(seat, hand, board);
          break;
        }
        case 'B':
          board = parseCards(arg);
          st->overrideCards(seat, {}, board);
          break;
        case 'O': opp_hand = parseCards(arg); break;
        case 'D': delta = std::stoi(arg); round_over = true; break;
        case 'Q': quit = true; break;
        default: break;
      }
    }
    if (quit) break;
    if (round_over) {
      HandResult r;
      r.seat = seat;
      r.round_num = round_num;
      r.delta = delta;
      r.showdown = !opp_hand.empty();
      r.my_hand = hand;
      r.opp_hand = opp_hand;
      r.board = board;
      r.bids = st ? st->bids() : std::array<int, 2>{-1, -1};
      r.history = &st->history();
      bot.onHandEnd(r);
      sock.writeLine("K");
      continue;
    }
    // Our turn.
    BotView v;
    v.seat = seat;
    v.round_num = round_num;
    v.game_clock = clock;
    v.street = st->street();
    v.auction = st->inAuction();
    v.hand = hand;
    v.board = board;
    v.pips = st->pips();
    v.stacks = st->stacks();
    v.bids = v.auction ? std::array<int, 2>{-1, -1} : st->bids();
    v.pot = st->pot();
    v.continue_cost = st->continueCost();
    v.legal = st->legalActions();
    v.raise_bounds = st->raiseBounds();
    v.bid_bounds = st->bidBounds();
    v.history = &st->history();
    sock.writeLine(toString(bot.act(v)));
  }
  close(fd);
  std::fprintf(stderr, "pokerbot: %d rounds, %d state mismatches, final clock %.3f s\n", round_num, mismatches,
               clock);
  return mismatches == 0 ? 0 : 1;
}
