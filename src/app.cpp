#include "app.h"
#include "util.h"
#include "ffmpeg.h"
#include "project.h"
#include "edit.h"
#include "render.h"
#include "texture.h"

#include "imgui.h"

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
constexpr ImU32 TlRed = IM_COL32(215, 72, 56, 255);
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
enum class Screen { Project, Trim, MarkUp, Media, Music, Prompt, Edit };

struct Clip {
    uint64_t id = 0;
    std::string path;        // absolute source path
    std::string fileName;
    double playhead = 0;
    std::vector<TrimSection> secs;   // cut-mark sections covering [0..duration]
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

    // trim screen
    std::vector<Clip> clips;
    int sel = -1;
    int dragMode = 0;              // 0 none, 3 playhead, 100+i = dragging cut mark i
    TexPtr preview;
    uint64_t previewClipId = 0;
    double lastReqT = -1;
    std::string trimMsg;

    // trim scrub quality: preview is extracted at sourceWidth / scrubDiv
    int scrubDiv = 2;

    // media screen
    std::vector<MediaFile> media;
    double lastScanT = 0;

    // music screen
    MusicConfig music;
    bool musicProbed = true;   // false while the chosen file is being probed
    std::string musicMsg;

    // prompt screen
    std::string promptText;
    std::string promptMsg;

    // edit screen
    std::string editText;
    EditScript script;
    bool validated = false;
    RenderState render;
};
static AppState g;
static uint64_t g_nextClipId = 1;

