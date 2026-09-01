#include "render.h"
#include "util.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string F3(double v) { char b[64]; std::snprintf(b, sizeof(b), "%.3f", v); return b; }
std::wstring WF3(double v) { return Utf8ToWide(F3(v)); }

// Decompose rate into atempo factors, each within [0.5 .. 2.0].
std::string AtempoChain(double rate) {
    std::string s;
    auto add = [&](double f) { s += (s.empty() ? "" : ",") + std::string("atempo=") + F3(f); };
    while (rate > 2.0 + 1e-9) { add(2.0); rate /= 2.0; }
    while (rate < 0.5 - 1e-9) { add(0.5); rate /= 0.5; }
    add(rate);
    return s;
}

// For values inside drawtext's '...' quotes. The graph parser strips the
// quotes, then the filter's own option parser splits on ':' — so colons must
// be backslash-escaped to survive the second pass.
std::string EscapeDrawtext(std::string s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\'') { out += "\xE2\x80\x99"; continue; }   // ' -> typographic
        if (c == '\\') { out += '/'; continue; }
        if (c == ':') { out += "\\:"; continue; }
        out += c;
    }
    return out;
}

std::string FindFontFile() {
    const char* candidates[] = {
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeuib.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibrib.ttf",
    };
    std::error_code ec;
    for (auto c : candidates)
        if (fs::exists(fs::path(c), ec)) return c;
    return "";
}

void SetStage(RenderState& st, const std::string& s) {
    std::lock_guard<std::mutex> lk(st.m);
    st.stage = s;
}
void AppendLog(RenderState& st, const std::string& s) {
    std::lock_guard<std::mutex> lk(st.m);
    st.log += s;
    if (st.log.size() > 200000) st.log.erase(0, st.log.size() - 150000);
}

// Run one ffmpeg invocation, mapping its -progress output to [p0..p1].
bool RunStage(RenderState& st, std::vector<std::wstring> args, const std::string& label,
              double p0, double p1, double expectDur, std::string* errOut) {
    if (st.cancelRequested) { *errOut = "canceled"; return false; }
    SetStage(st, label);
    st.progress = (float)p0;
    st.frame = 0;
    st.outSec = 0;
    {
        std::string cmd;
        for (auto& a : args) { cmd += WideToUtf8(a); cmd += ' '; }
        AppendLog(st, "\n> " + cmd + "\n");
    }
    std::string errText;
    int code = RunProcessStream(
        args,
        [&](const std::string& line) {
            if (line.rfind("frame=", 0) == 0) {
                st.frame = std::atoll(line.c_str() + 6);
                return;
            }
            // out_time_us / out_time_ms are both microseconds
            double us = -1;
            if (line.rfind("out_time_us=", 0) == 0) us = std::atof(line.c_str() + 12);
            else if (line.rfind("out_time_ms=", 0) == 0) us = std::atof(line.c_str() + 12);
            if (us >= 0) {
                st.outSec = us / 1e6;
                if (expectDur > 0.01) {
                    double frac = std::clamp(us / 1e6 / expectDur, 0.0, 1.0);
                    st.progress = (float)(p0 + frac * (p1 - p0));
                }
            }
        },
        &errText,
        [&](void* h) {
            std::lock_guard<std::mutex> lk(st.m);
            st.hProc = h;
        });
    {
        std::lock_guard<std::mutex> lk(st.m);
        if (st.hProc) { CloseHandle((HANDLE)st.hProc); st.hProc = nullptr; }
    }
    if (!errText.empty()) AppendLog(st, errText);
    if (st.cancelRequested) { *errOut = "canceled"; return false; }
    if (code != 0) {
        std::string tail = errText.size() > 1500 ? errText.substr(errText.size() - 1500) : errText;
        *errOut = "ffmpeg failed (" + label + "):\n" + tail;
        return false;
    }
    st.progress = (float)p1;
    return true;
}

std::vector<std::wstring> BaseArgs() {
    return { FfmpegExe(), L"-y", L"-nostdin", L"-v", L"error", L"-nostats", L"-progress", L"pipe:1" };
}

struct Seg { double a, b, rate; };

// Easing helpers -> ffmpeg expression strings. p must be an expression that
// evaluates to progress in [0..1].
std::string EaseOutBack(const std::string& p) {
    return "(1+2.70158*pow(" + p + "-1,3)+1.70158*pow(" + p + "-1,2))";
}
std::string EaseInBack(const std::string& p) {
    return "(2.70158*pow(" + p + ",3)-1.70158*pow(" + p + ",2))";
}

void HexColor(const std::string& hex, int& r, int& g, int& b) {
    unsigned v = (unsigned)std::strtoul(hex.c_str() + 1, nullptr, 16);
    r = (v >> 16) & 255;
    g = (v >> 8) & 255;
    b = v & 255;
}

