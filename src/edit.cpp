#include "edit.h"
#include "util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {

struct Tok {
    std::string word;                       // op name
    std::string quoted;                     // first "..." payload
    bool hasQuoted = false;
    std::map<std::string, std::string> kv;  // key=value pairs
};

// Tokenize one line: WORD ["quoted string"] key=value key=value ...
bool TokenizeLine(const std::string& line, Tok& t, std::string& err) {
    size_t i = 0, n = line.size();
    auto skipWs = [&] { while (i < n && (line[i] == ' ' || line[i] == '\t')) i++; };
    skipWs();
    size_t start = i;
    while (i < n && line[i] != ' ' && line[i] != '\t') i++;
    t.word = ToLower(line.substr(start, i - start));
    for (;;) {
        skipWs();
        if (i >= n) break;
        if (line[i] == '"') {
            size_t close = line.find('"', i + 1);
            if (close == std::string::npos) { err = "unterminated quote"; return false; }
            if (!t.hasQuoted) {
                t.quoted = line.substr(i + 1, close - i - 1);
                t.hasQuoted = true;
            }
            i = close + 1;
            continue;
        }
        start = i;
        while (i < n && line[i] != ' ' && line[i] != '\t') {
            // key="value with spaces" — jump over the quoted part
            if (line[i] == '=' && i + 1 < n && line[i + 1] == '"') {
                size_t close = line.find('"', i + 2);
                if (close == std::string::npos) { err = "unterminated quote"; return false; }
                i = close + 1;
                break;
            }
            i++;
        }
        std::string tok = line.substr(start, i - start);
        size_t eq = tok.find('=');
        if (eq == std::string::npos || eq == 0) { err = "expected key=value, got '" + tok + "'"; return false; }
        std::string key = ToLower(tok.substr(0, eq));
        std::string val = tok.substr(eq + 1);
        // value may itself be quoted: key="something"
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        t.kv[key] = val;
    }
    return true;
}

bool GetNum(const Tok& t, std::initializer_list<const char*> names, double& out) {
    for (auto n : names) {
        auto it = t.kv.find(n);
        if (it != t.kv.end()) { out = std::atof(it->second.c_str()); return true; }
    }
    return false;
}

bool GetStr(const Tok& t, std::initializer_list<const char*> names, std::string& out) {
    for (auto n : names) {
        auto it = t.kv.find(n);
        if (it != t.kv.end()) { out = ToLower(it->second); return true; }
    }
    return false;
}

bool ValidColor(const std::string& c) {
    if (c.size() != 7 || c[0] != '#') return false;
    for (size_t i = 1; i < 7; i++)
        if (!isxdigit((unsigned char)c[i])) return false;
    return true;
}

} // namespace

bool ValidCurveName(const std::string& name) {
    static const char* names[] = { "linear", "ease", "easein", "easeout", "backin",
                                   "backout", "expoin", "expoout", "spike", "sine" };
    for (auto n : names)
        if (name == n) return true;
    return false;
}

std::string CurveExprFor(const std::string& name, const std::string& P) {
    char e4[32];
    std::snprintf(e4, sizeof(e4), "%.3f", 53.598);   // exp(4)-1
    if (name == "linear") return P;
    if (name == "easein") return "pow(" + P + ",3)";
    if (name == "easeout") return "(1-pow(1-" + P + ",3))";
    if (name == "backin") return "(2.70158*pow(" + P + ",3)-1.70158*pow(" + P + ",2))";
    if (name == "backout") return "(1+2.70158*pow(" + P + "-1,3)+1.70158*pow(" + P + "-1,2))";
    if (name == "expoin") return "((exp(4*" + P + ")-1)/" + std::string(e4) + ")";
    if (name == "expoout") return "(1-(exp(4*(1-" + P + "))-1)/" + std::string(e4) + ")";
    if (name == "spike") return "((exp(4*(1-2*abs(" + P + "-0.5)))-1)/" + std::string(e4) + ")";
    if (name == "sine" || name == "smooth") return "((1-cos(PI*" + P + "))/2)";
    // default "ease": cubic in-out
    return "(if(lt(" + P + ",0.5),4*pow(" + P + ",3),1-pow(2-2*" + P + ",3)/2))";
}

