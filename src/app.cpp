#include "app.h"
#include "util.h"
#include "ffmpeg.h"
#include "project.h"
#include "edit.h"
#include "script.h"
#include "render.h"
#include "texture.h"

#include "imgui.h"

#include <mmsystem.h>   // MCI: in-app music playback for beat tapping

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------- theme
namespace theme {
constexpr ImVec4 Lime{ 0.66f, 1.00f, 0.00f, 1.f };      // #A8FF00
constexpr ImVec4 LimeHi{ 0.80f, 1.00f, 0.30f, 1.f };
constexpr ImVec4 LimeDim{ 0.45f, 0.72f, 0.06f, 1.f };
constexpr ImVec4 LimeDark{ 0.20f, 0.32f, 0.04f, 1.f };
constexpr ImVec4 LimeFaint{ 0.66f, 1.00f, 0.00f, 0.16f };
constexpr ImVec4 Black{ 0.033f, 0.036f, 0.030f, 1.f };
constexpr ImVec4 Panel{ 0.055f, 0.060f, 0.050f, 1.f };
constexpr ImVec4 Frame{ 0.095f, 0.105f, 0.080f, 1.f };
constexpr ImVec4 FrameHi{ 0.140f, 0.165f, 0.095f, 1.f };
constexpr ImVec4 BorderCol{ 0.165f, 0.185f, 0.130f, 1.f };
constexpr ImVec4 Text{ 0.90f, 0.94f, 0.86f, 1.f };
constexpr ImVec4 TextDim{ 0.50f, 0.55f, 0.45f, 1.f };
constexpr ImVec4 Bad{ 1.f, 0.42f, 0.36f, 1.f };
constexpr ImVec4 Warn{ 0.95f, 0.82f, 0.40f, 1.f };
constexpr ImU32 TlLime = IM_COL32(168, 255, 0, 255);
constexpr ImU32 TlMark = IM_COL32(245, 245, 240, 255);
constexpr ImU32 TlPlayhead = IM_COL32(255, 82, 70, 255);
} // namespace theme

static void ApplyTheme(float dpi) {
    using namespace theme;
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);

    s.WindowRounding = 0.f;
    s.FrameRounding = 5.f;
    s.GrabRounding = 5.f;
    s.ChildRounding = 8.f;
    s.PopupRounding = 6.f;
    s.ScrollbarRounding = 8.f;
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(9, 8);
    s.ItemInnerSpacing = ImVec2(7, 5);
    s.ScrollbarSize = 13.f;
    s.GrabMinSize = 12.f;
    s.FrameBorderSize = 1.f;
    s.ChildBorderSize = 1.f;
    s.SeparatorTextBorderSize = 2.f;
    s.SeparatorTextAlign = ImVec2(0.f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = Text;
    c[ImGuiCol_TextDisabled] = TextDim;
    c[ImGuiCol_WindowBg] = Black;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(Panel.x, Panel.y, Panel.z, 0.98f);
    c[ImGuiCol_Border] = BorderCol;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = Frame;
    c[ImGuiCol_FrameBgHovered] = FrameHi;
    c[ImGuiCol_FrameBgActive] = LimeDark;
    c[ImGuiCol_ScrollbarBg] = Black;
    c[ImGuiCol_ScrollbarGrab] = FrameHi;
    c[ImGuiCol_ScrollbarGrabHovered] = LimeDim;
    c[ImGuiCol_ScrollbarGrabActive] = Lime;
    c[ImGuiCol_CheckMark] = Lime;
    c[ImGuiCol_SliderGrab] = LimeDim;
    c[ImGuiCol_SliderGrabActive] = Lime;
    c[ImGuiCol_Button] = Frame;
    c[ImGuiCol_ButtonHovered] = FrameHi;
    c[ImGuiCol_ButtonActive] = LimeDark;
    c[ImGuiCol_Header] = ImVec4(Lime.x, Lime.y, Lime.z, 0.22f);
    c[ImGuiCol_HeaderHovered] = ImVec4(Lime.x, Lime.y, Lime.z, 0.32f);
    c[ImGuiCol_HeaderActive] = ImVec4(Lime.x, Lime.y, Lime.z, 0.42f);
    c[ImGuiCol_Separator] = BorderCol;
    c[ImGuiCol_SeparatorHovered] = LimeDim;
    c[ImGuiCol_SeparatorActive] = Lime;
    c[ImGuiCol_ResizeGrip] = LimeFaint;
    c[ImGuiCol_ResizeGripHovered] = LimeDim;
    c[ImGuiCol_ResizeGripActive] = Lime;
    c[ImGuiCol_PlotHistogram] = ImVec4(0.42f, 0.66f, 0.05f, 1.f);
    c[ImGuiCol_PlotHistogramHovered] = Lime;
    c[ImGuiCol_TableHeaderBg] = Frame;
    c[ImGuiCol_TableBorderStrong] = BorderCol;
    c[ImGuiCol_TableBorderLight] = ImVec4(BorderCol.x, BorderCol.y, BorderCol.z, 0.5f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(Lime.x, Lime.y, Lime.z, 0.30f);
    c[ImGuiCol_DragDropTarget] = Lime;
    c[ImGuiCol_NavCursor] = Lime;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);

    s.ScaleAllSizes(dpi);
}

// ---------------------------------------------------------------- state
enum class Screen { Project, MarkUp, Prompt, Edit };

struct Clip {
    uint64_t id = 0;
    std::string path;        // absolute source path
    std::string fileName;
    double playhead = 0;
    std::vector<TrimMarker> markers;
    bool probed = false;
    std::string err;
    MediaInfo info;
    std::vector<TexPtr> strip;       // filmstrip thumbnails
    bool stripStarted = false;
};

struct AppState {
    HWND hwnd = nullptr;
    float dpi = 1.f;
    Screen screen = Screen::Project;

    std::atomic<bool> ffmpegOk{ false }, ffprobeOk{ false }, detecting{ false };

    // project screen
    char baseDir[1024] = {};
    char projName[256] = {};
    std::string projMsg;
    Project proj;
    bool hasProject = false;

    // markup screen
    std::vector<Clip> clips;
    std::string overview;
    int sel = -1;
    int dragMode = 0;              // 0 none, 3 playhead, 5 scrollbar, 1000+i marker
    TexPtr preview;
    uint64_t previewClipId = 0;
    double lastReqT = -1;
    std::string trimMsg;
    int scrubDiv = 2;
    double lastReqWall = 0;
    bool hiResDone = true;
    float tlZoom = 1.f;
    float tlScroll = 0.f;

    // media library
    std::vector<MediaFile> media;
    double lastScanT = 0;

    // music (lives on the markup screen now)
    MusicConfig music;
    bool musicProbed = true;
    std::string musicMsg;
    bool tapping = false;
    bool tapPreparing = false;
    std::vector<double> tapTimes;

    // prompt screen
    std::string promptText;
    std::string promptMsg;

    // edit screen
    std::string editText;
    EditScript script;
    bool validated = false;
    std::vector<std::string> qAnswers;
    double lastClipCheck = 0;
    std::string lastAutoPasted;
    std::string editMsg;
    RenderState render;
};
static AppState g;
static uint64_t g_nextClipId = 1;

