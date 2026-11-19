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
    std::string verb;
    if (!(words >> verb)) continue;

    if (verb == "node") {
      std::string name;
      long long period_ms = 0;
      NodeConfig config;
      if (!(words >> name >> period_ms) || period_ms <= 0)
        return fail("expected: node <name> <period_ms>");
      config.period = period_ms * kMillisecond;
      unsigned long miss = 0;
      if (words >> miss) config.miss_periods = static_cast<std::uint32_t>(miss);
      if (detector.add_node(name, config) == kMaxNodes) return fail("node refused");
      continue;
    }

    Nanos t = 0;
    if (!(words >> t) || t < last) return fail("expected a time that does not decrease");
    if (!ticks_until(detector, t)) return fail("detector did not advance");
    last = t;

    if (verb == "msg") {
      std::string name, boot_hex;
      unsigned long seq = 0;
      if (!(words >> name >> boot_hex >> seq))
        return fail("expected: msg <t_ns> <name> <boot_hex> <seq>");
      const std::size_t node = detector.find(name);
      if (node == kMaxNodes) return fail("unknown node");
      std::uint64_t boot = 0;
      try {
        boot = std::stoull(boot_hex, nullptr, 16);
      } catch (...) {
        return fail("boot id is not hex");
      }
      detector.on_message(node, boot, static_cast<std::uint32_t>(seq), t);
    } else if (verb == "end") {
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
