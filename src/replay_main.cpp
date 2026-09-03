// pokex-replay: reads a command script, writes the resulting event stream.
//
// This is the only way the engine is exercised in A1. It is a pure function of
// its input: the same bytes in produce the same bytes out, on any machine.
//
//   pokex-replay [--book v0|v1|v2|v3] [input] [output]   (defaults: stdin, stdout)
//
// Every book version must produce byte-identical output for the same input, so
// --book is also a way to check that from the shell.
//
// A line of "---" starts a new independent flow: the engine is rebuilt from
// scratch and "---" is echoed to the output. That lets one file carry thousands
// of independent scenarios, which is what the differential test against the
// Python reference uses.
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "pokex/all_books.hpp"
#include "pokex/matching.hpp"
#include "pokex/text_codec.hpp"

namespace {

template <typename BookT>
int run(std::istream& in, std::ostream& out) {
  pokex::MatchingEngine<BookT> engine;
  std::string line;
  std::size_t lineno = 0;
  int status = 0;

  while (std::getline(in, line)) {
    ++lineno;
    if (line.rfind("---", 0) == 0) {
      engine = pokex::MatchingEngine<BookT>{};  // a new independent flow
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

}  // namespace

int main(int argc, char** argv) {
  std::ios::sync_with_stdio(false);

  std::string book = "v0";
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--book" && i + 1 < argc) {
      book = argv[++i];
      continue;
    }
    positional.emplace_back(argv[i]);
  }

  std::ifstream fin;
  std::ofstream fout;
  if (!positional.empty()) {
    fin.open(positional[0]);
    if (!fin) {
      std::cerr << "pokex-replay: cannot open " << positional[0] << "\n";
      return 2;
    }
  }
  if (positional.size() > 1) {
    fout.open(positional[1]);
    if (!fout) {
      std::cerr << "pokex-replay: cannot write " << positional[1] << "\n";
      return 2;
    }
  }
  std::istream& in = !positional.empty() ? static_cast<std::istream&>(fin) : std::cin;
  std::ostream& out = positional.size() > 1 ? static_cast<std::ostream&>(fout) : std::cout;

  if (book == "v0") return run<pokex::BookV0Map>(in, out);
  if (book == "v1") return run<pokex::BookV1Ladder>(in, out);
  if (book == "v2") return run<pokex::BookV2Pool>(in, out);
  if (book == "v3") return run<pokex::BookV3Hash>(in, out);
  std::cerr << "pokex-replay: unknown book '" << book << "' (want v0, v1, v2 or v3)\n";
  return 2;
}