// ---------------------------------------------------------------- MCI playback
static bool g_mciOpen = false;
static void MciStop() {
    if (g_mciOpen) {
        mciSendStringW(L"close aivebeat", nullptr, 0, nullptr);
        g_mciOpen = false;
    }
}
static bool MciTryOpenAndPlay(const std::wstring& openCmd) {
    if (mciSendStringW(openCmd.c_str(), nullptr, 0, nullptr) != 0) return false;
    g_mciOpen = true;
    mciSendStringW(L"set aivebeat time format milliseconds", nullptr, 0, nullptr);
    if (mciSendStringW(L"play aivebeat", nullptr, 0, nullptr) != 0) {
        MciStop();
        return false;
    }
    return true;
}
static bool MciStart(const std::string& path) {
    MciStop();
    std::wstring w = Utf8ToWide(path);
    if (MciTryOpenAndPlay(L"open \"" + w + L"\" alias aivebeat")) return true;
    if (MciTryOpenAndPlay(L"open \"" + w + L"\" type mpegvideo alias aivebeat")) return true;
    return false;
}
static double MciPosSec() {
    if (!g_mciOpen) return -1;
    wchar_t buf[64]{};
    if (mciSendStringW(L"status aivebeat position", buf, 64, nullptr) != 0) return -1;
    return _wtof(buf) / 1000.0;
}
static bool MciPlaying() {
    if (!g_mciOpen) return false;
    wchar_t buf[64]{};
    if (mciSendStringW(L"status aivebeat mode", buf, 64, nullptr) != 0) return false;
    return wcscmp(buf, L"playing") == 0;
}

// ---------------------------------------------------------------- preview worker
namespace {
std::mutex pvM;
std::condition_variable pvCv;
bool pvHasReq = false, pvStop = false, pvStarted = false;
struct { uint64_t clipId; std::string path; double t; int width; bool fast; } pvReq;
std::thread pvThread;

Clip* FindClip(uint64_t id) {
    for (auto& c : g.clips)
        if (c.id == id) return &c;
    return nullptr;
}

void PreviewThreadMain() {
    for (;;) {
        decltype(pvReq) req;
        {
            std::unique_lock<std::mutex> lk(pvM);
            pvCv.wait(lk, [] { return pvStop || pvHasReq; });
            if (pvStop) return;
            req = pvReq;
            pvHasReq = false;
        }
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        std::string err;
        if (ExtractFrameRGBA(req.path, req.t, req.width, rgba, w, h, &err, req.fast)) {
            uint64_t id = req.clipId;
            PostToMainThread([id, rgba = std::move(rgba), w, h]() {
                if (g.sel >= 0 && g.sel < (int)g.clips.size() && g.clips[g.sel].id == id) {
                    g.preview = CreateTextureRGBA(rgba.data(), w, h);
                    g.previewClipId = id;
                }
            });
        }
    }
}

void RequestPreview(const Clip& c, double t, bool forceFull = false) {
    int srcW = c.info.w > 0 ? c.info.w : 1920;
    int w, fast;
    if (forceFull) {
        w = std::clamp(srcW, 64, 3840);
        fast = false;
    } else {
        w = std::clamp(srcW / std::max(1, g.scrubDiv), 64, 3840);
        fast = g.scrubDiv >= 8;
    }
    g.lastReqWall = NowSeconds();
    g.hiResDone = forceFull || g.scrubDiv == 1;
    {
        std::lock_guard<std::mutex> lk(pvM);
        if (!pvStarted) { pvStarted = true; pvThread = std::thread(PreviewThreadMain); }
        pvReq = { c.id, c.path, t, w, fast != 0 };
        pvHasReq = true;
    }
    pvCv.notify_one();
}
} // namespace

// ---------------------------------------------------------------- helpers
static void AddMarkerAt(Clip& c, double t) {
    TrimMarker m;
    m.t = t;
    auto it = std::lower_bound(c.markers.begin(), c.markers.end(), t,
                               [](const TrimMarker& a, double v) { return a.t < v; });
    c.markers.insert(it, m);
}

static double TotalSourceDuration() {
    double t = 0;
    for (const auto& c : g.clips)
        if (c.probed) t += c.info.duration;
    return t;
}

static std::vector<TrimClipV2> ToTrimV2() {
    std::vector<TrimClipV2> v;
    for (const auto& c : g.clips) {
        TrimClipV2 t;
        t.path = c.path;
        t.markers = c.markers;
        v.push_back(std::move(t));
    }
    return v;
}

static void SaveTrimNow() {
    if (!g.hasProject || g.clips.empty()) return;
    std::string err;
    if (!SaveTrimFile(g.proj, ToTrimV2(), g.overview, &err)) g.trimMsg = err;
}

static std::vector<ScriptSource> Sources() {
    std::vector<ScriptSource> v;
    for (const auto& c : g.clips)
        v.push_back({ c.path, c.probed ? c.info.duration : 0 });
    return v;
}

static double ProjectFps() {
    if (!g.clips.empty() && g.clips[0].probed && g.clips[0].info.fps > 0)
        return g.clips[0].info.fps;
    return 30;
}

// ---------------------------------------------------------------- async
static const int kStripCount = 14;

static void StartFilmstrip(Clip& c) {
    if (c.stripStarted || !c.probed || !c.info.hasVideo || c.info.duration <= 0) return;
    c.stripStarted = true;
    c.strip.assign(kStripCount, nullptr);
    for (int i = 0; i < kStripCount; i++) {
        uint64_t id = c.id;
        std::string path = c.path;
        double t = c.info.duration * (i + 0.5) / kStripCount;
        JobsPush([id, path, t, i]() {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            std::string err;
            if (ExtractFrameRGBA(path, t, 160, rgba, w, h, &err, true)) {
                PostToMainThread([id, i, rgba = std::move(rgba), w, h]() {
                    Clip* c = FindClip(id);
                    if (c && i < (int)c->strip.size())
                        c->strip[i] = CreateTextureRGBA(rgba.data(), w, h);
                });
            }
        });
    }
}

static void StartProbeClip(Clip& c) {
    uint64_t id = c.id;
    std::string path = c.path;
    JobsPush([id, path]() {
        MediaInfo mi = ProbeMedia(path);
        PostToMainThread([id, mi]() {
            Clip* c = FindClip(id);
            if (!c) return;
            c->info = mi;
            c->probed = true;
            if (!mi.ok) c->err = mi.error.empty() ? "cannot read file" : mi.error;
            else if (!mi.hasVideo) c->err = "no video stream in this file";
            else {
                for (auto& mk : c->markers)
                    mk.t = std::clamp(mk.t, 0.0, mi.duration);
                c->playhead = 0;
                StartFilmstrip(*c);
                if (g.sel >= 0 && g.sel < (int)g.clips.size() && g.clips[g.sel].id == c->id)
                    RequestPreview(*c, c->playhead);
            }
        });
    });
}

static void AddClipPath(const std::string& path) {
    Clip c;
    c.id = g_nextClipId++;
    c.path = path;
    size_t slash = path.find_last_of("\\/");
    c.fileName = slash == std::string::npos ? path : path.substr(slash + 1);
    g.clips.push_back(std::move(c));
    StartProbeClip(g.clips.back());
    if (g.sel < 0) g.sel = (int)g.clips.size() - 1;
}

static void StartProbeMedia(const MediaFile& m) {
    std::string name = m.name, path = m.fullPath;
    JobsPush([name, path]() {
        MediaInfo mi = ProbeMedia(path);
        PostToMainThread([name, mi]() {
            for (auto& mm : g.media)
                if (mm.name == name) { mm.info = mi; mm.probed = true; }
        });
    });
}

