#include "project.h"
#include "util.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>

namespace fs = std::filesystem;

static fs::path P(const std::string& utf8) { return fs::path(Utf8ToWide(utf8)); }

std::string MediaKindFromExt(const std::string& fileName) {
    size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos) return "other";
    std::string ext = ToLower(fileName.substr(dot + 1));
    static const char* img[] = { "png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff" };
    static const char* aud[] = { "mp3", "wav", "ogg", "flac", "m4a", "aac", "wma", "opus" };
    static const char* vid[] = { "mp4", "mov", "mkv", "avi", "webm", "m4v", "wmv", "ts", "mts", "flv", "3gp" };
    for (auto e : img) if (ext == e) return "image";
    for (auto e : aud) if (ext == e) return "audio";
    for (auto e : vid) if (ext == e) return "video";
    return "other";
}

std::string FontFileFor(const std::string& name, bool bold) {
    struct F { const char* key; const char* reg; const char* boldFile; };
    static const F fonts[] = {
        { "impact",   "impact.ttf",  nullptr },
        { "arial",    "arial.ttf",   "arialbd.ttf" },
        { "georgia",  "georgia.ttf", "georgiab.ttf" },
        { "comic",    "comic.ttf",   "comicbd.ttf" },
        { "times",    "times.ttf",   "timesbd.ttf" },
        { "courier",  "cour.ttf",    "courbd.ttf" },
        { "consolas", "consola.ttf", "consolab.ttf" },
        { "verdana",  "verdana.ttf", "verdanab.ttf" },
        { "tahoma",   "tahoma.ttf",  "tahomabd.ttf" },
        { "gabriola", "gabriola.ttf", nullptr },
        { "segoe",    "segoeui.ttf", "segoeuib.ttf" },
    };
    std::string lo = ToLower(name);
    for (const auto& f : fonts)
        if (lo == f.key)
            return std::string("C:/Windows/Fonts/") + ((bold && f.boldFile) ? f.boldFile : f.reg);
    return "";
}

bool ReadTextFile(const std::string& path, std::string& out) {
    std::ifstream f(P(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool WriteTextFile(const std::string& path, const std::string& text, std::string* err) {
    std::ofstream f(P(path), std::ios::binary | std::ios::trunc);
    if (!f) { if (err) *err = "cannot open for writing: " + path; return false; }
    f.write(text.data(), (std::streamsize)text.size());
    if (!f) { if (err) *err = "write failed: " + path; return false; }
    return true;
}

// ---------------------------------------------------------------- .trim
static std::string Quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    o += '"';
    return o;
}

// Parse a quoted string starting at s[i] == '"'; supports \" and \\ escapes.
static bool ParseQuoted(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    out.clear();
    for (i++; i < s.size(); i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) { out += s[++i]; continue; }
        if (c == '"') { i++; return true; }
        out += c;
    }
    return false;
}

static double KeyNum(const std::string& line, const char* key, double def) {
    std::string k = std::string(key) + "=";
    size_t p = line.find(k);
    return p == std::string::npos ? def : std::atof(line.c_str() + p + k.size());
}

static bool KeyStr(const std::string& line, const char* key, std::string& out) {
    std::string k = std::string(key) + "=\"";
    size_t p = line.find(k);
    if (p == std::string::npos) return false;
    size_t i = p + k.size() - 1;
    return ParseQuoted(line, i, out);
}

bool SaveTrimFile(const Project& p, const std::vector<TrimClipV2>& clips, std::string* err) {
    std::ostringstream ss;
    ss << "AIVE_TRIM v2\n";
    ss << "project " << Quote(p.name) << "\n";
    char buf[96];
    for (const auto& c : clips) {
        ss << "clip " << Quote(c.path) << "\n";
        for (const auto& s : c.secs) {
            std::snprintf(buf, sizeof(buf), "section start=%.3f end=%.3f keep=%d",
                          s.a, s.b, s.keep ? 1 : 0);
            ss << buf;
            if (!s.note.empty()) ss << " note=" << Quote(s.note);
            if (!s.trans.empty()) ss << " trans=" << Quote(s.trans);
            ss << "\n";
        }
    }
    return WriteTextFile(p.TrimFilePath(), ss.str(), err);
}

bool LoadTrimFile(const Project& p, std::vector<TrimClipV2>& clips, std::string* err) {
    clips.clear();
    std::string text;
    if (!ReadTextFile(p.TrimFilePath(), text)) {
        if (err) *err = "cannot read " + p.TrimFilePath();
        return false;
    }
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.rfind("clip ", 0) == 0) {
            size_t i = line.find('"');
            std::string path;
            if (i == std::string::npos || !ParseQuoted(line, i, path)) {
                if (err) *err += "bad clip line: " + line + "\n";
                continue;
            }
            TrimClipV2 c;
            c.path = path;
            // v1 back-compat: clip "path" in=X out=Y  -> one kept section
            size_t ip = line.find(" in=", i);
            size_t op = line.find(" out=", i);
            if (ip != std::string::npos && op != std::string::npos) {
                TrimSection s;
                s.a = std::atof(line.c_str() + ip + 4);
                s.b = std::atof(line.c_str() + op + 5);
                s.keep = true;
                if (s.b > s.a) c.secs.push_back(s);
            }
            clips.push_back(std::move(c));
        } else if (line.rfind("section ", 0) == 0 && !clips.empty()) {
            TrimSection s;
            s.a = KeyNum(line, "start", 0);
            s.b = KeyNum(line, "end", 0);
            s.keep = KeyNum(line, "keep", 1) != 0;
            KeyStr(line, "note", s.note);
            KeyStr(line, "trans", s.trans);
            if (s.b > s.a) clips.back().secs.push_back(s);
        }
    }
    return !clips.empty();
}

