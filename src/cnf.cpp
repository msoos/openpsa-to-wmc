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
    auto h = m.houses.find(f.name);
    if (h != m.houses.end()) return h->second ? TL : -TL;
    auto v = c.var.find(f.name);
    if (v == c.var.end()) throw Error{f.loc, "event " + f.name + " is used but not defined"};
    return v->second;
  }

  void refs(const Formula& f, std::vector<std::string>& gs, std::set<std::string>& bs) {
    if (f.kind == Formula::Ref) {
      if (gate.count(f.name)) gs.push_back(f.name);
      else if (!m.houses.count(f.name)) bs.insert(f.name);
    }
    for (const auto& k : f.kids) refs(k, gs, bs);
  }

  // Iterative: synthetic models chain tens of thousands of gates.
  std::vector<std::string> order(const std::vector<std::string>& roots, std::set<std::string>& used) {
    std::vector<std::string> out, stack;
    std::vector<size_t> next;
    std::map<std::string, std::vector<std::string>> deps;
    auto push = [&](const std::string& nm) {
      state[nm] = 1;
      refs(*gate.at(nm), deps[nm], used);
      stack.push_back(nm);
      next.push_back(0);
    };
    for (const auto& r : roots) {
      if (state[r]) continue;
      push(r);
      while (!stack.empty()) {
        const std::string nm = stack.back();
        const auto& ds = deps[nm];
        if (next.back() < ds.size()) {
          const std::string d = ds[next.back()++];
          if (state[d] == 2) continue;
          if (state[d] == 1) {
            std::string path;
            for (auto it = std::find(stack.begin(), stack.end(), d); it != stack.end(); ++it) path += *it + " -> ";
            throw Error{gate.at(nm)->loc, "cycle: " + path + d};
          }
          push(d);
        } else {
          state[nm] = 2;
          out.push_back(nm);
          stack.pop_back();
          next.pop_back();
        }
      }
    }
    return out;
  }

  void define(const std::string& nm) {
    const int l = lit(*gate.at(nm));
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
  }

  void run(const std::string& top) {
    std::vector<std::string> roots;
    std::set<std::string> used;
    if (top.empty())
      for (const auto& [nm, f] : m.gates) roots.push_back(nm);
    else if (gate.count(top)) roots.push_back(top);
    else used.insert(top);
    const std::vector<std::string> ord = order(roots, used);
    for (const auto& b : m.basics)
      if (top.empty() || used.count(b)) c.var[b] = fresh();
    nbasics = c.nvars;
    for (const auto& nm : ord) define(nm);
    // Gates are functions of the basic events, so projecting onto the basic events keeps the
    // count; counters are much faster that way. Without --top, show everything for assumptions.
    if (top.empty()) {
      std::set<int> show;
      for (const auto& [nm, v] : c.var) show.insert(v);
      c.show.assign(show.begin(), show.end());
    } else
      for (int v = 1; v <= nbasics; v++) c.show.push_back(v);
  }
};

}  // namespace

Cnf encode(const Model& m, const std::string& top) {
  Cnf c;
  Enc e(m, c);
  e.run(top);
  return c;
}

void write_cnf(std::ostream& o, const Model& m, const Cnf& c, const std::string& top) {
  o << "c t pwmc\n";
  o << "p cnf " << c.nvars << " " << c.clauses.size() + (top.empty() ? 0 : 1) << "\n";
  for (const auto& b : m.basics) {
    if (!c.var.count(b)) continue;
    const int v = c.var.at(b);
    const mpq_class& p = m.prob.at(b);
    o << "c p weight " << v << " " << p.get_str() << " 0\n";
    o << "c p weight " << -v << " " << mpq_class(1 - p).get_str() << " 0\n";
  }
  o << "c p show";
  for (const int v : c.show) o << " " << v;
  o << " 0\n";
  for (const auto& [nm, v] : c.var) o << "c " << v << " " << nm << "\n";
  for (const auto& t : m.tops)
    if (c.var.count(t)) o << "c top " << c.var.at(t) << " " << t << "\n";
  for (const auto& cl : c.clauses) {
    for (const int l : cl) o << l << " ";
    o << "0\n";
  }
  if (!top.empty()) o << c.var.at(top) << " 0\n";
}

}  // namespace opsa