static void RescanMedia(bool force) {
    if (!g.hasProject) return;
    double now = NowSeconds();
    if (!force && now - g.lastScanT < 1.5) return;
    g.lastScanT = now;
    auto found = ScanMediaDir(g.proj);
    std::vector<MediaFile> merged;
    for (auto& f : found) {
        bool known = false;
        for (auto& m : g.media)
            if (m.name == f.name && m.sizeBytes == f.sizeBytes) {
                merged.push_back(m);
                known = true;
                break;
            }
        if (!known) {
            merged.push_back(f);
            if (g.ffprobeOk) StartProbeMedia(merged.back());
        }
    }
    g.media = std::move(merged);
}

// copy a picked file into media\ and return its file name ("" on failure)
static std::string ImportIntoMedia(const std::string& srcPath) {
    std::error_code ec;
    fs::create_directories(fs::path(Utf8ToWide(g.proj.MediaDir())), ec);
    size_t slash = srcPath.find_last_of("\\/");
    std::string name = slash == std::string::npos ? srcPath : srcPath.substr(slash + 1);
    fs::copy_file(fs::path(Utf8ToWide(srcPath)),
                  fs::path(Utf8ToWide(g.proj.MediaDir() + "\\" + name)),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) return "";
    RescanMedia(true);
    return name;
}

static void StartDetectFfmpeg() {
    if (g.detecting) return;
    g.detecting = true;
    JobsPush([]() {
        DetectFfmpeg();
        bool ff = FfmpegAvailable(), fp = FfprobeAvailable();
        PostToMainThread([ff, fp]() {
            g.ffmpegOk = ff;
            g.ffprobeOk = fp;
            g.detecting = false;
        });
    });
}

static void StartTapping() {
    if (MciStart(g.music.path)) {
        g.tapping = true;
        g.tapTimes.clear();
        return;
    }
    g.tapPreparing = true;
    g.musicMsg.clear();
    std::string src = g.music.path;
    JobsPush([src]() {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        std::string wav = WideToUtf8(tmp) + "aive_beat.wav";
        ProcResult r = RunProcess({ FfmpegExe(), L"-y", L"-nostdin", L"-v", L"error",
                                    L"-i", Utf8ToWide(src),
                                    L"-vn", L"-ac", L"2", L"-ar", L"44100",
                                    L"-c:a", L"pcm_s16le", Utf8ToWide(wav) });
        bool ok = r.started && r.exitCode == 0;
        PostToMainThread([ok, wav]() {
            g.tapPreparing = false;
            if (ok && MciStart(wav)) {
                g.tapping = true;
                g.tapTimes.clear();
            } else {
                g.musicMsg = "Could not play or convert this audio file for tapping.";
            }
        });
    });
}

static void StartProbeMusic() {
    g.musicProbed = false;
    g.musicMsg.clear();
    std::string path = g.music.path;
    JobsPush([path]() {
        MediaInfo mi = ProbeMedia(path);
        PostToMainThread([path, mi]() {
            if (g.music.path != path) return;
            g.musicProbed = true;
            if (!mi.ok || !mi.hasAudio) {
                g.musicMsg = "This file has no usable audio: " +
                             (mi.error.empty() ? path : mi.error);
                g.music.path.clear();
            } else {
                g.music.duration = mi.duration;
            }
        });
    });
}

// ---------------------------------------------------------------- UI helpers
static int InputTextResizeCb(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = (std::string*)d->UserData;
        s->resize(d->BufTextLen);
        d->Buf = (char*)s->c_str();
    }
    return 0;
}
static bool InputTextMultilineStr(const char* label, std::string& s, ImVec2 size,
                                  ImGuiInputTextFlags extraFlags = 0) {
    return ImGui::InputTextMultiline(label, (char*)s.c_str(), s.capacity() + 1, size,
                                     ImGuiInputTextFlags_CallbackResize | extraFlags,
                                     InputTextResizeCb, &s);
}
static bool InputTextStrHint(const char* label, const char* hint, std::string& s) {
    return ImGui::InputTextWithHint(label, hint, (char*)s.c_str(), s.capacity() + 1,
                                    ImGuiInputTextFlags_CallbackResize, InputTextResizeCb, &s);
}

static void FfmpegBanner() {
    if (g.ffmpegOk && g.ffprobeOk) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.15f, 0.10f, 1.f));
    ImGui::BeginChild("##ffbanner", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.6f),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextUnformatted(g.detecting
        ? "Looking for FFmpeg..."
        : "FFmpeg not found. Install it (e.g. `winget install Gyan.FFmpeg`, then restart this app)\n"
          "or place ffmpeg.exe + ffprobe.exe next to this program. All video work needs it.");
    if (!g.detecting) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Re-detect")) StartDetectFfmpeg();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

static bool BottomBar(const char* primaryLabel, bool enabled, const char* disabledWhy) {
    ImGui::Separator();
    bool clicked = false;
    if (!enabled && disabledWhy && disabledWhy[0]) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::Warn, "%s", disabledWhy);
        ImGui::SameLine();
    }
    float w = 190.f * g.dpi;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - w));
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::Lime);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::LimeHi);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::LimeDim);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.07f, 0.02f, 1.f));
    if (ImGui::Button(primaryLabel, ImVec2(w, 0))) clicked = true;
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return clicked && enabled;
}

// ---------------------------------------------------------------- validate / render
static void ValidateEditNow() {
    RescanMedia(true);
    g.script = ParseScript(g.editText, Sources(), g.media, &g.music, ProjectFps());
    g.validated = true;
    g.qAnswers.assign(g.script.questions.size(), std::string());
    std::string err;
    WriteTextFile(g.proj.EditFilePath(), g.editText, &err);
}

static void StartRenderNow() {
    std::string err;
    WriteTextFile(g.proj.EditFilePath(), g.editText, &err);
    StartRender(g.render, g.proj, Sources(), g.script, g.media, g.music);
}

