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

struct XParser {
  const std::string& s;
  size_t i = 0;
  int line = 1;
  size_t ls = 0;
  explicit XParser(const std::string& src) : s(src) {}
  Loc loc() const { return Loc{line, (int)(i - ls) + 1}; }
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
      else o += "&" + ent + ";";
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

const XNode* formula_child(const XNode& n) {
  for (const auto& k : n.kids)
    if (k.tag != "label" && k.tag != "attributes") return &k;
  return nullptr;
}

std::string label_of(const XNode& n) {
  for (const auto& k : n.kids)
    if (k.tag == "label") return trim(k.text);
  return "";
}

struct Reader {
  Model m;
  std::set<std::string> gate_refs;

  void collect(const XNode& n, std::vector<const XNode*>& gates, std::vector<const XNode*>& basics,
               std::vector<const XNode*>& houses) {
    if (n.tag == "define-gate") gates.push_back(&n);
    else if (n.tag == "define-basic-event") basics.push_back(&n);
    else if (n.tag == "define-house-event") houses.push_back(&n);
    else if (n.tag == "define-CCF-group" || n.tag == "define-parameter" || n.tag == "define-substitution" ||
             n.tag == "define-event-tree" || n.tag == "define-initiating-event" || n.tag == "define-extern-function")
      throw Error{n.loc, "<" + n.tag + "> is not supported yet"};
    for (const auto& k : n.kids) collect(k, gates, basics, houses);
  }

  Formula formula(const XNode& n) {
    Formula f;
    f.loc = n.loc;
    const std::string& t = n.tag;
    auto kids = [&](size_t min, size_t max) {
      if (n.kids.size() < min || n.kids.size() > max)
        throw Error{n.loc, "<" + t + "> takes " + (min == max ? std::to_string(min) : std::to_string(min) + " or more") +
                               " arguments, not " + std::to_string(n.kids.size())};
      for (const auto& k : n.kids) f.kids.push_back(formula(k));
    };
    if (t == "gate" || t == "basic-event" || t == "house-event" || t == "event") {
      f.kind = Formula::Ref;
      f.name = req(n, "name");
      if (t == "gate") gate_refs.insert(f.name);
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

  void run(const XNode& root) {
    std::vector<const XNode*> gates, basics, houses;
    collect(root, gates, basics, houses);
    std::set<std::string> seen;
    auto declare = [&](const XNode& n) {
      const std::string nm = req(n, "name");
      if (!seen.insert(nm).second) throw Error{n.loc, "'" + nm + "' is defined twice"};
      const std::string l = label_of(n);
      if (!l.empty()) m.labels[nm] = l;
      return nm;
    };
    for (const XNode* b : basics) {
      const std::string nm = declare(*b);
      const XNode* f = formula_child(*b);
      if (!f) throw Error{b->loc, "basic event " + nm + " has no probability"};
      if (f->tag != "float" && f->tag != "int")
        throw Error{f->loc, "basic event " + nm + ": only a <float> probability is supported yet, not <" + f->tag + ">"};
      const std::string v = req(*f, "value");
      mpq_class q;
      if (!parse_prob(v, q) || q < 0 || q > 1) throw Error{f->loc, "bad probability " + v};
      m.basics.push_back(nm);
      m.prob[nm] = q;
    }
    for (const XNode* h : houses) {
      const std::string nm = declare(*h);
      const XNode* f = formula_child(*h);
      bool v = false;
      if (f) {
        if (f->tag != "constant") throw Error{f->loc, "house event " + nm + " must have a <constant>"};
        const std::string s = req(*f, "value");
        if (s != "true" && s != "false") throw Error{f->loc, "<constant> value must be true or false"};
        v = s == "true";
      }
      m.houses[nm] = v;
    }
    for (const XNode* g : gates) {
      const std::string nm = declare(*g);
      const XNode* f = formula_child(*g);
      if (!f) throw Error{g->loc, "gate " + nm + " has no formula"};
      m.gates.emplace_back(nm, formula(*f));
    }
    for (const auto& [nm, f] : m.gates)
      if (!gate_refs.count(nm)) m.tops.push_back(nm);
  }
};

}  // namespace

Model read_mef(const std::string& xml) {
  XParser xp(xml);
  const XNode root = xp.element();
  Reader r;
  r.run(root);
  return r.m;
}

}  // namespace opsa
