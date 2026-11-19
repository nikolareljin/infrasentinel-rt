#include "isrt/detector.hpp"

#include <algorithm>

namespace isrt {

namespace {

Nanos abs_nanos(Nanos v) noexcept { return v < 0 ? -v : v; }

// EWMA step with alpha 1/16, in integers.
Nanos ewma(Nanos mean, Nanos sample) noexcept { return mean + (sample - mean) / 16; }

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
  if (config.period <= 0 || config.miss_periods == 0 || config.rate_window <= 0 ||
      config.degrade_streak == 0) {
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
                    std::uint32_t count, std::uint32_t seq) noexcept {
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
  sink_.emit(a);
}

std::uint32_t Detector::rate_limit(const NodeConfig& c) noexcept {
  if (c.rate_max > 0) return c.rate_max;
  const Nanos expected = c.rate_window / c.period;
  return static_cast<std::uint32_t>(std::max<Nanos>(5, 5 * expected));
}

std::uint32_t Detector::rate_count(const NodeState& n) noexcept {
  std::uint32_t total = 0;
  for (std::uint32_t b : n.rate) total += b;
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
  } else if (n.storming && count <= limit / 2) {
    n.storming = false;
    emit(AlertKind::kRecovered, Severity::kInfo, index, now, count);
  }
}

void Detector::update_timing(std::size_t index, Nanos now) noexcept {
  NodeState& n = nodes_[index];
  const Nanos interval = now - n.last_arrival;
  n.last_arrival = now;
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
    emit(AlertKind::kRecovered, Severity::kInfo, index, now);
  }
}

void Detector::on_message(std::size_t index, std::uint64_t boot, std::uint32_t seq,
                          Nanos now) noexcept {
  if (index >= count_) return;
  NodeState& n = nodes_[index];

  // Every arrival counts toward the rate, valid or not.
  advance_rate(n, now);
  ++n.rate[n.rate_head];
  check_storm(index, now);

  if (!n.seen) {
    n.seen = true;
    n.boot = boot;
    n.last_seq = seq;
    n.last_arrival = now;
    return;
  }

  if (boot != n.boot) {
    n.boot = boot;
    n.last_seq = seq;
    emit(AlertKind::kRebooted, Severity::kInfo, index, now, 0, seq);
    if (n.missing) {
      n.missing = false;
      emit(AlertKind::kRecovered, Severity::kInfo, index, now);
    }
    n.last_arrival = now;  // the interval across a reboot is not a sample
    return;
  }

  if (seq == n.last_seq) {
    ++n.seq_faults;
    n.good_run = 0;
    emit(AlertKind::kDuplicate, Severity::kWarning, index, now, 0, seq);
    return;  // a repeated message does not prove the node is alive
  }
  if (seq < n.last_seq) {
    ++n.seq_faults;
    n.good_run = 0;
    emit(AlertKind::kReordered, Severity::kWarning, index, now, 0, seq);
    return;
  }
  n.last_seq = seq;
  if (n.seq_faults > 0 && ++n.good_run >= 10) {
    --n.seq_faults;
    n.good_run = 0;
  }

  if (n.missing) {
    n.missing = false;
    n.last_arrival = now;  // the silent gap is not a sample
    emit(AlertKind::kRecovered, Severity::kInfo, index, now);
    return;
  }
  update_timing(index, now);
}

void Detector::on_tick(Nanos now) noexcept {
  for (std::size_t i = 0; i < count_; ++i) {
    NodeState& n = nodes_[i];
    if (n.storming) {
      advance_rate(n, now);
      check_storm(i, now);
    }
    const Nanos due = deadline(i);
    if (due != 0 && now >= due) {
      n.missing = true;
      n.degrading = false;
      n.slow_streak = 0;
      n.normal_streak = 0;
      emit(AlertKind::kMissing, Severity::kCritical, i, now);
    }
  }
}

Nanos Detector::deadline(std::size_t index) const noexcept {
  if (index >= count_) return 0;
  const NodeState& n = nodes_[index];
  if (!n.seen || n.missing) return 0;
  return n.last_arrival + static_cast<Nanos>(n.config.miss_periods) * n.config.period;
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