static bool LooksLikeEditReply(const std::string& s) {
    size_t i = 0;
    for (int guard = 0; guard < 6 && i < s.size(); guard++) {
        size_t eol = s.find('\n', i);
        std::string line = s.substr(i, eol == std::string::npos ? std::string::npos : eol - i);
        size_t ws = line.find_first_not_of(" \t\r");
        if (ws != std::string::npos) {
            std::string t = line.substr(ws);
            if (t.rfind("```", 0) != 0)
                return t.rfind("!!!", 0) == 0;
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return false;
}

// ---------------------------------------------------------------- project open / prompt
static void GeneratePromptNow() {
    RescanMedia(true);
    int W = 0, H = 0;
    if (!g.clips.empty() && g.clips[0].probed) {
        W = g.clips[0].info.w;
        H = g.clips[0].info.h;
    }
    std::vector<double> durs;
    for (const auto& c : g.clips) durs.push_back(c.probed ? c.info.duration : 0);
    g.promptText = GeneratePromptScript(g.proj, ToTrimV2(), durs, g.overview, g.media,
                                        g.music, W, H, ProjectFps());
    std::string err;
    WriteTextFile(g.proj.PromptFilePath(), g.promptText, &err);
    ImGui::SetClipboardText(g.promptText.c_str());
    g.promptMsg = err.empty()
        ? "Prompt generated, saved and COPIED TO YOUR CLIPBOARD. Paste it into your AI of choice."
        : ("Prompt copied to clipboard, but saving failed: " + err);
}

static void OpenExistingProject(const std::string& dir) {
    g.proj.dir = dir;
    size_t slash = dir.find_last_of("\\/");
    g.proj.name = slash == std::string::npos ? dir : dir.substr(slash + 1);
    g.hasProject = true;
    g.clips.clear();
    g.sel = -1;
    g.media.clear();
    g.preview = nullptr;
    g.promptText.clear();
    g.editText.clear();
    g.validated = false;
    g.overview.clear();
    g.music = MusicConfig{};
    g.musicProbed = true;
    g.musicMsg.clear();
    if (LoadMusicFile(g.proj, g.music) && g.music.enabled()) StartProbeMusic();

    std::error_code ec;
    std::string err;
    std::vector<TrimClipV2> tc;
    if (LoadTrimFile(g.proj, tc, &g.overview, &err)) {
        for (const auto& t : tc) {
            AddClipPath(t.path);
            g.clips.back().markers = t.markers;
        }
    }
    fs::create_directories(fs::path(Utf8ToWide(g.proj.MediaDir())), ec);
    RescanMedia(true);
    bool hasPrompt = fs::exists(fs::path(Utf8ToWide(g.proj.PromptFilePath())), ec);
    bool hasEdit = fs::exists(fs::path(Utf8ToWide(g.proj.EditFilePath())), ec);
    if (hasEdit || hasPrompt) {
        ReadTextFile(g.proj.PromptFilePath(), g.promptText);
        if (hasEdit) {
            ReadTextFile(g.proj.EditFilePath(), g.editText);
            g.screen = Screen::Edit;
        } else {
            g.screen = Screen::Prompt;
        }
        g.promptMsg = "Loaded existing prompt file.";
    } else {
        g.screen = Screen::MarkUp;
    }
}

// ---------------------------------------------------------------- screens
static void ScreenProject() {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, 40.f * g.dpi);
    ImGui::TextColored(theme::Lime, "AI VIDEO EDITOR");
    ImGui::PopFont();
    ImGui::TextDisabled("mark it up  -  let an AI direct the edit  -  render with one click");
    ImGui::Spacing();
    ImGui::Spacing();
    FfmpegBanner();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::Lime);
    ImGui::SeparatorText("New project");
    ImGui::PopStyleColor();
    ImGui::TextUnformatted("Where to create it:");
    ImGui::SetNextItemWidth(-140.f * g.dpi);
    ImGui::InputText("##basedir", g.baseDir, sizeof(g.baseDir));
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(120.f * g.dpi, 0))) {
        std::string dir;
        if (OpenFolderDialog(g.hwnd, dir)) {
            strncpy(g.baseDir, dir.c_str(), sizeof(g.baseDir) - 1);
            g.baseDir[sizeof(g.baseDir) - 1] = 0;
        }
    }
    ImGui::TextUnformatted("Project folder name:");
    ImGui::SetNextItemWidth(-140.f * g.dpi);
    ImGui::InputText("##pname", g.projName, sizeof(g.projName));
    ImGui::SameLine();
    if (ImGui::Button("Create", ImVec2(120.f * g.dpi, 0))) {
        std::string name = g.projName;
        std::string clean;
        for (char c : name)
            if (std::string("<>:\"/\\|?*").find(c) == std::string::npos) clean += c;
        while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
        if (clean.empty()) {
            g.projMsg = "Enter a project name.";
        } else if (!g.baseDir[0]) {
            g.projMsg = "Choose where to create the project.";
        } else {
            std::string dir = std::string(g.baseDir) + "\\" + clean;
            std::error_code ec;
            fs::create_directories(fs::path(Utf8ToWide(dir + "\\media")), ec);
            if (ec) {
                g.projMsg = "Could not create folder: " + dir;
            } else {
                g.proj.dir = dir;
                g.proj.name = clean;
                g.hasProject = true;
                g.clips.clear();
                g.sel = -1;
                g.media.clear();
                g.preview = nullptr;
                g.promptText.clear();
                g.editText.clear();
                g.validated = false;
                g.overview.clear();
                g.music = MusicConfig{};
                g.musicProbed = true;
                g.projMsg.clear();
                g.screen = Screen::MarkUp;
            }
        }
    }
    if (!g.projMsg.empty()) ImGui::TextColored(theme::Bad, "%s", g.projMsg.c_str());

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::Lime);
    ImGui::SeparatorText("Or continue where you left off");
    ImGui::PopStyleColor();
    if (ImGui::Button("Open existing project folder...", ImVec2(260.f * g.dpi, 0))) {
        std::string dir;
        if (OpenFolderDialog(g.hwnd, dir)) OpenExistingProject(dir);
    }
}

