// openpsa-to-wmc: an Open-PSA MEF model as a weighted CNF for any weighted model counter.

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "openpsa.hpp"

namespace {
const char* usage =
  "usage: openpsa-to-wmc [options] <model.xml | ->...\n"
  "  several files are read as one model (e.g. fault trees and their basic events)\n"
  "  -o FILE      write the CNF to FILE (default: stdout)\n"
  "  --top NAME   add NAME as a unit clause and write only what it depends on:\n"
  "               the weighted count is then P(NAME)\n"
  "  --tops       list the top events (gates nothing uses, event-tree sequences) and exit\n";
}

int main(int argc, char** argv) {
  std::vector<std::string> in;
  std::string out, top;
  bool list_tops = false;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      std::cout << usage;
      return EXIT_SUCCESS;
    } else if ((a == "-o" || a == "--top") && i + 1 < argc) (a == "-o" ? out : top) = argv[++i];
    else if (a == "--tops") list_tops = true;
    else if (a.size() > 1 && a[0] == '-') {
      std::cerr << "openpsa-to-wmc: unknown option " << a << "\n" << usage;
      return EXIT_FAILURE;
    } else in.push_back(a);
  }
  if (in.empty() || std::count(in.begin(), in.end(), "-") > 1) {
    std::cerr << usage;
    return EXIT_FAILURE;
  }
  std::vector<std::string> text;
  for (const auto& f : in) {
    std::stringstream buf;
    if (f == "-") buf << std::cin.rdbuf();
    else {
      std::ifstream s(f, std::ios::binary);
      if (!s) {
        std::cerr << "openpsa-to-wmc: cannot open " << f << "\n";
        return EXIT_FAILURE;
      }
      buf << s.rdbuf();
    }
    text.push_back(buf.str());
  }
  try {
    const opsa::Model m = opsa::read_mef(text);
    if (list_tops) {
      for (const auto& t : m.tops) std::cout << t << "\n";
      return EXIT_SUCCESS;
    }
    if (!top.empty() && !m.prob.count(top) &&
        std::none_of(m.gates.begin(), m.gates.end(), [&](const auto& g) { return g.first == top; })) {
      std::cerr << "openpsa-to-wmc: no gate or basic event named " << top << "\n";
      return EXIT_FAILURE;
    }
    const opsa::Cnf c = opsa::encode(m, top);
    if (out.empty()) opsa::write_cnf(std::cout, m, c, top);
    else {
      std::ofstream o(out);
      if (!o) {
        std::cerr << "openpsa-to-wmc: cannot write " << out << "\n";
        return EXIT_FAILURE;
      }
      opsa::write_cnf(o, m, c, top);
    }
  } catch (const opsa::Error& e) {
    const std::string& f = in.at(e.loc.file);
    std::cerr << (f == "-" ? "<stdin>" : f) << ":" << e.loc.line << ":" << e.loc.col << ": error: " << e.msg << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
