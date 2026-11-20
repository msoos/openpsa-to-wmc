// Tseitin encoding (both directions, structurally hashed) and the CNF writer.

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <set>

#include "openpsa.hpp"

namespace opsa {

namespace {

constexpr int TL = INT_MAX;  // literal "true"; -TL is "false"

struct Enc {
  const Model& m;
  Cnf& c;
  std::map<std::vector<int>, int> hash;
  std::map<std::string, const Formula*> gate;
  std::map<std::string, int> state;
  std::vector<std::string> stack;
  std::set<int> owned;
  int nbasics = 0;

  Enc(const Model& model, Cnf& cnf) : m(model), c(cnf) {
    for (const auto& [nm, f] : m.gates) gate[nm] = &f;
  }

  int fresh() { return ++c.nvars; }
  void cl(std::vector<int> v) { c.clauses.push_back(std::move(v)); }

  int mk_and(const std::vector<int>& ls) {
    std::vector<int> v;
    for (const int l : ls) {
      if (l == -TL) return -TL;
      if (l != TL) v.push_back(l);
    }
    std::sort(v.begin(), v.end(), [](int a, int b) { return std::abs(a) != std::abs(b) ? std::abs(a) < std::abs(b) : a < b; });
    v.erase(std::unique(v.begin(), v.end()), v.end());
    for (size_t k = 1; k < v.size(); k++)
      if (v[k] == -v[k - 1]) return -TL;
    if (v.empty()) return TL;
    if (v.size() == 1) return v[0];
    std::vector<int> key{0};
    key.insert(key.end(), v.begin(), v.end());
    auto it = hash.find(key);
    if (it != hash.end()) return it->second;
    const int x = fresh();
    std::vector<int> big{x};
    for (const int l : v) {
      cl({-x, l});
      big.push_back(-l);
    }
    cl(big);
    hash[key] = x;
    return x;
  }
  int mk_or(std::vector<int> ls) {
    for (int& l : ls) l = -l;
    return -mk_and(ls);
  }
  int mk_xor(int a, int b) {
    if (std::abs(a) == TL) return a == TL ? -b : b;
    if (std::abs(b) == TL) return b == TL ? -a : a;
    bool neg = false;
    if (a < 0) {
      a = -a;
      neg = !neg;
    }
    if (b < 0) {
      b = -b;
      neg = !neg;
    }
    if (a == b) return neg ? TL : -TL;
    if (a > b) std::swap(a, b);
    const std::vector<int> key{1, a, b};
    int x;
    auto it = hash.find(key);
    if (it != hash.end()) x = it->second;
    else {
      x = fresh();
      cl({-x, a, b});
      cl({-x, -a, -b});
      cl({x, -a, b});
      cl({x, a, -b});
      hash[key] = x;
    }
    return neg ? -x : x;
  }
  int atleast(const std::vector<int>& xs, int k) {
    if (k <= 0) return TL;
    if (k > (int)xs.size()) return -TL;
    std::vector<int> s(k + 1, -TL);
    s[0] = TL;
    for (const int x : xs)
      for (int j = k; j >= 1; j--) s[j] = mk_or({s[j], mk_and({x, s[j - 1]})});
    return s[k];
  }

  int lit(const Formula& f) {
    std::vector<int> ks;
    for (const auto& k : f.kids) ks.push_back(lit(k));
    switch (f.kind) {
      case Formula::Const: return f.val ? TL : -TL;
      case Formula::Ref: return ref(f);
      case Formula::Not: return -ks[0];
      case Formula::And: return mk_and(ks);
      case Formula::Or: return mk_or(ks);
      case Formula::Xor: {
        int acc = -TL;
        for (const int k : ks) acc = mk_xor(acc, k);
        return acc;
      }
      case Formula::Iff: return -mk_xor(ks[0], ks[1]);
      case Formula::Imply: return mk_or({-ks[0], ks[1]});
      case Formula::AtLeast: return atleast(ks, f.lo);
      case Formula::Card: return mk_and({atleast(ks, f.lo), -atleast(ks, f.hi + 1)});
    }
    return -TL;
  }

  int ref(const Formula& f) {
    auto v = c.var.find(f.name);
    if (v != c.var.end() && (state[f.name] == 2 || !gate.count(f.name))) return v->second;
    auto h = m.houses.find(f.name);
    if (h != m.houses.end()) return h->second ? TL : -TL;
    if (!gate.count(f.name)) throw Error{f.loc, "event " + f.name + " is used but not defined"};
    return gate_lit(f.name, f.loc);
  }

  int gate_lit(const std::string& nm, Loc loc) {
    if (state[nm] == 2) return c.var[nm];
    if (state[nm] == 1) {
      std::string path;
      for (auto it = std::find(stack.begin(), stack.end(), nm); it != stack.end(); ++it) path += *it + " -> ";
      throw Error{loc, "cycle: " + path + nm};
    }
    state[nm] = 1;
    stack.push_back(nm);
    const int l = lit(*gate[nm]);
    stack.pop_back();
    int v;
    if (l > 0 && l != TL && l > nbasics && !owned.count(l)) v = l;
    else {
      v = fresh();
      if (l == TL) cl({v});
      else if (l == -TL) cl({-v});
      else {
        cl({-v, l});
        cl({v, -l});
      }
    }
    owned.insert(v);
    c.var[nm] = v;
    state[nm] = 2;
    return v;
  }

  void run() {
    for (const auto& b : m.basics) c.var[b] = fresh();
    nbasics = c.nvars;
    for (const auto& [nm, f] : m.gates) gate_lit(nm, f.loc);
    std::set<int> show;
    for (const auto& [nm, v] : c.var) show.insert(v);
    c.show.assign(show.begin(), show.end());
  }
};

}  // namespace

Cnf encode(const Model& m) {
  Cnf c;
  Enc e(m, c);
  e.run();
  return c;
}

void write_cnf(std::ostream& o, const Model& m, const Cnf& c, const std::string& top) {
  o << "c t pwmc\n";
  o << "p cnf " << c.nvars << " " << c.clauses.size() + (top.empty() ? 0 : 1) << "\n";
  for (const auto& b : m.basics) {
    const int v = c.var.at(b);
    const mpq_class& p = m.prob.at(b);
    o << "c p weight " << v << " " << p.get_str() << " 0\n";
    o << "c p weight " << -v << " " << mpq_class(1 - p).get_str() << " 0\n";
  }
  o << "c p show";
  for (const int v : c.show) o << " " << v;
  o << " 0\n";
  for (const auto& [nm, v] : c.var) o << "c " << v << " " << nm << "\n";
  for (const auto& t : m.tops) o << "c top " << c.var.at(t) << " " << t << "\n";
  for (const auto& cl : c.clauses) {
    for (const int l : cl) o << l << " ";
    o << "0\n";
  }
  if (!top.empty()) o << c.var.at(top) << " 0\n";
}

}  // namespace opsa
