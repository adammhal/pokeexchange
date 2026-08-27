// pokex-replay: reads a command script, writes the resulting event stream.
//
// This is the only way the engine is exercised in A1. It is a pure function of
// its input: the same bytes in produce the same bytes out, on any machine.
//
//   pokex-replay [input] [output]     (defaults: stdin, stdout)
//
// A line of "---" starts a new independent flow: the engine is rebuilt from
// scratch and "---" is echoed to the output. That lets one file carry thousands
// of independent scenarios, which is what the differential test against the
// Python reference uses.
#include <fstream>
#include <iostream>
#include <string>

#include "pokex/book_v0_map.hpp"
#include "pokex/matching.hpp"
#include "pokex/text_codec.hpp"

int main(int argc, char** argv) {
  std::ios::sync_with_stdio(false);

  std::ifstream fin;
  std::ofstream fout;
  if (argc > 1) {
    fin.open(argv[1]);
    if (!fin) {
      std::cerr << "pokex-replay: cannot open " << argv[1] << "\n";
      return 2;
    }
  }
  if (argc > 2) {
    fout.open(argv[2]);
    if (!fout) {
      std::cerr << "pokex-replay: cannot write " << argv[2] << "\n";
      return 2;
    }
  }
  std::istream& in = (argc > 1) ? static_cast<std::istream&>(fin) : std::cin;
  std::ostream& out = (argc > 2) ? static_cast<std::ostream&>(fout) : std::cout;

  pokex::MatchingEngine<pokex::BookV0Map> engine;
  std::string line;
  std::size_t lineno = 0;
  int status = 0;

  while (std::getline(in, line)) {
    ++lineno;
    if (line.rfind("---", 0) == 0) {
      engine = pokex::MatchingEngine<pokex::BookV0Map>{};
      out << "---\n";
      continue;
    }
    const auto parsed = pokex::codec::parse_command(line);
    using S = pokex::codec::ParseResult::Status;
    if (parsed.status == S::Skip) continue;
    if (parsed.status == S::Error) {
      std::cerr << "pokex-replay: line " << lineno << ": " << parsed.error << "\n";
      status = 1;
      continue;
    }
    engine.submit(parsed.command, [&out](const pokex::Event& e) {
      out << pokex::codec::format_event(e) << "\n";
    });
  }
  out.flush();
  return status;
}