bool SanitizeExprFormula(const std::string& f, std::string* whyNot) {
    static const char* funcs[] = { "t", "PI", "sin", "cos", "tan", "abs", "floor", "ceil",
                                   "trunc", "mod", "pow", "sqrt", "exp", "log", "min",
                                   "max", "clip", "if", "lt", "lte", "gt", "gte", "eq",
                                   "between", "sgn", "hypot", "random" };
    if (f.empty()) { if (whyNot) *whyNot = "empty formula"; return false; }
    if (f.size() > 240) { if (whyNot) *whyNot = "formula too long (240 chars max)"; return false; }
    int depth = 0;
    for (size_t i = 0; i < f.size();) {
        char c = f[i];
        if (c == ' ' || c == '\t') { i++; continue; }
        if ((c >= '0' && c <= '9') || c == '.') {
            while (i < f.size() && ((f[i] >= '0' && f[i] <= '9') || f[i] == '.')) i++;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            size_t s = i;
            while (i < f.size() && ((f[i] >= 'a' && f[i] <= 'z') || (f[i] >= 'A' && f[i] <= 'Z') ||
                                    (f[i] >= '0' && f[i] <= '9') || f[i] == '_'))
                i++;
            std::string ident = f.substr(s, i - s);
            bool ok = false;
            for (auto fn : funcs)
                if (ident == fn) ok = true;
            if (!ok) {
                if (whyNot) *whyNot = "unknown name '" + ident + "' (allowed: t, PI, sin, cos, "
                                      "tan, abs, floor, ceil, trunc, mod, pow, sqrt, exp, log, "
                                      "min, max, clip, if, lt, lte, gt, gte, eq, between, sgn, "
                                      "hypot, random)";
                return false;
            }
            continue;
        }
        if (c == '(') { depth++; i++; continue; }
        if (c == ')') { depth--; i++; if (depth < 0) { if (whyNot) *whyNot = "unbalanced ')'"; return false; } continue; }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == ',') { i++; continue; }
        if (whyNot) *whyNot = std::string("character '") + c + "' is not allowed in formulas";
        return false;
    }
    if (depth != 0) { if (whyNot) *whyNot = "unbalanced '('"; return false; }
    return true;
}

