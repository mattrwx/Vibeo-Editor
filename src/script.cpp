#include "script.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>

// ---------------------------------------------------------------------------
// AIVE_SCRIPT v1
//
//   def shake(amp) { x: v + 0.01*(amp)*sin(31*t); }
//   clip intro   { src:1; from:0; to:6.5; speed:1; shake(t/5); }
//   media logo   { path:"logo.png"; during:intro; x:0.9; y:0.1; scale:0.12; }
//   text title   { content:"YO"; start:1; end:4; x:0.5; y:0.2; }
//   sound boom   { path:"boom.wav"; at:3.2; volume:0.8; }
//   settings     { motionblur:med; fadeout:1; musicstart:4.0; }
//   timeline     { intro; kill1; kill2; }
//   question "..." lines are also allowed.
// ---------------------------------------------------------------------------

namespace {

struct FxDef {
    std::vector<std::string> params;
    std::vector<std::pair<std::string, std::string>> assigns;   // prop -> raw expr
    int line = 0;
};

struct ObjDecl {
    std::string kind;   // clip | media | text | sound
    std::string name;
    std::map<std::string, std::string> statics;                 // raw values
    std::vector<std::pair<std::string, std::string>> assigns;   // expanded, in order
    int line = 0;
};

struct Parser {
    const std::string& s;
    size_t i = 0;
    int line = 1;
    EditScript* out;

    explicit Parser(const std::string& text, EditScript* o) : s(text), out(o) {}

    void err(int ln, const std::string& msg) {
        char b[512];
        std::snprintf(b, sizeof(b), "line %d: %s", ln, msg.c_str());
        out->errors.push_back(b);
    }
    void warn(int ln, const std::string& msg) {
        char b[512];
        std::snprintf(b, sizeof(b), "line %d: %s", ln, msg.c_str());
        out->warnings.push_back(b);
    }

    void skipWs() {
        for (;;) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
                if (s[i] == '\n') line++;
                i++;
            }
            if (i < s.size() && (s[i] == '#' || (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/'))) {
                while (i < s.size() && s[i] != '\n') i++;
                continue;
            }
            break;
        }
    }
    bool atEnd() { skipWs(); return i >= s.size(); }
    char peek() { skipWs(); return i < s.size() ? s[i] : 0; }
    bool eat(char c) {
        skipWs();
        if (i < s.size() && s[i] == c) { i++; return true; }
        return false;
    }
    std::string readIdent() {
        skipWs();
        size_t st = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
        return ToLower(s.substr(st, i - st));
    }
    bool readString(std::string& v) {
        // plain whitespace only — a following value may start with # (color),
        // which skipWs would swallow as a comment
        size_t save = i;
        int saveLine = line;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
            if (s[i] == '\n') line++;
            i++;
        }
        if (i >= s.size() || s[i] != '"') {
            i = save;
            line = saveLine;
            return false;
        }
        size_t close = s.find('"', i + 1);
        if (close == std::string::npos) return false;
        v = s.substr(i + 1, close - i - 1);
        i = close + 1;
        return true;
    }
    // raw value text until ';' at paren depth 0
    std::string readRaw() {
        // no skipWs() here: values may legitimately start with # (colors),
        // which skipWs would treat as a comment
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        std::string v;
        int depth = 0;
        while (i < s.size()) {
            char c = s[i];
            if (c == ';' && depth == 0) break;
            if (c == '}' && depth == 0) break;
            if (c == '(') depth++;
            if (c == ')') depth--;
            if (c == '\n') { line++; i++; break; }   // values end at line end too
            v += c;
            i++;
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        return v;
    }
};

// token-level identifier replacement
std::string ReplaceIdents(const std::string& f,
                          const std::map<std::string, std::string>& map) {
    std::string outS;
    for (size_t i = 0; i < f.size();) {
        char c = f[i];
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t st = i;
            while (i < f.size() && (std::isalnum((unsigned char)f[i]) || f[i] == '_')) i++;
            std::string ident = f.substr(st, i - st);
            auto it = map.find(ident);
            outS += it != map.end() ? it->second : ident;
            continue;
        }
        outS += c;
        i++;
    }
    return outS;
}

