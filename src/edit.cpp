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
        while (i < n && line[i] != ' ' && line[i] != '\t') i++;
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

EditScript ParseEdit(const std::string& text, double baseDuration,
                     const std::vector<MediaFile>& media) {
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
            if (m->kind != "image") { errAt(lineNo, "\"" + t.quoted + "\" is not an image (only images can be overlaid)"); continue; }
            op.file = m->name;
            if (!GetNum(t, { "start", "from" }, op.a) || !GetNum(t, { "end", "to" }, op.b)) {
                errAt(lineNo, "overlay needs start= and end="); continue;
            }
            GetNum(t, { "x" }, op.x);
            GetNum(t, { "y" }, op.y);
            GetNum(t, { "scale" }, op.scale);
            GetNum(t, { "opacity" }, op.opacity);
            if (op.scale <= 0 || op.scale > 1.5) { errAt(lineNo, "scale must be in (0 .. 1.5]"); continue; }
            if (op.opacity <= 0 || op.opacity > 1) { errAt(lineNo, "opacity must be in (0 .. 1]"); continue; }
            if (!parsePop(t, op, lineNo)) continue;
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
            if (!parsePop(t, op, lineNo)) continue;
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
                if (m != "in" && m != "out" && m != "pulse") { errAt(lineNo, "zoom mode must be in, out or pulse"); continue; }
                op.mode = m;
            }
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
    double prevEnd = -1;
    double finalDur = s.baseDur;
    for (const auto* op : timeOps) {
        if (op->b <= op->a) errAt(op->line, "range end must be after start");
        else if (op->a < -eps || op->b > s.baseDur + eps)
            errAt(op->line, "range exceeds BASE duration (" + FormatTime(s.baseDur) + ")");
        else if (op->a < prevEnd - eps)
            errAt(op->line, "cut/speed ranges overlap");
        prevEnd = std::max(prevEnd, op->b);
        double len = op->b - op->a;
        if (op->type == OpType::Cut) finalDur -= len;
        else finalDur += len / op->rate - len;
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
    if (s.ops.empty() && s.errors.empty())
        s.errors.push_back("no operations found — paste the .edit file content");
    return s;
}
