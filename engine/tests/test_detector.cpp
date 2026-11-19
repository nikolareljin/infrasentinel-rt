#include <cstdint>
#include <vector>

#include "check.hpp"
#include "isrt/detector.hpp"

using isrt::Alert;
using isrt::AlertKind;
using isrt::Detector;
using isrt::kMaxNodes;
using isrt::kMillisecond;
using isrt::Nanos;
using isrt::NodeConfig;

namespace {

struct Recorder : isrt::AlertSink {
  std::vector<Alert> alerts;
  void emit(const Alert& a) override { alerts.push_back(a); }
  int count(AlertKind k) const {
    int c = 0;
    for (const auto& a : alerts) c += a.kind == k ? 1 : 0;
    return c;
  }
};

NodeConfig period_ms(Nanos ms) {
  NodeConfig c;
  c.period = ms * kMillisecond;
  return c;
}

// Runs every timer tick due at or before `t`, as the OS layer would.
void tick_until(Detector& d, Nanos t) {
  while (d.next_tick() != 0 && d.next_tick() <= t) d.on_tick(d.next_tick());
}

// Like feed, with the timer ticking between messages.
Nanos feed_ticking(Detector& d, std::size_t node, Nanos t, Nanos step, int n, std::uint32_t& seq) {
  for (int i = 0; i < n; ++i) {
    t += step;
    tick_until(d, t);
    d.on_message(node, 1, ++seq, t);
  }
  return t;
}

// Feeds `n` messages `step` apart starting after `t`; returns the last time.
Nanos feed(Detector& d, std::size_t node, Nanos t, Nanos step, int n, std::uint32_t& seq) {
  for (int i = 0; i < n; ++i) {
    t += step;
    d.on_message(node, 1, ++seq, t);
  }
  return t;
}

}  // namespace

TEST_CASE("add_node refuses bad names, duplicates and a full table") {
  Recorder r;
  Detector d(r);
  CHECK(d.add_node("", period_ms(100)) == kMaxNodes);
  CHECK(d.add_node("a-name-that-is-longer-than-23", period_ms(100)) == kMaxNodes);
  CHECK(d.add_node("esp01", period_ms(100)) == 0);
  CHECK(d.add_node("esp01", period_ms(100)) == kMaxNodes);
  NodeConfig bad = period_ms(100);
  bad.period = 0;
  CHECK(d.add_node("esp02", bad) == kMaxNodes);
  for (std::size_t i = 1; i < kMaxNodes; ++i) {
    char name[8] = {'n', static_cast<char>('a' + i / 26), static_cast<char>('a' + i % 26), 0};
    CHECK(d.add_node(name, period_ms(100)) == i);
  }
  CHECK(d.add_node("one-more", period_ms(100)) == kMaxNodes);
  CHECK(d.find("esp01") == 0);
  CHECK(d.find("nope") == kMaxNodes);
}

TEST_CASE("a node never seen is never missing") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  CHECK(d.deadline(0) == 0);
  d.on_tick(10 * isrt::kSecond);
  CHECK(r.alerts.empty());
  CHECK(d.health(0) == 0);
}

TEST_CASE("missing fires at the deadline, not before, and once") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  const Nanos last = feed(d, 0, 0, 100 * kMillisecond, 3, seq);
  CHECK(d.deadline(0) == last + 500 * kMillisecond);
  d.on_tick(d.deadline(0) - 1);
  CHECK(r.count(AlertKind::kMissing) == 0);
  d.on_tick(d.deadline(0));
  CHECK(r.count(AlertKind::kMissing) == 1);
  CHECK(r.alerts.back().severity == isrt::Severity::kCritical);
  d.on_tick(last + 10 * isrt::kSecond);
  CHECK(r.count(AlertKind::kMissing) == 1);
  CHECK(d.deadline(0) == 0);
  CHECK(d.health(0) == 0);
}

TEST_CASE("a message after missing recovers and re-arms the deadline") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 3, seq);
  t += isrt::kSecond;
  d.on_tick(t);
  d.on_message(0, 1, ++seq, t + 1);
  CHECK(r.count(AlertKind::kRecovered) == 1);
  CHECK(d.deadline(0) == t + 1 + 500 * kMillisecond);
  CHECK(d.node(0).samples == 2);  // the silent gap was not a sample
}