// ---------------------------------------------------------------- .music
bool SaveMusicFile(const Project& p, const MusicConfig& m, std::string* err) {
    std::ostringstream ss;
    ss << "AIVE_MUSIC v1\n";
    if (m.enabled()) ss << "file " << Quote(m.path) << "\n";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "volume=%.3f loop=%d autobalance=%d\n",
                  m.volume, m.loop ? 1 : 0, m.autobalance ? 1 : 0);
    ss << buf;
    return WriteTextFile(p.MusicFilePath(), ss.str(), err);
}

bool LoadMusicFile(const Project& p, MusicConfig& m) {
    std::string text;
    if (!ReadTextFile(p.MusicFilePath(), text)) return false;
    m = MusicConfig{};
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.rfind("file ", 0) == 0) {
            size_t i = line.find('"');
            std::string path;
            if (i != std::string::npos && ParseQuoted(line, i, path)) m.path = path;
        } else if (line.rfind("volume", 0) == 0) {
            m.volume = KeyNum(line, "volume", 0.20);
            m.loop = KeyNum(line, "loop", 1) != 0;
            m.autobalance = KeyNum(line, "autobalance", 1) != 0;
        }
    }
    m.volume = std::min(std::max(m.volume, 0.02), 1.0);
    return true;
}

// ---------------------------------------------------------------- media scan
std::vector<MediaFile> ScanMediaDir(const Project& p) {
    std::vector<MediaFile> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(P(p.MediaDir()), ec)) {
        if (!e.is_regular_file(ec)) continue;
        MediaFile m;
        m.name = WideToUtf8(e.path().filename().wstring());
        m.fullPath = WideToUtf8(e.path().wstring());
        m.sizeBytes = (uint64_t)e.file_size(ec);
        m.kind = MediaKindFromExt(m.name);
        out.push_back(std::move(m));
    }
    return out;
}

// ---------------------------------------------------------------- prompt
static const char* kEditSpec = R"SPEC(====================================================================
HOW TO WRITE A .edit FILE  (format: AIVE_EDIT v1)
====================================================================

A .edit file is plain text: one operation per line. Lines starting
with `#` are comments; blank lines are ignored. The FIRST line must
be exactly:

    AIVE_EDIT v1

TWO TIMELINES — read this carefully:
  * BASE timeline  = the trimmed video as described in section 1
    (before your edits). `cut` and `speed` use BASE seconds.
  * FINAL timeline = the video after your `cut` and `speed`
    operations are applied. ALL other operations (`overlay`, `text`,
    `sound`, `mute`, `fadein`, `fadeout`) use FINAL seconds.

