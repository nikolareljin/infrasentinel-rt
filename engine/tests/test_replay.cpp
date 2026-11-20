#include <sstream>
#include <string>

#include "check.hpp"
#include "isrt/replay.hpp"

namespace {

struct Run {
  int rc;
  std::string out;
  std::string err;
};

Run run(const std::string& input) {
  std::istringstream in(input);
  std::ostringstream out, err;
  const int rc = isrt::replay(in, out, err);
  return {rc, out.str(), err.str()};
}

bool has(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE("replay ticks at the deadline between messages") {
  const Run r =
      run("node esp01 100\n"
          "msg 100000000 esp01 1 1\n"
          "msg 200000000 esp01 1 2\n"
          "end 2000000000\n");
  CHECK(r.rc == 0);
  CHECK(has(r.out, "{\"t\":700000000,\"node\":\"esp01\",\"kind\":\"missing\""));
  CHECK(has(r.out, "\"node\":\"esp01\",\"health\":0}"));
}

TEST_CASE("replay reports recovered with what cleared") {
  const Run r =
      run("node esp01 100\n"
          "msg 100000000 esp01 1 1\n"
          "msg 2000000000 esp01 1 2\n"
          "end 2000000000\n");
  CHECK(has(r.out, "\"kind\":\"recovered\""));
  CHECK(has(r.out, "\"cleared\":\"missing\""));
}

TEST_CASE("replay ignores comments and blank lines") {
  const Run r = run("# header\n\nnode esp01 100 # trailing\nend 0\n");
  CHECK(r.rc == 0);
  CHECK(has(r.out, "\"health\":0"));
}

TEST_CASE("replay refuses malformed input with the line number") {
  CHECK(run("node esp01\n").rc == 1);
  CHECK(has(run("node esp01 100\nmsg 5 nobody 1 1\n").err, "line 2: unknown node"));
  CHECK(run("node esp01 100\nmsg 10 esp01 1 1\nmsg 5 esp01 1 2\n").rc == 1);
  CHECK(run("node esp01 100\nmsg 10 esp01 zz 1\n").rc == 1);
  CHECK(run("frob 10\n").rc == 1);
}

TEST_CASE("replay refuses numbers the stream would half-read") {
  const std::string head = "node esp01 100\n";
  CHECK(has(run(head + "msg 1 esp01 1 -1\n").err, "line 2: seq is not a 32-bit number"));
  CHECK(run(head + "msg 1 esp01 1 4294967296\n").rc == 1);
  CHECK(run(head + "msg 1 esp01 1zz 2\n").rc == 1);
  CHECK(run(head + "msg 1 esp01 1 2 3\n").rc == 1);
  CHECK(run(head + "msg 12abc esp01 1 2\n").rc == 1);
  CHECK(run(head + "msg -5 esp01 1 2\n").rc == 1);
  CHECK(run("node esp01 -100\n").rc == 1);
  CHECK(has(run("node a\"b 100\n").err, "line 1: node names use"));
  CHECK(run("node a\\b 100\n").rc == 1);
  CHECK(run("node esp-01_a.b 100\nend 0\n").rc == 0);
  CHECK(run("node esp01 100 -1\n").rc == 1);
  CHECK(run("node esp01 99999999999999\n").rc == 1);
  CHECK(run(head + "msg 1 esp01 FFFFFFFFFFFFFFFF 4294967295\nend 1\n").rc == 0);
  CHECK(run(head + "msg 4611686018427387903 esp01 1 1\nend 4611686018427387903\n").rc == 0);
  CHECK(run(head + "msg 4611686018427387904 esp01 1 1\n").rc == 1);
}

TEST_CASE("replay refuses a stream cut off before its end line") {
  const Run r = run("node esp01 100\nmsg 1 esp01 1 1\n");
  CHECK(r.rc == 1);
  CHECK(has(r.err, "line 3: input ended without an end line"));
}