// Marker timeline: scrub, zoom, drop lime marker flags. No trimming.
static void TimelineUI(Clip& c) {
    double dur = c.info.duration;
    if (dur <= 0) {
        ImGui::TextDisabled("this file reports no duration");
        return;
    }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float tlH = 96.f * g.dpi;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 sz(std::max(avail.x, 60.f), tlH);
    ImGui::InvisibleButton("##timeline", sz);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mp = io.MousePos;

    float contentW = std::max(sz.x * g.tlZoom, sz.x);
    g.tlScroll = std::clamp(g.tlScroll, 0.f, std::max(0.f, contentW - sz.x));
    auto X = [&](double t) { return p0.x + (float)(t / dur) * contentW - g.tlScroll; };
    auto T = [&](float sx) {
        return std::clamp((double)(sx - p0.x + g.tlScroll) / contentW * dur, 0.0, dur);
    };

    float sbH = (g.tlZoom > 1.001f) ? 6.f * g.dpi : 0.f;
    float y0 = p0.y + 3, y1 = p0.y + sz.y - 18.f * g.dpi - sbH;
    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(14, 15, 13, 255), 4);
    int n = (int)c.strip.size();
    if (n > 0) {
        dl->PushClipRect(ImVec2(p0.x, y0), ImVec2(p0.x + sz.x, y1), true);
        float tw = contentW / n;
        for (int i = 0; i < n; i++) {
            if (!c.strip[i]) continue;
            float xa = p0.x + i * tw - g.tlScroll;
            float xb = xa + tw;
            if (xb < p0.x || xa > p0.x + sz.x) continue;
            dl->AddImage((ImTextureID)c.strip[i]->ImId(), ImVec2(xa, y0), ImVec2(xb, y1));
        }
        dl->PopClipRect();
    }
    // markers
    for (const auto& mk : c.markers) {
        float x = X(mk.t);
        if (x < p0.x || x > p0.x + sz.x) continue;
        dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), theme::TlLime, 1.6f);
        float d = 5.f * g.dpi;
        dl->AddQuadFilled(ImVec2(x, y0), ImVec2(x + d, y0 + d), ImVec2(x, y0 + 2 * d),
                          ImVec2(x - d, y0 + d), theme::TlLime);
        if (ImGui::IsItemHovered() && std::abs(mp.x - x) < 6.f * g.dpi)
            ImGui::SetTooltip("marker @ %s%s%s - drag to move, right-click to delete",
                              FormatTime(mk.t).c_str(), mk.note.empty() ? "" : "\n",
                              mk.note.c_str());
    }
    // playhead
    float px = X(c.playhead);
    dl->AddRectFilled(ImVec2(px - 1.5f, p0.y), ImVec2(px + 1.5f, y1 + 2), theme::TlPlayhead);
    // scrollbar
    if (sbH > 0) {
        float sy0 = y1 + 1, sy1 = y1 + sbH - 1;
        dl->AddRectFilled(ImVec2(p0.x, sy0), ImVec2(p0.x + sz.x, sy1), IM_COL32(28, 30, 25, 255), 2);
        float f0 = g.tlScroll / contentW, f1 = (g.tlScroll + sz.x) / contentW;
        dl->AddRectFilled(ImVec2(p0.x + f0 * sz.x, sy0), ImVec2(p0.x + f1 * sz.x, sy1),
                          IM_COL32(120, 170, 40, 255), 2);
    }

    auto requestAt = [&](double t) {
        if (std::abs(t - g.lastReqT) > 0.001) {
            g.lastReqT = t;
            RequestPreview(c, t);
        }
    };
    if (ImGui::IsItemActivated()) {
        g.dragMode = 3;
        if (sbH > 0 && mp.y > y1) g.dragMode = 5;
        if (g.dragMode == 3) {
            for (size_t mi = 0; mi < c.markers.size(); mi++) {
                if (std::abs(mp.x - X(c.markers[mi].t)) < 6.f * g.dpi) {
                    g.dragMode = 1000 + (int)mi;
                    break;
                }
            }
        }
    }
    if (ImGui::IsItemActive()) {
        if (g.dragMode == 5) {
            g.tlScroll = std::clamp((mp.x - p0.x) / sz.x * contentW - sz.x * 0.5f, 0.f,
                                    std::max(0.f, contentW - sz.x));
        } else if (g.dragMode >= 1000) {
            size_t mi = (size_t)(g.dragMode - 1000);
            if (mi < c.markers.size()) {
                c.markers[mi].t = T(mp.x);
                c.playhead = c.markers[mi].t;
                requestAt(c.playhead);
            }
        } else if (g.dragMode == 3) {
            c.playhead = T(mp.x);
            requestAt(c.playhead);
        }
    } else if (ImGui::IsItemDeactivated()) {
        g.dragMode = 0;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        for (size_t mi = 0; mi < c.markers.size(); mi++)
            if (std::abs(mp.x - X(c.markers[mi].t)) < 8.f * g.dpi) {
                c.markers.erase(c.markers.begin() + mi);
                break;
            }
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.f) {
        if (io.KeyCtrl) {
            double tA = T(mp.x);
            g.tlZoom = std::clamp(g.tlZoom * (io.MouseWheel > 0 ? 1.3f : 1.f / 1.3f), 1.f, 60.f);
            contentW = std::max(sz.x * g.tlZoom, sz.x);
            g.tlScroll = std::clamp((float)(tA / dur) * contentW - (mp.x - p0.x), 0.f,
                                    std::max(0.f, contentW - sz.x));
        } else {
            g.tlScroll = std::clamp(g.tlScroll - io.MouseWheel * 90.f * g.dpi, 0.f,
                                    std::max(0.f, contentW - sz.x));
        }
    }

    char lbl[160];
    snprintf(lbl, sizeof(lbl), "%s  |  %d marker(s)", FormatTime(c.playhead).c_str(),
             (int)c.markers.size());
    dl->AddText(ImVec2(p0.x + 4, y1 + sbH + 2), IM_COL32(216, 226, 205, 255), lbl);
    char rl[64];
    snprintf(rl, sizeof(rl), "%s  |  zoom %.1fx", FormatTime(dur).c_str(), g.tlZoom);
    ImVec2 ts = ImGui::CalcTextSize(rl);
    dl->AddText(ImVec2(p0.x + sz.x - ts.x - 4, y1 + sbH + 2), IM_COL32(126, 136, 114, 255), rl);
}

// left-column global panel: overview, global media, music + beats
static void WholeVideoPanel() {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::Lime);
    ImGui::SeparatorText("Whole video");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("The vision: vibe, style, what you want.");
    InputTextMultilineStr("##overview", g.overview,
                          ImVec2(-1, ImGui::GetTextLineHeight() * 4.5f));

    ImGui::Spacing();
    ImGui::TextDisabled("Global media (watermark, logo, ads...):");
    int nGlobal = 0;
    for (const auto& m : g.media)
        if (m.kind != "other") nGlobal++;
    ImGui::Text("%d file(s) in the media library", nGlobal);
    if (ImGui::Button("Attach media file...", ImVec2(-1, 0))) {
        std::vector<std::string> paths;
        if (OpenAnyMediaFilesDialog(g.hwnd, paths))
            for (const auto& p : paths) ImportIntoMedia(p);
    }
    ImGui::SameLine(0, 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copied into the project's media folder and listed in the prompt.\n"
                          "You can also drop files into the media folder directly.");
    ImGui::NewLine();
    if (ImGui::SmallButton("Open media folder")) OpenInExplorer(g.proj.MediaDir());

    // ---- music -----------------------------------------------------------
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::Lime);
    ImGui::SeparatorText("Music");
    ImGui::PopStyleColor();
    ImGui::BeginDisabled(g.tapping);
    if (g.music.enabled()) {
        std::string name = g.music.path;
        size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (!g.musicProbed) ImGui::Text("%s (reading...)", name.c_str());
        else ImGui::Text("%s (%s)", name.c_str(), FormatTime(g.music.duration).c_str());
        if (ImGui::SmallButton("Change...")) {
            std::string path;
            if (OpenAudioFileDialog(g.hwnd, path)) {
                g.music.path = path;
                StartProbeMusic();
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            g.music.path.clear();
            g.music.duration = 0;
            g.musicProbed = true;
        }
        float vol = (float)g.music.volume;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##mvol", &vol, 0.02f, 1.0f, "volume %.2f"))
            g.music.volume = vol;
        bool loop = g.music.loop;
        if (ImGui::Checkbox("Loop if shorter", &loop)) g.music.loop = loop;
    } else {
        if (ImGui::Button("Choose music file...", ImVec2(-1, 0))) {
            std::string path;
            if (OpenAudioFileDialog(g.hwnd, path)) {
                g.music.path = path;
                StartProbeMusic();
            }
        }
    }
    ImGui::EndDisabled();
    if (!g.musicMsg.empty()) ImGui::TextColored(theme::Bad, "%s", g.musicMsg.c_str());

    // beat tapping
    if (g.music.enabled() && g.musicProbed) {
        if (g.tapping) {
            double pos = MciPosSec();
            ImGui::Text("%s / %s", FormatTime(pos < 0 ? 0 : pos).c_str(),
                        FormatTime(g.music.duration).c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, theme::Lime);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::LimeHi);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::LimeDim);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.07f, 0.02f, 1.f));
            bool tapped = ImGui::Button("TAP (Space)", ImVec2(-1, 44.f * g.dpi));
            ImGui::PopStyleColor(4);
            if (!tapped && !ImGui::GetIO().WantTextInput &&
                ImGui::IsKeyPressed(ImGuiKey_Space, false))
                tapped = true;
            if (tapped && pos >= 0) g.tapTimes.push_back(std::max(0.0, pos - 0.06));
            ImGui::Text("%d taps", (int)g.tapTimes.size());
            bool finish = false;
            if (ImGui::SmallButton("Done")) finish = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) {
                MciStop();
                g.tapping = false;
            }
            if (pos > 0.3 && !MciPlaying()) finish = true;
            if (finish && g.tapping) {
                g.music.beats = g.tapTimes;
                MciStop();
                g.tapping = false;
                SaveMusicFile(g.proj, g.music, nullptr);
            }
        } else if (g.tapPreparing) {
            ImGui::TextColored(theme::Warn, "Preparing audio...");
        } else {
            if (!g.music.beats.empty()) {
                ImGui::Text("%d beats tapped", (int)g.music.beats.size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    g.music.beats.clear();
                    SaveMusicFile(g.proj, g.music, nullptr);
                }
            }
            if (ImGui::Button(g.music.beats.empty() ? "Tap beats..." : "Re-tap beats...",
                              ImVec2(-1, 0)))
                StartTapping();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The song plays; press SPACE on every hard beat/drop.\n"
                                  "The AI sizes its clips so kills land on those beats.");
        }
    }
    bool ab = g.music.autobalance;
    if (ImGui::Checkbox("Auto-balance final audio", &ab)) g.music.autobalance = ab;
}