// expand helper "macro functions" (rand, ramp, curve names) recursively
bool ExpandMacros(std::string& f, std::string* why, int depth = 0) {
    if (depth > 8) { *why = "expression too deeply nested"; return false; }
    static const std::set<std::string> curves = { "ease", "easein", "easeout", "backin",
                                                  "backout", "expoin", "expoout", "spike",
                                                  "smooth" };
    for (size_t i = 0; i < f.size();) {
        if (!(std::isalpha((unsigned char)f[i]) || f[i] == '_')) { i++; continue; }
        size_t st = i;
        while (i < f.size() && (std::isalnum((unsigned char)f[i]) || f[i] == '_')) i++;
        std::string ident = f.substr(st, i - st);
        bool isMacro = ident == "rand" || ident == "ramp" || curves.count(ident);
        if (!isMacro || i >= f.size() || f[i] != '(') continue;
        // find matching close paren
        int d = 0;
        size_t open = i, close = i;
        for (; close < f.size(); close++) {
            if (f[close] == '(') d++;
            if (f[close] == ')' && --d == 0) break;
        }
        if (d != 0) { *why = "unbalanced '(' in " + ident + "()"; return false; }
        std::string argsRaw = f.substr(open + 1, close - open - 1);
        if (!ExpandMacros(argsRaw, why, depth + 1)) return false;
        // split top-level args
        std::vector<std::string> args;
        {
            int ad = 0;
            std::string cur;
            for (char c : argsRaw) {
                if (c == '(') ad++;
                if (c == ')') ad--;
                if (c == ',' && ad == 0) { args.push_back(cur); cur.clear(); continue; }
                cur += c;
            }
            args.push_back(cur);
        }
        std::string repl;
        if (ident == "rand") {
            if (args.size() != 1) { *why = "rand() takes 1 argument"; return false; }
            repl = "((random(0)*2-1)*(" + args[0] + "))";
        } else if (ident == "ramp") {
            if (args.size() != 2) { *why = "ramp(a,b) takes 2 arguments"; return false; }
            repl = "clip(((t)-(" + args[0] + "))/max(0.0001,(" + args[1] + ")-(" + args[0] +
                   ")),0,1)";
        } else {
            if (args.size() != 1) { *why = ident + "() takes 1 argument (progress 0..1)"; return false; }
            repl = CurveExprFor(ident, "clip(" + args[0] + ",0,1)");
        }
        f = f.substr(0, st) + repl + f.substr(close + 1);
        i = st + repl.size();
    }
    return true;
}

struct PropSpec {
    const char* name;
    double dflt;
};
const PropSpec kClipProps[] = { { "zoom", 1 }, { "rot", 0 }, { "hue", 0 }, { "sat", 1 },
                                { "bright", 0 }, { "dx", 0 }, { "dy", 0 }, { "volume", 1 } };
const PropSpec kMediaProps[] = { { "x", 0.5 }, { "y", 0.5 }, { "scale", 0.25 }, { "rot", 0 },
                                 { "opacity", 1 }, { "hue", 0 }, { "sat", 1 }, { "bright", 0 } };
const PropSpec kTextProps[] = { { "x", 0.5 }, { "y", 0.5 }, { "opacity", 1 } };

const PropSpec* FindProp(const ObjDecl& o, const std::string& p) {
    auto scan = [&](const PropSpec* arr, size_t n) -> const PropSpec* {
        for (size_t k = 0; k < n; k++)
            if (p == arr[k].name) return &arr[k];
        return nullptr;
    };
    if (o.kind == "clip") return scan(kClipProps, std::size(kClipProps));
    if (o.kind == "media") return scan(kMediaProps, std::size(kMediaProps));
    if (o.kind == "text") return scan(kTextProps, std::size(kTextProps));
    return nullptr;
}

bool IsNumber(const std::string& v, double* out) {
    if (v.empty()) return false;
    char* end = nullptr;
    double d = std::strtod(v.c_str(), &end);
    while (end && *end == ' ') end++;
    if (!end || *end != 0) return false;
    if (out) *out = d;
    return true;
}

} // namespace

