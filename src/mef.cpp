// A small XML reader and the Open-PSA MEF fault-tree subset.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>

#include "openpsa.hpp"

namespace opsa {

bool parse_prob(const std::string& str, mpq_class& out) {
  std::string s = str;
  bool pct = false;
  if (!s.empty() && s.back() == '%') {
    pct = true;
    s.pop_back();
  }
  auto digits = [](const std::string& x) {
    return !x.empty() && std::all_of(x.begin(), x.end(), [](char c) { return std::isdigit((unsigned char)c); });
  };
  if (s.empty()) return false;
  const auto slash = s.find('/');
  if (slash != std::string::npos) {
    const std::string a = s.substr(0, slash), b = s.substr(slash + 1);
    if (!digits(a) || !digits(b)) return false;
    // base 10: GMP's default base 0 reads a leading 0 as octal
    mpz_class den(b, 10);
    if (den == 0) return false;
    out = mpq_class(mpz_class(a, 10), den);
  } else {
    std::string mant = s;
    long ex = 0;
    const auto e = s.find_first_of("eE");
    if (e != std::string::npos) {
      mant = s.substr(0, e);
      std::string es = s.substr(e + 1);
      bool neg = false;
      if (!es.empty() && (es[0] == '+' || es[0] == '-')) {
        neg = es[0] == '-';
        es = es.substr(1);
      }
      if (!digits(es) || es.size() > 6) return false;
      ex = std::stol(es) * (neg ? -1 : 1);
    }
    const auto dot = mant.find('.');
    std::string ip = mant, fp;
    if (dot != std::string::npos) {
      ip = mant.substr(0, dot);
      fp = mant.substr(dot + 1);
    }
    if (ip.empty() && fp.empty()) return false;
    if ((!ip.empty() && !digits(ip)) || (!fp.empty() && !digits(fp))) return false;
    mpz_class num(ip + fp, 10);
    ex -= (long)fp.size();
    mpz_class p10;
    mpz_ui_pow_ui(p10.get_mpz_t(), 10, (unsigned long)std::labs(ex));
    out = ex >= 0 ? mpq_class(num * p10) : mpq_class(num, p10);
  }
  if (pct) out /= 100;
  out.canonicalize();
  return true;
}

namespace {

struct XNode {
  std::string tag;
  std::map<std::string, std::string> attr;
  std::vector<XNode> kids;
  std::string text;
  Loc loc;
};

void put_utf8(std::string& o, uint32_t c) {
  if (c < 0x80) o += (char)c;
  else if (c < 0x800) {
    o += (char)(0xC0 | (c >> 6));
    o += (char)(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    o += (char)(0xE0 | (c >> 12));
    o += (char)(0x80 | ((c >> 6) & 0x3F));
    o += (char)(0x80 | (c & 0x3F));
  } else {
    o += (char)(0xF0 | (c >> 18));
    o += (char)(0x80 | ((c >> 12) & 0x3F));
    o += (char)(0x80 | ((c >> 6) & 0x3F));
    o += (char)(0x80 | (c & 0x3F));
  }
}

// Some generators write UTF-16 with a BOM; everything else is taken as UTF-8.
std::string to_utf8(const std::string& raw) {
  if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) return raw.substr(3);
  if (raw.size() < 2) return raw;
  const bool le = (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE;
  const bool be = (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF;
  if (!le && !be) return raw;
  std::string o;
  auto unit = [&](size_t k) -> uint32_t {
    const unsigned a = (unsigned char)raw[k], b = (unsigned char)raw[k + 1];
    return le ? a | (b << 8) : (a << 8) | b;
  };
  for (size_t k = 2; k + 1 < raw.size(); k += 2) {
    uint32_t c = unit(k);
    if (c >= 0xD800 && c < 0xDC00 && k + 3 < raw.size()) {
      const uint32_t d = unit(k + 2);
      if (d >= 0xDC00 && d < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
        k += 2;
      }
    }
    put_utf8(o, c);
  }
  return o;
}

struct XParser {
  const std::string& s;
  int file;
  size_t i = 0;
  int line = 1;
  size_t ls = 0;
  XParser(const std::string& src, int f) : s(src), file(f) {}
  Loc loc() const { return Loc{line, (int)(i - ls) + 1, file}; }
  void adv(size_t n = 1) {
    for (size_t k = 0; k < n && i < s.size(); k++, i++)
      if (s[i] == '\n') {
        line++;
        ls = i + 1;
      }
  }
  [[noreturn]] void fail(const std::string& m) const { throw Error{loc(), m}; }
  void ws() {
    while (i < s.size() && std::isspace((unsigned char)s[i])) adv();
  }
  static std::string unescape(const std::string& v) {
    std::string o;
    for (size_t k = 0; k < v.size(); k++) {
      if (v[k] != '&') {
        o += v[k];
        continue;
      }
      const auto e = v.find(';', k);
      if (e == std::string::npos) {
        o += v[k];
        continue;
      }
      const std::string ent = v.substr(k + 1, e - k - 1);
      if (ent == "amp") o += '&';
      else if (ent == "lt") o += '<';
      else if (ent == "gt") o += '>';
      else if (ent == "quot") o += '"';
      else if (ent == "apos") o += '\'';
      else if (ent.size() > 1 && ent[0] == '#') {
        const bool hex = ent[1] == 'x';
        const std::string d = ent.substr(hex ? 2 : 1);
        const bool ok = !d.empty() && d.size() <= 6 && std::all_of(d.begin(), d.end(), [&](char c) {
          return hex ? std::isxdigit((unsigned char)c) : std::isdigit((unsigned char)c);
        });
        const unsigned long cp = ok ? std::stoul(d, nullptr, hex ? 16 : 10) : 0;
        if (ok && cp > 0 && cp <= 0x10FFFF) put_utf8(o, (uint32_t)cp);
        else o += "&" + ent + ";";
      } else o += "&" + ent + ";";
      k = e;
    }
    return o;
  }
  void skip_misc() {
    while (true) {
      ws();
      if (s.compare(i, 4, "<!--") == 0) {
        const auto e = s.find("-->", i);
        if (e == std::string::npos) fail("unterminated comment");
        adv(e + 3 - i);
      } else if (s.compare(i, 2, "<?") == 0) {
        const auto e = s.find("?>", i);
        if (e == std::string::npos) fail("unterminated <?");
        adv(e + 2 - i);
      } else if (s.compare(i, 2, "<!") == 0) {
        const auto e = s.find('>', i);
        if (e == std::string::npos) fail("unterminated <!");
        adv(e + 1 - i);
      } else return;
    }
  }
  std::string name() {
    const size_t b = i;
    while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_' || s[i] == ':' || s[i] == '.'))
      adv();
    if (b == i) fail("expected a name");
    return s.substr(b, i - b);
  }
  XNode element() {
    skip_misc();
    if (i >= s.size() || s[i] != '<') fail("expected an element");
    XNode n;
    n.loc = loc();
    adv();
    n.tag = name();
    while (true) {
      ws();
      if (s.compare(i, 2, "/>") == 0) {
        adv(2);
        return n;
      }
      if (i < s.size() && s[i] == '>') {
        adv();
        break;
      }
      const std::string a = name();
      ws();
      if (i >= s.size() || s[i] != '=') fail("expected '=' after attribute " + a);
      adv();
      ws();
      if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) fail("expected a quoted value for " + a);
      const char q = s[i];
      adv();
      const size_t b = i;
      while (i < s.size() && s[i] != q) adv();
      n.attr[a] = unescape(s.substr(b, i - b));
      adv();
    }
    while (true) {
      const size_t b = i;
      while (i < s.size() && s[i] != '<') adv();
      n.text += unescape(s.substr(b, i - b));
      if (i >= s.size()) fail("unterminated element <" + n.tag + ">");
      if (s.compare(i, 2, "</") == 0) {
        adv(2);
        const std::string t = name();
        if (t != n.tag) fail("</" + t + "> closes <" + n.tag + ">");
        ws();
        if (i >= s.size() || s[i] != '>') fail("expected '>'");
        adv();
        return n;
      }
      if (s.compare(i, 4, "<!--") == 0 || s.compare(i, 2, "<?") == 0) {
        skip_misc();
        continue;
      }
      n.kids.push_back(element());
    }
  }
};

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::string req(const XNode& n, const char* a) {
  auto it = n.attr.find(a);
  if (it == n.attr.end()) throw Error{n.loc, "<" + n.tag + "> needs a '" + a + "' attribute"};
  return it->second;
}

int req_int(const XNode& n, const char* a) {
  const std::string v = req(n, a);
  if (v.empty() || v.size() > 6 || !std::all_of(v.begin(), v.end(), [](char c) { return std::isdigit((unsigned char)c); }))
    throw Error{n.loc, std::string("'") + a + "' must be a non-negative integer, not " + v};
  return std::stoi(v);
}

bool is_meta(const XNode& n) { return n.tag == "label" || n.tag == "attributes"; }

const XNode* formula_child(const XNode& n) {
  for (const auto& k : n.kids)
    if (!is_meta(k)) return &k;
  return nullptr;
}

std::string label_of(const XNode& n) {
  for (const auto& k : n.kids)
    if (k.tag == "label") return trim(k.text);
  return "";
}

enum class Kind { Basic, House, Gate };

const char* kind_name(Kind k) { return k == Kind::Basic ? "basic event" : k == Kind::House ? "house event" : "gate"; }

using Scope = std::vector<std::string>;

std::string join(const Scope& sc, size_t n, const std::string& last) {
  std::string o;
  for (size_t k = 0; k < n; k++) o += sc[k] + ".";
  return o + last;
}

struct Def {
  const XNode* n;
  Scope scope;
  bool priv;
  std::string canon;
};

struct Reader {
  Model m;
  std::vector<Def> gates, basics, houses;
  std::vector<const XNode*> trees;
  // Every event under its full path, and public ones also under their bare name.
  std::map<std::string, std::string> by_path;
  std::map<std::string, Kind> kind;
  std::set<std::string> gate_refs;

  static bool role_private(const XNode& n, bool inherited) {
    auto it = n.attr.find("role");
    if (it == n.attr.end()) return inherited;
    if (it->second != "private" && it->second != "public")
      throw Error{n.loc, "role must be private or public, not " + it->second};
    return it->second == "private";
  }

  void collect(const XNode& n, const Scope& scope, bool priv) {
    const std::string& t = n.tag;
    if (t == "define-gate") gates.push_back({&n, scope, role_private(n, priv), ""});
    else if (t == "define-basic-event") basics.push_back({&n, scope, role_private(n, priv), ""});
    else if (t == "define-house-event") houses.push_back({&n, scope, role_private(n, priv), ""});
    else if (t == "define-event-tree") {
      if (!scope.empty()) throw Error{n.loc, "<define-event-tree> inside a fault tree"};
      trees.push_back(&n);
    } else if (t == "define-initiating-event") {
    } else if (t == "define-fault-tree" || t == "define-component") {
      Scope sc = scope;
      sc.push_back(req(n, "name"));
      for (const auto& k : n.kids) collect(k, sc, role_private(n, priv));
    } else if (t == "model-data") {
      for (const auto& k : n.kids) collect(k, {}, false);
    } else if (t == "define-CCF-group" || t == "define-parameter" || t == "define-substitution" ||
               t == "define-extern-function" || t == "define-extern-library" || t == "define-alignment" ||
               t == "define-rule" || t == "define-consequence" || t == "define-consequence-group") {
      throw Error{n.loc, "<" + t + "> is not supported yet"};
    } else {
      for (const auto& k : n.kids) collect(k, scope, priv);
    }
  }

  void add_path(const std::string& path, const std::string& canon, Loc loc) {
    if (!by_path.emplace(path, canon).second) throw Error{loc, "'" + path + "' is defined twice"};
  }

  void declare(Def& d, Kind k) {
    const std::string nm = req(*d.n, "name");
    const std::string full = join(d.scope, d.scope.size(), nm);
    d.canon = d.priv ? full : nm;
    add_path(full, d.canon, d.n->loc);
    if (!d.priv && full != nm) add_path(nm, d.canon, d.n->loc);
    kind[d.canon] = k;
    const std::string l = label_of(*d.n);
    if (!l.empty()) m.labels[d.canon] = l;
  }

  // The innermost container's name wins: "G" inside FT.C is tried as FT.C.G, FT.G, then G.
  std::string resolve(const std::string& name, const Scope& scope, const char* want, Loc loc) {
    for (size_t k = scope.size() + 1; k-- > 0;) {
      auto it = by_path.find(join(scope, k, name));
      if (it == by_path.end()) continue;
      const Kind got = kind.at(it->second);
      if (want && got != (std::string(want) == "gate" ? Kind::Gate : std::string(want) == "basic-event" ? Kind::Basic : Kind::House))
        throw Error{loc, "<" + std::string(want) + " name=\"" + name + "\"> refers to a " + kind_name(got)};
      return it->second;
    }
    throw Error{loc, "event " + name + " is used but not defined"};
  }

  Formula formula(const XNode& n, const Scope& scope) {
    Formula f;
    f.loc = n.loc;
    const std::string& t = n.tag;
    auto kids = [&](size_t min, size_t max) {
      if (n.kids.size() < min || n.kids.size() > max)
        throw Error{n.loc, "<" + t + "> takes " + (min == max ? std::to_string(min) : std::to_string(min) + " or more") +
                               " arguments, not " + std::to_string(n.kids.size())};
      for (const auto& k : n.kids) f.kids.push_back(formula(k, scope));
    };
    if (t == "gate" || t == "basic-event" || t == "house-event" || t == "event") {
      f.kind = Formula::Ref;
      f.name = resolve(req(n, "name"), scope, t == "event" ? nullptr : t.c_str(), n.loc);
      if (kind.at(f.name) == Kind::Gate) gate_refs.insert(f.name);
    } else if (t == "constant") {
      f.kind = Formula::Const;
      const std::string v = req(n, "value");
      if (v != "true" && v != "false") throw Error{n.loc, "<constant> value must be true or false"};
      f.val = v == "true";
    } else if (t == "and" || t == "nand") {
      f.kind = Formula::And;
      kids(1, SIZE_MAX);
    } else if (t == "or" || t == "nor") {
      f.kind = Formula::Or;
      kids(1, SIZE_MAX);
    } else if (t == "xor") {
      f.kind = Formula::Xor;
      kids(1, SIZE_MAX);
    } else if (t == "iff") {
      f.kind = Formula::Iff;
      kids(2, 2);
    } else if (t == "imply") {
      f.kind = Formula::Imply;
      kids(2, 2);
    } else if (t == "not") {
      f.kind = Formula::Not;
      kids(1, 1);
    } else if (t == "atleast") {
      f.kind = Formula::AtLeast;
      f.lo = req_int(n, "min");
      kids(1, SIZE_MAX);
    } else if (t == "cardinality") {
      f.kind = Formula::Card;
      f.lo = req_int(n, "min");
      f.hi = req_int(n, "max");
      kids(1, SIZE_MAX);
    } else {
      throw Error{n.loc, "<" + t + "> is not supported in a formula"};
    }
    if (t == "nand" || t == "nor") {
      Formula neg;
      neg.kind = Formula::Not;
      neg.loc = n.loc;
      neg.kids.push_back(std::move(f));
      return neg;
    }
    return f;
  }

  struct Tree {
    std::string name;
    std::set<std::string> fes;
    std::map<std::string, const XNode*> branches;
    std::map<std::string, std::vector<Formula>> reach;
    std::set<std::string> active;
  };

  // Each sequence is the OR, over the paths that end in it, of the AND of what those paths collect.
  void walk(Tree& tr, const XNode& n, std::vector<Formula> conj) {
    for (const auto& k : n.kids) {
      if (is_meta(k)) continue;
      if (k.tag == "collect-formula") {
        const XNode* f = formula_child(k);
        if (!f) throw Error{k.loc, "<collect-formula> needs a formula"};
        conj.push_back(formula(*f, {}));
      } else if (k.tag == "fork") {
        const std::string fe = req(k, "functional-event");
        if (!tr.fes.count(fe)) throw Error{k.loc, "functional event " + fe + " is not defined in event tree " + tr.name};
        for (const auto& p : k.kids) {
          if (is_meta(p)) continue;
          if (p.tag != "path") throw Error{p.loc, "<fork> may only contain <path>, not <" + p.tag + ">"};
          req(p, "state");
          walk(tr, p, conj);
        }
      } else if (k.tag == "sequence") {
        const std::string s = req(k, "name");
        auto it = tr.reach.find(s);
        if (it == tr.reach.end()) throw Error{k.loc, "sequence " + s + " is not defined in event tree " + tr.name};
        Formula a;
        a.kind = Formula::And;
        a.loc = k.loc;
        a.kids = conj;
        it->second.push_back(std::move(a));
      } else if (k.tag == "branch") {
        const std::string b = req(k, "name");
        auto it = tr.branches.find(b);
        if (it == tr.branches.end()) throw Error{k.loc, "branch " + b + " is not defined in event tree " + tr.name};
        if (!tr.active.insert(b).second) throw Error{k.loc, "branch " + b + " leads back to itself"};
        walk(tr, *it->second, conj);
        tr.active.erase(b);
      } else {
        throw Error{k.loc, "<" + k.tag + "> is not supported yet in an event tree"};
      }
    }
  }

  void event_tree(const XNode& et) {
    Tree tr;
    tr.name = req(et, "name");
    const XNode* init = nullptr;
    std::vector<const XNode*> seqs;
    for (const auto& k : et.kids) {
      if (is_meta(k)) continue;
      if (k.tag == "define-functional-event") tr.fes.insert(req(k, "name"));
      else if (k.tag == "define-sequence") {
        if (formula_child(k)) throw Error{formula_child(k)->loc, "instructions in a <define-sequence> are not supported yet"};
        if (!tr.reach.emplace(req(k, "name"), std::vector<Formula>{}).second)
          throw Error{k.loc, "sequence " + req(k, "name") + " is defined twice"};
        seqs.push_back(&k);
      } else if (k.tag == "define-branch") {
        if (!tr.branches.emplace(req(k, "name"), &k).second) throw Error{k.loc, "branch " + req(k, "name") + " is defined twice"};
      } else if (k.tag == "initial-state") init = &k;
      else throw Error{k.loc, "<" + k.tag + "> is not supported yet in an event tree"};
    }
    if (!init) throw Error{et.loc, "event tree " + tr.name + " has no <initial-state>"};
    walk(tr, *init, {});
    for (const XNode* s : seqs) {
      const std::string nm = req(*s, "name");
      add_path(nm, nm, s->loc);
      kind[nm] = Kind::Gate;
      const std::string l = label_of(*s);
      if (!l.empty()) m.labels[nm] = l;
      Formula f;
      f.kind = Formula::Or;
      f.loc = s->loc;
      f.kids = std::move(tr.reach[nm]);
      m.gates.emplace_back(nm, std::move(f));
    }
  }

  void run(const std::vector<XNode>& roots) {
    for (const auto& r : roots) collect(r, {}, false);
    for (auto& d : basics) declare(d, Kind::Basic);
    for (auto& d : houses) declare(d, Kind::House);
    for (auto& d : gates) declare(d, Kind::Gate);
    for (const auto& d : basics) {
      const XNode* f = formula_child(*d.n);
      if (!f) throw Error{d.n->loc, "basic event " + d.canon + " has no probability"};
      if (f->tag != "float" && f->tag != "int")
        throw Error{f->loc, "basic event " + d.canon + ": only a <float> probability is supported yet, not <" + f->tag + ">"};
      const std::string v = trim(req(*f, "value"));
      mpq_class q;
      if (!parse_prob(v, q) || q < 0 || q > 1) throw Error{f->loc, "bad probability " + v};
      m.basics.push_back(d.canon);
      m.prob[d.canon] = q;
    }
    for (const auto& d : houses) {
      const XNode* f = formula_child(*d.n);
      bool v = false;
      if (f) {
        if (f->tag != "constant") throw Error{f->loc, "house event " + d.canon + " must have a <constant>"};
        const std::string s = req(*f, "value");
        if (s != "true" && s != "false") throw Error{f->loc, "<constant> value must be true or false"};
        v = s == "true";
      }
      m.houses[d.canon] = v;
    }
    for (const auto& d : gates) {
      const XNode* f = formula_child(*d.n);
      if (!f) throw Error{d.n->loc, "gate " + d.canon + " has no formula"};
      m.gates.emplace_back(d.canon, formula(*f, d.scope));
    }
    for (const XNode* t : trees) event_tree(*t);
    for (const auto& [nm, f] : m.gates)
      if (!gate_refs.count(nm)) m.tops.push_back(nm);
  }
};

}  // namespace

Model read_mef(const std::vector<std::string>& files) {
  std::vector<std::string> text;
  std::vector<XNode> roots;
  for (const auto& f : files) text.push_back(to_utf8(f));
  for (size_t k = 0; k < text.size(); k++) {
    XParser xp(text[k], (int)k);
    roots.push_back(xp.element());
  }
  Reader r;
  r.run(roots);
  return r.m;
}

}  // namespace opsa
