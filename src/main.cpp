// openpsa-to-wmc: an Open-PSA MEF fault tree as a weighted CNF for any weighted model counter.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "openpsa.hpp"

namespace {
const char* usage =
  "usage: openpsa-to-wmc [options] <model.xml | ->\n"
  "  -o FILE      write the CNF to FILE (default: stdout)\n"
  "  --top GATE   add GATE as a unit clause: the weighted count is then P(GATE)\n"
  "  --tops       list the top gates (gates no other gate uses) and exit\n";
}

int main(int argc, char** argv) {
  std::string in, out, top;
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
    } else if (in.empty()) in = a;
    else {
      std::cerr << usage;
      return EXIT_FAILURE;
    }
  }
  if (in.empty()) {
    std::cerr << usage;
    return EXIT_FAILURE;
  }
  std::stringstream buf;
  if (in == "-") buf << std::cin.rdbuf();
  else {
    std::ifstream f(in);
    if (!f) {
      std::cerr << "openpsa-to-wmc: cannot open " << in << "\n";
      return EXIT_FAILURE;
    }
    buf << f.rdbuf();
  }
  try {
    const opsa::Model m = opsa::read_mef(buf.str());
    if (list_tops) {
      for (const auto& t : m.tops) std::cout << t << "\n";
      return EXIT_SUCCESS;
    }
    const opsa::Cnf c = opsa::encode(m);
    if (!top.empty() && !c.var.count(top)) {
      std::cerr << "openpsa-to-wmc: no event named " << top << "\n";
      return EXIT_FAILURE;
    }
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
    std::cerr << (in == "-" ? "<stdin>" : in) << ":" << e.loc.line << ":" << e.loc.col << ": error: " << e.msg << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