static void ScreenMarkUp() {
    FfmpegBanner();
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi;

    // ---- left: clips + whole-video panel ---------------------------------
    ImGui::BeginChild("##left", ImVec2(300.f * g.dpi, -bottomH));
    ImGui::BeginDisabled(!g.ffprobeOk);
    if (ImGui::Button("+ Add raw videos...", ImVec2(-1, 0))) {
        std::vector<std::string> paths;
        if (OpenVideoFilesDialog(g.hwnd, paths))
            for (const auto& p : paths) AddClipPath(p);
    }
    ImGui::EndDisabled();
    int removeIdx = -1, moveFrom = -1, moveTo = -1;
    for (int i = 0; i < (int)g.clips.size(); i++) {
        Clip& c = g.clips[i];
        ImGui::PushID((int)c.id);
        bool selected = g.sel == i;
        std::string label = "src " + std::to_string(i + 1) + ": " + c.fileName;
        if (!c.err.empty()) ImGui::PushStyleColor(ImGuiCol_Text, theme::Bad);
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(214.f * g.dpi, 0))) {
            g.sel = i;
            g.preview = nullptr;
            g.tlZoom = 1.f;
            g.tlScroll = 0.f;
            if (c.probed && c.err.empty()) RequestPreview(c, c.playhead);
        }
        if (!c.err.empty()) {
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.err.c_str());
        } else if (c.probed && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%dx%d @ %.6g fps, %s", c.path.c_str(), c.info.w, c.info.h,
                              c.info.fps, FormatTime(c.info.duration).c_str());
        }
        ImGui::SameLine(220.f * g.dpi);
        if (ImGui::SmallButton("^") && i > 0) { moveFrom = i; moveTo = i - 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("v") && i + 1 < (int)g.clips.size()) { moveFrom = i; moveTo = i + 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeIdx = i;
        ImGui::PopID();
    }
    if (moveFrom >= 0) {
        std::swap(g.clips[moveFrom], g.clips[moveTo]);
        if (g.sel == moveFrom) g.sel = moveTo;
        else if (g.sel == moveTo) g.sel = moveFrom;
    }
    if (removeIdx >= 0) {
        g.clips.erase(g.clips.begin() + removeIdx);
        if (g.sel >= (int)g.clips.size()) g.sel = (int)g.clips.size() - 1;
        g.preview = nullptr;
    }
    if (g.clips.empty())
        ImGui::TextDisabled("Add your raw footage, then drop\nmarkers on the moments that matter.");
    ImGui::Spacing();
    WholeVideoPanel();
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right: preview + timeline + markers ------------------------------
    ImGui::BeginChild("##right", ImVec2(0, -bottomH));
    if (g.sel >= 0 && g.sel < (int)g.clips.size()) {
        Clip& c = g.clips[g.sel];
        if (!c.probed) {
            ImGui::TextDisabled("Reading video info...");
        } else if (!c.err.empty()) {
            ImGui::TextColored(theme::Bad, "This file can't be used: %s", c.err.c_str());
        } else {
            float markersH = 170.f * g.dpi;
            float ctrlH = ImGui::GetFrameHeightWithSpacing() + 8.f * g.dpi;
            float tlH = 104.f * g.dpi;
            ImVec2 pv = ImGui::GetContentRegionAvail();
            pv.y = std::max(80.f, pv.y - tlH - ctrlH - markersH);
            {
                ImVec2 box = pv;
                float aspect = c.info.h > 0 ? (float)c.info.w / c.info.h : 16.f / 9.f;
                ImVec2 img(box.x, box.x / aspect);
                if (img.y > box.y) { img.y = box.y; img.x = box.y * aspect; }
                ImVec2 cur = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(cur.x + (box.x - img.x) * 0.5f, cur.y + (box.y - img.y) * 0.5f));
                if (g.preview && g.previewClipId == c.id)
                    ImGui::Image((ImTextureID)g.preview->ImId(), img);
                else {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImGui::GetCursorScreenPos(),
                        ImVec2(ImGui::GetCursorScreenPos().x + img.x, ImGui::GetCursorScreenPos().y + img.y),
                        IM_COL32(10, 11, 9, 255), 4);
                    ImGui::Dummy(img);
                }
                ImGui::SetCursorPos(ImVec2(cur.x, cur.y + box.y));
            }
            TimelineUI(c);
            if (!g.hiResDone && NowSeconds() - g.lastReqWall > 0.35)
                RequestPreview(c, c.playhead, true);
            if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_M, false))
                AddMarkerAt(c, c.playhead);
            // controls
            if (ImGui::Button("Add marker (M)")) AddMarkerAt(c, c.playhead);
            ImGui::SameLine();
            auto step = [&](double d) {
                c.playhead = std::clamp(c.playhead + d, 0.0, c.info.duration);
                g.lastReqT = c.playhead;
                RequestPreview(c, c.playhead);
            };
            if (ImGui::Button("-1s")) step(-1);
            ImGui::SameLine();
            if (ImGui::Button("-1f")) step(-1.0 / std::max(10.0, c.info.fps > 0 ? c.info.fps : 30.0));
            ImGui::SameLine();
            if (ImGui::Button("+1f")) step(1.0 / std::max(10.0, c.info.fps > 0 ? c.info.fps : 30.0));
            ImGui::SameLine();
            if (ImGui::Button("+1s")) step(1);
            ImGui::SameLine();
            ImGui::BeginDisabled(g.tlZoom <= 1.001f);
            if (ImGui::Button("Fit")) { g.tlZoom = 1.f; g.tlScroll = 0.f; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Scrub:");
            ImGui::SameLine();
            {
                static const char* items = "1/1\0" "1/2\0" "1/4\0" "1/8\0" "1/16\0";
                static const int divs[] = { 1, 2, 4, 8, 16 };
                int idx = 1;
                for (int di = 0; di < 5; di++)
                    if (divs[di] == g.scrubDiv) idx = di;
                ImGui::SetNextItemWidth(76.f * g.dpi);
                if (ImGui::Combo("##scrubres", &idx, items)) {
                    g.scrubDiv = divs[idx];
                    g.lastReqT = c.playhead;
                    RequestPreview(c, c.playhead);
                }
            }
            // marker list with notes + media attachments
            ImGui::BeginChild("##markers", ImVec2(0, 0), ImGuiChildFlags_Borders);
            if (c.markers.empty()) {
                ImGui::TextDisabled("No markers yet. Scrub to a kill / punchline / moment and press "
                                    "M. Each marker takes a note and (optionally) an attached media "
                                    "file - the AI anchors the edit to these.");
            }
            int delMk = -1;
            for (int mi = 0; mi < (int)c.markers.size(); mi++) {
                auto& mk = c.markers[mi];
                ImGui::PushID(mi);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::Lime, "@ %s", FormatTime(mk.t).c_str());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 190.f * g.dpi);
                InputTextStrHint("##note", "what happens here / what do you want...", mk.note);
                ImGui::SameLine();
                if (mk.media.empty()) {
                    if (ImGui::SmallButton("attach media")) {
                        std::vector<std::string> paths;
                        if (OpenAnyMediaFilesDialog(g.hwnd, paths) && !paths.empty()) {
                            std::string name = ImportIntoMedia(paths[0]);
                            if (!name.empty()) mk.media = name;
                        }
                    }
                } else {
                    if (ImGui::SmallButton(("[" + mk.media + "]").c_str())) mk.media.clear();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("attached - click to detach");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) delMk = mi;
                ImGui::PopID();
            }
            if (delMk >= 0) c.markers.erase(c.markers.begin() + delMk);
            ImGui::EndChild();
        }
    } else {
        ImGui::TextDisabled("Select a source on the left.");
    }
    ImGui::EndChild();

    std::string why;
    bool ready = !g.clips.empty() && g.ffmpegOk && !g.tapping && !g.tapPreparing;
    for (const auto& c : g.clips) {
        if (!c.probed) { ready = false; why = "still reading video info..."; }
        else if (!c.err.empty()) { ready = false; why = "remove sources marked red first"; }
    }
    if (g.clips.empty()) why = "add at least one video";
    else if (!g.ffmpegOk && why.empty()) why = "FFmpeg is required";
    else if ((g.tapping || g.tapPreparing) && why.empty()) why = "finish beat tapping first";
    if (BottomBar("Next: generate prompt >", ready, why.c_str())) {
        SaveTrimNow();
        SaveMusicFile(g.proj, g.music, nullptr);
        GeneratePromptNow();
        g.screen = Screen::Prompt;
    }
    if (!g.trimMsg.empty()) ImGui::TextColored(theme::Bad, "%s", g.trimMsg.c_str());
}