// Named easing curve -> ffmpeg expression in terms of progress P (in [0..1]).
std::string CurveExpr(const std::string& name, const std::string& P) {
    const std::string e4 = F3(std::exp(4.0) - 1.0);
    if (name == "linear") return P;
    if (name == "easein") return "pow(" + P + ",3)";
    if (name == "easeout") return "(1-pow(1-" + P + ",3))";
    if (name == "backin") return EaseInBack(P);
    if (name == "backout") return EaseOutBack(P);
    if (name == "expoin") return "((exp(4*" + P + ")-1)/" + e4 + ")";
    if (name == "expoout") return "(1-(exp(4*(1-" + P + "))-1)/" + e4 + ")";
    if (name == "spike") return "((exp(4*(1-2*abs(" + P + "-0.5)))-1)/" + e4 + ")";
    if (name == "sine") return "((1-cos(PI*" + P + "))/2)";
    // default "ease": cubic in-out
    return "(if(lt(" + P + ",0.5),4*pow(" + P + ",3),1-pow(2-2*" + P + ",3)/2))";
}

std::string AnimP(const EditOp* op, const char* tv = "t") {
    return std::string("clip((") + tv + "-" + F3(op->a) + ")/" +
           F3(std::max(0.01, op->b - op->a)) + ",0,1)";
}
// v0 -> v1 along the op's curve (holds v0 before, v1 after the window)
std::string AnimVal(const EditOp* op, const char* tv = "t") {
    return "(" + F3(op->v0) + "+" + F3(op->v1 - op->v0) + "*" +
           CurveExpr(op->curveName, AnimP(op, tv)) + ")";
}

// Piecewise keyframe interpolation -> expression (kfs sorted by time). The
// curve of each segment comes from the keyframe being eased INTO.
std::string KfExpr(const std::vector<const EditOp*>& kfs, const char* tv) {
    if (kfs.size() == 1) return F3(kfs[0]->v0);
    std::string e = F3(kfs.back()->v0);
    for (int i = (int)kfs.size() - 2; i >= 0; i--) {
        const EditOp* k0 = kfs[i];
        const EditOp* k1 = kfs[i + 1];
        std::string P = std::string("clip((") + tv + "-" + F3(k0->a) + ")/" +
                        F3(std::max(0.01, k1->a - k0->a)) + ",0,1)";
        std::string seg = "(" + F3(k0->v0) + "+" + F3(k1->v0 - k0->v0) + "*" +
                          CurveExpr(k1->curveName, P) + ")";
        e = std::string("if(lt(") + tv + "," + F3(k1->a) + ")," + seg + "," + e + ")";
    }
    return e;
}

// Swap the sanitized formula's `t` for another time variable (geq uses T).
std::string ReplaceTimeVar(const std::string& f, const char* tv) {
    std::string out;
    for (size_t i = 0; i < f.size();) {
        char c = f[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            size_t s = i;
            while (i < f.size() && ((f[i] >= 'a' && f[i] <= 'z') || (f[i] >= 'A' && f[i] <= 'Z') ||
                                    (f[i] >= '0' && f[i] <= '9') || f[i] == '_'))
                i++;
            std::string ident = f.substr(s, i - s);
            out += (ident == "t") ? tv : ident;
            continue;
        }
        out += c;
        i++;
    }
    return out;
}

