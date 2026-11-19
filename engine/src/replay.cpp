#include "isrt/replay.hpp"

#include <cstdint>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

#include "isrt/detector.hpp"

namespace isrt {

namespace {

class JsonSink : public AlertSink {
 public:
  explicit JsonSink(std::ostream& out) : out_(out) {}
  void attach(const Detector* detector) { detector_ = detector; }

  void emit(const Alert& a) override {
    out_ << "{\"t\":" << a.at << ",\"node\":\"" << detector_->node(a.node).name.data()
         << "\",\"kind\":\"" << to_string(a.kind) << "\",\"severity\":\"" << to_string(a.severity)
         << "\",\"interval\":" << a.interval << ",\"expected\":" << a.expected
         << ",\"baseline\":" << a.baseline << ",\"count\":" << a.count << ",\"seq\":" << a.seq;
    if (a.kind == AlertKind::kRecovered) out_ << ",\"cleared\":\"" << to_string(a.cleared) << '"';
    out_ << "}\n";
  }

 private:
  std::ostream& out_;
  const Detector* detector_ = nullptr;
};

// Runs every tick due at or before `t`. The cap guards against a detector
// state that would not advance; it is far above any real stream.
bool ticks_until(Detector& d, Nanos t) {
  for (int guard = 0; guard < 10'000'000; ++guard) {
    const Nanos next = d.next_tick();
    if (next == 0 || next > t) return true;
    d.on_tick(next);
  }
  return false;
}

// Whole-word numbers only: no sign, no trailing characters, no value above
// `max`. The stream extractors accept "-1" for unsigned types and stop at the
// first bad character, which would let a malformed line through.
bool parse_number(const std::string& word, int base, std::uint64_t max, std::uint64_t& value) {
  if (word.empty() || word.size() > 20) return false;
  std::uint64_t v = 0;
  for (const char ch : word) {
    unsigned digit = 0;
    if (ch >= '0' && ch <= '9') {
      digit = static_cast<unsigned>(ch - '0');
    } else if (base == 16 && ch >= 'a' && ch <= 'f') {
      digit = static_cast<unsigned>(ch - 'a' + 10);
    } else if (base == 16 && ch >= 'A' && ch <= 'F') {
      digit = static_cast<unsigned>(ch - 'A' + 10);
    } else {
      return false;
    }
    if (v > (max - digit) / static_cast<std::uint64_t>(base)) return false;
    v = v * static_cast<std::uint64_t>(base) + digit;
  }
  value = v;
  return true;
}

constexpr std::uint64_t kMaxPeriodMs = 3'600'000;  // the detector's one-hour cap
// The detector ignores messages after kMaxMessageTime; refuse them here instead.
constexpr std::uint64_t kMaxTime = static_cast<std::uint64_t>(kMaxMessageTime);

}  // namespace

int replay(std::istream& in, std::ostream& out, std::ostream& err) {
  JsonSink sink(out);
  Detector detector(sink);
  sink.attach(&detector);

  std::string line;
  long number = 0;
  Nanos last = 0;
  auto fail = [&](const char* what) {
    err << "replay: line " << number << ": " << what << '\n';
    return 1;
  };

  while (std::getline(in, line)) {
    ++number;
    const auto hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::istringstream words(line);
    std::string verb, extra;
    if (!(words >> verb)) continue;

    if (verb == "node") {
      std::string name, period_word, miss_word;
      std::uint64_t period_ms = 0, miss = 0;
      NodeConfig config;
      if (!(words >> name >> period_word) ||
          !parse_number(period_word, 10, kMaxPeriodMs, period_ms) || period_ms == 0)
        return fail("expected: node <name> <period_ms>");
      config.period = static_cast<Nanos>(period_ms) * kMillisecond;
      if (words >> miss_word) {
        if (!parse_number(miss_word, 10, UINT32_MAX, miss))
          return fail("miss_periods is not a number");
        config.miss_periods = static_cast<std::uint32_t>(miss);
      }
      if (words >> extra) return fail("unexpected text after the node");
      if (detector.add_node(name, config) == kMaxNodes) return fail("node refused");
      continue;
    }

    std::string time_word;
    std::uint64_t time = 0;
    if (!(words >> time_word) || !parse_number(time_word, 10, kMaxTime, time) ||
        static_cast<Nanos>(time) < last)
      return fail("expected a time that does not decrease");
    const Nanos t = static_cast<Nanos>(time);
    if (!ticks_until(detector, t)) return fail("detector did not advance");
    last = t;

    if (verb == "msg") {
      std::string name, boot_word, seq_word;
      if (!(words >> name >> boot_word >> seq_word) || (words >> extra))
        return fail("expected: msg <t_ns> <name> <boot_hex> <seq>");
      const std::size_t node = detector.find(name);
      if (node == kMaxNodes) return fail("unknown node");
      std::uint64_t boot = 0, seq = 0;
      if (!parse_number(boot_word, 16, UINT64_MAX, boot)) return fail("boot id is not hex");
      if (!parse_number(seq_word, 10, UINT32_MAX, seq)) return fail("seq is not a 32-bit number");
      detector.on_message(node, boot, static_cast<std::uint32_t>(seq), t);
    } else if (verb == "end") {
      if (words >> extra) return fail("unexpected text after end");
      for (std::size_t i = 0; i < detector.size(); ++i) {
        out << "{\"t\":" << t << ",\"node\":\"" << detector.node(i).name.data()
            << "\",\"health\":" << detector.health(i) << "}\n";
      }
      return 0;
    } else {
      return fail("unknown verb");
    }
  }
  return 0;
}

}  // namespace isrt