static void ScreenPrompt() {
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi;
    ImGui::TextWrapped("%s", g.promptMsg.empty() ? "Prompt ready." : g.promptMsg.c_str());
    ImGui::TextWrapped("Paste it into ChatGPT / Claude / your AI of choice. It replies with an "
                       "AIVE_SCRIPT program - copy that reply, then click Next: if a valid "
                       "script is on your clipboard the render starts automatically.");
    ImGui::Spacing();
    if (ImGui::Button("Copy prompt again")) {
        ImGui::SetClipboardText(g.promptText.c_str());
        g.promptMsg = "Copied to clipboard again.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Regenerate")) GeneratePromptNow();
    ImGui::SameLine();
    if (ImGui::Button("Open project folder")) OpenInExplorer(g.proj.dir);
    ImGui::Spacing();
    ImGui::BeginChild("##promptview", ImVec2(0, -bottomH), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(g.promptText.c_str());
    ImGui::EndChild();

    if (BottomBar("Next: edit + render >", !g.promptText.empty(), "")) {
        g.screen = Screen::Edit;
        // auto-paste + validate + render straight from the clipboard
        const char* cb = ImGui::GetClipboardText();
        if (cb && *cb && LooksLikeEditReply(cb)) {
            g.editText = cb;
            g.lastAutoPasted = cb;
            ValidateEditNow();
            if (g.script.Valid() && g.ffmpegOk && !g.render.running) {
                StartRenderNow();
                g.editMsg = "Found a valid script on your clipboard - rendering now.";
            } else {
                g.editMsg = "Pasted the script from your clipboard.";
            }
        }
    }
}