Converting BASE -> FINAL for a base time T: subtract the length of
every cut segment that ends before T, and for every sped segment
[a,b] rate=r that ends before T add (b-a)/r - (b-a). Cuts and speed
ranges must not overlap each other.

--------------------------------------------------------------------
OPERATIONS
--------------------------------------------------------------------
cut from=<sec> to=<sec>
    Removes [from,to] of the BASE timeline. Use this to delete dead
    air, mistakes, or boring sections.

speed from=<sec> to=<sec> rate=<r>
    Speeds up (r>1) or slows down (r<1) [from,to] of the BASE
    timeline. Audio pitch is preserved. r must be in [0.25 .. 4.0].

overlay "<media-file>" start=<sec> end=<sec> x=<0..1> y=<0..1> scale=<0..1>
        [opacity=<0..1>] [pop=in|out|both] [popdur=<sec>]
    Shows an image from the media folder on top of the video from
    `start` to `end` (FINAL seconds). x,y is the CENTER of the image
    as a fraction of the frame (x=0.5 y=0.5 = dead center; y=0.15 =
    near the top). `scale` is the image width as a fraction of the
    video width (0.25 = a quarter of the screen wide). `opacity`
    defaults to 1.0. `pop` animates the image on/off screen with a
    springy overshoot curve (never linear); `popdur` is the length
    of each pop (default 0.35). Prefer still images (PNG with
    transparency works best); GIFs do not sync to `start`.

text "<content>" start=<sec> end=<sec> x=<0..1> y=<0..1> [size=<px>]
     [color=#RRGGBB] [font=<name>] [bold=1] [pop=in|out|both] [popdur=<sec>]
    Draws text (with a subtle dark outline for readability). x,y is
    the center of the text block. `size` is font height in pixels
    (default 48). `color` defaults to #FFFFFF. Fonts available:
    impact, arial, georgia, comic, times, courier, consolas,
    verdana, tahoma, gabriola, segoe — add bold=1 for the bold cut
    (impact/gabriola have none). Pick a font that matches the vibe:
    impact = memes/punchy, georgia/times = classy, comic = playful,
    consolas/courier = techy. `pop` animates the text on/off screen
    with a springy overshoot. The content must NOT contain double
    quotes; for apostrophes prefer the typographic one (’). One
    line of text per operation.

transition at=<sec> duration=<sec> [color=black|white]
    Briefly dips the video to black (or white), centered on `at`
    (FINAL seconds): fades out for duration/2 and back in for
    duration/2. Audio is unaffected. YOU choose the length - a
    0.4-0.8s dip reads as a scene change. Best at segment
    boundaries.

zoom start=<sec> end=<sec> amount=<1.05..4> [mode=in|out|pulse]
    Smoothly eased zoom on the footage (cubic ease-in-out, never
    linear). The window [start..end] controls the SPEED: a short
    window = fast punch, a long window = slow creep.
      mode=in    : 1x -> amount across the window, then snaps back;
                   put `end` right at a cut for a punch-in.
      mode=out   : starts at amount, eases back to 1x - good right
                   after a cut.
      mode=pulse : in and back out within the window - sync it to a
                   beat or an impact.

flicker start=<sec> end=<sec> [frequency=<Hz>] [amplitude=<0.02..1>]
    Rapid brightness oscillation. frequency defaults to 8 Hz,
    amplitude to 0.3. Use briefly (0.3-1s) for glitchy/intense
    moments; high amplitude + high frequency is very aggressive.

sound "<media-file>" at=<sec> [volume=<0..4>]
    Plays a sound/music file starting at `at` (FINAL seconds), mixed
    over the existing audio. `volume` defaults to 1.0. Audio playing
    past the end of the video is cut off.

mute from=<sec> to=<sec>
    Silences the ORIGINAL video audio between from and to (FINAL
    seconds). Does not affect `sound` operations.

fadein duration=<sec>
fadeout duration=<sec>
    Fade video+audio from/to black at the very start / very end.

--------------------------------------------------------------------
RULES
--------------------------------------------------------------------
1. Output ONLY the .edit file content, in a single code block, no
   commentary before or after.
2. NEVER use the `text` operation unless the user's notes in
   section 1 explicitly ask for text, a title, or a caption. No
   note asking for text means ZERO text operations.
3. Only reference media files listed in section 2, by exact file
   name (e.g. "boom.wav"). Do not invent files.
4. `cut`/`speed` ranges: within [0, BASE duration], no overlaps.
5. All other timestamps: within [0, FINAL duration]. Remember the
   FINAL duration changes if you cut or change speed!
6. Times are decimal seconds (e.g. 12.5). Keep 1-3 decimals.
7. Prefer tasteful editing: don't bury the video in overlays or
   effects; sync sounds/zooms to visual moments; a couple of
   well-placed effects beat a dozen random ones.

--------------------------------------------------------------------
EXAMPLE .edit FILE
--------------------------------------------------------------------
AIVE_EDIT v1
# tighten the intro
cut from=0.0 to=1.2
speed from=45.0 to=60.0 rate=2.0
# the user's note asked for a title here
text "My Epic Day" start=0.8 end=3.5 x=0.5 y=0.18 size=64 color=#FFD700 font=impact pop=both
fadein duration=0.5
overlay "logo.png" start=1.0 end=6.0 x=0.92 y=0.08 scale=0.10 opacity=0.6 pop=in
sound "whoosh.wav" at=3.4 volume=0.8
transition at=12.0 duration=0.6
zoom start=8.0 end=8.4 amount=1.6 mode=in
flicker start=8.0 end=8.5 frequency=10 amplitude=0.25
mute from=10.0 to=12.0
fadeout duration=1.0
)SPEC";