TEST_CASE("one slow interval is not degradation; a streak is") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 25, seq);  // warm-up
  t = feed(d, 0, t, 300 * kMillisecond, 1, seq);
  t = feed(d, 0, t, 100 * kMillisecond, 1, seq);
  CHECK(r.count(AlertKind::kDegrading) == 0);
  t = feed(d, 0, t, 300 * kMillisecond, 3, seq);
  CHECK(r.count(AlertKind::kDegrading) == 1);
  CHECK(r.alerts.back().baseline == 100 * kMillisecond);
  CHECK(r.alerts.back().interval == 300 * kMillisecond);
  CHECK(d.health(0) <= 70);
  feed(d, 0, t, 100 * kMillisecond, 3, seq);
  CHECK(r.count(AlertKind::kRecovered) == 1);
  CHECK(!d.node(0).degrading);
}

TEST_CASE("no degradation during warm-up") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 5, seq);
  feed(d, 0, t, 400 * kMillisecond, 5, seq);
  CHECK(r.count(AlertKind::kDegrading) == 0);
}

TEST_CASE("jitter within the baseline does not alert") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = 0;
  for (int i = 0; i < 200; ++i) {
    t += (i % 2 == 0 ? 80 : 120) * kMillisecond;
    d.on_message(0, 1, ++seq, t);
  }
  CHECK(r.alerts.empty());
  CHECK(d.health(0) >= 60);
}

TEST_CASE("storm alerts above the limit and clears when the rate drops") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));  // limit 5 x 10/s = 50 per second
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 10, seq);
  CHECK(r.count(AlertKind::kStorm) == 0);
  t = feed(d, 0, t, 5 * kMillisecond, 60, seq);  // 60 in 0.3 s
  CHECK(r.count(AlertKind::kStorm) == 1);
  CHECK(r.alerts.back().count > 50);
  feed(d, 0, t, 5 * kMillisecond, 60, seq);
  CHECK(r.count(AlertKind::kStorm) == 1);  // once per storm
}

TEST_CASE("a storm clears on a timer tick while the node is still alive") {
  Recorder r;
  Detector d(r);
  NodeConfig c = period_ms(100);
  c.rate_window = 200 * kMillisecond;  // limit 5
  c.miss_periods = 10;                 // deadline 1 s after the last message
  d.add_node("esp01", c);
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 10 * kMillisecond, 20, seq);
  CHECK(d.node(0).storming);
  while (d.next_tick() != 0 && d.next_tick() <= t + 400 * kMillisecond) {
    d.on_tick(d.next_tick());
  }
  CHECK(!d.node(0).storming);
  CHECK(!d.node(0).missing);
  CHECK(r.count(AlertKind::kRecovered) == 1);
  CHECK(r.alerts.back().cleared == AlertKind::kStorm);
}

TEST_CASE("a storm clears with a limit of 1") {
  Recorder r;
  Detector d(r);
  NodeConfig c = period_ms(1000);  // deadline 5 s, window 1 s
  c.rate_max = 1;
  d.add_node("esp01", c);
  std::uint32_t seq = 0;
  Nanos t = feed_ticking(d, 0, 0, 50 * kMillisecond, 5, seq);
  CHECK(d.node(0).storming);
  t = feed_ticking(d, 0, t, 1100 * kMillisecond, 4, seq);  // one per window, under the deadline
  CHECK(!d.node(0).storming);
  CHECK(!d.node(0).missing);
  CHECK(r.count(AlertKind::kMissing) == 0);
}

TEST_CASE("duplicates and reorders alert and do not refresh liveness") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 3, seq);
  const Nanos due = d.deadline(0);
  d.on_message(0, 1, seq, t + 50 * kMillisecond);
  CHECK(r.count(AlertKind::kDuplicate) == 1);
  d.on_message(0, 1, seq - 1, t + 60 * kMillisecond);
  CHECK(r.count(AlertKind::kReordered) == 1);
  CHECK(d.deadline(0) == due);
  CHECK(d.health(0) <= 90);
}

TEST_CASE("a new boot id resets the sequence and is not a fault") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 5, seq);
  d.on_message(0, 2, 1, t + 100 * kMillisecond);
  CHECK(r.count(AlertKind::kRebooted) == 1);
  d.on_message(0, 2, 2, t + 200 * kMillisecond);
  d.on_message(0, 2, 3, t + 300 * kMillisecond);
  CHECK(r.count(AlertKind::kReordered) == 0);
  CHECK(d.node(0).seq_faults == 0);
}