// ---------------------------------------------------------------- preview worker
namespace {
std::mutex pvM;
std::condition_variable pvCv;
bool pvHasReq = false, pvStop = false, pvStarted = false;
struct { uint64_t clipId; std::string path; double t; int width; } pvReq;
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
        if (ExtractFrameRGBA(req.path, req.t, req.width, rgba, w, h, &err)) {
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

void RequestPreview(const Clip& c, double t) {
    int srcW = c.info.w > 0 ? c.info.w : 1920;
    int w = std::clamp(srcW / std::max(1, g.scrubDiv), 64, 3840);
    {
        std::lock_guard<std::mutex> lk(pvM);
        if (!pvStarted) { pvStarted = true; pvThread = std::thread(PreviewThreadMain); }
        pvReq = { c.id, c.path, t, w };
        pvHasReq = true;
    }
    pvCv.notify_one();
}
} // namespace

// ---------------------------------------------------------------- section model
// Make secs a sorted, contiguous cover of [0..duration].
static void NormalizeSections(Clip& c) {
    double dur = c.info.duration;
    if (dur <= 0) { c.secs = { TrimSection{ 0, 0, true } }; return; }
    std::vector<TrimSection> in;
    for (auto s : c.secs) {
        s.a = std::clamp(s.a, 0.0, dur);
        s.b = std::clamp(s.b, 0.0, dur);
        if (s.b - s.a > 0.02) in.push_back(std::move(s));
    }
    std::sort(in.begin(), in.end(),
              [](const TrimSection& x, const TrimSection& y) { return x.a < y.a; });
    std::vector<TrimSection> out;
    double cur = 0;
    for (auto& s : in) {
        if (s.a - cur > 0.05) out.push_back(TrimSection{ cur, s.a, false });
        if (s.a < cur) s.a = cur;
        if (s.b - s.a > 0.02) { out.push_back(s); cur = out.back().b; }
    }
    if (dur - cur > 0.05) out.push_back(TrimSection{ cur, dur, false });
    if (out.empty()) out.push_back(TrimSection{ 0, dur, true });
    out.front().a = 0;
    out.back().b = dur;
    for (size_t i = 1; i < out.size(); i++) out[i].a = out[i - 1].b;
    c.secs = std::move(out);
}

static void AddCutMark(Clip& c, double t) {
    for (size_t i = 0; i < c.secs.size(); i++) {
        TrimSection& s = c.secs[i];
        if (t > s.a + 0.05 && t < s.b - 0.05) {
            TrimSection right = s;      // inherits keep flag + transition note
            right.a = t;
            right.note.clear();
            s.b = t;
            s.trans.clear();            // the transition after now belongs to the right half
            c.secs.insert(c.secs.begin() + i + 1, std::move(right));
            return;
        }
    }
}

static void RemoveCutNear(Clip& c, double t) {
    int best = -1;
    double bd = 0.5;   // seconds of tolerance
    for (size_t i = 0; i + 1 < c.secs.size(); i++) {
        double d = std::abs(c.secs[i].b - t);
        if (d < bd) { bd = d; best = (int)i; }
    }
    if (best < 0) return;
    TrimSection& L = c.secs[best];
    TrimSection& R = c.secs[best + 1];
    L.b = R.b;
    L.keep = L.keep || R.keep;
    if (L.note.empty()) L.note = R.note;
    L.trans = R.trans;
    c.secs.erase(c.secs.begin() + best + 1);
}

static TrimSection* SectionAt(Clip& c, double t) {
    for (auto& s : c.secs)
        if (t >= s.a && t <= s.b) return &s;
    return c.secs.empty() ? nullptr : &c.secs.back();
}

static double ClipKeptDuration(const Clip& c) {
    double t = 0;
    for (const auto& s : c.secs)
        if (s.keep) t += s.b - s.a;
    return t;
}

static double TotalTrimmedDuration() {
    double t = 0;
    for (const auto& c : g.clips) t += ClipKeptDuration(c);
    return t;
}

static std::vector<TrimClip> FlatKeptSegments() {
    std::vector<TrimClip> v;
    for (const auto& c : g.clips)
        for (const auto& s : c.secs)
            if (s.keep) v.push_back({ c.path, s.a, s.b });
    return v;
}

static std::vector<TrimClipV2> ToTrimV2() {
    std::vector<TrimClipV2> v;
    for (const auto& c : g.clips) v.push_back({ c.path, c.secs });
    return v;
}

static void SaveTrimNow() {
    if (!g.hasProject || g.clips.empty()) return;
    std::string err;
    if (!SaveTrimFile(g.proj, ToTrimV2(), &err)) g.trimMsg = err;
}

// ---------------------------------------------------------------- async helpers
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
            if (ExtractFrameRGBA(path, t, 160, rgba, w, h, &err)) {
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
                NormalizeSections(*c);
                for (const auto& s : c->secs)
                    if (s.keep) { c->playhead = s.a; break; }
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

// ---------------------------------------------------------------- small UI helpers
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

static bool AllClipsReady(std::string* why) {
    if (g.clips.empty()) { if (why) *why = "add at least one video"; return false; }
    for (const auto& c : g.clips) {
        if (!c.probed) { if (why) *why = "still reading video info..."; return false; }
        if (!c.err.empty()) { if (why) *why = "remove clips marked red first"; return false; }
    }
    return true;
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

// ---------------------------------------------------------------- project open / prompt
static void StartProbeMusic();

static void EnterEditScreenLoadingMedia() {
    RescanMedia(true);
    g.screen = Screen::Edit;
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
    g.music = MusicConfig{};
    g.musicProbed = true;
    g.musicMsg.clear();
    if (LoadMusicFile(g.proj, g.music) && g.music.enabled()) StartProbeMusic();

    std::error_code ec;
    std::string err;
    std::vector<TrimClipV2> tc;
    if (LoadTrimFile(g.proj, tc, &err)) {
        for (const auto& t : tc) {
            AddClipPath(t.path);
            g.clips.back().secs = t.secs;
        }
    }
    bool hasTrim = fs::exists(fs::path(Utf8ToWide(g.proj.TrimFilePath())), ec);
    bool hasPrompt = fs::exists(fs::path(Utf8ToWide(g.proj.PromptFilePath())), ec);
    bool hasEdit = fs::exists(fs::path(Utf8ToWide(g.proj.EditFilePath())), ec);
    if (hasEdit || hasPrompt) {
        ReadTextFile(g.proj.PromptFilePath(), g.promptText);
        if (hasEdit) ReadTextFile(g.proj.EditFilePath(), g.editText);
        EnterEditScreenLoadingMedia();
        if (!hasEdit) g.screen = Screen::Prompt;
        g.promptMsg = "Loaded existing prompt file.";
    } else if (hasTrim) {
        fs::create_directories(fs::path(Utf8ToWide(g.proj.MediaDir())), ec);
        RescanMedia(true);
        g.screen = Screen::MarkUp;
    } else {
        g.screen = Screen::Trim;
    }
}

static void GeneratePromptNow() {
    RescanMedia(true);
    int W = 0, H = 0;
    if (!g.clips.empty() && g.clips[0].probed) { W = g.clips[0].info.w; H = g.clips[0].info.h; }
    g.promptText = GeneratePrompt(g.proj, ToTrimV2(), g.media, g.music, W, H);
    std::string err;
    WriteTextFile(g.proj.PromptFilePath(), g.promptText, &err);
    ImGui::SetClipboardText(g.promptText.c_str());
    g.promptMsg = err.empty()
        ? "Prompt generated, saved and COPIED TO YOUR CLIPBOARD. Paste it into your AI of choice."
        : ("Prompt copied to clipboard, but saving failed: " + err);
}

// ---------------------------------------------------------------- screens
static void ScreenProject() {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, 40.f * g.dpi);
    ImGui::TextColored(theme::Lime, "AI VIDEO EDITOR");
    ImGui::PopFont();
    ImGui::TextDisabled("trim it yourself  -  let an AI write the edit  -  render with one click");
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
            fs::create_directories(fs::path(Utf8ToWide(dir)), ec);
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
                g.projMsg.clear();
                g.screen = Screen::Trim;
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

// Timeline with cut marks: sections tinted by keep/remove, draggable yellow
// marks, right-click toggles a section, drag elsewhere scrubs.
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
    auto X = [&](double t) { return p0.x + (float)(t / dur) * sz.x; };
    auto T = [&](float x) { return std::clamp((double)(x - p0.x) / sz.x * dur, 0.0, dur); };

    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), IM_COL32(14, 15, 13, 255), 4);
    float barH = 5.f * g.dpi;
    float y0 = p0.y + barH + 2, y1 = p0.y + sz.y - 18.f * g.dpi;
    int n = (int)c.strip.size();
    if (n > 0) {
        float tw = sz.x / n;
        for (int i = 0; i < n; i++) {
            if (!c.strip[i]) continue;
            dl->AddImage((ImTextureID)c.strip[i]->ImId(),
                         ImVec2(p0.x + i * tw, y0), ImVec2(p0.x + (i + 1) * tw, y1));
        }
    }
    // section tint + keep/remove color bar on top
    for (const auto& s : c.secs) {
        float x0 = X(s.a), x1 = X(s.b);
        if (s.keep) {
            dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x1, p0.y + barH), theme::TlLime);
        } else {
            dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x1, p0.y + barH), theme::TlRed);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(5, 6, 4, 190));
        }
    }
    // cut marks (boundaries between sections)
    for (size_t i = 0; i + 1 < c.secs.size(); i++) {
        float x = X(c.secs[i].b);
        dl->AddRectFilled(ImVec2(x - 1.5f, p0.y), ImVec2(x + 1.5f, y1), theme::TlMark);
        dl->AddTriangleFilled(ImVec2(x - 6.f * g.dpi, p0.y), ImVec2(x + 6.f * g.dpi, p0.y),
                              ImVec2(x, p0.y + 9.f * g.dpi), theme::TlMark);
    }
    // playhead
    float px = X(c.playhead);
    dl->AddRectFilled(ImVec2(px - 1.5f, p0.y), ImVec2(px + 1.5f, p0.y + sz.y), theme::TlPlayhead);

    // interaction
    ImVec2 mp = ImGui::GetIO().MousePos;
    auto requestAt = [&](double t) {
        if (std::abs(t - g.lastReqT) > 0.001) {
            g.lastReqT = t;
            RequestPreview(c, t);
        }
    };
    if (ImGui::IsItemActivated()) {
        g.dragMode = 3;
        float grab = 8.f * g.dpi;
        for (size_t i = 0; i + 1 < c.secs.size(); i++) {
            if (std::abs(mp.x - X(c.secs[i].b)) < grab) { g.dragMode = 100 + (int)i; break; }
        }
    }
    if (ImGui::IsItemActive()) {
        double t = T(mp.x);
        if (g.dragMode >= 100) {
            size_t i = (size_t)(g.dragMode - 100);
            if (i + 1 < c.secs.size()) {
                t = std::clamp(t, c.secs[i].a + 0.05, c.secs[i + 1].b - 0.05);
                c.secs[i].b = t;
                c.secs[i + 1].a = t;
            }
        }
        c.playhead = t;
        requestAt(t);
    } else if (ImGui::IsItemDeactivated()) {
        g.dragMode = 0;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (TrimSection* s = SectionAt(c, T(mp.x))) s->keep = !s->keep;
    }
    if (ImGui::IsItemHovered() && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        AddCutMark(c, T(mp.x));
    }

    // labels under the strip
    char lbl[160];
    snprintf(lbl, sizeof(lbl), "%s  |  %d section(s), keeping %s of this clip",
             FormatTime(c.playhead).c_str(), (int)c.secs.size(),
             FormatTime(ClipKeptDuration(c)).c_str());
    dl->AddText(ImVec2(p0.x + 4, y1 + 2), IM_COL32(216, 226, 205, 255), lbl);
    std::string durs = FormatTime(dur);
    ImVec2 ts = ImGui::CalcTextSize(durs.c_str());
    dl->AddText(ImVec2(p0.x + sz.x - ts.x - 4, y1 + 2), IM_COL32(126, 136, 114, 255), durs.c_str());
}