EditScript ParseEdit(const std::string& text, double baseDuration,
                     const std::vector<MediaFile>& media,
                     const MusicConfig* music) {
    EditScript s;
    s.baseDur = baseDuration;

    auto findMedia = [&](std::string name) -> const MediaFile* {
        // accept "media/x.png", "media\x.png" or bare "x.png", case-insensitive
        size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        std::string lo = ToLower(name);
        for (const auto& m : media)
            if (ToLower(m.name) == lo) return &m;
        return nullptr;
    };

    auto errAt = [&](int line, const std::string& msg) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "line %d: %s", line, msg.c_str());
        s.errors.push_back(buf);
    };
    auto warnAt = [&](int line, const std::string& msg) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "line %d: %s", line, msg.c_str());
        s.warnings.push_back(buf);
    };

    // shared glow= parsing for overlay + text
    auto parseGlow = [&](const Tok& t, EditOp& op, int line) -> bool {
        auto it = t.kv.find("glow");
        if (it == t.kv.end()) return true;
        if (!ValidColor(it->second)) { errAt(line, "glow must be a #RRGGBB color"); return false; }
        op.glow = it->second;
        return true;
    };

    // shared id= parsing for overlay + text (target handle for animate ops)
    auto parseId = [&](const Tok& t, EditOp& op) {
        std::string id;
        if (GetStr(t, { "id" }, id)) op.idName = id;
    };

    // shared pop=/popdur= parsing for overlay + text
    auto parsePop = [&](const Tok& t, EditOp& op, int line) -> bool {
        std::string pv;
        if (GetStr(t, { "pop" }, pv)) {
            if (pv == "in") op.pop = 1;
            else if (pv == "out") op.pop = 2;
            else if (pv == "both") op.pop = 3;
            else { errAt(line, "pop must be in, out or both"); return false; }
        }
        GetNum(t, { "popdur" }, op.popdur);
        if (op.popdur < 0.1 || op.popdur > 2.0) {
            errAt(line, "popdur must be in [0.1 .. 2.0] seconds");
            return false;
        }
        return true;
    };

    size_t pos = 0;
    int lineNo = 0;
    bool sawHeader = false;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? text.size() + 1 : eol + 1;
        lineNo++;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        // strip whitespace-only, comments, accidental markdown fences
        size_t ws = line.find_first_not_of(" \t");
        if (ws == std::string::npos) continue;
        std::string trimmed = line.substr(ws);
        if (trimmed[0] == '#') continue;
        if (trimmed.rfind("```", 0) == 0) continue;
        if (trimmed.rfind("!!!", 0) == 0) continue;   // clipboard sentinel line
        if (!sawHeader) {
            if (ToLower(trimmed).rfind("aive_edit", 0) == 0) { sawHeader = true; continue; }
            // tolerate a missing header: fall through and try to parse as an op
            sawHeader = true;
        }

        Tok t;
        std::string terr;
        if (!TokenizeLine(trimmed, t, terr)) { errAt(lineNo, terr); continue; }
        EditOp op;
        op.line = lineNo;

        if (t.word == "cut") {
            op.type = OpType::Cut;
            if (!GetNum(t, { "from", "start" }, op.a) || !GetNum(t, { "to", "end" }, op.b)) {
                errAt(lineNo, "cut needs from= and to="); continue;
            }
        } else if (t.word == "speed") {
            op.type = OpType::Speed;
            if (!GetNum(t, { "from", "start" }, op.a) || !GetNum(t, { "to", "end" }, op.b)) {
                errAt(lineNo, "speed needs from= and to="); continue;
            }
            if (!GetNum(t, { "rate" }, op.rate)) { errAt(lineNo, "speed needs rate="); continue; }
            if (op.rate < 0.25 || op.rate > 4.0) { errAt(lineNo, "rate must be in [0.25 .. 4.0]"); continue; }
        } else if (t.word == "overlay" || t.word == "image") {
            op.type = OpType::Overlay;
            if (!t.hasQuoted) { errAt(lineNo, "overlay needs a quoted file name"); continue; }
            const MediaFile* m = findMedia(t.quoted);
            if (!m) { errAt(lineNo, "media file not found: \"" + t.quoted + "\""); continue; }
            if (m->kind != "image" && m->kind != "video") {
                errAt(lineNo, "\"" + t.quoted + "\" cannot be overlaid (images and videos only)");
                continue;
            }
            op.file = m->name;
            if (!GetNum(t, { "start", "from" }, op.a) || !GetNum(t, { "end", "to" }, op.b)) {
                errAt(lineNo, "overlay needs start= and end="); continue;
            }
            auto keyIt = t.kv.find("key");
            if (keyIt != t.kv.end()) {
                std::string kc = ToLower(keyIt->second);
                if (kc == "green") op.keyColor = "#00FF00";
                else if (kc == "blue") op.keyColor = "#0000FF";
                else if (ValidColor(keyIt->second)) op.keyColor = keyIt->second;
                else { errAt(lineNo, "key must be green, blue or a #RRGGBB color"); continue; }
                GetNum(t, { "keysim" }, op.keySim);
                GetNum(t, { "keyblend" }, op.keyBlend);
                if (op.keySim < 0.01 || op.keySim > 1) { errAt(lineNo, "keysim must be in [0.01 .. 1]"); continue; }
                if (op.keyBlend < 0 || op.keyBlend > 1) { errAt(lineNo, "keyblend must be in [0 .. 1]"); continue; }
            }
            GetNum(t, { "x" }, op.x);
            GetNum(t, { "y" }, op.y);
            GetNum(t, { "scale" }, op.scale);
            GetNum(t, { "opacity" }, op.opacity);
            if (op.scale <= 0 || op.scale > 1.5) { errAt(lineNo, "scale must be in (0 .. 1.5]"); continue; }
            if (op.opacity <= 0 || op.opacity > 1) { errAt(lineNo, "opacity must be in (0 .. 1]"); continue; }
            GetNum(t, { "corners", "corner" }, op.corners);
            if (op.corners < 0 || op.corners > 0.5) { errAt(lineNo, "corners must be in [0 .. 0.5]"); continue; }
            if (!parseGlow(t, op, lineNo)) continue;
            if (!parsePop(t, op, lineNo)) continue;
            parseId(t, op);
            op.layer = -100000;
            double lv;
            if (GetNum(t, { "layer" }, lv)) op.layer = (int)lv;
        } else if (t.word == "text") {
            op.type = OpType::Text;
            if (!t.hasQuoted || t.quoted.empty()) { errAt(lineNo, "text needs quoted content"); continue; }
            op.text = t.quoted;
            if (!GetNum(t, { "start", "from" }, op.a) || !GetNum(t, { "end", "to" }, op.b)) {
                errAt(lineNo, "text needs start= and end="); continue;
            }
            GetNum(t, { "x" }, op.x);
            GetNum(t, { "y" }, op.y);
            double sz = 48;
            if (GetNum(t, { "size" }, sz)) op.size = (int)sz;
            if (op.size < 8 || op.size > 400) { errAt(lineNo, "size must be in [8 .. 400] px"); continue; }
            auto it = t.kv.find("color");
            if (it != t.kv.end()) {
                if (!ValidColor(it->second)) { errAt(lineNo, "color must be #RRGGBB"); continue; }
                op.color = it->second;
            }
            std::string fontName;
            if (GetStr(t, { "font" }, fontName)) {
                double bold = 0;
                GetNum(t, { "bold" }, bold);
                op.bold = bold != 0;
                if (FontFileFor(fontName, false).empty()) {
                    warnAt(lineNo, "unknown font '" + fontName + "', using the default font");
                } else {
                    op.font = fontName;
                }
            } else {
                double bold = 0;
                if (GetNum(t, { "bold" }, bold)) op.bold = bold != 0;
            }
            if (!parseGlow(t, op, lineNo)) continue;
            if (!parsePop(t, op, lineNo)) continue;
            parseId(t, op);
            op.layer = -100000;
            double lv;
            if (GetNum(t, { "layer" }, lv)) op.layer = (int)lv;
        } else if (t.word == "sound" || t.word == "sfx" || t.word == "audio" || t.word == "music") {
            op.type = OpType::Sound;
            if (!t.hasQuoted) { errAt(lineNo, "sound needs a quoted file name"); continue; }
            const MediaFile* m = findMedia(t.quoted);
            if (!m) { errAt(lineNo, "media file not found: \"" + t.quoted + "\""); continue; }
            if (m->kind != "audio" && m->kind != "video") {
                errAt(lineNo, "\"" + t.quoted + "\" has no audio to play"); continue;
            }
            op.file = m->name;
            if (!GetNum(t, { "at", "start", "from" }, op.a)) { errAt(lineNo, "sound needs at="); continue; }
            GetNum(t, { "volume" }, op.volume);
            if (op.volume <= 0 || op.volume > 4) { errAt(lineNo, "volume must be in (0 .. 4]"); continue; }
        } else if (t.word == "mute") {
            op.type = OpType::Mute;
            if (!GetNum(t, { "from", "start" }, op.a) || !GetNum(t, { "to", "end" }, op.b)) {
                errAt(lineNo, "mute needs from= and to="); continue;
            }
        } else if (t.word == "fadein") {
            op.type = OpType::FadeIn;
            if (!GetNum(t, { "duration", "d" }, op.b) || op.b <= 0) { errAt(lineNo, "fadein needs duration="); continue; }
        } else if (t.word == "fadeout") {
            op.type = OpType::FadeOut;
            if (!GetNum(t, { "duration", "d" }, op.b) || op.b <= 0) { errAt(lineNo, "fadeout needs duration="); continue; }
        } else if (t.word == "transition") {
            op.type = OpType::Transition;
            if (!GetNum(t, { "at" }, op.a)) { errAt(lineNo, "transition needs at="); continue; }
            if (!GetNum(t, { "duration", "d" }, op.b)) { errAt(lineNo, "transition needs duration="); continue; }
            if (op.b < 0.1 || op.b > 5.0) { errAt(lineNo, "transition duration must be in [0.1 .. 5.0]"); continue; }
            op.mode = "black";
            std::string col;
            if (GetStr(t, { "color" }, col)) {
                if (col != "black" && col != "white") { errAt(lineNo, "transition color must be black or white"); continue; }
                op.mode = col;
            }
        } else if (t.word == "zoom") {
            op.type = OpType::Zoom;
            if (!GetNum(t, { "start", "from" }, op.a) || !GetNum(t, { "end", "to" }, op.b)) {
                errAt(lineNo, "zoom needs start= and end="); continue;
            }
            GetNum(t, { "amount" }, op.amount);
            if (op.amount < 1.05 || op.amount > 4.0) { errAt(lineNo, "zoom amount must be in [1.05 .. 4.0]"); continue; }
            op.mode = "in";
            std::string m;
            if (GetStr(t, { "mode" }, m)) {
                if (m != "in" && m != "out" && m != "pulse" && m != "spike") {
                    errAt(lineNo, "zoom mode must be in, out, pulse or spike");
                    continue;
                }
                op.mode = m;
            }
        } else if (t.word == "animate") {
            op.type = OpType::Animate;
            if (!GetStr(t, { "id" }, op.idName) || op.idName.empty()) {
                errAt(lineNo, "animate needs id= (matching an overlay/text id=)"); continue;
            }
            if (!GetStr(t, { "prop" }, op.animProp) ||
                (op.animProp != "x" && op.animProp != "y" && op.animProp != "rot")) {
                errAt(lineNo, "animate prop must be x, y or rot"); continue;
            }
            if (!GetNum(t, { "from", "start" }, op.a) || !GetNum(t, { "to", "end" }, op.b)) {
                errAt(lineNo, "animate needs from= and to= (time window)"); continue;
            }
            if (!GetNum(t, { "v0" }, op.v0) || !GetNum(t, { "v1" }, op.v1)) {
                errAt(lineNo, "animate needs v0= and v1= (value range)"); continue;
            }
            if (op.animProp == "rot") {
                if (op.v0 < -3600 || op.v0 > 3600 || op.v1 < -3600 || op.v1 > 3600) {
                    errAt(lineNo, "rot values must be in [-3600 .. 3600] degrees"); continue;
                }
            } else if (op.v0 < -0.5 || op.v0 > 1.5 || op.v1 < -0.5 || op.v1 > 1.5) {
                errAt(lineNo, "x/y values must be in [-0.5 .. 1.5]"); continue;
            }
            op.curveName = "ease";
            std::string cv;
            if (GetStr(t, { "curve" }, cv)) {
                if (!ValidCurveName(cv)) { errAt(lineNo, "unknown curve '" + cv + "'"); continue; }
                op.curveName = cv;
            }
        } else if (t.word == "spin") {
            op.type = OpType::Spin;
            if (!GetNum(t, { "from", "start" }, op.a) || !GetNum(t, { "to", "end" }, op.b)) {
                errAt(lineNo, "spin needs from= and to="); continue;
            }
            if (!GetNum(t, { "degrees", "deg" }, op.v0)) { errAt(lineNo, "spin needs degrees="); continue; }
            if (op.v0 < -3600 || op.v0 > 3600) { errAt(lineNo, "degrees must be in [-3600 .. 3600]"); continue; }
            op.curveName = "ease";
            std::string cv;
            if (GetStr(t, { "curve" }, cv)) {
                if (!ValidCurveName(cv)) { errAt(lineNo, "unknown curve '" + cv + "'"); continue; }
                op.curveName = cv;
            }
        } else if (t.word == "motionblur") {
            op.type = OpType::MotionBlur;
            op.mode = "med";
            std::string st;
            if (GetStr(t, { "strength", "amount" }, st)) {
                if (st != "low" && st != "med" && st != "high") {
                    errAt(lineNo, "motionblur strength must be low, med or high");
                    continue;
                }
                op.mode = st;
            }
        } else if (t.word == "musicstart") {
            op.type = OpType::MusicStart;
            if (!GetNum(t, { "at" }, op.a)) { errAt(lineNo, "musicstart needs at="); continue; }
            if (!music || !music->enabled() || music->beats.empty()) {
                errAt(lineNo, "musicstart only works when background music with tapped beats is set");
                continue;
            }
        } else if (t.word == "object") {
            // primitive: a layered media/text element with animatable properties
            if (!GetStr(t, { "id" }, op.idName) || op.idName.empty()) {
                errAt(lineNo, "object needs id="); continue;
            }
            bool badId = op.idName == "base";
            for (char c : op.idName)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) badId = true;
            if (badId) { errAt(lineNo, "object id must be lowercase letters/digits/_ (not 'base')"); continue; }
            op.isObject = true;
            op.layer = -100000;
            double lv;
            if (GetNum(t, { "layer" }, lv)) op.layer = (int)lv;
            if (!GetNum(t, { "in", "start" }, op.a) || !GetNum(t, { "out", "end" }, op.b)) {
                errAt(lineNo, "object needs in= and out="); continue;
            }
            GetNum(t, { "x" }, op.x);
            GetNum(t, { "y" }, op.y);
            GetNum(t, { "opacity" }, op.opacity);
            if (op.opacity <= 0 || op.opacity > 1) { errAt(lineNo, "opacity must be in (0 .. 1]"); continue; }
            auto srcIt = t.kv.find("src");
            auto txtIt = t.kv.find("text");
            if (srcIt != t.kv.end()) {
                op.type = OpType::Overlay;
                const MediaFile* m = findMedia(srcIt->second);
                if (!m) { errAt(lineNo, "media file not found: \"" + srcIt->second + "\""); continue; }
                if (m->kind != "image" && m->kind != "video") {
                    errAt(lineNo, "\"" + srcIt->second + "\" cannot be an object (images/videos only)");
                    continue;
                }
                op.file = m->name;
                GetNum(t, { "scale" }, op.scale);
                if (op.scale <= 0 || op.scale > 1.5) { errAt(lineNo, "scale must be in (0 .. 1.5]"); continue; }
                GetNum(t, { "rot" }, op.rot);
                GetNum(t, { "corners", "corner" }, op.corners);
                if (op.corners < 0 || op.corners > 0.5) { errAt(lineNo, "corners must be in [0 .. 0.5]"); continue; }
                auto keyIt = t.kv.find("key");
                if (keyIt != t.kv.end()) {
                    std::string kc = ToLower(keyIt->second);
                    if (kc == "green") op.keyColor = "#00FF00";
                    else if (kc == "blue") op.keyColor = "#0000FF";
                    else if (ValidColor(keyIt->second)) op.keyColor = keyIt->second;
                    else { errAt(lineNo, "key must be green, blue or a #RRGGBB color"); continue; }
                    GetNum(t, { "keysim" }, op.keySim);
                    GetNum(t, { "keyblend" }, op.keyBlend);
                }
                if (!parseGlow(t, op, lineNo)) continue;
            } else if (txtIt != t.kv.end()) {
                op.type = OpType::Text;
                if (txtIt->second.empty()) { errAt(lineNo, "object text= must not be empty"); continue; }
                op.text = txtIt->second;
                double sz = 48;
                if (GetNum(t, { "size" }, sz)) op.size = (int)sz;
                if (op.size < 8 || op.size > 400) { errAt(lineNo, "size must be in [8 .. 400] px"); continue; }
                auto colIt = t.kv.find("color");
                if (colIt != t.kv.end()) {
                    if (!ValidColor(colIt->second)) { errAt(lineNo, "color must be #RRGGBB"); continue; }
                    op.color = colIt->second;
                }
                std::string fontName;
                if (GetStr(t, { "font" }, fontName)) {
                    if (FontFileFor(fontName, false).empty())
                        warnAt(lineNo, "unknown font '" + fontName + "', using the default font");
                    else op.font = fontName;
                }
                double bold = 0;
                if (GetNum(t, { "bold" }, bold)) op.bold = bold != 0;
                if (!parseGlow(t, op, lineNo)) continue;
            } else {
                errAt(lineNo, "object needs src=\"file\" or text=\"content\"");
                continue;
            }
        } else if (t.word == "keyframe" || t.word == "kf") {
            op.type = OpType::Keyframe;
            if (!GetStr(t, { "id" }, op.idName) || op.idName.empty()) {
                errAt(lineNo, "keyframe needs id="); continue;
            }
            if (!GetStr(t, { "prop" }, op.animProp)) { errAt(lineNo, "keyframe needs prop="); continue; }
            if (!GetNum(t, { "t" }, op.a)) { errAt(lineNo, "keyframe needs t="); continue; }
            if (!GetNum(t, { "v" }, op.v0)) { errAt(lineNo, "keyframe needs v="); continue; }
            op.curveName = "ease";
            std::string cv;
            if (GetStr(t, { "curve" }, cv)) {
                if (!ValidCurveName(cv)) { errAt(lineNo, "unknown curve '" + cv + "'"); continue; }
                op.curveName = cv;
            }
        } else if (t.word == "expr") {
            op.type = OpType::ExprAnim;
            if (!GetStr(t, { "id" }, op.idName) || op.idName.empty()) {
                errAt(lineNo, "expr needs id="); continue;
            }
            if (!GetStr(t, { "prop" }, op.animProp)) { errAt(lineNo, "expr needs prop="); continue; }
            if (!t.hasQuoted || t.quoted.empty()) {
                errAt(lineNo, "expr needs a quoted formula, e.g. \"0.5+0.05*sin(2*PI*3*t)\"");
                continue;
            }
            std::string why;
            if (!SanitizeExprFormula(t.quoted, &why)) { errAt(lineNo, "bad formula: " + why); continue; }
            op.text = t.quoted;
        } else if (t.word == "question") {
            if (!t.hasQuoted || t.quoted.empty()) { errAt(lineNo, "question needs quoted text"); continue; }
            s.questions.push_back(t.quoted);
            continue;   // questions are not timeline operations
        } else if (t.word == "flicker") {
            op.type = OpType::Flicker;
            if (!GetNum(t, { "start", "from" }, op.a) || !GetNum(t, { "end", "to" }, op.b)) {
                errAt(lineNo, "flicker needs start= and end="); continue;
            }
            GetNum(t, { "frequency", "freq" }, op.freq);
            GetNum(t, { "amplitude", "amp" }, op.amp);
            if (op.freq < 0.5 || op.freq > 30) { errAt(lineNo, "flicker frequency must be in [0.5 .. 30] Hz"); continue; }
            if (op.amp < 0.02 || op.amp > 1.0) { errAt(lineNo, "flicker amplitude must be in [0.02 .. 1.0]"); continue; }
        } else {
            errAt(lineNo, "unknown operation '" + t.word + "'");
            continue;
        }
        s.ops.push_back(op);
    }

    // ---- semantic validation --------------------------------------------
    const double eps = 0.011;
    std::vector<const EditOp*> timeOps;   // cut+speed on the BASE timeline
    for (const auto& op : s.ops)
        if (op.type == OpType::Cut || op.type == OpType::Speed) timeOps.push_back(&op);
    std::sort(timeOps.begin(), timeOps.end(),
              [](const EditOp* a, const EditOp* b) { return a->a < b->a; });
    // Speed is length-preserving: the span [a,b] keeps its duration. r<1 plays
    // the first (b-a)*r of footage slowed to fill it (rest of the span is
    // dropped); r>1 consumes (b-a)*r of footage, reaching past b. Only cuts
    // (and clamped/fast spans) change the FINAL duration.
    double prevEnd = -1;
    double finalDur = s.baseDur;
    for (const auto* op : timeOps) {
        if (op->b <= op->a) errAt(op->line, "range end must be after start");
        else if (op->a < -eps || op->b > s.baseDur + eps)
            errAt(op->line, "range exceeds BASE duration (" + FormatTime(s.baseDur) + ")");
        else if (op->a < prevEnd - eps)
            errAt(op->line, "cut/speed ranges overlap (a fast span also consumes the "
                            "footage after its `to`)");
        double len = op->b - op->a;
        double effEnd = op->b;
        if (op->type == OpType::Cut) {
            finalDur -= len;
        } else {
            double consumed = std::min(op->a + len * op->rate, s.baseDur);
            if (op->a + len * op->rate > s.baseDur + eps)
                s.warnings.push_back("line " + std::to_string(op->line) +
                                     ": fast span consumes footage past the end of the "
                                     "video; it will run short");
            double outLen = (consumed - op->a) / op->rate;
            effEnd = std::max(op->b, consumed);
            finalDur -= (effEnd - op->a) - outLen;
        }
        prevEnd = std::max(prevEnd, effEnd);
    }
    if (finalDur < 0.1 && s.errors.empty()) {
        s.errors.push_back("edits remove (almost) the entire video");
        finalDur = 0;
    }
    s.finalDur = finalDur;

    for (const auto& op : s.ops) {
        switch (op.type) {
        case OpType::Overlay:
        case OpType::Text:
        case OpType::Mute:
        case OpType::Zoom:
        case OpType::Flicker:
            if (op.b <= op.a) errAt(op.line, "end must be after start");
            else if (op.a < -eps || op.a > finalDur + eps)
                errAt(op.line, "start exceeds FINAL duration (" + FormatTime(finalDur) + ")");
            else if (op.b > finalDur + eps)
                s.warnings.push_back("line " + std::to_string(op.line) +
                                     ": end exceeds FINAL duration; will be clipped");
            break;
        case OpType::Sound:
            if (op.a < -eps || op.a > finalDur + eps)
                errAt(op.line, "at= exceeds FINAL duration (" + FormatTime(finalDur) + ")");
            break;
        case OpType::Transition:
            if (op.a < 0.1 || op.a > finalDur - 0.1)
                errAt(op.line, "transition at= must be inside the FINAL timeline (" +
                               FormatTime(finalDur) + ")");
            break;
        case OpType::Animate:
        case OpType::Spin:
            if (op.b <= op.a) errAt(op.line, "to= must be after from=");
            else if (op.a < -eps || op.a > finalDur + eps)
                errAt(op.line, "window exceeds FINAL duration (" + FormatTime(finalDur) + ")");
            break;
        case OpType::MusicStart:
            if (op.a < -eps || op.a > finalDur - 0.1)
                errAt(op.line, "musicstart at= must be inside the FINAL timeline");
            break;
        case OpType::FadeIn:
        case OpType::FadeOut:
            if (op.b > finalDur)
                s.warnings.push_back("line " + std::to_string(op.line) +
                                     ": fade longer than the video; will be shortened");
            break;
        default:
            break;
        }
    }
    // default layers follow declaration order (explicit layer= overrides)
    {
        int decl = 0;
        for (auto& op : s.ops)
            if (op.type == OpType::Overlay || op.type == OpType::Text) {
                if (op.layer == -100000) op.layer = decl;
                decl++;
            }
    }

    // keyframes / exprs: target must exist, prop must fit the target type,
    // values in range, and a prop can't have both keyframes and an expr
    {
        auto propRank = [](const std::string& p) -> int {
            // 0 = invalid, 1 = text-safe (x/y/opacity), 2 = media-only, 3 = color
            if (p == "x" || p == "y" || p == "opacity") return 1;
            if (p == "scale" || p == "rot") return 2;
            if (p == "hue" || p == "sat" || p == "bright") return 3;
            return 0;
        };
        std::map<std::string, int> kindOf;   // id -> 1 text, 2 media
        for (const auto& op : s.ops)
            if (!op.idName.empty() && (op.type == OpType::Overlay || op.type == OpType::Text))
                kindOf[op.idName] = op.type == OpType::Text ? 1 : 2;
        std::map<std::string, int> hasKf, hasExpr;   // id+"/"+prop
        for (const auto& op : s.ops) {
            if (op.type != OpType::Keyframe && op.type != OpType::ExprAnim) continue;
            int pr = propRank(op.animProp);
            if (pr == 0) {
                errAt(op.line, "unknown prop '" + op.animProp +
                               "' (x, y, scale, rot, opacity, hue, sat, bright)");
                continue;
            }
            if (op.idName == "base") {
                if (pr != 3)
                    errAt(op.line, "the base video only supports hue/sat/bright");
            } else if (!kindOf.count(op.idName)) {
                errAt(op.line, "id \"" + op.idName + "\" matches no object/overlay/text with id=");
                continue;
            } else if (kindOf[op.idName] == 1 && pr != 1) {
                errAt(op.line, "text objects only support x, y and opacity");
            }
            if (op.type == OpType::Keyframe) {
                double lo = -1, hi = 2;
                if (op.animProp == "scale") { lo = 0.05; hi = 3; }
                else if (op.animProp == "rot") { lo = -7200; hi = 7200; }
                else if (op.animProp == "opacity") { lo = 0; hi = 1; }
                else if (op.animProp == "hue") { lo = -360; hi = 360; }
                else if (op.animProp == "sat") { lo = 0; hi = 3; }
                else if (op.animProp == "bright") { lo = -1; hi = 1; }
                if (op.v0 < lo || op.v0 > hi) {
                    char rb[128];
                    std::snprintf(rb, sizeof(rb), "%s value must be in [%g .. %g]",
                                  op.animProp.c_str(), lo, hi);
                    errAt(op.line, rb);
                }
                if (op.a < -eps || op.a > finalDur + eps)
                    errAt(op.line, "keyframe t exceeds FINAL duration (" + FormatTime(finalDur) + ")");
                hasKf[op.idName + "/" + op.animProp]++;
            } else {
                hasExpr[op.idName + "/" + op.animProp]++;
            }
        }
        for (const auto& [k, n] : hasExpr) {
            if (n > 1) s.errors.push_back("more than one expr for " + k);
            if (hasKf.count(k))
                s.errors.push_back(k + " has both keyframes and an expr - pick one");
        }
    }

    // ---- cross-op validation --------------------------------------------
    // animate must target an existing id; one animate per (id, prop);
    // rot only works on image/video overlays; at most one musicstart
    int nMusicStart = 0, nMotionBlur = 0;
    std::map<std::string, std::string> seenAnim;   // id+prop -> ""
    for (const auto& op : s.ops) {
        if (op.type == OpType::MusicStart && ++nMusicStart > 1)
            errAt(op.line, "only one musicstart allowed");
        if (op.type == OpType::MotionBlur && ++nMotionBlur > 1)
            errAt(op.line, "only one motionblur allowed");
        if (op.type != OpType::Animate) continue;
        const EditOp* target = nullptr;
        for (const auto& q : s.ops)
            if ((q.type == OpType::Overlay || q.type == OpType::Text) && q.idName == op.idName)
                target = &q;
        if (!target) {
            errAt(op.line, "animate id \"" + op.idName + "\" matches no overlay/text with id=");
            continue;
        }
        if (op.animProp == "rot" && target->type == OpType::Text)
            errAt(op.line, "prop=rot only works on image/video overlays, not text");
        std::string key = op.idName + "/" + op.animProp;
        if (seenAnim.count(key))
            errAt(op.line, "duplicate animate for id \"" + op.idName + "\" prop " + op.animProp);
        seenAnim[key] = "";
    }

    if (s.ops.empty() && s.errors.empty())
        s.errors.push_back("no operations found — paste the .edit file content");
    return s;
}