EditScript ParseScript(const std::string& text,
                       const std::vector<ScriptSource>& sources,
                       const std::vector<MediaFile>& media,
                       const MusicConfig* music, double fps) {
    EditScript sc;
    if (fps < 10 || fps > 240) fps = 30;
    Parser P(text, &sc);

    std::map<std::string, FxDef> defs;
    std::vector<ObjDecl> objs;
    std::map<std::string, int> objIndex;
    std::vector<std::pair<std::string, int>> timelineNames;   // name, line
    std::map<std::string, std::string> settings;
    int settingsLine = 0;

    auto findMedia = [&](std::string name) -> const MediaFile* {
        size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        std::string lo = ToLower(name);
        for (const auto& m : media)
            if (ToLower(m.name) == lo) return &m;
        return nullptr;
    };

    // ---- parse ----------------------------------------------------------
    // header lines (!!! / AIVE_SCRIPT / markdown fences) are skipped leniently
    while (!P.atEnd()) {
        size_t save = P.i;
        int ln = P.line;
        if (P.peek() == '!') { while (P.i < P.s.size() && P.s[P.i] != '\n') P.i++; continue; }
        if (P.peek() == '`') { while (P.i < P.s.size() && P.s[P.i] != '\n') P.i++; continue; }
        std::string word = P.readIdent();
        if (word.empty()) { P.err(ln, std::string("unexpected character '") + P.peek() + "'"); break; }
        if (word == "aive_script" || word == "aive_edit") {   // header
            while (P.i < P.s.size() && P.s[P.i] != '\n') P.i++;
            continue;
        }
        if (word == "question") {
            std::string q;
            if (!P.readString(q) || q.empty()) { P.err(ln, "question needs quoted text"); break; }
            sc.questions.push_back(q);
            P.eat(';');
            continue;
        }
        if (word == "def") {
            FxDef d;
            d.line = ln;
            std::string name = P.readIdent();
            if (name.empty()) { P.err(ln, "def needs a name"); break; }
            if (!P.eat('(')) { P.err(ln, "def " + name + " needs (params)"); break; }
            if (P.peek() != ')') {
                for (;;) {
                    std::string pn = P.readIdent();
                    if (pn.empty()) { P.err(P.line, "bad parameter name in def " + name); break; }
                    d.params.push_back(pn);
                    if (!P.eat(',')) break;
                }
            }
            if (!P.eat(')') || !P.eat('{')) { P.err(ln, "bad def " + name + " syntax"); break; }
            while (!P.eat('}')) {
                if (P.atEnd()) { P.err(ln, "unterminated def " + name); break; }
                int aln = P.line;
                std::string prop = P.readIdent();
                if (prop.empty() || !P.eat(':')) { P.err(aln, "expected `prop: expr;` in def " + name); break; }
                std::string raw = P.readRaw();
                P.eat(';');
                d.assigns.push_back({ prop, raw });
            }
            defs[name] = d;
            continue;
        }
        if (word == "settings" || word == "timeline") {
            bool isTl = word == "timeline";
            if (!P.eat('{')) { P.err(ln, word + " needs { }"); break; }
            if (!isTl) settingsLine = ln;
            while (!P.eat('}')) {
                if (P.atEnd()) { P.err(ln, "unterminated " + word + " block"); break; }
                int iln = P.line;
                std::string id = P.readIdent();
                if (id.empty()) { P.err(iln, "bad entry in " + word); break; }
                if (isTl) {
                    timelineNames.push_back({ id, iln });
                    P.eat(';');
                } else {
                    if (!P.eat(':')) { P.err(iln, "settings entries are `name: value;`"); break; }
                    std::string raw;
                    if (!P.readString(raw)) raw = P.readRaw();
                    P.eat(';');
                    settings[id] = raw;
                }
            }
            continue;
        }
        if (word == "clip" || word == "media" || word == "text" || word == "sound") {
            ObjDecl o;
            o.kind = word;
            o.line = ln;
            o.name = P.readIdent();
            if (o.name.empty()) { P.err(ln, word + " needs a name"); break; }
            if (objIndex.count(o.name)) P.err(ln, "duplicate object name '" + o.name + "'");
            if (!P.eat('{')) { P.err(ln, word + " " + o.name + " needs { }"); break; }
            while (!P.eat('}')) {
                if (P.atEnd()) { P.err(ln, "unterminated " + word + " " + o.name); break; }
                int iln = P.line;
                std::string id = P.readIdent();
                if (id.empty()) { P.err(iln, "bad entry in " + o.name); break; }
                if (P.eat(':')) {
                    std::string raw;
                    if (!P.readString(raw)) raw = P.readRaw();
                    P.eat(';');
                    if (FindProp(o, id)) o.assigns.push_back({ id, raw });
                    else o.statics[id] = raw;
                } else if (P.peek() == '(' || P.peek() == ';') {
                    // effect call
                    std::vector<std::string> args;
                    if (P.eat('(')) {
                        if (P.peek() != ')') {
                            int d = 0;
                            std::string cur;
                            while (P.i < P.s.size()) {
                                char c = P.s[P.i];
                                if (c == '(') d++;
                                if (c == ')') { if (d == 0) break; d--; }
                                if (c == ',' && d == 0) { args.push_back(cur); cur.clear(); P.i++; continue; }
                                if (c == '\n') P.line++;
                                cur += c;
                                P.i++;
                            }
                            args.push_back(cur);
                        }
                        if (!P.eat(')')) { P.err(iln, "unbalanced ( in call to " + id); break; }
                    }
                    P.eat(';');
                    auto di = defs.find(id);
                    if (di == defs.end()) {
                        P.err(iln, "unknown effect '" + id + "' (define it with def " + id + "(...) first)");
                        continue;
                    }
                    if (args.size() != di->second.params.size()) {
                        char b[128];
                        std::snprintf(b, sizeof(b), "%s() takes %d argument(s), got %d",
                                      id.c_str(), (int)di->second.params.size(), (int)args.size());
                        P.err(iln, b);
                        continue;
                    }
                    std::map<std::string, std::string> pmap;
                    for (size_t k = 0; k < args.size(); k++)
                        pmap[di->second.params[k]] = "(" + args[k] + ")";
                    for (const auto& [prop, expr] : di->second.assigns) {
                        if (!FindProp(o, prop)) {
                            P.err(iln, "effect " + id + " sets '" + prop + "' which " + o.kind +
                                       " objects don't have");
                            continue;
                        }
                        o.assigns.push_back({ prop, ReplaceIdents(expr, pmap) });
                    }
                } else {
                    P.err(iln, "expected `:` or `(...)` after '" + id + "'");
                    break;
                }
            }
            objIndex[o.name] = (int)objs.size();
            objs.push_back(std::move(o));
            continue;
        }
        P.err(ln, "unknown statement '" + word + "'");
        P.i = save;
        break;
    }

    auto getStatic = [&](const ObjDecl& o, const char* k, std::string* v) {
        auto it = o.statics.find(k);
        if (it == o.statics.end()) return false;
        *v = it->second;
        return true;
    };
    auto getNum = [&](const ObjDecl& o, const char* k, double* v) {
        std::string raw;
        if (!getStatic(o, k, &raw)) return false;
        if (!IsNumber(raw, v)) {
            P.err(o.line, o.name + "." + k + " must be a plain number");
            return false;
        }
        return true;
    };

    // ---- resolve the timeline -------------------------------------------
    if (timelineNames.empty()) {
        sc.errors.push_back("no timeline { } block - the timeline defines the video");
    }
    double cursor = 0;
    std::map<std::string, std::pair<double, double>> clipWindow;   // name -> final start,end
    for (auto& [nm, ln] : timelineNames) {
        auto oi = objIndex.find(nm);
        if (oi == objIndex.end() || objs[oi->second].kind != "clip") {
            P.err(ln, "timeline entry '" + nm + "' is not a declared clip");
            continue;
        }
        ObjDecl& o = objs[oi->second];
        TimelineSeg seg;
        seg.clipName = nm;
        double srcN = 1;
        getNum(o, "src", &srcN);
        seg.srcIndex = (int)srcN - 1;
        if (seg.srcIndex < 0 || seg.srcIndex >= (int)std::max<size_t>(1, sources.size())) {
            P.err(o.line, nm + ": src " + std::to_string((int)srcN) + " does not exist");
            continue;
        }
        if (!getNum(o, "from", &seg.from) || !getNum(o, "to", &seg.to)) {
            P.err(o.line, nm + " needs from: and to: (source seconds)");
            continue;
        }
        if (seg.to <= seg.from) { P.err(o.line, nm + ": to must be after from"); continue; }
        double srcDur = seg.srcIndex < (int)sources.size() ? sources[seg.srcIndex].duration : 0;
        if (srcDur > 0 && (seg.from < -0.011 || seg.to > srcDur + 0.011))
            P.err(o.line, nm + ": [from..to] exceeds source duration (" + FormatTime(srcDur) + ")");
        seg.rate = 1;
        getNum(o, "speed", &seg.rate);
        if (seg.rate < 0.25 || seg.rate > 4.0) { P.err(o.line, nm + ": speed must be in [0.25 .. 4]"); continue; }
        seg.finalStart = cursor;
        double d = (seg.to - seg.from) / seg.rate;
        clipWindow[nm] = { cursor, cursor + d };
        cursor += d;
        sc.timeline.push_back(seg);
    }
    sc.finalDur = sc.baseDur = cursor;
    double finalDur = cursor;

    // ---- compile property assignment chains -----------------------------
    // returns "" for untouched props; static numbers stay static via *staticOut
    auto compileProp = [&](const ObjDecl& o, const PropSpec& ps, double start, double dur,
                           bool* isStatic, double* staticOut) -> std::string {
        std::string cur = "";
        double curNum = ps.dflt;
        bool numeric = true;
        for (const auto& [prop, rawIn] : o.assigns) {
            if (prop != ps.name) continue;
            std::string raw = rawIn;
            double n;
            if (IsNumber(raw, &n)) {
                numeric = true;
                curNum = n;
                cur = "";
                continue;
            }
            std::string why;
            if (!ExpandMacros(raw, &why)) { P.err(o.line, o.name + "." + prop + ": " + why); return ""; }
            char nb[64];
            std::snprintf(nb, sizeof(nb), "%.6g", curNum);
            std::map<std::string, std::string> vmap;
            vmap["v"] = numeric ? std::string("(") + nb + ")" : "(" + cur + ")";
            raw = ReplaceIdents(raw, vmap);
            if (!SanitizeExprFormula(raw, &why)) { P.err(o.line, o.name + "." + prop + ": " + why); return ""; }
            cur = raw;
            numeric = false;
        }
        if (numeric) {
            *isStatic = true;
            *staticOut = curNum;
            return "";
        }
        // local time -> final time
        char sb[64], db[64], fb[64];
        std::snprintf(sb, sizeof(sb), "(t-%.3f)", start);
        std::snprintf(db, sizeof(db), "(%.3f)", dur);
        std::snprintf(fb, sizeof(fb), "((t-%.3f)*%.3f)", start, fps);
        std::map<std::string, std::string> tmap;
        tmap["t"] = sb;
        tmap["dur"] = db;
        tmap["f"] = fb;
        // uppercase T = absolute final seconds
        tmap["T"] = "t";
        *isStatic = false;
        return ReplaceIdents(cur, tmap);
    };

    // ---- emit objects ----------------------------------------------------
    int layerCounter = 0;
    for (ObjDecl& o : objs) {
        if (o.kind == "clip") {
            auto wi = clipWindow.find(o.name);
            if (wi == clipWindow.end()) {
                if (!timelineNames.empty())
                    P.warn(o.line, "clip '" + o.name + "' is not in the timeline - ignored");
                continue;
            }
            double start = wi->second.first, end = wi->second.second;
            for (const auto& ps : kClipProps) {
                bool isStatic = false;
                double sv = 0;
                std::string e = compileProp(o, ps, start, end - start, &isStatic, &sv);
                if (isStatic && std::abs(sv - ps.dflt) < 1e-9) continue;   // untouched
                EditOp op;
                op.type = OpType::ClipFx;
                op.line = o.line;
                op.animProp = ps.name;
                op.a = start;
                op.b = end;
                if (isStatic) {
                    char nb[64];
                    std::snprintf(nb, sizeof(nb), "%.6g", sv);
                    op.text = nb;
                } else {
                    op.text = e;
                }
                if (op.text.empty()) continue;
                sc.ops.push_back(op);
            }
            continue;
        }

        // shared timing for media/text/sound
        double start = 0, end = finalDur;
        std::string duringName;
        bool hasDuring = getStatic(o, "during", &duringName);
        double clipS = 0, clipE = finalDur;
        if (hasDuring) {
            auto wi = clipWindow.find(ToLower(duringName));
            if (wi == clipWindow.end()) {
                P.err(o.line, o.name + ": during '" + duringName + "' is not a timeline clip");
                continue;
            }
            clipS = wi->second.first;
            clipE = wi->second.second;
            start = clipS;
            end = clipE;
        }
        double sVal, eVal;
        if (getNum(o, "start", &sVal)) start = hasDuring ? clipS + sVal : sVal;
        if (getNum(o, "end", &eVal)) end = hasDuring ? clipS + eVal : eVal;
        if (o.kind != "sound") {
            if (end <= start) { P.err(o.line, o.name + ": end must be after start"); continue; }
            if (start < -0.011 || start > finalDur + 0.011)
                P.err(o.line, o.name + ": start is outside the video (" + FormatTime(finalDur) + ")");
            if (end > finalDur + 0.011) end = finalDur;
        }

        if (o.kind == "sound") {
            std::string path;
            if (!getStatic(o, "path", &path)) { P.err(o.line, o.name + " needs path:\"file\""); continue; }
            const MediaFile* m = findMedia(path);
            if (!m) { P.err(o.line, o.name + ": media file not found: \"" + path + "\""); continue; }
            if (m->kind != "audio" && m->kind != "video") { P.err(o.line, "\"" + path + "\" has no audio"); continue; }
            EditOp op;
            op.type = OpType::Sound;
            op.line = o.line;
            op.file = m->name;
            double at = hasDuring ? clipS : 0;
            double atv;
            if (getNum(o, "at", &atv)) at = hasDuring ? clipS + atv : atv;
            op.a = at;
            op.volume = 1;
            getNum(o, "volume", &op.volume);
            if (op.volume <= 0 || op.volume > 4) { P.err(o.line, o.name + ": volume must be in (0..4]"); continue; }
            if (op.a < -0.011 || op.a > finalDur + 0.011)
                P.err(o.line, o.name + ": at is outside the video");
            sc.ops.push_back(op);
            continue;
        }

        EditOp op;
        op.line = o.line;
        op.isObject = true;
        op.idName = o.name;
        op.a = start;
        op.b = end;
        op.layer = layerCounter++;
        double lv;
        if (getNum(o, "layer", &lv)) op.layer = (int)lv;

        if (o.kind == "media") {
            op.type = OpType::Overlay;
            std::string path;
            if (!getStatic(o, "path", &path)) { P.err(o.line, o.name + " needs path:\"file\""); continue; }
            const MediaFile* m = findMedia(path);
            if (!m) { P.err(o.line, o.name + ": media file not found: \"" + path + "\""); continue; }
            if (m->kind != "image" && m->kind != "video") { P.err(o.line, "\"" + path + "\" is not an image/video"); continue; }
            op.file = m->name;
            std::string kc;
            if (getStatic(o, "key", &kc)) {
                kc = ToLower(kc);
                if (kc == "green") op.keyColor = "#00FF00";
                else if (kc == "blue") op.keyColor = "#0000FF";
                else if (kc.size() == 7 && kc[0] == '#') op.keyColor = kc;
                else { P.err(o.line, o.name + ": key must be green, blue or #RRGGBB"); continue; }
                getNum(o, "keysim", &op.keySim);
                getNum(o, "keyblend", &op.keyBlend);
            }
            getNum(o, "corners", &op.corners);
            if (op.corners < 0 || op.corners > 0.5) { P.err(o.line, o.name + ": corners must be in [0..0.5]"); continue; }
            std::string gl;
            if (getStatic(o, "glow", &gl)) {
                if (gl.size() != 7 || gl[0] != '#') { P.err(o.line, o.name + ": glow must be #RRGGBB"); continue; }
                op.glow = gl;
            }
        } else {
            op.type = OpType::Text;
            std::string content;
            if (!getStatic(o, "content", &content) || content.empty()) {
                P.err(o.line, o.name + " needs content:\"...\"");
                continue;
            }
            op.text = content;
            double sz = 48;
            getNum(o, "size", &sz);
            op.size = (int)sz;
            if (op.size < 8 || op.size > 400) { P.err(o.line, o.name + ": size must be in [8..400]"); continue; }
            std::string col;
            if (getStatic(o, "color", &col)) {
                if (col.size() != 7 || col[0] != '#') { P.err(o.line, o.name + ": color must be #RRGGBB"); continue; }
                op.color = col;
            }
            std::string fn;
            if (getStatic(o, "font", &fn)) {
                fn = ToLower(fn);
                if (FontFileFor(fn, false).empty())
                    P.warn(o.line, o.name + ": unknown font '" + fn + "', using default");
                else op.font = fn;
            }
            double bold = 0;
            getNum(o, "bold", &bold);
            op.bold = bold != 0;
            std::string gl;
            if (getStatic(o, "glow", &gl)) {
                if (gl.size() != 7 || gl[0] != '#') { P.err(o.line, o.name + ": glow must be #RRGGBB"); continue; }
                op.glow = gl;
            }
        }

        // animated / static properties -> statics on the op + ExprAnim entries
        const PropSpec* arr = o.kind == "media" ? kMediaProps : kTextProps;
        size_t nProps = o.kind == "media" ? std::size(kMediaProps) : std::size(kTextProps);
        std::vector<EditOp> anims;
        for (size_t k = 0; k < nProps; k++) {
            bool isStatic = false;
            double sv = 0;
            std::string e = compileProp(o, arr[k], start, end - start, &isStatic, &sv);
            std::string pn = arr[k].name;
            if (isStatic) {
                if (pn == "x") op.x = sv;
                else if (pn == "y") op.y = sv;
                else if (pn == "scale") op.scale = sv;
                else if (pn == "rot") op.rot = sv;
                else if (pn == "opacity") op.opacity = sv;
                else if (std::abs(sv - arr[k].dflt) > 1e-9) {
                    // static hue/sat/bright still need an expr channel
                    char nb[64];
                    std::snprintf(nb, sizeof(nb), "%.6g", sv);
                    EditOp a;
                    a.type = OpType::ExprAnim;
                    a.idName = o.name;
                    a.animProp = pn;
                    a.text = nb;
                    a.line = o.line;
                    anims.push_back(a);
                }
                continue;
            }
            if (e.empty()) continue;
            EditOp a;
            a.type = OpType::ExprAnim;
            a.idName = o.name;
            a.animProp = pn;
            a.text = e;
            a.line = o.line;
            anims.push_back(a);
        }
        if (op.scale <= 0 || op.scale > 1.5) { P.err(o.line, o.name + ": scale must be in (0..1.5]"); continue; }
        sc.ops.push_back(op);
        for (auto& a : anims) sc.ops.push_back(a);
    }

    // ---- settings --------------------------------------------------------
    for (auto& [k, v] : settings) {
        double n = 0;
        if (k == "motionblur") {
            std::string m = ToLower(v);
            if (m != "low" && m != "med" && m != "high") {
                P.err(settingsLine, "motionblur must be low, med or high");
                continue;
            }
            EditOp op;
            op.type = OpType::MotionBlur;
            op.mode = m;
            op.line = settingsLine;
            sc.ops.push_back(op);
        } else if (k == "fadein" || k == "fadeout") {
            if (!IsNumber(v, &n) || n <= 0) { P.err(settingsLine, k + " must be a positive number"); continue; }
            EditOp op;
            op.type = k == "fadein" ? OpType::FadeIn : OpType::FadeOut;
            op.b = n;
            op.line = settingsLine;
            sc.ops.push_back(op);
        } else if (k == "musicstart") {
            if (!IsNumber(v, &n)) { P.err(settingsLine, "musicstart must be a number (final seconds)"); continue; }
            if (!music || !music->enabled() || music->beats.empty()) {
                P.err(settingsLine, "musicstart needs background music with tapped beats");
                continue;
            }
            if (n < 0 || n > finalDur) { P.err(settingsLine, "musicstart is outside the video"); continue; }
            EditOp op;
            op.type = OpType::MusicStart;
            op.a = n;
            op.line = settingsLine;
            sc.ops.push_back(op);
        } else {
            P.warn(settingsLine, "unknown setting '" + k + "' ignored");
        }
    }

    if (sc.timeline.empty() && sc.errors.empty())
        sc.errors.push_back("the timeline is empty - declare clips and list them in timeline { }");
    return sc;
}
