// Open-PSA MEF fault trees as weighted CNF.

#pragma once

#include <gmpxx.h>

#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace opsa {

struct Loc {
  int line = 0, col = 0, file = 0;
};

struct Error {
  Loc loc;
  std::string msg;
};

struct Formula {
  enum Kind { Const, Ref, Not, And, Or, Xor, Iff, Imply, AtLeast, Card } kind = Const;
  bool val = false;
  std::string name;
  int lo = 0, hi = 0;
  std::vector<Formula> kids;
  Loc loc;
};

struct Model {
  std::vector<std::string> basics;
  std::map<std::string, mpq_class> prob;
  std::map<std::string, bool> houses;
  std::vector<std::pair<std::string, Formula>> gates;
  std::vector<std::string> tops;  // gates nothing else uses, e.g. event-tree sequences
  std::map<std::string, std::string> labels;
};

struct Cnf {
  int nvars = 0;
  std::vector<std::vector<int>> clauses;
  std::map<std::string, int> var;  // basic events and gates
  std::vector<int> show;
};

bool parse_prob(const std::string& s, mpq_class& out);
Model read_mef(const std::vector<std::string>& files);  // file contents; throws Error
// Only what `top` depends on, or everything if it is empty. Throws Error (cycles, unknown names).
Cnf encode(const Model& m, const std::string& top);
void write_cnf(std::ostream& o, const Model& m, const Cnf& c, const std::string& top);

}  // namespace opsa