static std::string HumanSize(uint64_t b) {
    char buf[32];
    if (b >= 1024ull * 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.2f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024));
    else std::snprintf(buf, sizeof(buf), "%.0f KB", b / 1024.0);
    return buf;
}

std::string GeneratePrompt(const Project& p, const std::vector<TrimClipV2>& clips,
                           const std::vector<MediaFile>& media, const MusicConfig& music,
                           int W, int H) {
    std::ostringstream ss;
    char buf[256];

    // flatten kept sections into the ordered BASE timeline
    struct Flat {
        std::string srcName;
        double len;
        const TrimSection* sec;
    };
    std::vector<Flat> flat;
    for (const auto& c : clips) {
        std::string fname = c.path;
        size_t slash = fname.find_last_of("\\/");
        if (slash != std::string::npos) fname = fname.substr(slash + 1);
        for (const auto& s : c.secs)
            if (s.keep) flat.push_back({ fname, s.b - s.a, &s });
    }
    double total = 0;
    for (const auto& f : flat) total += f.len;

    ss << "====================================================================\n";
    ss << "AI VIDEO EDIT REQUEST — project \"" << p.name << "\"\n";
    ss << "====================================================================\n\n";
    ss << "You are an expert video editor. Read everything below, then reply\n";
    ss << "with a single " << p.name << ".edit file that edits this video well.\n\n";
    ss << "Project folder: " << p.dir << "\n";
    ss << "Trim file:      " << p.TrimFilePath() << "\n\n";

    ss << "--------------------------------------------------------------------\n";
    ss << "1. THE VIDEO YOU ARE EDITING (BASE timeline)\n";
    ss << "--------------------------------------------------------------------\n";
    std::snprintf(buf, sizeof(buf),
                  "Resolution %dx%d, 30 fps, total duration %.3f seconds.\n", W, H, total);
    ss << buf;
    ss << "The user cut " << clips.size() << " source video(s) into " << flat.size()
       << " kept segment(s), joined back to back in this order.\n"
       << "Every boundary between segments is currently a hard cut.\n"
       << "\"user note\" lines are the user's own descriptions and wishes —\n"
       << "treat them as creative instructions and follow them.\n\n";
    double t = 0;
    for (size_t i = 0; i < flat.size(); i++) {
        const Flat& f = flat[i];
        std::snprintf(buf, sizeof(buf),
                      "  segment %d (from \"%s\"): BASE [%.3f .. %.3f] (%.3f s)\n",
                      (int)i + 1, f.srcName.c_str(), t, t + f.len, f.len);
        ss << buf;
        if (!f.sec->note.empty())
            ss << "      user note: \"" << f.sec->note << "\"\n";
        if (i + 1 < flat.size()) {
            std::snprintf(buf, sizeof(buf), "  transition segment %d -> %d at BASE %.3f",
                          (int)i + 1, (int)i + 2, t + f.len);
            ss << buf;
            if (!f.sec->trans.empty())
                ss << "  — user note: \"" << f.sec->trans << "\"";
            ss << "\n";
        }
        t += f.len;
    }
    ss << "\nSegment boundaries are natural scene changes — good spots for cuts,\n";
    ss << "sounds and text.\n\n";

    ss << "--------------------------------------------------------------------\n";
    ss << "2. AVAILABLE MEDIA (files in the project's media folder)\n";
    ss << "--------------------------------------------------------------------\n";
    int nListed = 0;
    for (const auto& m : media) {
        if (m.kind == "image") {
            if (m.probed && m.info.w > 0)
                std::snprintf(buf, sizeof(buf), "  image  \"%s\"  %dx%d px, %s\n",
                              m.name.c_str(), m.info.w, m.info.h, HumanSize(m.sizeBytes).c_str());
            else
                std::snprintf(buf, sizeof(buf), "  image  \"%s\"  %s\n",
                              m.name.c_str(), HumanSize(m.sizeBytes).c_str());
        } else if (m.kind == "audio") {
            if (m.probed && m.info.duration > 0)
                std::snprintf(buf, sizeof(buf), "  sound  \"%s\"  %.2f s, %s\n",
                              m.name.c_str(), m.info.duration, HumanSize(m.sizeBytes).c_str());
            else
                std::snprintf(buf, sizeof(buf), "  sound  \"%s\"  %s\n",
                              m.name.c_str(), HumanSize(m.sizeBytes).c_str());
        } else if (m.kind == "video") {
            std::snprintf(buf, sizeof(buf),
                          "  video  \"%s\"  %.2f s (usable ONLY as a `sound` source)\n",
                          m.name.c_str(), m.probed ? m.info.duration : 0.0);
        } else {
            continue;   // unknown types are not usable
        }
        ss << buf;
        nListed++;
    }
    if (nListed == 0)
        ss << "  (none — do not use overlay/sound operations that need files)\n";
    ss << "\n";

    ss << "--------------------------------------------------------------------\n";
    ss << "3. BACKGROUND MUSIC & AUDIO\n";
    ss << "--------------------------------------------------------------------\n";
    if (music.enabled()) {
        std::string mname = music.path;
        size_t slash = mname.find_last_of("\\/");
        if (slash != std::string::npos) mname = mname.substr(slash + 1);
        std::snprintf(buf, sizeof(buf),
                      "The user already chose background music: \"%s\" (%.1f s, volume %.2f%s).\n",
                      mname.c_str(), music.duration, music.volume,
                      music.loop ? ", looped" : "");
        ss << buf;
        ss << "It is mixed under the whole video automatically. Do NOT add your\n";
        ss << "own music with `sound` — only short effects.\n";
    } else {
        ss << "No background music is set. You may lay a music file from the\n";
        ss << "media list (if any) under the video using `sound`.\n";
    }
    if (music.autobalance)
        ss << "The final mix is loudness-normalized automatically, so don't\n"
              "worry about overall levels — just keep `volume` values sane.\n";
    ss << "\n";

    ss << kEditSpec;
    ss << "\n";
    ss << "--------------------------------------------------------------------\n";
    ss << "YOUR TASK\n";
    ss << "--------------------------------------------------------------------\n";
    std::snprintf(buf, sizeof(buf),
                  "Produce a creative, tasteful edit of this %.1f-second video using\n", total);
    ss << buf;
    ss << "the operations and media above. Tighten pacing with cuts/speed where\n";
    ss << "it helps, add titles/overlays/sounds where they add value, and above\n";
    ss << "all follow the user's segment and transition notes from section 1.\n";
    ss << "Reply with ONLY the .edit file content in one code block.\n";
    return ss.str();
}