static void ScreenTrim() {
    FfmpegBanner();
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi;

    // ---- left: clip list -------------------------------------------------
    ImGui::BeginChild("##left", ImVec2(300.f * g.dpi, -bottomH));
    ImGui::BeginDisabled(!g.ffprobeOk);
    if (ImGui::Button("+ Add videos...", ImVec2(-1, 0))) {
        std::vector<std::string> paths;
        if (OpenVideoFilesDialog(g.hwnd, paths))
            for (const auto& p : paths) AddClipPath(p);
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    int removeIdx = -1, moveFrom = -1, moveTo = -1;
    for (int i = 0; i < (int)g.clips.size(); i++) {
        Clip& c = g.clips[i];
        ImGui::PushID((int)c.id);
        bool selected = g.sel == i;
        std::string label = std::to_string(i + 1) + ". " + c.fileName;
        if (!c.err.empty()) ImGui::PushStyleColor(ImGuiCol_Text, theme::Bad);
        if (ImGui::Selectable(label.c_str(), selected,
                              ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(214.f * g.dpi, 0))) {
            g.sel = i;
            g.preview = nullptr;
            if (c.probed && c.err.empty()) RequestPreview(c, c.playhead);
        }
        if (!c.err.empty()) {
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.err.c_str());
        } else if (c.probed) {
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%dx%d, %s", c.path.c_str(), c.info.w, c.info.h,
                                  FormatTime(c.info.duration).c_str());
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
    if (g.clips.empty()) {
        ImGui::TextDisabled("Add your raw videos, then cut them\ninto sections on the right.");
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("Total kept: %s", FormatTime(TotalTrimmedDuration()).c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right: preview + timeline --------------------------------------
    ImGui::BeginChild("##right", ImVec2(0, -bottomH));
    if (g.sel >= 0 && g.sel < (int)g.clips.size()) {
        Clip& c = g.clips[g.sel];
        if (!c.probed) {
            ImGui::TextDisabled("Reading video info...");
        } else if (!c.err.empty()) {
            ImGui::TextColored(theme::Bad, "This file can't be used: %s", c.err.c_str());
        } else {
            float ctrlH = ImGui::GetFrameHeightWithSpacing() * 2 + 10.f * g.dpi;
            float tlH = 96.f * g.dpi + 8.f * g.dpi;
            ImVec2 pv = ImGui::GetContentRegionAvail();
            pv.y = std::max(80.f, pv.y - tlH - ctrlH);
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
            // controls under the timeline
            if (ImGui::Button("Cut at playhead (C)")) AddCutMark(c, c.playhead);
            ImGui::SameLine();
            if (ImGui::Button("Remove cut at playhead")) RemoveCutNear(c, c.playhead);
            ImGui::SameLine();
            if (ImGui::Button("Keep / remove this section")) {
                if (TrimSection* s = SectionAt(c, c.playhead)) s->keep = !s->keep;
            }
            ImGui::SameLine();
            auto step = [&](double d) {
                c.playhead = std::clamp(c.playhead + d, 0.0, c.info.duration);
                g.lastReqT = c.playhead;
                RequestPreview(c, c.playhead);
            };
            if (ImGui::Button("-1s")) step(-1);
            ImGui::SameLine();
            if (ImGui::Button("-1f")) step(-1.0 / 30.0);
            ImGui::SameLine();
            if (ImGui::Button("+1f")) step(1.0 / 30.0);
            ImGui::SameLine();
            if (ImGui::Button("+1s")) step(1);
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Scrub res:");
            ImGui::SameLine();
            {
                static const char* items = "1/1\0" "1/2\0" "1/4\0" "1/8\0" "1/16\0";
                static const int divs[] = { 1, 2, 4, 8, 16 };
                int idx = 1;
                for (int di = 0; di < 5; di++)
                    if (divs[di] == g.scrubDiv) idx = di;
                ImGui::SetNextItemWidth(80.f * g.dpi);
                if (ImGui::Combo("##scrubres", &idx, items)) {
                    g.scrubDiv = divs[idx];
                    g.lastReqT = c.playhead;
                    RequestPreview(c, c.playhead);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Preview resolution while scrubbing.\nLower = faster seeking. "
                                      "Does not affect the final render.");
            }
            ImGui::TextDisabled("Cut the video into sections, then choose what stays: green bar = kept, "
                                "red bar = removed. Right-click a section to toggle it. Drag the yellow "
                                "marks to fine-tune, drag anywhere else to scrub.");
        }
    } else {
        ImGui::TextDisabled("Select a clip on the left.");
    }
    ImGui::EndChild();

    std::string why;
    bool ready = AllClipsReady(&why) && g.ffmpegOk;
    if (ready && TotalTrimmedDuration() < 0.1) { ready = false; why = "everything is marked removed"; }
    if (!g.ffmpegOk && why.empty()) why = "FFmpeg is required";
    if (BottomBar("Next: mark up >", ready, why.c_str())) {
        SaveTrimNow();
        g.screen = Screen::MarkUp;
    }
    if (!g.trimMsg.empty()) ImGui::TextColored(theme::Bad, "%s", g.trimMsg.c_str());
}

static void ScreenMarkUp() {
    FfmpegBanner();
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi;

    ImGui::TextWrapped("Describe each kept section (what happens in it, what you want done with it) "
                       "and each transition between sections. Everything you write here is handed "
                       "to the AI as instructions. You can leave fields empty.");
    ImGui::Spacing();

    ImGui::BeginChild("##markup", ImVec2(0, -bottomH));
    int segNo = 0;
    TrimSection* prev = nullptr;
    for (auto& c : g.clips) {
        if (!c.probed || !c.err.empty()) continue;
        double dur = c.info.duration;
        for (auto& s : c.secs) {
            if (!s.keep) continue;
            segNo++;
            ImGui::PushID((void*)&s);
            if (prev) {
                ImGui::Indent(46.f * g.dpi);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::LimeHi, "transition %d -> %d",
                                   segNo - 1, segNo);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-20.f * g.dpi);
                InputTextStrHint("##trans",
                                 "e.g. hard cut is fine / add a whoosh sound / flash a title here...",
                                 prev->trans);
                ImGui::Unindent(46.f * g.dpi);
                ImGui::Spacing();
            }
            // thumbnail from the filmstrip nearest to the section middle
            TexPtr th;
            if (dur > 0 && !c.strip.empty()) {
                int idx = std::clamp((int)(((s.a + s.b) * 0.5 / dur) * kStripCount), 0,
                                     (int)c.strip.size() - 1);
                th = c.strip[idx];
            }
            ImVec2 tsz(88.f * g.dpi, 50.f * g.dpi);
            if (th) ImGui::Image((ImTextureID)th->ImId(), tsz);
            else ImGui::Dummy(tsz);
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("Section %d  -  %s  [%s .. %s]  (%s)", segNo, c.fileName.c_str(),
                        FormatTime(s.a).c_str(), FormatTime(s.b).c_str(),
                        FormatTime(s.b - s.a).c_str());
            ImGui::SetNextItemWidth(-20.f * g.dpi);
            InputTextStrHint("##note",
                             "what is this section / what should the AI do with it...",
                             s.note);
            ImGui::EndGroup();
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::PopID();
            prev = &s;
        }
    }
    if (segNo == 0)
        ImGui::TextColored(theme::Bad,
                           "No kept sections. Go back to Trim and keep at least one section.");
    ImGui::EndChild();

    if (BottomBar("Next: add media >", segNo > 0, segNo == 0 ? "nothing is kept" : "")) {
        SaveTrimNow();
        std::error_code ec;
        fs::create_directories(fs::path(Utf8ToWide(g.proj.MediaDir())), ec);
        RescanMedia(true);
        OpenInExplorer(g.proj.MediaDir());
        g.screen = Screen::Media;
    }
}

static void ScreenMedia() {
    FfmpegBanner();
    RescanMedia(false);
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi;

    ImGui::TextWrapped("Your trim was saved to \"%s\". Now drop any extra media the AI may use "
                       "(images, sound effects, music) into the media folder below. "
                       "This window refreshes automatically.",
                       g.proj.TrimFilePath().c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted(g.proj.MediaDir().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Open media folder")) OpenInExplorer(g.proj.MediaDir());
    ImGui::Spacing();

    ImGui::BeginChild("##mediatable", ImVec2(0, -bottomH));
    if (g.media.empty()) {
        ImGui::TextDisabled("No media yet. That's OK - the AI can still cut, speed up and add text.");
    } else if (ImGui::BeginTable("media", 4,
                                 ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.f * g.dpi);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.f * g.dpi);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 170.f * g.dpi);
        ImGui::TableHeadersRow();
        for (const auto& m : g.media) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.kind.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f KB", m.sizeBytes / 1024.0);
            ImGui::TableNextColumn();
            if (!m.probed) ImGui::TextDisabled("...");
            else if (m.kind == "image") ImGui::Text("%dx%d px", m.info.w, m.info.h);
            else if (m.kind == "audio" || m.kind == "video")
                ImGui::TextUnformatted(FormatTime(m.info.duration).c_str());
            else ImGui::TextDisabled("unsupported");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    bool probing = false;
    for (const auto& m : g.media)
        if (!m.probed && g.ffprobeOk && m.kind != "other") probing = true;
    if (BottomBar("Next: music >", !probing, probing ? "measuring media files..." : "")) {
        g.screen = Screen::Music;
    }
}

static void StartProbeMusic() {
    g.musicProbed = false;
    g.musicMsg.clear();
    std::string path = g.music.path;
    JobsPush([path]() {
        MediaInfo mi = ProbeMedia(path);
        PostToMainThread([path, mi]() {
            if (g.music.path != path) return;   // changed meanwhile
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

static void ScreenMusic() {
    FfmpegBanner();
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi;

    ImGui::TextWrapped("Optional: pick one music track to lay under the whole video. "
                       "The AI is told about it so it won't add its own music. "
                       "The track is trimmed to the video length and faded out at the end.");
    ImGui::Spacing();

    ImGui::BeginChild("##musicbody", ImVec2(0, -bottomH));
    if (g.music.enabled()) {
        std::string name = g.music.path;
        size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (!g.musicProbed)
            ImGui::Text("%s  (reading...)", name.c_str());
        else
            ImGui::Text("%s  (%s)", name.c_str(), FormatTime(g.music.duration).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", g.music.path.c_str());
        if (ImGui::Button("Change...")) {
            std::string path;
            if (OpenAudioFileDialog(g.hwnd, path)) {
                g.music.path = path;
                StartProbeMusic();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove music")) {
            g.music.path.clear();
            g.music.duration = 0;
            g.musicProbed = true;
        }
        ImGui::Spacing();
        float vol = (float)g.music.volume;
        ImGui::SetNextItemWidth(320.f * g.dpi);
        if (ImGui::SliderFloat("Music volume", &vol, 0.02f, 1.0f, "%.2f"))
            g.music.volume = vol;
        bool loop = g.music.loop;
        if (ImGui::Checkbox("Loop if shorter than the video", &loop)) g.music.loop = loop;
    } else {
        if (ImGui::Button("Choose music file...", ImVec2(240.f * g.dpi, 0))) {
            std::string path;
            if (OpenAudioFileDialog(g.hwnd, path)) {
                g.music.path = path;
                StartProbeMusic();
            }
        }
        ImGui::TextDisabled("No music selected - that's fine, you can skip this step.");
    }
    if (!g.musicMsg.empty())
        ImGui::TextColored(theme::Bad, "%s", g.musicMsg.c_str());

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::Lime);
    ImGui::SeparatorText("Audio balancing");
    ImGui::PopStyleColor();
    bool ab = g.music.autobalance;
    if (ImGui::Checkbox("Auto-balance the final mix (loudness normalization)", &ab))
        g.music.autobalance = ab;
    ImGui::TextDisabled("Evens out music, sound effects and the original audio so nothing\n"
                        "blows out or disappears. Applies whenever audio is mixed.");
    ImGui::EndChild();

    bool ready = g.musicProbed;
    if (BottomBar("Next: generate prompt >", ready, ready ? "" : "reading music file...")) {
        std::string err;
        SaveMusicFile(g.proj, g.music, &err);
        GeneratePromptNow();
        g.screen = Screen::Prompt;
    }
}

static void ScreenPrompt() {
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi;
    ImGui::TextWrapped("%s", g.promptMsg.empty() ? "Prompt ready." : g.promptMsg.c_str());
    ImGui::TextWrapped("Paste it into ChatGPT / Claude / your AI of choice. It will reply with a "
                       ".edit file - copy that reply and continue to the next step.");
    ImGui::Spacing();
    if (ImGui::Button("Copy prompt again")) {
        ImGui::SetClipboardText(g.promptText.c_str());
        g.promptMsg = "Copied to clipboard again.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Regenerate (rescan media)")) GeneratePromptNow();
    ImGui::SameLine();
    if (ImGui::Button("Open project folder")) OpenInExplorer(g.proj.dir);
    ImGui::Spacing();
    ImGui::BeginChild("##promptview", ImVec2(0, -bottomH), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(g.promptText.c_str());
    ImGui::EndChild();

    if (BottomBar("Next: paste the .edit >", !g.promptText.empty(), "")) {
        EnterEditScreenLoadingMedia();
    }
}

static void ScreenEdit() {
    FfmpegBanner();
    bool rendering = g.render.running;
    bool renderDone = g.render.done;

    ImGui::TextWrapped("Paste the .edit file your AI replied with, validate it, then render.");
    ImGui::Spacing();

    float logH = 0;
    if (rendering || renderDone) logH = 190.f * g.dpi;
    float bottomH = ImGui::GetFrameHeightWithSpacing() + 12.f * g.dpi + logH;

    ImGui::BeginDisabled(rendering);
    if (ImGui::Button("Paste from clipboard")) {
        const char* t = ImGui::GetClipboardText();
        if (t) { g.editText = t; g.validated = false; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate")) {
        RescanMedia(true);
        g.script = ParseEdit(g.editText, TotalTrimmedDuration(), g.media);
        g.validated = true;
        std::string err;
        WriteTextFile(g.proj.EditFilePath(), g.editText, &err);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("base duration: %s", FormatTime(TotalTrimmedDuration()).c_str());

    float editorH = ImGui::GetContentRegionAvail().y - bottomH;
    float msgH = 0;
    if (g.validated) msgH = 96.f * g.dpi;
    if (InputTextMultilineStr("##edit", g.editText, ImVec2(-1, std::max(60.f, editorH - msgH))))
        g.validated = false;
    if (g.validated) {
        ImGui::BeginChild("##valmsg", ImVec2(0, msgH), ImGuiChildFlags_Borders);
        if (!g.script.errors.empty()) {
            for (const auto& e : g.script.errors)
                ImGui::TextColored(theme::Bad, "%s", e.c_str());
        } else {
            ImGui::TextColored(theme::LimeHi,
                               "Valid: %d operation(s). Final video will be about %s long.",
                               (int)g.script.ops.size(), FormatTime(g.script.finalDur).c_str());
        }
        for (const auto& w : g.script.warnings)
            ImGui::TextColored(theme::Warn, "warning: %s", w.c_str());
        ImGui::EndChild();
    }
    ImGui::EndDisabled();

    // ---- render status ----------------------------------------------------
    if (rendering || renderDone) {
        std::string stage, error;
        {
            std::lock_guard<std::mutex> lk(g.render.m);
            stage = g.render.stage;
            error = g.render.error;
        }
        ImGui::Spacing();
        if (rendering) {
            ImGui::ProgressBar(g.render.progress, ImVec2(-120.f * g.dpi, 0), stage.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.f * g.dpi, 0))) CancelRender(g.render);
        } else if (g.render.success) {
            ImGui::TextColored(theme::LimeHi, "Render complete: %s",
                               g.proj.OutputPath().c_str());
            if (ImGui::Button("Play video")) ShellOpen(g.proj.OutputPath());
            ImGui::SameLine();
            if (ImGui::Button("Open folder")) OpenInExplorer(g.proj.dir);
        } else {
            ImGui::TextColored(theme::Bad, "Render failed.");
            ImGui::TextWrapped("%s", error.c_str());
        }
        if (ImGui::CollapsingHeader("FFmpeg log")) {
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
    if (BottomBar("Render final video", canRender, why)) {
        std::string err;
        WriteTextFile(g.proj.EditFilePath(), g.editText, &err);
        StartRender(g.render, g.proj, FlatKeptSegments(), g.script, g.media, g.music);
    }
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
        { "1. Trim", Screen::Trim },
        { "2. Mark up", Screen::MarkUp },
        { "3. Media", Screen::Media },
        { "4. Music", Screen::Music },
        { "5. Prompt", Screen::Prompt },
        { "6. Edit + Render", Screen::Edit },
    };
    for (int i = 0; i < 6; i++) {
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
                if (g.screen == Screen::Trim || g.screen == Screen::MarkUp) SaveTrimNow();
                if (g.screen == Screen::Music) SaveMusicFile(g.proj, g.music, nullptr);
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
        if (g.screen == Screen::Trim || g.screen == Screen::MarkUp) SaveTrimNow();
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
    case Screen::Trim:    ScreenTrim(); break;
    case Screen::MarkUp:  ScreenMarkUp(); break;
    case Screen::Media:   ScreenMedia(); break;
    case Screen::Music:   ScreenMusic(); break;
    case Screen::Prompt:  ScreenPrompt(); break;
    case Screen::Edit:    ScreenEdit(); break;
    }
    ImGui::End();
}

void AppShutdown() {
    if (g.hasProject && (g.screen == Screen::Trim || g.screen == Screen::MarkUp))
        SaveTrimNow();
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
