#include "render.h"
#include "util.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    {
        std::string cmd;
        for (auto& a : args) { cmd += WideToUtf8(a); cmd += ' '; }
        AppendLog(st, "\n> " + cmd + "\n");
    }
    std::string errText;
    int code = RunProcessStream(
        args,
        [&](const std::string& line) {
            // out_time_us / out_time_ms are both microseconds
            double us = -1;
            if (line.rfind("out_time_us=", 0) == 0) us = std::atof(line.c_str() + 12);
            else if (line.rfind("out_time_ms=", 0) == 0) us = std::atof(line.c_str() + 12);
            if (us >= 0 && expectDur > 0.01) {
                double frac = std::clamp(us / 1e6 / expectDur, 0.0, 1.0);
                st.progress = (float)(p0 + frac * (p1 - p0));
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

void RenderThreadMain(RenderState* st, Project p, std::vector<TrimClip> clips,
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
    SetStage(*st, "Probing source clips");
    int W = 0, H = 0;
    std::vector<MediaInfo> infos;
    for (auto& c : clips) {
        MediaInfo mi = ProbeMedia(c.path);
        if (!mi.ok || !mi.hasVideo) { fail("cannot read video: " + c.path + "\n" + mi.error); return; }
        if (mi.duration > 0) {
            c.in = std::clamp(c.in, 0.0, mi.duration);
            c.out = std::clamp(c.out, c.in + 0.05, mi.duration);
        }
        if (W == 0) { W = mi.w - (mi.w % 2); H = mi.h - (mi.h % 2); }
        infos.push_back(mi);
    }
    if (W <= 0 || H <= 0) { fail("could not determine output resolution"); return; }

    double totalBase = 0;
    for (const auto& c : clips) totalBase += c.out - c.in;

    // ---- plan cut/speed segments ----------------------------------------
    std::vector<const EditOp*> timeOps;
    for (const auto& op : script.ops)
        if (op.type == OpType::Cut || op.type == OpType::Speed) timeOps.push_back(&op);
    std::sort(timeOps.begin(), timeOps.end(),
              [](const EditOp* a, const EditOp* b) { return a->a < b->a; });
    std::vector<Seg> segs;
    if (!timeOps.empty()) {
        double cur = 0;
        for (const auto* op : timeOps) {
            double a = std::clamp(op->a, 0.0, totalBase);
            double b = std::clamp(op->b, 0.0, totalBase);
            if (a - cur > 0.015) segs.push_back({ cur, a, 1.0 });
            if (op->type == OpType::Speed) segs.push_back({ a, b, op->rate });
            cur = std::max(cur, b);
        }
        if (totalBase - cur > 0.015) segs.push_back({ cur, totalBase, 1.0 });
        if (segs.empty()) { fail("cut/speed operations leave no video"); return; }
    }
    double finalDur = totalBase;
    if (!timeOps.empty()) {
        finalDur = 0;
        for (const auto& s : segs) finalDur += (s.b - s.a) / s.rate;
    }

    // ---- classify composite ops ------------------------------------------
    std::vector<const EditOp*> overlays, texts, sounds, mutes, transitions, zooms, flickers;
    double fadeIn = 0, fadeOut = 0;
    for (const auto& op : script.ops) {
        switch (op.type) {
        case OpType::Overlay:    overlays.push_back(&op); break;
        case OpType::Text:       texts.push_back(&op); break;
        case OpType::Sound:      sounds.push_back(&op); break;
        case OpType::Mute:       mutes.push_back(&op); break;
        case OpType::Transition: transitions.push_back(&op); break;
        case OpType::Zoom:       zooms.push_back(&op); break;
        case OpType::Flicker:    flickers.push_back(&op); break;
        case OpType::FadeIn:     fadeIn = std::max(fadeIn, op.b); break;
        case OpType::FadeOut:    fadeOut = std::max(fadeOut, op.b); break;
        default: break;
        }
    }
    fadeIn = std::min(fadeIn, finalDur);
    fadeOut = std::min(fadeOut, finalDur);
    bool hasVideoOps = !overlays.empty() || !texts.empty() || fadeIn > 0 || fadeOut > 0 ||
                       !transitions.empty() || !zooms.empty() || !flickers.empty();
    bool hasAudioOps = !sounds.empty() || !mutes.empty() || fadeIn > 0 || fadeOut > 0 ||
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
    for (const auto& s : segs) wTotal += (s.b - s.a) / s.rate;
    if (!segs.empty()) wTotal += 0.15 * finalDur;
    wTotal += (hasVideoOps || hasAudioOps) ? (zooms.empty() ? 1.2 : 2.2) * finalDur
                                           : 0.15 * finalDur;
    double wDone = 0;
    auto frac = [&](double w) { return std::clamp(w / wTotal, 0.0, 1.0); };

    std::string err;

    // ---- stage 1: trim + normalize each clip ------------------------------
    char nbuf[64];
    for (size_t i = 0; i < clips.size(); i++) {
        const auto& c = clips[i];
        double dur = c.out - c.in;
        std::snprintf(nbuf, sizeof(nbuf), "clip_%02d.mp4", (int)i);
        std::string outFile = temp + "\\" + nbuf;
        std::snprintf(nbuf, sizeof(nbuf), "%dx%d", W, H);
        std::string vf = "scale=" + std::string(nbuf) + ":force_original_aspect_ratio=decrease,"
                         "pad=" + std::to_string(W) + ":" + std::to_string(H) +
                         ":(ow-iw)/2:(oh-ih)/2,fps=30,format=yuv420p";
        // NOTE: -ss stays on the input (fast seek), but -t must be an OUTPUT
        // option — input-side -t is unreliable on some inputs in FFmpeg 8/9.
        auto args = BaseArgs();
        args.insert(args.end(), { L"-ss", WF3(c.in), L"-i", Utf8ToWide(c.path) });
        if (!infos[i].hasAudio)
            args.insert(args.end(), { L"-f", L"lavfi", L"-t", WF3(dur), L"-i",
                                      L"anullsrc=channel_layout=stereo:sample_rate=48000" });
        args.insert(args.end(), { L"-map", L"0:v:0",
                                  L"-map", infos[i].hasAudio ? L"0:a:0" : L"1:a:0" });
        args.insert(args.end(), { L"-vf", Utf8ToWide(vf) });
        if (infos[i].hasAudio)
            args.insert(args.end(), { L"-af",
                L"aresample=async=1:first_pts=0,aformat=sample_rates=48000:channel_layouts=stereo,apad" });
        args.insert(args.end(), { L"-t", WF3(dur),
                                  L"-c:v", L"libx264", L"-preset", L"veryfast", L"-crf", L"18",
                                  L"-c:a", L"aac", L"-b:a", L"192k", L"-shortest",
                                  Utf8ToWide(outFile) });
        std::snprintf(nbuf, sizeof(nbuf), "Trimming clip %d / %d", (int)i + 1, (int)clips.size());
        if (!RunStage(*st, args, nbuf, frac(wDone), frac(wDone + dur), dur, &err)) { fail(err); return; }
        wDone += dur;
    }

    // ---- stage 2: concat ---------------------------------------------------
    {
        std::ostringstream list;
        for (size_t i = 0; i < clips.size(); i++) {
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

    // ---- stage 3: cuts & speed --------------------------------------------
    if (!segs.empty()) {
        for (size_t j = 0; j < segs.size(); j++) {
            const Seg& s = segs[j];
            double len = s.b - s.a;
            double outLen = len / s.rate;
            std::snprintf(nbuf, sizeof(nbuf), "seg_%02d.mp4", (int)j);
            std::string outFile = temp + "\\" + nbuf;
            auto args = BaseArgs();
            args.insert(args.end(), { L"-ss", WF3(s.a), L"-i", Utf8ToWide(base) });
            if (s.rate != 1.0) {
                args.insert(args.end(), { L"-vf", Utf8ToWide("setpts=PTS/" + F3(s.rate) + ",fps=30"),
                                          L"-af", Utf8ToWide(AtempoChain(s.rate)) });
            }
            // output-side -t; for speed segments the output lasts len/rate
            args.insert(args.end(), { L"-t", WF3(outLen),
                                      L"-c:v", L"libx264", L"-preset", L"veryfast", L"-crf", L"18",
                                      L"-c:a", L"aac", L"-b:a", L"192k", Utf8ToWide(outFile) });
            std::snprintf(nbuf, sizeof(nbuf), "Applying cuts + speed (%d / %d)", (int)j + 1, (int)segs.size());
            if (!RunStage(*st, args, nbuf, frac(wDone), frac(wDone + outLen), outLen, &err)) { fail(err); return; }
            wDone += outLen;
        }
        std::ostringstream list;
        for (size_t j = 0; j < segs.size(); j++) {
            std::snprintf(nbuf, sizeof(nbuf), "seg_%02d.mp4", (int)j);
            list << "file '" << nbuf << "'\n";
        }
        if (!WriteTextFile(temp + "\\list2.txt", list.str(), &err)) { fail(err); return; }
        auto args = BaseArgs();
        args.insert(args.end(), { L"-f", L"concat", L"-safe", L"0",
                                  L"-i", Utf8ToWide(temp + "\\list2.txt"),
                                  L"-c", L"copy", Utf8ToWide(temp + "\\base2.mp4") });
        double w = 0.15 * finalDur;
        if (!RunStage(*st, args, "Joining segments", frac(wDone), frac(wDone + w), finalDur, &err)) { fail(err); return; }
        wDone += w;
        base = temp + "\\base2.mp4";
    }

    // ---- stage 4: composite -----------------------------------------------
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
        for (size_t k = 0; k < overlays.size(); k++) {
            // loop still images so alpha fades / pop animations have a live
            // timeline to act on; gifs loop via their own demuxer option
            std::string mp = mediaPath(overlays[k]->file);
            std::string lo = ToLower(mp);
            if (lo.size() > 4 && lo.compare(lo.size() - 4, 4, ".gif") == 0)
                args.insert(args.end(), { L"-ignore_loop", L"0", L"-i", Utf8ToWide(mp) });
            else
                args.insert(args.end(), { L"-loop", L"1", L"-i", Utf8ToWide(mp) });
            ovInput[k] = inputIdx++;
        }
        for (size_t k = 0; k < sounds.size(); k++) {
            args.insert(args.end(), { L"-i", Utf8ToWide(mediaPath(sounds[k]->file)) });
            sndInput[k] = inputIdx++;
        }
        int musInput = -1;
        if (music.enabled()) {
            if (music.loop) args.insert(args.end(), { L"-stream_loop", L"-1" });
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

        // eased zooms via zoompan on an upscaled frame (reduces jitter)
        if (!zooms.empty()) {
            int upW = std::min(2 * W, 4096) & ~1;
            std::string z = "1";
            for (auto it = zooms.rbegin(); it != zooms.rend(); ++it) {
                const EditOp* op = *it;
                std::string P = "st(1,clip((it-" + F3(op->a) + ")/" + F3(op->b - op->a) + ",0,1))";
                std::string E = "if(lt(" + P + ",0.5),4*pow(ld(1),3),1-pow(2-2*ld(1),3)/2)";
                std::string M = F3(op->amount - 1);
                std::string zexpr;
                if (op->mode == "out")       zexpr = "1+" + M + "*(1-(" + E + "))";
                else if (op->mode == "pulse") zexpr = "1+" + M + "*pow(sin(PI*" + P + "),2)";
                else                          zexpr = "1+" + M + "*(" + E + ")";
                z = "if(between(it," + F3(op->a) + "," + F3(op->b) + ")," + zexpr + "," + z + ")";
            }
            fg << cur << "scale=" << upW << ":-2,"
               << "zoompan=z='" << z << "'"
               << ":x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)'"
               << ":d=1:s=" << W << "x" << H << ":fps=30";
            nextV();
        }

        // brightness flicker
        if (!flickers.empty()) {
            std::string expr;
            for (const EditOp* op : flickers) {
                if (!expr.empty()) expr += "+";
                expr += "if(between(t," + F3(op->a) + "," + F3(op->b) + ")," +
                        F3(op->amp) + "*sin(2*PI*" + F3(op->freq) + "*t),0)";
            }
            fg << cur << "eq=brightness='" << expr << "':eval=frame";
            nextV();
        }

        // image overlays (with optional pop animation)
        for (size_t k = 0; k < overlays.size(); k++) {
            const EditOp* op = overlays[k];
            double D = std::min(op->popdur, (op->b - op->a) * 0.5);
            int pw = std::max(16, (int)(W * op->scale)) & ~1;
            fg << "[" << ovInput[k] << ":v]scale=" << pw << ":-2,format=rgba";
            if (op->opacity < 0.999) fg << ",colorchannelmixer=aa=" << F3(op->opacity);
            if (op->pop & 1) fg << ",fade=t=in:st=" << F3(op->a) << ":d=" << F3(D) << ":alpha=1";
            if (op->pop & 2) fg << ",fade=t=out:st=" << F3(op->b - D) << ":d=" << F3(D) << ":alpha=1";
            fg << "[ov" << k << "];";
            std::string yExpr = "main_h*" + F3(op->y) + "-overlay_h/2";
            if (op->pop & 1) {   // springy slide up into place
                std::string P = "clip((t-" + F3(op->a) + ")/" + F3(D) + ",0,1)";
                yExpr += "+(1-" + EaseOutBack(P) + ")*main_h*0.08";
            }
            if (op->pop & 2) {   // springy drop away
                std::string Q = "clip((t-" + F3(op->b - D) + ")/" + F3(D) + ",0,1)";
                yExpr += "+" + EaseInBack(Q) + "*main_h*0.08";
            }
            fg << cur << "[ov" << k << "]overlay="
               << "x='main_w*" << F3(op->x) << "-overlay_w/2'"
               << ":y='" << yExpr << "'"
               << ":enable='between(t," << F3(op->a) << "," << F3(op->b) << ")'"
               << ":shortest=1:eval=frame";
            nextV();
        }

        // text (per-op font, optional pop)
        for (const EditOp* op : texts) {
            std::string f = op->font.empty() ? "" : FontFileFor(op->font, op->bold);
            std::error_code fec;
            if (f.empty() || !fs::exists(fs::path(f), fec)) f = font;
            std::string fontEsc;
            for (char fc : f) fontEsc += (fc == ':') ? std::string("\\:") : std::string(1, fc);

            double D = std::min(op->popdur, (op->b - op->a) * 0.5);
            std::string A = F3(op->a), B = F3(op->b), Ds = F3(D);
            std::string yExpr = "h*" + F3(op->y) + "-th/2";
            std::string alpha;
            if (op->pop == 1) alpha = "clip((t-" + A + ")/" + Ds + ",0,1)";
            else if (op->pop == 2) alpha = "clip((" + B + "-t)/" + Ds + ",0,1)";
            else if (op->pop == 3)
                alpha = "min(clip((t-" + A + ")/" + Ds + ",0,1),clip((" + B + "-t)/" + Ds + ",0,1))";
            if (op->pop & 1) {
                std::string P = "clip((t-" + A + ")/" + Ds + ",0,1)";
                yExpr += "+(1-" + EaseOutBack(P) + ")*h*0.06";
            }
            if (op->pop & 2) {
                std::string Q = "clip((t-" + F3(op->b - D) + ")/" + Ds + ",0,1)";
                yExpr += "+" + EaseInBack(Q) + "*h*0.06";
            }
            fg << cur << "drawtext=fontfile='" << fontEsc << "'"
               << ":text='" << EscapeDrawtext(op->text) << "'"
               << ":expansion=none"
               << ":fontsize=" << op->size
               << ":fontcolor=" << op->color
               << ":borderw=3:bordercolor=black@0.55"
               << ":x='w*" << F3(op->x) << "-tw/2'"
               << ":y='" << yExpr << "'";
            if (!alpha.empty()) fg << ":alpha='" << alpha << "'";
            fg << ":enable='between(t," << A << "," << B << ")'";
            nextV();
        }

        // transitions (dip to black/white) + start/end fades
        {
            std::string tailChain;
            auto addFade = [&](const std::string& f) {
                if (!tailChain.empty()) tailChain += ",";
                tailChain += f;
            };
            for (const EditOp* op : transitions) {
                double half = op->b * 0.5;
                double st0 = std::max(0.0, op->a - half);
                std::string col = op->mode == "white" ? ":color=white" : "";
                // gate each half with enable=: a bare fade-in shows black for
                // everything BEFORE st, and a fade-out holds black AFTER it
                addFade("fade=t=out:st=" + F3(st0) + ":d=" + F3(half) + col +
                        ":enable='between(t," + F3(st0) + "," + F3(op->a) + ")'");
                addFade("fade=t=in:st=" + F3(op->a) + ":d=" + F3(half) + col +
                        ":enable='between(t," + F3(op->a) + "," + F3(op->a + half) + ")'");
            }
            if (fadeIn > 0) addFade("fade=t=in:st=0:d=" + F3(fadeIn));
            if (fadeOut > 0)
                addFade("fade=t=out:st=" + F3(std::max(0.0, finalDur - fadeOut)) + ":d=" + F3(fadeOut));
            if (tailChain.empty()) tailChain = "null";
            fg << cur << tailChain << "[vout]";
        }

        // ---- audio chain ------------------------------------------------
        std::string acur = "[0:a]";
        if (!mutes.empty()) {
            fg << ";" << acur;
            bool first = true;
            for (const EditOp* op : mutes) {
                if (!first) fg << ",";
                first = false;
                fg << "volume=enable='between(t," << F3(op->a) << "," << F3(op->b) << ")':volume=0";
            }
            fg << "[am]";
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
               << "aformat=sample_rates=48000:channel_layouts=stereo,"
               << "atrim=0:" << F3(finalDur)
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

void StartRender(RenderState& st, const Project& p, std::vector<TrimClip> clips,
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
    st.worker = std::thread(RenderThreadMain, &st, p, std::move(clips),
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