void RenderThreadMain(RenderState* st, Project p, std::vector<ScriptSource> sources,
                      EditScript script, std::vector<MediaFile> media, MusicConfig music) {
    auto fail = [&](const std::string& msg) {
        {
            std::lock_guard<std::mutex> lk(st->m);
            st->error = msg;
        }
        AppendLog(*st, "\nERROR: " + msg + "\n");
        st->success = false;
        st->done = true;
        st->running = false;
    };

    std::error_code ec;
    fs::create_directories(fs::path(Utf8ToWide(p.TempDir())), ec);
    std::string temp = p.TempDir();

    // ---- probe sources ---------------------------------------------------
    SetStage(*st, "Probing sources");
    if (script.timeline.empty()) { fail("the script has an empty timeline"); return; }
    std::vector<MediaInfo> infos;
    for (auto& srcE : sources) {
        MediaInfo mi = ProbeMedia(srcE.path);
        if (!mi.ok || !mi.hasVideo) { fail("cannot read video: " + srcE.path + "\n" + mi.error); return; }
        infos.push_back(mi);
    }
    int W = 0, H = 0;
    double FPS = 30;
    {
        int fi = script.timeline[0].srcIndex;
        if (fi < 0 || fi >= (int)infos.size()) { fail("timeline references a missing source"); return; }
        W = infos[fi].w - (infos[fi].w % 2);
        H = infos[fi].h - (infos[fi].h % 2);
        if (infos[fi].fps >= 10 && infos[fi].fps <= 240) FPS = infos[fi].fps;
    }
    if (W <= 0 || H <= 0) { fail("could not determine output resolution"); return; }
    std::string FPSs = F3(FPS);

    // clamp timeline spans to real source durations
    for (auto& seg : script.timeline) {
        if (seg.srcIndex < 0 || seg.srcIndex >= (int)infos.size()) {
            fail("clip '" + seg.clipName + "' references a missing source");
            return;
        }
        double sd = infos[seg.srcIndex].duration;
        if (sd > 0) {
            seg.from = std::clamp(seg.from, 0.0, sd);
            seg.to = std::clamp(seg.to, seg.from + 0.05, sd);
        }
    }
    double finalDur = 0;
    for (const auto& seg : script.timeline) finalDur += (seg.to - seg.from) / seg.rate;
    double totalBase = finalDur;

    // ---- classify composite ops ------------------------------------------
    std::vector<const EditOp*> overlays, texts, sounds;
    const EditOp* musicStart = nullptr;
    const EditOp* motionBlur = nullptr;
    double fadeIn = 0, fadeOut = 0;
    // per-property clip effects on the base video, in timeline order
    std::map<std::string, std::vector<const EditOp*>> clipFx;
    std::map<std::string, const EditOp*> exprMap;   // object prop animations
    for (const auto& op : script.ops) {
        switch (op.type) {
        case OpType::Overlay:    overlays.push_back(&op); break;
        case OpType::Text:       texts.push_back(&op); break;
        case OpType::Sound:      sounds.push_back(&op); break;
        case OpType::MusicStart: musicStart = &op; break;
        case OpType::MotionBlur: motionBlur = &op; break;
        case OpType::FadeIn:     fadeIn = std::max(fadeIn, op.b); break;
        case OpType::FadeOut:    fadeOut = std::max(fadeOut, op.b); break;
        case OpType::ClipFx:     clipFx[op.animProp].push_back(&op); break;
        case OpType::ExprAnim:   exprMap[op.idName + "/" + op.animProp] = &op; break;
        default: break;
        }
    }
    auto resolveProp = [&](const std::string& id, const char* prop,
                           const char* tv) -> std::string {
        if (id.empty()) return "";
        auto ei = exprMap.find(id + "/" + prop);
        if (ei != exprMap.end()) return "(" + ReplaceTimeVar(ei->second->text, tv) + ")";
        return "";
    };
    // piecewise expression over clip windows for a base-video property
    auto fxPiece = [&](const char* prop, const std::string& dflt,
                       const char* tv) -> std::string {
        auto it = clipFx.find(prop);
        if (it == clipFx.end()) return "";
        std::string e = dflt;
        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            const EditOp* op = *rit;
            e = std::string("if(between(") + tv + "," + F3(op->a) + "," + F3(op->b) + "),(" +
                ReplaceTimeVar(op->text, tv) + ")," + e + ")";
        }
        return e;
    };
    fadeIn = std::min(fadeIn, finalDur);
    fadeOut = std::min(fadeOut, finalDur);
    bool hasShake = clipFx.count("dx") || clipFx.count("dy");
    bool hasZoomFx = clipFx.count("zoom");
    bool hasRotFx = clipFx.count("rot");
    bool hasColorFx = clipFx.count("hue") || clipFx.count("sat") || clipFx.count("bright");
    bool hasVolFx = clipFx.count("volume");
    bool hasVideoOps = !overlays.empty() || !texts.empty() || fadeIn > 0 || fadeOut > 0 ||
                       motionBlur != nullptr || hasShake || hasZoomFx || hasRotFx || hasColorFx;
    bool hasAudioOps = !sounds.empty() || hasVolFx || fadeIn > 0 || fadeOut > 0 ||
                       music.enabled();
    bool doLoudnorm = music.autobalance && hasAudioOps;

    std::string font;
    if (!texts.empty()) {
        font = FindFontFile();
        if (font.empty()) { fail("no usable font found in C:\\Windows\\Fonts for text operations"); return; }
    }
    auto mediaPath = [&](const std::string& name) -> std::string {
        for (const auto& m : media)
            if (m.name == name) return m.fullPath;
        return p.MediaDir() + "\\" + name;
    };

    // ---- progress weights -------------------------------------------------
    double wTotal = totalBase /*stage1*/ + 0.15 * totalBase /*concat*/;
    double s4w = hasZoomFx ? 2.2 : 1.2;
    if (motionBlur) s4w *= 5;   // minterpolate is by far the slowest thing we run
    wTotal += (hasVideoOps || hasAudioOps) ? s4w * finalDur : 0.15 * finalDur;
    double wDone = 0;
    auto frac = [&](double w) { return std::clamp(w / wTotal, 0.0, 1.0); };

    std::string err;

    // ---- stage 1: render each timeline clip (trim + normalize + speed) ----
    char nbuf[64];
    for (size_t i = 0; i < script.timeline.size(); i++) {
        const auto& seg = script.timeline[i];
        const MediaInfo& si = infos[seg.srcIndex];
        double srcLen = seg.to - seg.from;
        double outLen = srcLen / seg.rate;
        std::snprintf(nbuf, sizeof(nbuf), "clip_%02d.mp4", (int)i);
        std::string outFile = temp + "\\" + nbuf;
        std::snprintf(nbuf, sizeof(nbuf), "%dx%d", W, H);
        std::string vf = "scale=" + std::string(nbuf) + ":force_original_aspect_ratio=decrease,"
                         "pad=" + std::to_string(W) + ":" + std::to_string(H) +
                         ":(ow-iw)/2:(oh-ih)/2";
        if (seg.rate != 1.0) vf += ",setpts=PTS/" + F3(seg.rate);
        vf += ",fps=" + FPSs + ",format=yuv420p";
        std::string af = "aresample=async=1:first_pts=0,"
                         "aformat=sample_rates=48000:channel_layouts=stereo";
        if (seg.rate != 1.0) af += "," + AtempoChain(seg.rate);
        af += ",apad";
        // NOTE: -ss stays on the input (fast seek), but -t must be an OUTPUT
        // option — input-side -t is unreliable on some inputs in FFmpeg 8/9.
        auto args = BaseArgs();
        args.insert(args.end(), { L"-ss", WF3(seg.from), L"-i",
                                  Utf8ToWide(sources[seg.srcIndex].path) });
        if (!si.hasAudio)
            args.insert(args.end(), { L"-f", L"lavfi", L"-t", WF3(outLen), L"-i",
                                      L"anullsrc=channel_layout=stereo:sample_rate=48000" });
        args.insert(args.end(), { L"-map", L"0:v:0",
                                  L"-map", si.hasAudio ? L"0:a:0" : L"1:a:0" });
        args.insert(args.end(), { L"-vf", Utf8ToWide(vf) });
        if (si.hasAudio) args.insert(args.end(), { L"-af", Utf8ToWide(af) });
        args.insert(args.end(), { L"-t", WF3(outLen),
                                  L"-c:v", L"libx264", L"-preset", L"veryfast", L"-crf", L"18",
                                  L"-c:a", L"aac", L"-b:a", L"192k", L"-shortest",
                                  Utf8ToWide(outFile) });
        std::snprintf(nbuf, sizeof(nbuf), "Rendering clip %d / %d (%s)", (int)i + 1,
                      (int)script.timeline.size(), seg.clipName.c_str());
        if (!RunStage(*st, args, nbuf, frac(wDone), frac(wDone + outLen), outLen, &err)) { fail(err); return; }
        wDone += outLen;
    }

    // ---- stage 2: concat ---------------------------------------------------
    {
        std::ostringstream list;
        for (size_t i = 0; i < script.timeline.size(); i++) {
            std::snprintf(nbuf, sizeof(nbuf), "clip_%02d.mp4", (int)i);
            list << "file '" << nbuf << "'\n";
        }
        if (!WriteTextFile(temp + "\\list1.txt", list.str(), &err)) { fail(err); return; }
        auto args = BaseArgs();
        args.insert(args.end(), { L"-f", L"concat", L"-safe", L"0",
                                  L"-i", Utf8ToWide(temp + "\\list1.txt"),
                                  L"-c", L"copy", Utf8ToWide(temp + "\\base.mp4") });
        double w = 0.15 * totalBase;
        if (!RunStage(*st, args, "Joining clips", frac(wDone), frac(wDone + w), totalBase, &err)) { fail(err); return; }
        wDone += w;
    }
    std::string base = temp + "\\base.mp4";

    // ---- stage 3: composite -----------------------------------------------
    if (!hasVideoOps && !hasAudioOps) {
        auto args = BaseArgs();
        args.insert(args.end(), { L"-i", Utf8ToWide(base), L"-c", L"copy",
                                  L"-movflags", L"+faststart", Utf8ToWide(p.OutputPath()) });
        if (!RunStage(*st, args, "Writing output", frac(wDone), 1.0, finalDur, &err)) { fail(err); return; }
    } else {
        std::ostringstream fg;
        auto args = BaseArgs();
        args.insert(args.end(), { L"-i", Utf8ToWide(base) });
        int inputIdx = 1;
        std::vector<int> ovInput(overlays.size()), sndInput(sounds.size());
        std::vector<bool> ovIsVideo(overlays.size(), false);
        for (size_t k = 0; k < overlays.size(); k++) {
            // still images loop (so alpha fades / pops have a live timeline),
            // gifs loop via their demuxer, videos play once from `start`
            std::string mp = mediaPath(overlays[k]->file);
            std::string lo = ToLower(mp);
            for (const auto& m : media)
                if (m.name == overlays[k]->file) ovIsVideo[k] = m.kind == "video";
            if (ovIsVideo[k])
                args.insert(args.end(), { L"-i", Utf8ToWide(mp) });
            else if (lo.size() > 4 && lo.compare(lo.size() - 4, 4, ".gif") == 0)
                args.insert(args.end(), { L"-ignore_loop", L"0", L"-i", Utf8ToWide(mp) });
            else
                args.insert(args.end(), { L"-loop", L"1", L"-i", Utf8ToWide(mp) });
            ovInput[k] = inputIdx++;
        }
        for (size_t k = 0; k < sounds.size(); k++) {
            args.insert(args.end(), { L"-i", Utf8ToWide(mediaPath(sounds[k]->file)) });
            sndInput[k] = inputIdx++;
        }
        // musicstart: shift the track so its FIRST tapped beat lands at at=
        double musOffset = 0;
        if (musicStart && !music.beats.empty())
            musOffset = musicStart->a - music.beats[0];
        int musInput = -1;
        if (music.enabled()) {
            if (music.loop) args.insert(args.end(), { L"-stream_loop", L"-1" });
            if (musOffset < -0.01)
                args.insert(args.end(), { L"-ss", WF3(-musOffset) });   // skip into the song
            args.insert(args.end(), { L"-i", Utf8ToWide(music.path) });
            musInput = inputIdx++;
        }

        // ---- video chain ------------------------------------------------
        std::string cur = "[0:v]";
        int vn = 0;
        auto nextV = [&]() {
            std::string lbl = "[v" + std::to_string(vn++) + "]";
            fg << lbl << ";";
            cur = lbl;
        };

        // ---- base-video clip effects (dx/dy shake, zoom, rot, color) ----
        if (hasShake) {
            // slight upscale + animated crop = frame displacement
            std::string dx = fxPiece("dx", "0", "t");
            std::string dy = fxPiece("dy", "0", "t");
            if (dx.empty()) dx = "0";
            if (dy.empty()) dy = "0";
            fg << cur << "scale=iw*1.06:-2,crop=" << W << ":" << H
               << ":x='clip((iw-" << W << ")/2+(" << dx << ")*" << W << ",0,iw-" << W << ")'"
               << ":y='clip((ih-" << H << ")/2+(" << dy << ")*" << H << ",0,ih-" << H << ")'";
            nextV();
        }
        if (hasZoomFx) {
            int upW = std::min(2 * W, 4096) & ~1;
            std::string z = fxPiece("zoom", "1", "it");
            fg << cur << "scale=" << upW << ":-2,"
               << "zoompan=z='clip(" << z << ",1,10)'"
               << ":x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)'"
               << ":d=1:s=" << W << "x" << H << ":fps=" << FPSs;
            nextV();
        }
        if (hasRotFx) {
            fg << cur << "rotate=a='(" << fxPiece("rot", "0", "t") << ")*PI/180':c=black";
            nextV();
        }
        if (hasColorFx) {
            std::string bh = clipFx.count("hue") ? fxPiece("hue", "0", "t") : "";
            std::string bs = clipFx.count("sat") ? fxPiece("sat", "1", "t") : "";
            std::string bb = clipFx.count("bright") ? fxPiece("bright", "0", "t") : "";
            fg << cur << "hue=";
            bool first = true;
            auto piece = [&](const char* kk, const std::string& e) {
                if (e.empty()) return;
                if (!first) fg << ":";
                first = false;
                fg << kk << "='" << e << "'";
            };
            piece("h", bh);
            piece("s", bs.empty() ? "" : "clip(" + bs + ",0,3)");
            piece("b", bb.empty() ? "" : "clip(" + bb + ",-1,1)");
            nextV();
        }

        // visual elements (objects + macro overlays/texts) composited in
        // layer order — higher layer = on top
        struct VisRef {
            const EditOp* op;
            int ovIdx;   // index into overlays[], -1 for text
        };
        std::vector<VisRef> vis;
        for (size_t k = 0; k < overlays.size(); k++) vis.push_back({ overlays[k], (int)k });
        for (const EditOp* tp : texts) vis.push_back({ tp, -1 });
        std::stable_sort(vis.begin(), vis.end(),
                         [](const VisRef& a, const VisRef& b) { return a.op->layer < b.op->layer; });

        for (const VisRef& vr : vis) {
          if (vr.ovIdx >= 0) {
            size_t k = (size_t)vr.ovIdx;
            const EditOp* op = vr.op;
            double D = std::min(op->popdur, (op->b - op->a) * 0.5);
            int pw = std::max(16, (int)(W * op->scale)) & ~1;
            const MediaFile* mf = nullptr;
            for (const auto& m : media)
                if (m.name == op->file) { mf = &m; break; }
            int ph = (mf && mf->probed && mf->info.w > 0)
                         ? std::max(2, (int)((double)pw * mf->info.h / mf->info.w)) & ~1
                         : 0;
            std::string lbl = "oa" + std::to_string(k);
            fg << "[" << ovInput[k] << ":v]";
            if (ovIsVideo[k])   // video overlays play from `start`
                fg << "setpts=PTS-STARTPTS+" << F3(op->a) << "/TB,";
            {
                // per-object color grading
                std::string hh = resolveProp(op->idName, "hue", "t");
                std::string hs = resolveProp(op->idName, "sat", "t");
                std::string hb = resolveProp(op->idName, "bright", "t");
                if (!hh.empty() || !hs.empty() || !hb.empty()) {
                    fg << "hue=";
                    bool first = true;
                    auto piece = [&](const char* kk, const std::string& e) {
                        if (e.empty()) return;
                        if (!first) fg << ":";
                        first = false;
                        fg << kk << "='" << e << "'";
                    };
                    piece("h", hh);
                    piece("s", hs.empty() ? "" : "clip(" + hs + ",0,3)");
                    piece("b", hb.empty() ? "" : "clip(" + hb + ",-1,1)");
                    fg << ",";
                }
            }
            fg << "scale=" << pw << ":-2,format=rgba";
            if (!op->keyColor.empty())   // chroma key (green screen etc.)
                fg << ",colorkey=0x" << op->keyColor.substr(1) << ":" << F3(op->keySim)
                   << ":" << F3(op->keyBlend);
            if (op->corners > 0 && ph > 0) {
                // alpha-mask a rounded rectangle (1px feathered edge)
                double R = op->corners * std::min(pw, ph);
                double c1 = (pw - 1) - R, c2 = (ph - 1) - R;
                fg << ",geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='alpha(X,Y)*clip(" << F3(R)
                   << "+0.5-hypot(max(max(" << F3(R) << "-X,X-" << F3(c1)
                   << "),0),max(max(" << F3(R) << "-Y,Y-" << F3(c2) << "),0)),0,1)'";
            }
            // animated scale / opacity resample via geq (T = time), else cheap
            // static opacity
            {
                std::string scE = resolveProp(op->idName, "scale", "T");
                std::string opE = resolveProp(op->idName, "opacity", "T");
                if (!scE.empty() || !opE.empty()) {
                    std::string S;
                    if (!scE.empty()) {
                        S = "(clip(" + scE + ",0.05,3)/" + F3(op->scale) + ")";
                        fg << ",pad=iw*3:ih*3:iw:ih:color=black@0";
                    }
                    std::string sx = scE.empty() ? "X" : "((X-W/2)/" + S + "+W/2)";
                    std::string sy = scE.empty() ? "Y" : "((Y-H/2)/" + S + "+H/2)";
                    std::string am = "alpha(" + sx + "," + sy + ")";
                    if (!opE.empty()) am += "*clip(" + opE + ",0,1)";
                    else if (op->opacity < 0.999) am += "*" + F3(op->opacity);
                    fg << ",geq=r='r(" << sx << "," << sy << ")':g='g(" << sx << "," << sy
                       << ")':b='b(" << sx << "," << sy << ")':a='" << am << "'";
                } else if (op->opacity < 0.999) {
                    fg << ",colorchannelmixer=aa=" << F3(op->opacity);
                }
            }
            fg << "[" << lbl << "];";
            if (!op->glow.empty()) {
                // pad with transparent margin, blur a colored silhouette, put
                // the sharp image back on top -> one glowing composite
                int P = std::max(12, pw / 8);
                int gr, gg, gb;
                HexColor(op->glow, gr, gg, gb);
                fg << "[" << lbl << "]pad=iw+" << 2 * P << ":ih+" << 2 * P << ":" << P << ":" << P
                   << ":color=black@0[ob" << k << "];";
                fg << "[ob" << k << "]split[oc" << k << "][od" << k << "];";
                fg << "[oc" << k << "]geq=r='" << gr << "':g='" << gg << "':b='" << gb
                   << "':a='min(255,alpha(X,Y)*2)',gblur=sigma=" << F3(P / 2.5) << ":steps=2[oh" << k << "];";
                fg << "[oh" << k << "][od" << k << "]overlay=0:0[og" << k << "];";
                lbl = "og" + std::to_string(k);
            }
            {
                // rotation: keyframes/expr/animate, or a static rot= value
                std::string rotE = resolveProp(op->idName, "rot", "t");
                if (rotE.empty() && op->rot != 0) rotE = F3(op->rot);
                if (!rotE.empty()) {
                    // rotate on an enlarged transparent canvas so corners never clip
                    fg << "[" << lbl << "]rotate=a='(" << rotE
                       << ")*PI/180':ow='hypot(iw,ih)':oh=ow:c=none[or" << k << "];";
                    lbl = "or" + std::to_string(k);
                }
            }
            {
                std::string fades;
                if (op->pop & 1) fades += "fade=t=in:st=" + F3(op->a) + ":d=" + F3(D) + ":alpha=1";
                if (op->pop & 2) {
                    if (!fades.empty()) fades += ",";
                    fades += "fade=t=out:st=" + F3(op->b - D) + ":d=" + F3(D) + ":alpha=1";
                }
                if (fades.empty()) fades = "null";
                fg << "[" << lbl << "]" << fades << "[ov" << k << "];";
            }
            std::string xE = resolveProp(op->idName, "x", "t");
            std::string yE = resolveProp(op->idName, "y", "t");
            std::string xExpr = !xE.empty() ? "main_w*" + xE + "-overlay_w/2"
                                            : "main_w*" + F3(op->x) + "-overlay_w/2";
            std::string yExpr = !yE.empty() ? "main_h*" + yE + "-overlay_h/2"
                                            : "main_h*" + F3(op->y) + "-overlay_h/2";
            if (op->pop & 1) {   // springy slide up into place
                std::string P = "clip((t-" + F3(op->a) + ")/" + F3(D) + ",0,1)";
                yExpr += "+(1-" + EaseOutBack(P) + ")*main_h*0.08";
            }
            if (op->pop & 2) {   // springy drop away
                std::string Q = "clip((t-" + F3(op->b - D) + ")/" + F3(D) + ",0,1)";
                yExpr += "+" + EaseInBack(Q) + "*main_h*0.08";
            }
            fg << cur << "[ov" << k << "]overlay="
               << "x='" << xExpr << "'"
               << ":y='" << yExpr << "'"
               << ":enable='between(t," << F3(op->a) << "," << F3(op->b) << ")'"
               << (ovIsVideo[k] ? ":eval=frame" : ":shortest=1:eval=frame");
            nextV();
          } else {
            // text element (per-op font, optional pop / animated props)
            const EditOp* op = vr.op;
            std::string f = op->font.empty() ? "" : FontFileFor(op->font, op->bold);
            std::error_code fec;
            if (f.empty() || !fs::exists(fs::path(f), fec)) f = font;
            std::string fontEsc;
            for (char fc : f) fontEsc += (fc == ':') ? std::string("\\:") : std::string(1, fc);

            double D = std::min(op->popdur, (op->b - op->a) * 0.5);
            std::string A = F3(op->a), B = F3(op->b), Ds = F3(D);
            std::string xE = resolveProp(op->idName, "x", "t");
            std::string yE = resolveProp(op->idName, "y", "t");
            std::string xExpr = !xE.empty() ? "w*" + xE + "-tw/2" : "w*" + F3(op->x) + "-tw/2";
            std::string yExpr = !yE.empty() ? "h*" + yE + "-th/2" : "h*" + F3(op->y) + "-th/2";
            std::string alpha;
            std::string opE = resolveProp(op->idName, "opacity", "t");
            if (!opE.empty()) alpha = "clip(" + opE + ",0,1)";
            else if (op->pop == 1) alpha = "clip((t-" + A + ")/" + Ds + ",0,1)";
            else if (op->pop == 2) alpha = "clip((" + B + "-t)/" + Ds + ",0,1)";
            else if (op->pop == 3)
                alpha = "min(clip((t-" + A + ")/" + Ds + ",0,1),clip((" + B + "-t)/" + Ds + ",0,1))";
            else if (op->opacity < 0.999) alpha = F3(op->opacity);
            if (op->pop & 1) {
                std::string P = "clip((t-" + A + ")/" + Ds + ",0,1)";
                yExpr += "+(1-" + EaseOutBack(P) + ")*h*0.06";
            }
            if (op->pop & 2) {
                std::string Q = "clip((t-" + F3(op->b - D) + ")/" + Ds + ",0,1)";
                yExpr += "+" + EaseInBack(Q) + "*h*0.06";
            }
            if (!op->glow.empty()) {
                // draw the text fattened on a transparent canvas, blur it,
                // composite the halo behind where the sharp text will go
                fg << "color=c=black@0.0:s=" << W << "x" << H << ":r=" << FPSs << ":d=" << F3(finalDur)
                   << ",format=rgba,drawtext=fontfile='" << fontEsc << "'"
                   << ":text='" << EscapeDrawtext(op->text) << "'"
                   << ":expansion=none"
                   << ":fontsize=" << op->size
                   << ":fontcolor=" << op->glow
                   << ":borderw=" << std::max(2, op->size / 8) << ":bordercolor=" << op->glow
                   << ":x='" << xExpr << "'"
                   << ":y='" << yExpr << "'";
                if (!alpha.empty()) fg << ":alpha='" << alpha << "'";
                fg << ",gblur=sigma=" << F3(std::max(3.0, op->size / 8.0)) << ":steps=2[tg" << vn << "];";
                fg << cur << "[tg" << vn << "]overlay=0:0:enable='between(t," << A << "," << B << ")'";
                nextV();
            }
            fg << cur << "drawtext=fontfile='" << fontEsc << "'"
               << ":text='" << EscapeDrawtext(op->text) << "'"
               << ":expansion=none"
               << ":fontsize=" << op->size
               << ":fontcolor=" << op->color
               << ":borderw=3:bordercolor=black@0.55"
               << ":x='" << xExpr << "'"
               << ":y='" << yExpr << "'";
            if (!alpha.empty()) fg << ":alpha='" << alpha << "'";
            fg << ":enable='between(t," << A << "," << B << ")'";
            nextV();
          }
        }

        // start/end fades + motion blur tail
        {
            std::string tailChain;
            auto addFade = [&](const std::string& f) {
                if (!tailChain.empty()) tailChain += ",";
                tailChain += f;
            };
            if (fadeIn > 0) addFade("fade=t=in:st=0:d=" + F3(fadeIn));
            if (fadeOut > 0)
                addFade("fade=t=out:st=" + F3(std::max(0.0, finalDur - fadeOut)) + ":d=" + F3(fadeOut));
            if (motionBlur) {
                // RSMB-style: motion-compensated interpolation to N*FPS,
                // average N consecutive subframes, step back down to FPS.
                // Cap the interpolation target so 60fps sources stay feasible.
                int N = motionBlur->mode == "low" ? 2 : motionBlur->mode == "high" ? 6 : 4;
                while (N > 2 && N * FPS > 260) N -= 2;
                int interpFps = (int)std::lround(N * FPS);
                addFade("minterpolate=fps=" + std::to_string(interpFps) +
                        ":mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1"
                        ",tmix=frames=" + std::to_string(N) +
                        ",framestep=" + std::to_string(N));
            }
            if (tailChain.empty()) tailChain = "null";
            fg << cur << tailChain << "[vout]";
        }

        // ---- audio chain ------------------------------------------------
        std::string acur = "[0:a]";
        if (hasVolFx) {
            fg << ";" << acur << "volume=volume='clip(" << fxPiece("volume", "1", "t")
               << ",0,4)':eval=frame[am]";
            acur = "[am]";
        }
        for (size_t k = 0; k < sounds.size(); k++) {
            const EditOp* op = sounds[k];
            long long ms = (long long)(op->a * 1000.0 + 0.5);
            fg << ";[" << sndInput[k] << ":a]"
               << "aformat=sample_rates=48000:channel_layouts=stereo,"
               << "adelay=" << ms << "|" << ms;
            if (op->volume != 1.0) fg << ",volume=" << F3(op->volume);
            fg << "[s" << k << "]";
        }
        if (musInput >= 0) {
            fg << ";[" << musInput << ":a]"
               << "aformat=sample_rates=48000:channel_layouts=stereo,";
            if (musOffset > 0.01) {   // delay the track so beat 1 lands at at=
                long long ms = (long long)(musOffset * 1000.0 + 0.5);
                fg << "adelay=" << ms << "|" << ms << ",";
            }
            fg << "atrim=0:" << F3(finalDur)
               << ",volume=" << F3(music.volume);
            if (finalDur > 3.0)
                fg << ",afade=t=out:st=" << F3(finalDur - 2.0) << ":d=2";
            fg << "[mus]";
        }
        int mixIns = 1 + (int)sounds.size() + (musInput >= 0 ? 1 : 0);
        if (mixIns > 1) {
            fg << ";" << acur;
            for (size_t k = 0; k < sounds.size(); k++) fg << "[s" << k << "]";
            if (musInput >= 0) fg << "[mus]";
            fg << "amix=inputs=" << mixIns << ":duration=first:normalize=0[amx]";
            acur = "[amx]";
        }
        {
            std::string tail;
            auto addA = [&](const std::string& f) {
                if (!tail.empty()) tail += ",";
                tail += f;
            };
            if (fadeIn > 0) addA("afade=t=in:st=0:d=" + F3(fadeIn));
            if (fadeOut > 0)
                addA("afade=t=out:st=" + F3(std::max(0.0, finalDur - fadeOut)) + ":d=" + F3(fadeOut));
            if (doLoudnorm) addA("loudnorm=I=-16:TP=-1.5:LRA=11,aresample=48000");
            if (tail.empty()) tail = "anull";
            fg << ";" << acur << tail << "[aout]";
        }

        AppendLog(*st, "\nfilter graph:\n" + fg.str() + "\n");

        // pass the graph inline: -filter_complex_script was removed in FFmpeg 8
        args.insert(args.end(), { L"-filter_complex", Utf8ToWide(fg.str()),
                                  L"-map", L"[vout]", L"-map", L"[aout]",
                                  L"-c:v", L"libx264", L"-preset", L"veryfast", L"-crf", L"18",
                                  L"-pix_fmt", L"yuv420p",
                                  L"-c:a", L"aac", L"-b:a", L"192k",
                                  L"-movflags", L"+faststart",
                                  Utf8ToWide(p.OutputPath()) });
        if (!RunStage(*st, args, "Compositing edits", frac(wDone), 1.0, finalDur, &err)) { fail(err); return; }
    }

    if (!std::getenv("AIVE_KEEP_TEMP"))   // debug: keep intermediates
        fs::remove_all(fs::path(Utf8ToWide(temp)), ec);
    SetStage(*st, "Done");
    st->progress = 1.f;
    st->success = true;
    st->done = true;
    st->running = false;
}

} // namespace

void StartRender(RenderState& st, const Project& p, std::vector<ScriptSource> sources,
                 EditScript script, std::vector<MediaFile> media, MusicConfig music) {
    if (st.running) return;
    if (st.worker.joinable()) st.worker.join();
    st.running = true;
    st.done = false;
    st.success = false;
    st.cancelRequested = false;
    st.progress = 0.f;
    {
        std::lock_guard<std::mutex> lk(st.m);
        st.error.clear();
        st.log.clear();
        st.stage = "Starting";
    }
    st.worker = std::thread(RenderThreadMain, &st, p, std::move(sources),
                            std::move(script), std::move(media), std::move(music));
}

void CancelRender(RenderState& st) {
    st.cancelRequested = true;
    std::lock_guard<std::mutex> lk(st.m);
    if (st.hProc) TerminateProcess((HANDLE)st.hProc, 1);
}

void ShutdownRender(RenderState& st) {
    if (st.running) CancelRender(st);
    if (st.worker.joinable()) st.worker.join();
}
