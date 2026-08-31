#include "ffmpeg.h"
#include "util.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

static std::wstring g_ffmpeg;    // empty = not found
static std::wstring g_ffprobe;
static std::mutex g_detectM;

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

static bool TryTool(const std::wstring& exe) {
    ProcResult r = RunProcess({ exe, L"-version" });
    return r.started && r.exitCode == 0;
}

void DetectFfmpeg() {
    std::lock_guard<std::mutex> lk(g_detectM);
    g_ffmpeg.clear();
    g_ffprobe.clear();
    std::wstring dir = ExeDir();
    const std::wstring ffCandidates[] = {
        dir + L"\\ffmpeg.exe", dir + L"\\ffmpeg\\bin\\ffmpeg.exe", L"ffmpeg.exe",
    };
    const std::wstring fpCandidates[] = {
        dir + L"\\ffprobe.exe", dir + L"\\ffmpeg\\bin\\ffprobe.exe", L"ffprobe.exe",
    };
    for (const auto& c : ffCandidates)
        if (TryTool(c)) { g_ffmpeg = c; break; }
    for (const auto& c : fpCandidates)
        if (TryTool(c)) { g_ffprobe = c; break; }
}

bool FfmpegAvailable()  { std::lock_guard<std::mutex> lk(g_detectM); return !g_ffmpeg.empty(); }
bool FfprobeAvailable() { std::lock_guard<std::mutex> lk(g_detectM); return !g_ffprobe.empty(); }
std::wstring FfmpegExe()  { std::lock_guard<std::mutex> lk(g_detectM); return g_ffmpeg.empty() ? L"ffmpeg.exe" : g_ffmpeg; }
std::wstring FfprobeExe() { std::lock_guard<std::mutex> lk(g_detectM); return g_ffprobe.empty() ? L"ffprobe.exe" : g_ffprobe; }

// Parse ffprobe -of flat output: lines like
//   streams.stream.0.codec_type="video"
//   streams.stream.0.width=1920
//   format.duration="10.000000"
MediaInfo ProbeMedia(const std::string& path) {
    MediaInfo mi;
    ProcResult r = RunProcess({
        FfprobeExe(), L"-v", L"error", L"-of", L"flat",
        L"-show_entries", L"format=duration:stream=codec_type,width,height",
        Utf8ToWide(path),
    });
    if (!r.started) { mi.error = "could not launch ffprobe"; return mi; }
    if (r.exitCode != 0) {
        mi.error = r.err.empty() ? "ffprobe failed" : r.err;
        return mi;
    }
    std::string text(r.out.begin(), r.out.end());
    size_t pos = 0;
    int curStream = -1;
    std::string curType;
    int curW = 0, curH = 0;
    bool gotDims = false;
    auto flushStream = [&]() {
        if (curStream < 0) return;
        if (curType == "video") {
            mi.hasVideo = true;
            if (!gotDims && curW > 0 && curH > 0) { mi.w = curW; mi.h = curH; gotDims = true; }
        } else if (curType == "audio") {
            mi.hasAudio = true;
        }
        curType.clear(); curW = curH = 0;
    };
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        const std::string sp = "streams.stream.";
        if (key.rfind(sp, 0) == 0) {
            size_t dot = key.find('.', sp.size());
            if (dot == std::string::npos) continue;
            int idx = std::atoi(key.substr(sp.size(), dot - sp.size()).c_str());
            std::string field = key.substr(dot + 1);
            if (idx != curStream) { flushStream(); curStream = idx; }
            if (field == "codec_type") curType = val;
            else if (field == "width") curW = std::atoi(val.c_str());
            else if (field == "height") curH = std::atoi(val.c_str());
        } else if (key == "format.duration") {
            if (val != "N/A") mi.duration = std::atof(val.c_str());
        }
    }
    flushStream();
    mi.ok = true;
    if (mi.duration < 0) mi.duration = 0;
    return mi;
}

bool ExtractFrameRGBA(const std::string& path, double t, int targetW,
                      std::vector<uint8_t>& rgba, int& w, int& h, std::string* err) {
    if (t < 0) t = 0;
    wchar_t tbuf[64];
    swprintf(tbuf, 64, L"%.3f", t);
    wchar_t sbuf[64];
    swprintf(sbuf, 64, L"scale=%d:-2", targetW);
    ProcResult r = RunProcess({
        FfmpegExe(), L"-v", L"error", L"-nostdin",
        L"-ss", tbuf, L"-i", Utf8ToWide(path),
        L"-frames:v", L"1", L"-vf", sbuf,
        L"-f", L"image2pipe", L"-vcodec", L"bmp", L"-",
    });
    if (!r.started) { if (err) *err = "could not launch ffmpeg"; return false; }
    if (r.exitCode != 0 || r.out.empty()) {
        if (err) *err = r.err.empty() ? "no frame produced" : r.err;
        return false;
    }
    int comp = 0;
    unsigned char* px = stbi_load_from_memory(r.out.data(), (int)r.out.size(), &w, &h, &comp, 4);
    if (!px) { if (err) *err = "failed to decode frame"; return false; }
    rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return true;
}