TEST_CASE("seq faults are forgiven after good messages") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 3, seq);
  d.on_message(0, 1, seq, t + 1);
  CHECK(d.node(0).seq_faults == 1);
  feed(d, 0, t + 1, 100 * kMillisecond, 10, seq);
  CHECK(d.node(0).seq_faults == 0);
}

TEST_CASE("unknown node index is ignored") {
  Recorder r;
  Detector d(r);
  d.on_message(3, 1, 1, 100);
  CHECK(r.alerts.empty());
  CHECK(d.deadline(3) == 0);
  CHECK(d.health(3) == 0);
}

TEST_CASE("alert kinds and severities have names") {
  CHECK(isrt::to_string(AlertKind::kMissing) == "missing");
  CHECK(isrt::to_string(AlertKind::kStorm) == "storm");
  CHECK(isrt::to_string(isrt::Severity::kCritical) == "critical");
}

TEST_CASE("a slow node gets a storm limit of at least 5") {
  Recorder r;
  Detector d(r);
  d.add_node("slow", period_ms(5000));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 5, seq);
  CHECK(r.count(AlertKind::kStorm) == 0);
  feed(d, 0, t, 100 * kMillisecond, 1, seq);
  CHECK(r.count(AlertKind::kStorm) == 1);
}

TEST_CASE("sequence numbers wrap without a fault") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0xFFFFFFFDu;
  Nanos t = 0;
  d.on_message(0, 1, seq, t);
  t = feed(d, 0, t, 100 * kMillisecond, 6, seq);  // crosses 0xFFFFFFFF -> 0
  CHECK(r.count(AlertKind::kReordered) == 0);
  CHECK(r.count(AlertKind::kDuplicate) == 0);
  CHECK(d.node(0).last_seq == seq);
  CHECK(d.deadline(0) == t + 500 * kMillisecond);
}

TEST_CASE("add_node refuses a zero warm-up and configs that could overflow") {
  Recorder r;
  Detector d(r);
  NodeConfig c = period_ms(100);
  c.warmup_samples = 0;
  CHECK(d.add_node("a", c) == kMaxNodes);
  c = period_ms(100);
  c.miss_periods = 2'000'000'000u;
  CHECK(d.add_node("b", c) == kMaxNodes);
  c = period_ms(100);
  c.period = isrt::kSecond * 4'000'000'000LL;
  CHECK(d.add_node("c", c) == kMaxNodes);
  c = period_ms(100);
  c.rate_window = isrt::kSecond * 4'000'000'000LL;
  CHECK(d.add_node("d", c) == kMaxNodes);
}

TEST_CASE("a replayed message from an old boot is not proof of life") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 3, seq);  // boot 1
  d.on_message(0, 2, 1, t += 100 * kMillisecond);       // boot 2
  d.on_message(0, 2, 2, t += 100 * kMillisecond);
  CHECK(r.count(AlertKind::kRebooted) == 1);
  const Nanos due = d.deadline(0);
  d.on_message(0, 1, 3, t + 10);  // replay from boot 1
  CHECK(d.deadline(0) == due);
  CHECK(d.node(0).boot == 2);
  CHECK(r.count(AlertKind::kRebooted) == 1);
  d.on_message(0, 2, 3, t += 100 * kMillisecond);
  CHECK(r.count(AlertKind::kRebooted) == 1);
  CHECK(d.deadline(0) == t + 500 * kMillisecond);
}

TEST_CASE("a missing node does not report a storm recovery") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 5 * kMillisecond, 60, seq);  // storm
  CHECK(r.count(AlertKind::kStorm) == 1);
  d.on_tick(t + 2 * isrt::kSecond);  // past the deadline: missing
  CHECK(r.count(AlertKind::kMissing) == 1);
  CHECK(!d.node(0).storming);
  d.on_message(0, 1, ++seq, t + 3 * isrt::kSecond);
  CHECK(r.count(AlertKind::kRecovered) == 1);
  CHECK(r.alerts.back().cleared == AlertKind::kMissing);
}

