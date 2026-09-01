#include "project.h"
#include "util.h"

#include <algorithm>
#include <cstring>
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
        if (c == '\n') { o += "\\n"; continue; }
        if (c == '\r') continue;
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    o += '"';
    return o;
}

// Parse a quoted string starting at s[i] == '"'; supports \" \\ and \n escapes.
static bool ParseQuoted(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    out.clear();
    for (i++; i < s.size(); i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            out += (n == 'n') ? '\n' : n;
            continue;
        }
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

bool SaveTrimFile(const Project& p, const std::vector<TrimClipV2>& clips,
                  const std::string& overview, std::string* err) {
    std::ostringstream ss;
    ss << "AIVE_TRIM v2\n";
    ss << "project " << Quote(p.name) << "\n";
    if (!overview.empty()) ss << "overview " << Quote(overview) << "\n";
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
        for (const auto& m : c.markers) {
            std::snprintf(buf, sizeof(buf), "marker t=%.3f", m.t);
            ss << buf;
            if (!m.note.empty()) ss << " note=" << Quote(m.note);
            if (!m.media.empty()) ss << " media=" << Quote(m.media);
            ss << "\n";
        }
    }
    return WriteTextFile(p.TrimFilePath(), ss.str(), err);
}

bool LoadTrimFile(const Project& p, std::vector<TrimClipV2>& clips,
                  std::string* overview, std::string* err) {
    clips.clear();
    if (overview) overview->clear();
    std::string text;
    if (!ReadTextFile(p.TrimFilePath(), text)) {
        if (err) *err = "cannot read " + p.TrimFilePath();
        return false;
    }
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.rfind("overview ", 0) == 0 && overview) {
            size_t i = line.find('"');
            std::string ov;
            if (i != std::string::npos && ParseQuoted(line, i, ov)) *overview = ov;
        } else if (line.rfind("clip ", 0) == 0) {
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
        } else if (line.rfind("marker ", 0) == 0 && !clips.empty()) {
            TrimMarker m;
            m.t = KeyNum(line, "t", -1);
            KeyStr(line, "note", m.note);
            KeyStr(line, "media", m.media);
            if (m.t >= 0) clips.back().markers.push_back(m);
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
    if (!m.beats.empty()) {
        ss << "beats=";
        for (size_t i = 0; i < m.beats.size(); i++) {
            std::snprintf(buf, sizeof(buf), "%s%.3f", i ? "," : "", m.beats[i]);
            ss << buf;
        }
        ss << "\n";
    }
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
        } else if (line.rfind("beats=", 0) == 0) {
            const char* s = line.c_str() + 6;
            while (*s) {
                m.beats.push_back(std::atof(s));
                const char* comma = std::strchr(s, ',');
                if (!comma) break;
                s = comma + 1;
            }
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
be exactly three exclamation marks (it lets the app auto-detect your
reply on the user's clipboard), followed by the format header:

    !!!
    AIVE_EDIT v1

TIMING — read this carefully, it is simpler than it sounds:
  * `speed` NEVER changes the timeline. A sped span keeps its exact
    start, end and length (see the speed op for how). So with speed
    ops alone, every timestamp in section 1 stays exactly where it
    is — no remapping math at all.
  * `cut` is the ONLY operation that removes time: after a cut,
    later timestamps shift earlier by the cut's length. (A fast
    speed-up r>1 also absorbs a little footage past its span — see
    the op.) You will rarely use cut (see RULES).
  * All non-timeline operations (`overlay`, `object`, `text`,
    `sound`, `mute`, fades, zooms...) use FINAL seconds — which are
    IDENTICAL to section 1's times unless you cut.

--------------------------------------------------------------------
THE MODEL — you are programming a compositor
--------------------------------------------------------------------
Treat the edit as a small program, not a form to fill in. You have:

  * a TIMELINE you reshape with `cut` and `speed`
  * OBJECTS — every image, video clip or piece of text you place is
    an object sitting on a numbered LAYER, with properties (x, y,
    scale, rot, opacity, hue, sat, bright) you can set statically or
    DRIVE OVER TIME with keyframes and algebraic formulas
  * an AUDIO mix (`sound`, `mute`, `musicstart`)

The shortcut operations further down (zoom, flicker, transition,
pop, animate...) are macros. Anything they do — and plenty they
can't — you can build yourself from the primitives below: a flicker
is an opacity formula, a pop is three scale keyframes with an
overshoot curve, a motion tile is the same image declared as several
objects on different layers. When you want something custom, build
it from primitives. Go wild.

--------------------------------------------------------------------
PRIMITIVES
--------------------------------------------------------------------
object id=<name> src="<media-file>" in=<sec> out=<sec> [layer=<n>]
       x=<0..1> y=<0..1> scale=<0..1.5> [rot=<deg>] [opacity=<0..1>]
       [key=green|blue|#RRGGBB] [keysim=] [keyblend=]
       [corners=<0..0.5>] [glow=#RRGGBB]
object id=<name> text="<content>" in=<sec> out=<sec> [layer=<n>]
       x=<0..1> y=<0..1> [size=<px>] [font=<name>] [bold=1]
       [color=#RRGGBB] [glow=#RRGGBB] [opacity=<0..1>]
    Declares a layered element, visible from `in` to `out` (FINAL
    seconds). x,y is its CENTER as a fraction of the frame; `scale`
    is width as a fraction of the video width. `layer` sets stacking
    (higher = on top; default = declaration order). Media objects
    support chroma key, rounded-corner masking (`corners`), `glow`
    and static rotation. ids: lowercase letters/digits/_ only.

keyframe id=<name> prop=<property> t=<sec> v=<value> [curve=<name>]
    Sets a property's value at a time. Two or more keyframes on the
    same property interpolate between values — the curve eases INTO
    each keyframe (default: ease). Before the first keyframe the
    property holds the first value; after the last, the last.

expr id=<name> prop=<property> "<formula of t>"
    Drives a property with an algebraic formula evaluated every
    frame (t = FINAL seconds). Allowed: numbers, t, PI, + - * / ( ),
    and sin cos tan abs floor ceil trunc mod pow sqrt exp log min
    max clip if lt lte gt gte eq between sgn hypot random.
    A property takes keyframes OR an expr, not both.

Properties and ranges:
  x, y      [-1 .. 2]      position (fraction of frame)
  scale     [0.05 .. 3]    media objects only (animated scale uses
                           fast resampling — keep within ~0.5-2x of
                           the declared scale= for crispest results)
  rot       [-7200..7200]  degrees, media objects only
  opacity   [0 .. 1]
  hue       [-360 .. 360]  degrees of hue shift, media objects + base
  sat       [0 .. 3]       1 = normal, 0 = grayscale
  bright    [-1 .. 1]      0 = normal
Text objects animate x, y and opacity only. The special id `base`
is the main video itself and accepts hue / sat / bright — grade or
pulse the whole frame with it.

RECIPES — things you can build (invent your own!):
  strobe flicker:   expr id=clip1 prop=opacity "if(lt(mod(t*12,1),0.5),1,0.35)"
  smooth shimmer:   expr id=clip1 prop=opacity "0.75+0.25*sin(2*PI*6*t)"
  wiggle/shake:     expr id=logo prop=rot "6*sin(2*PI*2.5*t)"
  orbit:            expr id=logo prop=x "0.5+0.12*cos(2*PI*0.5*t)"
                    expr id=logo prop=y "0.5+0.12*sin(2*PI*0.5*t)"
  scale pop-in:     keyframe id=logo prop=scale t=5.0 v=0.02
                    keyframe id=logo prop=scale t=5.35 v=0.28 curve=backout
  drop + settle:    keyframe id=title prop=y t=2 v=-0.2
                    keyframe id=title prop=y t=2.5 v=0.2 curve=backout
  motion tile:      the same src= declared as 3+ objects on layers
                    1/2/3 with offset x/y and staggered keyframes
  beat-pulse base:  expr id=base prop=sat "1+0.6*pow(sin(PI*mod(t*2,1)),8)"
  spin-in reveal:   keyframe id=card prop=rot t=1 v=180
                    keyframe id=card prop=rot t=1.6 v=0 curve=backout
                    + opacity keyframes 0 -> 1 over the same window

--------------------------------------------------------------------
SHORTCUT OPERATIONS (macros — cut/speed are core, the rest are
conveniences built from the same machinery)
--------------------------------------------------------------------
cut from=<sec> to=<sec>
    Removes [from,to] of the BASE timeline. Use this to delete dead
    air, mistakes, or boring sections.

speed from=<sec> to=<sec> rate=<r>
    Changes playback speed WITHOUT changing the timeline: the span
    [from,to] keeps its exact start, end and length. r in
    [0.25 .. 4.0], audio pitch preserved. How the length stays put:
      r<1 (slow motion): the FIRST (to-from)*r seconds of the
        span's footage play slowed to fill the whole span; the
        span's leftover footage is dropped. Nothing before or after
        the span moves. This is the zero-math case — use it freely.
      r>1 (speed-up): the span consumes (to-from)*r of footage, so
        it swallows (r-1)*(to-from) seconds of what comes AFTER
        `to`; everything after that point shifts earlier by that
        amount. Prefer slow motion; only speed up when asked, and
        never let a fast span swallow a marker or a kill.

overlay "<media-file>" start=<sec> end=<sec> x=<0..1> y=<0..1> scale=<0..1>
        [opacity=<0..1>] [pop=in|out|both] [popdur=<sec>]
        [corners=<0..0.5>] [glow=#RRGGBB] [id=<name>]
        [key=green|blue|#RRGGBB] [keysim=<0.01..1>] [keyblend=<0..1>]
    Shows an image OR video from the media folder on top of the main
    video from `start` to `end` (FINAL seconds). x,y is the CENTER
    as a fraction of the frame (x=0.5 y=0.5 = dead center; y=0.15 =
    near the top). `scale` is the width as a fraction of the video
    width (0.25 = a quarter of the screen wide). `opacity` defaults
    to 1.0. `pop` animates on/off screen with a springy overshoot;
    `popdur` is each pop's length (default 0.35). `corners` rounds
    the corners (fraction of the smaller side; 0.5 = pill/circle).
    `glow` adds a soft colored halo. `key` removes a solid backdrop
    color (green screen!): key=green is #00FF00; raise `keysim`
    (default 0.25) if colored fringes remain, `keyblend` (default
    0.08) softens the edge. Video overlays play their frames from
    `start` (their audio is ignored) — a green-screened clip +
    key=green composites cleanly. GIFs do not sync to `start`.
    `id` names the element so `animate` can drive it (see CUSTOM
    ANIMATIONS below).

text "<content>" start=<sec> end=<sec> x=<0..1> y=<0..1> [size=<px>]
     [color=#RRGGBB] [font=<name>] [bold=1] [pop=in|out|both] [popdur=<sec>]
     [glow=#RRGGBB] [id=<name>]
    Draws text (with a subtle dark outline for readability). x,y is
    the center of the text block. `size` is font height in pixels
    (default 48). `color` defaults to #FFFFFF. Fonts available:
    impact, arial, georgia, comic, times, courier, consolas,
    verdana, tahoma, gabriola, segoe — add bold=1 for the bold cut
    (impact/gabriola have none). Pick a font that matches the vibe:
    impact = memes/punchy, georgia/times = classy, comic = playful,
    consolas/courier = techy. `pop` animates the text on/off screen
    with a springy overshoot. `glow` adds a soft colored halo behind
    the letters — great for titles over busy footage. The content
    must NOT contain double quotes; for apostrophes prefer the
    typographic one (’). One line of text per operation.

musicstart at=<sec>
    Only when background music with tapped beats exists: shifts the
    music so its FIRST tapped beat lands exactly at `at` (FINAL
    seconds). This is how you anchor the first drop to the first big
    moment WITHOUT touching the opening's length. After this, tapped
    beat k lands at: at + (beat_k - beat_1).

--------------------------------------------------------------------
LEGACY ANIMATION SHORTCUTS
--------------------------------------------------------------------
`animate` is an older two-point shortcut; keyframes and exprs above
do everything it does and more. `spin` (whole-frame rotation) is
still the only way to rotate the base video.

animate id=<name> prop=<x|y|rot> from=<sec> to=<sec> v0=<val> v1=<val>
        [curve=<name>]
    Animates one property from v0 to v1 across [from..to] (FINAL
    seconds). Before `from` it holds v0; after `to` it holds v1.
      prop=x / prop=y : position, fraction of the frame (like x=/y=)
      prop=rot        : rotation in degrees (overlays only, not text)
    One animate per property per id; combine x + y + rot on the same
    id for complex motion. Stacks with pop=.

spin from=<sec> to=<sec> degrees=<deg> [curve=<name>]
    Rotates the WHOLE frame across the window. Use multiples of 360
    for a seamless spin transition, and pair it with
    `zoom ... mode=pulse` over the same window to hide the corners.

Curves: linear, ease (smooth in-out, the default), easein, easeout,
backin, backout (overshoot), expoin, expoout, spike (_/\_), sine.
Never pick linear unless it truly should feel mechanical.

Example — an eased rotational transition at 12s:
  spin from=11.7 to=12.3 degrees=360 curve=ease
  zoom start=11.7 end=12.3 amount=1.5 mode=pulse
Example — a title that slides down with overshoot while fading in:
  text "ROUND 2" start=5 end=8 x=0.5 y=0.2 font=impact id=r2 pop=in
  animate id=r2 prop=y from=5 to=5.6 v0=-0.15 v1=0.2 curve=backout

question "<your question>"
    ONLY for something absolutely critical that you cannot decide
    from the information given (a contradiction in the notes, a
    missing file you were told to use, an ambiguous instruction that
    changes the whole edit). Put question lines at the TOP of the
    file and STILL produce your best-guess edit below them. The user
    will answer your questions and ask you for a revised .edit.
    Do not ask about taste — make a call. Most edits need zero
    questions.

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
      mode=pulse : smoothly in and back out within the window - sync
                   it to a beat or an impact.
      mode=spike : _/\_ - exponential rise to a sharp peak at the
                   middle of the window, then an exponential fall.
                   THE beat-drop hit; keep the window short (0.3-1s).

flicker start=<sec> end=<sec> [frequency=<Hz>] [amplitude=<0.02..1>]
    Rapid brightness oscillation. frequency defaults to 8 Hz,
    amplitude to 0.3. Use briefly (0.3-1s) for glitchy/intense
    moments; high amplitude + high frequency is very aggressive.

motionblur [strength=low|med|high]
    Adds optical-flow (RSMB-style) motion blur to the ENTIRE video:
    motion gets smooth directional smears — fast gameplay, whips and
    your zooms/spins/pops all benefit. strength defaults to med.
    One per edit; costs a LOT of render time, so use it when the
    footage is fast and kinetic, skip it for calm talking footage.
    Static elements are untouched; expect minor artifacts around
    HUD/overlay edges over fast backgrounds (that is normal for
    optical-flow blur).

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
   commentary before or after. The first line must be `!!!`.
2. NEVER use the `text` operation unless the user's notes in
   section 1 explicitly ask for text, a title, or a caption. No
   note asking for text means ZERO text operations.
3. Only reference media files listed in section 2, by exact file
   name (e.g. "boom.wav"). Do not invent files.
4. NEVER change the video's timing unless the user EXPLICITLY asked
   for it in the overview or notes. The user already trimmed this
   video exactly how they want it. No cuts "to tighten pacing", no
   speed changes for style — `cut` and `speed` appear ONLY when a
   note says things like "trim this", "speed this up", "slow-mo
   after the kill". If no note asks for timing changes, use zero
   cut/speed operations.
5. `cut`/`speed` ranges: within [0, BASE duration], no overlaps
   (remember a fast span also consumes footage after its `to`).
6. All other timestamps: within [0, FINAL duration]. FINAL equals
   the section-1 timeline unless you cut (or use r>1); slow motion
   never moves anything.
7. Times are decimal seconds (e.g. 12.5). Keep 1-3 decimals.
8. Prefer tasteful editing: don't bury the video in objects or
   effects; sync sounds/zooms to visual moments; a couple of
   well-placed effects beat a dozen random ones. But when the
   footage calls for something custom, BUILD it from primitives
   rather than settling for the nearest macro.
9. Layers: higher layer= renders on top. Give objects explicit
   layers when stacking matters.

--------------------------------------------------------------------
EXAMPLE .edit FILE
--------------------------------------------------------------------
!!!
AIVE_EDIT v1
# tighten the intro
cut from=0.0 to=1.2
speed from=45.0 to=60.0 rate=2.0
# the user's note asked for a title: built from primitives with a
# drop-and-settle move and a shimmer
object id=title text="My Epic Day" in=0.8 out=3.5 x=0.5 y=0.18 size=64 color=#FFD700 font=impact layer=10
keyframe id=title prop=y t=0.8 v=-0.2
keyframe id=title prop=y t=1.3 v=0.18 curve=backout
keyframe id=title prop=opacity t=0.8 v=0
keyframe id=title prop=opacity t=1.1 v=1 curve=easeout
# corner logo that breathes
object id=logo src="logo.png" in=1.0 out=6.0 x=0.92 y=0.08 scale=0.10 corners=0.2 layer=5
expr id=logo prop=opacity "0.5+0.1*sin(2*PI*0.8*t)"
sound "whoosh.wav" at=3.4 volume=0.8
transition at=12.0 duration=0.6
zoom start=8.0 end=8.4 amount=1.6 mode=in
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
                           const std::string& overview,
                           const std::vector<MediaFile>& media, const MusicConfig& music,
                           int W, int H, double fps) {
    if (fps < 10 || fps > 240) fps = 30;
    std::ostringstream ss;
    char buf[256];

    // flatten kept sections into the ordered BASE timeline
    struct Flat {
        std::string srcName;
        double len;
        const TrimSection* sec;
        const TrimClipV2* clip;
    };
    std::vector<Flat> flat;
    for (const auto& c : clips) {
        std::string fname = c.path;
        size_t slash = fname.find_last_of("\\/");
        if (slash != std::string::npos) fname = fname.substr(slash + 1);
        for (const auto& s : c.secs)
            if (s.keep) flat.push_back({ fname, s.b - s.a, &s, &c });
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

    if (!overview.empty()) {
        ss << "--------------------------------------------------------------------\n";
        ss << "0. THE USER'S OVERVIEW — the vision for this edit, in their words\n";
        ss << "--------------------------------------------------------------------\n";
        ss << overview << "\n\n";
        ss << "Everything below serves this vision. When any guideline conflicts\n";
        ss << "with the overview, the overview wins.\n\n";
    }

    ss << "--------------------------------------------------------------------\n";
    ss << "1. THE VIDEO YOU ARE EDITING (BASE timeline)\n";
    ss << "--------------------------------------------------------------------\n";
    std::snprintf(buf, sizeof(buf),
                  "Resolution %dx%d, %.6g fps, total duration %.3f seconds.\n", W, H, fps, total);
    ss << buf;
    ss << "The user cut " << clips.size() << " source video(s) into " << flat.size()
       << " kept segment(s), joined back to back in this order.\n"
       << "Every boundary between segments is currently a hard cut.\n"
       << "\"user note\" lines are the user's own descriptions and wishes —\n"
       << "treat them as creative instructions and follow them.\n"
       << "\"marker\" lines flag an EXACT moment inside a segment (a punchline,\n"
       << "an impact, a reveal) — anchor sounds/zooms/effects precisely there.\n\n";
    double t = 0;
    for (size_t i = 0; i < flat.size(); i++) {
        const Flat& f = flat[i];
        std::snprintf(buf, sizeof(buf),
                      "  segment %d (from \"%s\"): BASE [%.3f .. %.3f] (%.3f s)\n",
                      (int)i + 1, f.srcName.c_str(), t, t + f.len, f.len);
        ss << buf;
        if (!f.sec->note.empty())
            ss << "      user note: \"" << f.sec->note << "\"\n";
        for (const auto& mk : f.clip->markers) {
            if (mk.t < f.sec->a || mk.t > f.sec->b || mk.note.empty()) continue;
            std::snprintf(buf, sizeof(buf), "      marker at BASE %.3f: \"",
                          t + (mk.t - f.sec->a));
            ss << buf << mk.note << "\"\n";
        }
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
                          "  video  \"%s\"  %.2f s, %dx%d (usable as `overlay` — frames play "
                          "from `start`, audio ignored — or as a `sound` source)\n",
                          m.name.c_str(), m.probed ? m.info.duration : 0.0,
                          m.probed ? m.info.w : 0, m.probed ? m.info.h : 0);
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
        if (!music.beats.empty()) {
            ss << "\nBEAT SYNC: the user tapped along to the track and marked its hard\n";
            ss << "beats/drops. In TRACK time (seconds into the song):\n  ";
            for (size_t i = 0; i < music.beats.size(); i++) {
                std::snprintf(buf, sizeof(buf), "%s%.2f", i ? ", " : "", music.beats[i]);
                ss << buf;
            }
            ss << "\nRelative to the first beat (+0 = beat 1):\n  ";
            for (size_t i = 0; i < music.beats.size(); i++) {
                std::snprintf(buf, sizeof(buf), "%s+%.2f", i ? ", " : "",
                              music.beats[i] - music.beats[0]);
                ss << buf;
            }
            ss << "\n\nBEAT SYNC RULES — follow these exactly:\n";
            ss << "1. Do NOT retime ANY footage to chase beats. The user's trim is\n";
            ss << "   final; timing changes happen only when their notes explicitly\n";
            ss << "   ask (see RULES). Your only sync lever is the music itself.\n";
            ss << "2. Pick the first big moment of the video — the first marker /\n";
            ss << "   kill / impact — and anchor beat 1 to it with\n";
            ss << "   `musicstart at=<that FINAL time>`. The music shifts; the video\n";
            ss << "   does not.\n";
            ss << "3. After the anchor, beat k lands at: at + (beat_k - beat_1).\n";
            ss << "   Place your EFFECTS on the beats that land near action: zoom\n";
            ss << "   spikes, flickers, sounds, pops on whichever markers/kills fall\n";
            ss << "   close to a beat. Moments that don't line up just don't get a\n";
            ss << "   beat — that's fine.\n";
            ss << "4. WHAT belongs on beats: the user's MARKERS and the action itself\n";
            ss << "   — kills, impacts, punchlines, reveals. Do NOT just sprinkle\n";
            ss << "   transitions and cuts on beats — a transition on a beat is only\n";
            ss << "   right when a scene change belongs there anyway.\n";
            ss << "5. Slow motion never moves timestamps, so it never breaks your\n";
            ss << "   beat placement. Nothing to recompute.\n";
        }
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