static void ScreenEdit() {
    FfmpegBanner();
    bool rendering = g.render.running;
    bool renderDone = g.render.done;

    // clipboard watcher: an AI reply starting with !!! pastes + validates itself
    if (!rendering) {
        double now = NowSeconds();
        if (now - g.lastClipCheck > 0.5) {
            g.lastClipCheck = now;
            const char* cb = ImGui::GetClipboardText();
            if (cb && *cb) {
                std::string s = cb;
                if (s != g.lastAutoPasted && s != g.editText && LooksLikeEditReply(s)) {
                    g.lastAutoPasted = s;
                    g.editText = s;
                    ValidateEditNow();
                    g.editMsg = "Detected a script on your clipboard - pasted and validated it.";
                }
            }
        }
    }

    ImGui::TextWrapped("Paste the AI's script, validate it, then render. (Copying the reply is "
                       "enough - it's detected and pasted automatically.)");
    if (!g.editMsg.empty()) ImGui::TextColored(theme::LimeHi, "%s", g.editMsg.c_str());
    ImGui::Spacing();

    float logH = 0;
    if (rendering || renderDone) logH = 190.f * g.dpi;
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi + logH;

    ImGui::BeginDisabled(rendering);
    if (ImGui::Button("Paste from clipboard")) {
        const char* t = ImGui::GetClipboardText();
        if (t) {
            g.editText = t;
            g.lastAutoPasted = t;
            ValidateEditNow();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate")) ValidateEditNow();
    ImGui::SameLine();
    ImGui::TextDisabled("raw footage: %s", FormatTime(TotalSourceDuration()).c_str());

    float editorH = ImGui::GetContentRegionAvail().y - bottomH;
    float msgH = 0;
    if (g.validated) {
        msgH = 120.f * g.dpi;
        if (!g.script.questions.empty())
            msgH += (float)g.script.questions.size() * 58.f * g.dpi + 66.f * g.dpi;
        msgH = std::min(msgH, editorH * 0.6f);
    }
    if (InputTextMultilineStr("##edit", g.editText, ImVec2(-1, std::max(60.f, editorH - msgH))))
        g.validated = false;
    if (g.validated) {
        ImGui::BeginChild("##valmsg", ImVec2(0, msgH), ImGuiChildFlags_Borders);
        // one-click copy of the whole validation report (AI-ready when broken)
        {
            char sum[160];
            std::snprintf(sum, sizeof(sum), "Valid: %d timeline clip(s), %d op(s). Final video ~%s.",
                          (int)g.script.timeline.size(), (int)g.script.ops.size(),
                          FormatTime(g.script.finalDur).c_str());
            if (ImGui::SmallButton(g.script.errors.empty() ? "Copy report"
                                                           : "Copy errors for the AI")) {
                std::string rep;
                if (g.script.errors.empty()) {
                    rep = sum;
                    for (const auto& w : g.script.warnings) rep += "\nwarning: " + w;
                } else {
                    rep = "The AIVE_SCRIPT validator rejected the script:\n";
                    for (const auto& e : g.script.errors) rep += "- " + e + "\n";
                    for (const auto& w : g.script.warnings) rep += "- warning: " + w + "\n";
                    rep += "\nReply with ONLY the corrected full script in one code block "
                           "starting with !!!.";
                }
                ImGui::SetClipboardText(rep.c_str());
            }
            ImGui::SameLine();
            if (!g.script.errors.empty()) {
                ImGui::TextColored(theme::Bad, "%d error(s):", (int)g.script.errors.size());
            } else {
                ImGui::TextColored(theme::LimeHi, "%s", sum);
            }
        }
        for (const auto& e : g.script.errors)
            ImGui::TextColored(theme::Bad, "%s", e.c_str());
        for (const auto& w : g.script.warnings)
            ImGui::TextColored(theme::Warn, "warning: %s", w.c_str());
        if (!g.script.questions.empty()) {
            ImGui::Separator();
            ImGui::TextColored(theme::Warn,
                               "The AI asked %d critical question(s). Answer below, send the answers "
                               "back, then paste its revised script:",
                               (int)g.script.questions.size());
            for (int i = 0; i < (int)g.script.questions.size(); i++) {
                ImGui::PushID(i);
                ImGui::TextWrapped("Q%d: %s", i + 1, g.script.questions[i].c_str());
                ImGui::SetNextItemWidth(-20.f * g.dpi);
                if (i < (int)g.qAnswers.size())
                    InputTextStrHint("##ans", "your answer...", g.qAnswers[i]);
                ImGui::PopID();
            }
            if (ImGui::Button("Copy answers for the AI")) {
                std::string reply = "Answers to your questions about the script:\n";
                for (int i = 0; i < (int)g.script.questions.size(); i++) {
                    reply += "Q" + std::to_string(i + 1) + ": " + g.script.questions[i] + "\n";
                    std::string a = i < (int)g.qAnswers.size() ? g.qAnswers[i] : "";
                    reply += "A" + std::to_string(i + 1) + ": " +
                             (a.empty() ? "(no answer - use your best judgment)" : a) + "\n";
                }
                reply += "\nNow reply with ONLY the corrected, final script in one code block.";
                ImGui::SetClipboardText(reply.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("paste into the same AI chat, then paste the new script here");
        }
        ImGui::EndChild();
    }
    ImGui::EndDisabled();

    if (rendering || renderDone) {
        std::string stage, error;
        {
            std::lock_guard<std::mutex> lk(g.render.m);
            stage = g.render.stage;
            error = g.render.error;
        }
        ImGui::Spacing();
        if (rendering) {
            char overlay[256];
            long long fr = g.render.frame.load();
            double os = g.render.outSec.load();
            if (fr > 0)
                std::snprintf(overlay, sizeof(overlay), "%s  |  frame %lld  (%s out)",
                              stage.c_str(), fr, FormatTime(os).c_str());
            else
                std::snprintf(overlay, sizeof(overlay), "%s", stage.c_str());
            ImGui::ProgressBar(g.render.progress, ImVec2(-120.f * g.dpi, 0), overlay);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.f * g.dpi, 0))) CancelRender(g.render);
        } else if (g.render.success) {
            ImGui::TextColored(theme::LimeHi, "Render complete: %s", g.proj.OutputPath().c_str());
            if (ImGui::Button("Play video")) ShellOpen(g.proj.OutputPath());
            ImGui::SameLine();
            if (ImGui::Button("Open folder")) OpenInExplorer(g.proj.dir);
        } else {
            ImGui::TextColored(theme::Bad, "Render failed.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy error for the AI")) {
                std::string rep = "The render failed with this FFmpeg error:\n" + error;
                {
                    std::lock_guard<std::mutex> lk(g.render.m);
                    std::string tail = g.render.log.size() > 2500
                                           ? g.render.log.substr(g.render.log.size() - 2500)
                                           : g.render.log;
                    rep += "\n\nLog tail:\n" + tail;
                }
                rep += "\n\nReply with ONLY the corrected full script in one code block "
                       "starting with !!!.";
                ImGui::SetClipboardText(rep.c_str());
            }
            ImGui::TextWrapped("%s", error.c_str());
        }
        if (ImGui::CollapsingHeader("FFmpeg log")) {
            if (ImGui::SmallButton("Copy full log")) {
                std::lock_guard<std::mutex> lk(g.render.m);
                ImGui::SetClipboardText(g.render.log.c_str());
            }
            ImGui::BeginChild("##fflog", ImVec2(0, 110.f * g.dpi), ImGuiChildFlags_Borders);
            std::lock_guard<std::mutex> lk(g.render.m);
            ImGui::TextUnformatted(g.render.log.c_str());
            ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();
        }
    }

    bool canRender = g.validated && g.script.Valid() && !rendering && g.ffmpegOk;
    const char* why = "";
    if (!g.ffmpegOk) why = "FFmpeg is required";
    else if (rendering) why = "rendering...";
    else if (!g.validated) why = "click Validate first";
    else if (!g.script.Valid()) why = "fix the errors above";
    if (BottomBar("Render final video", canRender, why)) StartRenderNow();
}

// ---------------------------------------------------------------- header / shell
static void Header() {
    if (!g.hasProject) return;
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(nullptr, 23.f * g.dpi);
    ImGui::TextColored(theme::Lime, "%s", g.proj.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, 24.f * g.dpi);
    struct Step { const char* label; Screen s; };
    const Step steps[] = {
        { "1. Mark up", Screen::MarkUp },
        { "2. Prompt", Screen::Prompt },
        { "3. Edit + Render", Screen::Edit },
    };
    for (int i = 0; i < 3; i++) {
        if (i) { ImGui::SameLine(); ImGui::TextDisabled(">"); ImGui::SameLine(); }
        bool current = g.screen == steps[i].s;
        bool reachable = (int)steps[i].s <= (int)g.screen && !g.render.running;
        if (current) {
            ImGui::PushStyleColor(ImGuiCol_Button, theme::Lime);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::Lime);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::Lime);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.07f, 0.02f, 1.f));
            ImGui::Button(steps[i].label);
            ImGui::PopStyleColor(4);
        } else {
            ImGui::BeginDisabled(!reachable);
            if (ImGui::Button(steps[i].label)) {
                if (g.screen == Screen::MarkUp) {
                    if (g.tapping) { MciStop(); g.tapping = false; }
                    SaveTrimNow();
                    SaveMusicFile(g.proj, g.music, nullptr);
                }
                g.screen = steps[i].s;
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::SameLine();
    float w = 130.f * g.dpi;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - w));
    ImGui::BeginDisabled(g.render.running);
    if (ImGui::Button("Close project", ImVec2(w, 0))) {
        if (g.screen == Screen::MarkUp) SaveTrimNow();
        if (g.tapping) { MciStop(); g.tapping = false; }
        g.hasProject = false;
        g.screen = Screen::Project;
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Spacing();
}

void AppInit(HWND hwnd, ID3D11Device* device, float dpiScale) {
    g.hwnd = hwnd;
    g.dpi = dpiScale;
    ApplyTheme(dpiScale);
    TextureSetDevice(device);
    wchar_t prof[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
    std::string base = n ? WideToUtf8(prof) + "\\Videos" : "C:\\";
    strncpy(g.baseDir, base.c_str(), sizeof(g.baseDir) - 1);
    StartDetectFfmpeg();
}

void AppFrame() {
    DrainMainThreadQueue();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    Header();
    switch (g.screen) {
    case Screen::Project: ScreenProject(); break;
    case Screen::MarkUp:  ScreenMarkUp(); break;
    case Screen::Prompt:  ScreenPrompt(); break;
    case Screen::Edit:    ScreenEdit(); break;
    }
    ImGui::End();
}

void AppShutdown() {
    MciStop();
    if (g.hasProject && g.screen == Screen::MarkUp) {
        SaveTrimNow();
        SaveMusicFile(g.proj, g.music, nullptr);
    }
    ShutdownRender(g.render);
    {
        std::lock_guard<std::mutex> lk(pvM);
        pvStop = true;
    }
    pvCv.notify_one();
    if (pvThread.joinable()) pvThread.join();
    JobsShutdown();
    DrainMainThreadQueue();
    g.clips.clear();
    g.preview = nullptr;
}