TEST_CASE("a storm that ends reports what recovered") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed_ticking(d, 0, 0, 100 * kMillisecond, 25, seq);  // warm-up
  t = feed_ticking(d, 0, t, 5 * kMillisecond, 60, seq);          // flood
  CHECK(r.count(AlertKind::kStorm) == 1);
  feed_ticking(d, 0, t, 100 * kMillisecond, 30, seq);
  CHECK(r.count(AlertKind::kMissing) == 0);
  CHECK(r.count(AlertKind::kRecovered) == 1);
  CHECK(r.count(AlertKind::kDegrading) == 0);
  for (const auto& a : r.alerts) {
    if (a.kind == AlertKind::kRecovered) CHECK(a.cleared == AlertKind::kStorm);
  }
  // flood intervals were not samples: the baseline is still the 100 ms cadence
  CHECK(d.node(0).mean_interval >= 90 * kMillisecond);
  CHECK(d.node(0).mean_interval <= 110 * kMillisecond);
}

TEST_CASE("time going backwards is ignored") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 25, seq);
  const Nanos mean = d.node(0).mean_interval;
  const Nanos due = d.deadline(0);
  d.on_message(0, 1, ++seq, t - 50 * kMillisecond);
  CHECK(d.node(0).mean_interval == mean);
  CHECK(d.deadline(0) == due);
}

TEST_CASE("an old-boot replay cannot block a real restart from recovering") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 3, seq);
  d.on_tick(t + isrt::kSecond);
  CHECK(d.node(0).missing);
  t += 2 * isrt::kSecond;
  for (std::uint32_t q = 0; q < 5; ++q) {
    d.on_message(0, 2, q, t += 100 * kMillisecond);
    d.on_message(0, 1, 3, t + 1);  // stale, from boot 1
    d.on_message(0, 7, 5, t + 2);  // a stray higher boot is a restart too
  }
  CHECK(!d.node(0).missing);
  CHECK(r.count(AlertKind::kRecovered) == 1);
}

TEST_CASE("two replayed old-boot messages are not proof of life") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  d.on_message(0, 1, 1, 0);
  d.on_message(0, 1, 2, 100 * kMillisecond);
  std::uint32_t seq = 0;
  Nanos t = 100 * kMillisecond;
  d.on_message(0, 2, ++seq, t += 100 * kMillisecond);
  t = feed(d, 0, t, 100 * kMillisecond, 2, seq);
  d.on_tick(t + isrt::kSecond);
  CHECK(d.node(0).missing);
  d.on_message(0, 1, 1, t + 2 * isrt::kSecond);
  d.on_message(0, 1, 2, t + 2 * isrt::kSecond + 100 * kMillisecond);
  CHECK(d.node(0).missing);
  CHECK(r.count(AlertKind::kRecovered) == 0);
  CHECK(d.node(0).boot == 2);
}

TEST_CASE("a run of duplicates raises one alert until a good message") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 3, seq);
  for (int i = 1; i <= 20; ++i) d.on_message(0, 1, seq, t + i);
  CHECK(r.count(AlertKind::kDuplicate) == 1);
  t = feed(d, 0, t, 100 * kMillisecond, 1, seq);
  d.on_message(0, 1, seq, t + 1);
  CHECK(r.count(AlertKind::kDuplicate) == 2);
}

TEST_CASE("a node flooding its own messages is alive") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 25, seq);
  for (int i = 0; i < 300; ++i) {
    t = feed(d, 0, t, 10 * kMillisecond, 1, seq);
    while (d.next_tick() != 0 && d.next_tick() <= t) d.on_tick(d.next_tick());
  }
  CHECK(r.count(AlertKind::kMissing) == 0);
  CHECK(r.count(AlertKind::kStorm) == 1);
  CHECK(d.node(0).mean_interval >= 90 * kMillisecond);  // floods are not samples
}

TEST_CASE("add_node caps the degrade factor") {
  Recorder r;
  Detector d(r);
  NodeConfig c = period_ms(100);
  c.degrade_factor_x4 = 401;
  CHECK(d.add_node("a", c) == kMaxNodes);
  c.degrade_factor_x4 = 400;
  CHECK(d.add_node("a", c) == 0);
}

