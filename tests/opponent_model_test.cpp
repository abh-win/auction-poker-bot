#include <gtest/gtest.h>

#include "poker/baseline_bots.h"
#include "poker/main_bot.h"

using namespace poker;

namespace {
// Plays `hands` hands and returns MainBot's view of the opponent.
Archetype profile(Bot& opp, int hands) {
  MainBot me;
  MatchOptions o;
  o.rounds = hands;
  o.seed = 11;
  playMatch(me, opp, o);
  return me.opponent().archetype();
}
}  // namespace

TEST(OpponentModel, PriorsBeforeEvidence) {
  OpponentModel m;
  EXPECT_EQ(m.archetype(), Archetype::Unknown);
  EXPECT_NEAR(m.vpip(), 0.6, 1e-9);
  EXPECT_GT(m.rangeWidth(false, false), m.rangeWidth(true, false));
  EXPECT_GT(m.rangeWidth(true, false), m.rangeWidth(true, true));
}

TEST(OpponentModel, ClassifiesArchetypes) {
  CallingStationBot station;
  NitBot nit;
  ManiacBot maniac;
  EXPECT_EQ(profile(station, 300), Archetype::CallingStation);
  EXPECT_EQ(profile(nit, 300), Archetype::Nit);
  EXPECT_EQ(profile(maniac, 300), Archetype::Maniac);
}

TEST(OpponentModel, LearnsBidSizes) {
  MainBot me;
  ManiacBot maniac;  // bids the whole pot
  MatchOptions o;
  o.rounds = 300;
  playMatch(me, maniac, o);
  EXPECT_GT(me.opponent().meanBidFraction(), 0.7);
  CallingStationBot station;  // bids 10% of the pot
  MainBot me2;
  playMatch(me2, station, o);
  EXPECT_LT(me2.opponent().meanBidFraction(), 0.3);
}
