#pragma once

// Platform-neutral detectors for node telemetry.
//
// Time is passed in by the caller (monotonic nanoseconds), so the same code
// runs under QNX timer pulses, Linux timerfd and unit tests with a fake clock.
// All state is fixed-size: no allocation after construction.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace isrt {

using Nanos = std::int64_t;

constexpr Nanos kMillisecond = 1'000'000;
constexpr Nanos kSecond = 1'000'000'000;

constexpr std::size_t kMaxNodes = 32;
constexpr std::size_t kNodeNameMax = 23;  // bytes, without the terminator
constexpr std::size_t kRateBuckets = 10;

enum class AlertKind : std::uint8_t {
  kMissing,    // no message for miss_periods expected periods
  kRecovered,  // a missing or degrading node is back to normal
  kDegrading,  // intervals well above the learned baseline, several in a row
  kStorm,      // message rate above rate_max in the rate window
  kDuplicate,  // the same sequence number twice in one boot
  kReordered,  // stale: a lower sequence number, or an older boot
  kRebooted,   // a higher boot counter
};

enum class Severity : std::uint8_t { kInfo, kWarning, kCritical };

std::string_view to_string(AlertKind kind) noexcept;
std::string_view to_string(Severity severity) noexcept;

// What caused an alert, so later layers explain from facts.
struct Alert {
  AlertKind kind;
  Severity severity;
  std::size_t node;     // index in the detector
  Nanos at;             // when the detector saw it
  Nanos interval;       // last inter-arrival interval, 0 when not known
  Nanos expected;       // configured period
  Nanos baseline;       // learned mean interval, 0 before warm-up
  std::uint32_t count;  // messages in the rate window (storm), else 0
  std::uint32_t seq;    // sequence number involved, else 0
  AlertKind cleared;    // for kRecovered: what cleared (missing, degrading, storm)
};

class AlertSink {
 public:
  virtual ~AlertSink() = default;
  virtual void emit(const Alert& alert) = 0;
};

struct NodeConfig {
  Nanos period = 1000 * kMillisecond;    // expected publish period
  std::uint32_t miss_periods = 5;        // missing after this many silent periods
  std::uint32_t warmup_samples = 20;     // intervals before the baseline is trusted
  std::uint32_t degrade_factor_x4 = 16;  // k = 4.0: threshold mean + k * deviation
  std::uint32_t degrade_streak = 3;  // consecutive slow intervals to alert, normal ones to clear
  Nanos rate_window = kSecond;       // storm window
  std::uint32_t rate_max = 0;        // 0: derived as 5 x the expected rate
};

// Per-node state. Public so the OS layer and tests can read it; only the
// Detector writes it.
struct NodeState {
  std::array<char, kNodeNameMax + 1> name{};
  NodeConfig config{};
  bool in_use = false;
  bool seen = false;
  bool missing = false;
  bool degrading = false;
  bool storming = false;

  Nanos last_arrival = 0;
  Nanos last_interval = 0;
  Nanos mean_interval = 0;   // EWMA, alpha 1/16
  Nanos mean_deviation = 0;  // EWMA of |interval - mean|, alpha 1/16
  std::uint32_t samples = 0;
  std::uint32_t slow_streak = 0;
  std::uint32_t normal_streak = 0;

  std::uint64_t boot = 0;
  std::uint32_t last_seq = 0;
  std::uint32_t seq_faults = 0;  // duplicates and reorders; one forgiven per 10 good messages
  std::uint32_t good_run = 0;
  bool duplicate_latched = false;  // one alert per run of duplicates
  bool reorder_latched = false;    // one alert per run of stale messages

  std::array<std::uint32_t, kRateBuckets> rate{};
  bool rate_started = false;
  Nanos rate_bucket_start = 0;
  std::size_t rate_head = 0;
};

class Detector {
 public:
  explicit Detector(AlertSink& sink) noexcept : sink_(sink) {}

  // Registers a node; returns its index, or kMaxNodes when the name is empty,
  // too long, already registered or the table is full.
  std::size_t add_node(std::string_view name, const NodeConfig& config) noexcept;

  // Index of a registered node, or kMaxNodes.
  std::size_t find(std::string_view name) const noexcept;

  // A valid message from a node arrived at `now`.
  void on_message(std::size_t node, std::uint64_t boot, std::uint32_t seq, Nanos now) noexcept;

  // Checks deadlines at `now`; called when a timer fires.
  void on_tick(Nanos now) noexcept;

  // When `node` becomes missing if nothing arrives, or 0 when not armed
  // (never seen, or already missing). The OS layer arms its timer to this.
  Nanos deadline(std::size_t node) const noexcept;

  // The earliest time on_tick has work to do: a node's deadline, or the next
  // rate bucket of a storming node. 0 when nothing is pending. The OS layer
  // keeps one timer armed to this.
  Nanos next_tick() const noexcept;

  // 0-100; see docs/engine/detectors.md for the formula.
  int health(std::size_t node) const noexcept;

  const NodeState& node(std::size_t index) const noexcept { return nodes_[index]; }
  std::size_t size() const noexcept { return count_; }

 private:
  void emit(AlertKind kind, Severity severity, std::size_t node, Nanos now, std::uint32_t count = 0,
            std::uint32_t seq = 0, AlertKind cleared = AlertKind::kRecovered) noexcept;
  void recovered(std::size_t node, Nanos now, AlertKind cleared, std::uint32_t count = 0) noexcept;
  void update_timing(std::size_t index, Nanos now) noexcept;
  void stale(std::size_t index, Nanos now, std::uint32_t seq) noexcept;
  void advance_rate(NodeState& n, Nanos now) noexcept;
  void check_storm(std::size_t index, Nanos now) noexcept;
  static std::uint32_t rate_count(const NodeState& n) noexcept;
  static std::uint32_t rate_limit(const NodeConfig& c) noexcept;

  AlertSink& sink_;
  std::array<NodeState, kMaxNodes> nodes_{};
  std::size_t count_ = 0;
};

}  // namespace isrt
