#include "isrt/detector.hpp"

#include <algorithm>

namespace isrt {

namespace {

Nanos abs_nanos(Nanos v) noexcept { return v < 0 ? -v : v; }

// EWMA step with alpha 1/16, in integers.
Nanos ewma(Nanos mean, Nanos sample) noexcept { return mean + (sample - mean) / 16; }

// Times beyond this (about 146 years of monotonic clock) are ignored, so every
// sum of a time and a configured span stays inside int64.
constexpr Nanos kMaxNow = INT64_MAX / 2;

bool usable_time(Nanos now) noexcept { return now >= 0 && now <= kMaxNow; }

// Faults counted toward health; the penalty stops at 20 (4 faults) anyway.
constexpr std::uint32_t kMaxSeqFaults = 4;

// Limits that keep every product below the int64 range.
constexpr Nanos kMaxPeriod = 3600 * kSecond;
constexpr std::uint32_t kMaxMissPeriods = 1000;
constexpr std::uint32_t kMaxDegradeFactorX4 = 400;

}  // namespace

std::string_view to_string(AlertKind kind) noexcept {
  switch (kind) {
    case AlertKind::kMissing:
      return "missing";
    case AlertKind::kRecovered:
      return "recovered";
    case AlertKind::kDegrading:
      return "degrading";
    case AlertKind::kStorm:
      return "storm";
    case AlertKind::kDuplicate:
      return "duplicate";
    case AlertKind::kReordered:
      return "reordered";
    case AlertKind::kRebooted:
      return "rebooted";
  }
  return "unknown";
}

std::string_view to_string(Severity severity) noexcept {
  switch (severity) {
    case Severity::kInfo:
      return "info";
    case Severity::kWarning:
      return "warning";
    case Severity::kCritical:
      return "critical";
  }
  return "unknown";
}

std::size_t Detector::add_node(std::string_view name, const NodeConfig& config) noexcept {
  if (name.empty() || name.size() > kNodeNameMax || count_ == kMaxNodes) return kMaxNodes;
  if (config.period <= 0 || config.period > kMaxPeriod || config.miss_periods == 0 ||
      config.miss_periods > kMaxMissPeriods || config.rate_window < 10 * kMillisecond ||
      config.rate_window > kMaxPeriod || config.degrade_streak == 0 || config.warmup_samples == 0 ||
      config.degrade_factor_x4 > kMaxDegradeFactorX4) {
    return kMaxNodes;
  }
  if (find(name) != kMaxNodes) return kMaxNodes;
  NodeState& n = nodes_[count_];
  n = NodeState{};
  std::copy(name.begin(), name.end(), n.name.begin());
  n.config = config;
  n.in_use = true;
  return count_++;
}

std::size_t Detector::find(std::string_view name) const noexcept {
  for (std::size_t i = 0; i < count_; ++i) {
    if (name == std::string_view(nodes_[i].name.data())) return i;
  }
  return kMaxNodes;
}

void Detector::emit(AlertKind kind, Severity severity, std::size_t index, Nanos now,
                    std::uint32_t count, std::uint32_t seq, AlertKind cleared) noexcept {
  const NodeState& n = nodes_[index];
  Alert a{};
  a.kind = kind;
  a.severity = severity;
  a.node = index;
  a.at = now;
  a.interval = n.last_interval;
  a.expected = n.config.period;
  a.baseline = n.samples >= n.config.warmup_samples ? n.mean_interval : 0;
  a.count = count;
  a.seq = seq;
  a.cleared = cleared;
  sink_.emit(a);
}

void Detector::recovered(std::size_t index, Nanos now, AlertKind cleared,
                         std::uint32_t count) noexcept {
  emit(AlertKind::kRecovered, Severity::kInfo, index, now, count, 0, cleared);
}

std::uint32_t Detector::rate_limit(const NodeConfig& c) noexcept {
  if (c.rate_max > 0) return c.rate_max;
  const Nanos expected = c.rate_window / c.period;
  const Nanos limit = std::clamp<Nanos>(5 * expected, 5, 0x7FFFFFFF);
  return static_cast<std::uint32_t>(limit);
}

std::uint32_t Detector::rate_count(const NodeState& n) noexcept {
  std::uint32_t total = 0;
  for (std::uint32_t b : n.rate) {
    total = total > UINT32_MAX - b ? UINT32_MAX : total + b;  // saturate
  }
  return total;
}

void Detector::advance_rate(NodeState& n, Nanos now) noexcept {
  const Nanos width = std::max<Nanos>(1, n.config.rate_window / static_cast<Nanos>(kRateBuckets));
  if (!n.rate_started) {
    n.rate_started = true;
    n.rate_bucket_start = now;
    return;
  }
  if (now < n.rate_bucket_start + width) return;
  const Nanos steps = (now - n.rate_bucket_start) / width;
  if (steps >= static_cast<Nanos>(kRateBuckets)) {
    n.rate.fill(0);
    n.rate_head = 0;
  } else {
    for (Nanos s = 0; s < steps; ++s) {
      n.rate_head = (n.rate_head + 1) % kRateBuckets;
      n.rate[n.rate_head] = 0;
    }
  }
  n.rate_bucket_start += steps * width;
}

void Detector::check_storm(std::size_t index, Nanos now) noexcept {
  NodeState& n = nodes_[index];
  const std::uint32_t count = rate_count(n);
  const std::uint32_t limit = rate_limit(n.config);
  if (!n.storming && count > limit) {
    n.storming = true;
    emit(AlertKind::kStorm, Severity::kWarning, index, now, count);
  } else if (n.storming && count <= std::max<std::uint32_t>(1, limit / 2)) {
    n.storming = false;
    recovered(index, now, AlertKind::kStorm, count);
  }
}

void Detector::update_timing(std::size_t index, Nanos now) noexcept {
  NodeState& n = nodes_[index];
  // Capped at the deadline span: a late tick cannot make an interval large
  // enough to overflow the threshold arithmetic.
  const Nanos interval =
      std::min(now - n.last_arrival, static_cast<Nanos>(n.config.miss_periods) * n.config.period);
  if (interval <= 0) return;  // a clock that went backwards moves nothing
  n.last_arrival = now;       // any in-order message is proof of life
  // Much faster than configured is flood traffic, counted by the storm rule;
  // it must not teach the baseline a cadence the node does not have.
  if (interval < n.config.period / 4) return;
  n.last_interval = interval;

  if (n.samples < n.config.warmup_samples) {
    if (n.samples == 0) {
      n.mean_interval = interval;
    } else {
      n.mean_deviation = ewma(n.mean_deviation, abs_nanos(interval - n.mean_interval));
      n.mean_interval = ewma(n.mean_interval, interval);
    }
    ++n.samples;
    return;
  }

  const Nanos spread = n.mean_deviation * n.config.degrade_factor_x4 / 4;
  const Nanos threshold = n.mean_interval + std::max(spread, n.config.period / 2);
  if (interval > threshold) {
    ++n.slow_streak;
    n.normal_streak = 0;
    if (!n.degrading && n.slow_streak >= n.config.degrade_streak) {
      n.degrading = true;
      emit(AlertKind::kDegrading, Severity::kWarning, index, now);
    }
    return;  // slow intervals do not move the baseline
  }
  n.slow_streak = 0;
  ++n.normal_streak;
  n.mean_deviation = ewma(n.mean_deviation, abs_nanos(interval - n.mean_interval));
  n.mean_interval = ewma(n.mean_interval, interval);
  if (n.degrading && n.normal_streak >= n.config.degrade_streak) {
    n.degrading = false;
    recovered(index, now, AlertKind::kDegrading);
  }
}

void Detector::on_message(std::size_t index, std::uint64_t boot, std::uint32_t seq,
                          Nanos now) noexcept {
  if (index >= count_ || !usable_time(now)) return;
  NodeState& n = nodes_[index];

  // Every arrival counts toward the rate, valid or not; a missing node raises
  // no storm (its return is reported as recovered first).
  advance_rate(n, now);
  if (n.rate[n.rate_head] < UINT32_MAX) ++n.rate[n.rate_head];
  if (!n.missing) check_storm(index, now);

  if (!n.seen) {
    n.seen = true;
    n.boot = boot;
    n.last_seq = seq;
    n.last_arrival = now;
    return;
  }

  // A message stamped no later than the last arrival moves nothing: no
  // sequence, boot, latch or liveness state.
  if (now <= n.last_arrival) return;

  // The boot id is a counter the node keeps across restarts: a higher one is a
  // restart, a lower one is an old message replayed and proves nothing.
  if (boot < n.boot) {
    stale(index, now, seq);
    return;
  }
  if (boot > n.boot) {
    n.boot = boot;
    n.last_seq = seq;
    n.duplicate_latched = false;
    n.reorder_latched = false;
    emit(AlertKind::kRebooted, Severity::kInfo, index, now, 0, seq);
    n.last_arrival = now;  // the gap across a restart is not a sample
    if (n.missing) {
      n.missing = false;
      recovered(index, now, AlertKind::kMissing);
    }
    return;
  }

  // Within one boot the sequence only rises: the node raises its boot counter
  // and starts again at 0 before the sequence would wrap. So any number of lost
  // messages is in order, and an older number is always a replay.
  if (seq == n.last_seq) {
    n.seq_faults = std::min(n.seq_faults + 1, kMaxSeqFaults);
    n.good_run = 0;
    if (!n.duplicate_latched) {
      n.duplicate_latched = true;
      emit(AlertKind::kDuplicate, Severity::kWarning, index, now, 0, seq);
    }
    return;  // a repeated message does not prove the node is alive
  }
  if (seq < n.last_seq) {
    stale(index, now, seq);
    return;
  }
  n.last_seq = seq;
  n.duplicate_latched = false;
  n.reorder_latched = false;
  if (n.seq_faults > 0 && ++n.good_run >= 10) {
    --n.seq_faults;
    n.good_run = 0;
  }

  if (n.missing) {
    n.missing = false;
    n.last_arrival = now;  // the silent gap is not a sample
    recovered(index, now, AlertKind::kMissing);
    return;
  }
  update_timing(index, now);
}

void Detector::stale(std::size_t index, Nanos now, std::uint32_t seq) noexcept {
  NodeState& n = nodes_[index];
  n.seq_faults = std::min(n.seq_faults + 1, kMaxSeqFaults);
  n.good_run = 0;
  if (!n.reorder_latched) {
    n.reorder_latched = true;
    emit(AlertKind::kReordered, Severity::kWarning, index, now, 0, seq);
  }
}

void Detector::on_tick(Nanos now) noexcept {
  // Any non-negative time: stored arrivals are at most INT64_MAX / 2, so a
  // deadline armed from the last accepted message is always reachable.
  if (now < 0) return;
  for (std::size_t i = 0; i < count_; ++i) {
    NodeState& n = nodes_[i];
    const Nanos due = deadline(i);
    if (due != 0 && now >= due) {
      n.missing = true;
      n.degrading = false;
      n.storming = false;  // a silent node has no storm; its return is the recovery
      n.slow_streak = 0;
      n.normal_streak = 0;
      emit(AlertKind::kMissing, Severity::kCritical, i, now);
      continue;
    }
    if (n.storming) {
      advance_rate(n, now);
      check_storm(i, now);
    }
  }
}

Nanos Detector::deadline(std::size_t index) const noexcept {
  if (index >= count_) return 0;
  const NodeState& n = nodes_[index];
  if (!n.seen || n.missing) return 0;
  return n.last_arrival + static_cast<Nanos>(n.config.miss_periods) * n.config.period;
}

Nanos Detector::next_tick() const noexcept {
  Nanos next = 0;
  for (std::size_t i = 0; i < count_; ++i) {
    const NodeState& n = nodes_[i];
    Nanos t = deadline(i);
    if (n.storming && n.rate_started) {
      const Nanos width =
          std::max<Nanos>(1, n.config.rate_window / static_cast<Nanos>(kRateBuckets));
      const Nanos bucket = n.rate_bucket_start + width;
      if (t == 0 || bucket < t) t = bucket;
    }
    if (t != 0 && (next == 0 || t < next)) next = t;
  }
  return next;
}

int Detector::health(std::size_t index) const noexcept {
  if (index >= count_) return 0;
  const NodeState& n = nodes_[index];
  if (!n.seen || n.missing) return 0;
  Nanos score = 100;
  if (n.samples >= n.config.warmup_samples) {
    score -= std::min<Nanos>(40, 100 * n.mean_deviation / n.config.period);
  }
  if (n.degrading) score -= 30;
  if (n.storming) score -= 20;
  score -= std::min<Nanos>(20, 5 * static_cast<Nanos>(n.seq_faults));
  return static_cast<int>(std::clamp<Nanos>(score, 0, 100));
}

}  // namespace isrt