TEST_CASE("next_tick is the earliest deadline or storm bucket") {
  Recorder r;
  Detector d(r);
  CHECK(d.next_tick() == 0);
  d.add_node("a", period_ms(100));
  d.add_node("b", period_ms(1000));
  CHECK(d.next_tick() == 0);  // nothing seen yet
  d.on_message(0, 1, 1, 0);
  d.on_message(1, 1, 1, 0);
  CHECK(d.next_tick() == 500 * kMillisecond);
  std::uint32_t seq = 1;
  feed(d, 0, 0, 5 * kMillisecond, 60, seq);  // storm on a
  CHECK(d.node(0).storming);
  CHECK(d.next_tick() == d.node(0).rate_bucket_start + 100 * kMillisecond);
}

TEST_CASE("a replay from far back in a long boot is stale, not proof of life") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  d.on_message(0, 1, 10, 0);
  // in order, each step within what the storm limit allows for the time passed
  Nanos t = 0;
  std::uint32_t seq = 10;
  for (int i = 0; i < 4; ++i) {
    t += 100 * kMillisecond;
    seq += 1;
    d.on_message(0, 1, seq, t);
  }
  const Nanos due = d.deadline(0);
  d.on_message(0, 1, seq + 1000000u, t + kMillisecond);  // far more than 1 ms allows
  CHECK(d.deadline(0) == due);
  CHECK(d.node(0).last_seq == seq);
  CHECK(r.count(AlertKind::kReordered) == 1);
  d.on_message(0, 1, seq + 1, t + 100 * kMillisecond);  // genuine traffic still counts
  CHECK(d.deadline(0) == t + 600 * kMillisecond);
  CHECK(r.count(AlertKind::kMissing) == 0);
}

TEST_CASE("a node back after a long outage may have advanced its sequence a lot") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 3, seq);
  tick_until(d, t + 3600 * isrt::kSecond);
  CHECK(d.node(0).missing);
  // an hour later, messages it sent meanwhile were lost: seq moved by 36000
  d.on_message(0, 1, seq + 36000, t + 3600 * isrt::kSecond);
  CHECK(!d.node(0).missing);
}

TEST_CASE("sequence faults stop counting at the health floor") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, 100 * kMillisecond, 3, seq);
  for (int i = 1; i <= 100; ++i) d.on_message(0, 1, seq, t + i);
  CHECK(d.node(0).seq_faults == 4);
  CHECK(d.health(0) >= 0);
}

TEST_CASE("add_node refuses a rate window under 10 ms") {
  Recorder r;
  Detector d(r);
  NodeConfig c = period_ms(100);
  c.rate_window = 5 * kMillisecond;
  CHECK(d.add_node("a", c) == kMaxNodes);
}

TEST_CASE("a message stamped before an outage does not end it") {
  Recorder r;
  Detector d(r);
  d.add_node("esp01", period_ms(100));
  std::uint32_t seq = 0;
  feed(d, 0, 0, 100 * kMillisecond, 3, seq);  // last at 300 ms
  tick_until(d, 2 * isrt::kSecond);
  CHECK(r.count(AlertKind::kMissing) == 1);
  d.on_message(0, 1, ++seq, 150 * kMillisecond);  // same boot, stamped earlier
  d.on_message(0, 2, 1, 160 * kMillisecond);      // a restart, stamped earlier
  tick_until(d, 3 * isrt::kSecond);
  CHECK(d.node(0).missing);
  CHECK(r.count(AlertKind::kRecovered) == 0);
  CHECK(r.count(AlertKind::kMissing) == 1);
  d.on_message(0, 2, 2, 3 * isrt::kSecond + 1);
  CHECK(!d.node(0).missing);
  CHECK(r.count(AlertKind::kRecovered) == 1);
}

TEST_CASE("the forward allowance reaches the whole serial window") {
  Recorder r;
  Detector d(r);
  NodeConfig c = period_ms(1);
  d.add_node("fast", c);
  std::uint32_t seq = 0;
  Nanos t = feed(d, 0, 0, kMillisecond, 3, seq);
  // 13 days silent at 1 ms: about 1.12e9 messages lost, more than 2^30
  t += 13LL * 24 * 3600 * isrt::kSecond;
  tick_until(d, t);
  CHECK(d.node(0).missing);
  d.on_message(0, 1, seq + 1'120'000'000u, t);
  CHECK(!d.node(0).missing);
}
