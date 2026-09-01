#include "project.h"
#include "util.h"

#include <sstream>
#include <cstdio>

// The AIVE_SCRIPT prompt: the AI is the editor AND director. It receives raw
// footage, markers and ideas, and writes a small program that composes the
// whole video.

static const char* kScriptSpec = R"SPEC(====================================================================
THE AIVE_SCRIPT LANGUAGE
====================================================================
Your reply is a PROGRAM, not a form. Write it in this order:

  1. `def` blocks — the effects you will need, built from property
     expressions (curves, oscillators, ramps — your own inventions).
  2. Objects — `clip`, `media`, `text`, `sound`. Declaration order
     of media/text = layer order (later = on top).
  3. `settings` — whole-video switches.
  4. `timeline { }` — the actual video: clips in playback order.
     THIS IS THE EDIT. You choose what footage is used, in what
     order, at what speed. Nothing exists until it's in the
     timeline.

Reply format: a single code block, first line `!!!`, second line
`AIVE_SCRIPT v1`, then the program. Comments with # or //.

--------------------------------------------------------------------
SYNTAX — exact rules, follow them to the letter
--------------------------------------------------------------------
* Every statement inside a block ends with a SEMICOLON:
    prop: value;      effectcall(args);      clipname;   (in timeline)
* Property syntax is `name: value;` — a COLON, never `=`.
* Names (objects, defs, params) are lowercase letters/digits/_ only.
* Strings (path:, content:) are double-quoted: path:"logo.png";
* Colors are #RRGGBB (no quotes): color:#FFD700; glow:#FF00FF;
* STATIC-ONLY properties take PLAIN NUMBERS, never expressions:
    src, from, to, speed, start, end, at, layer, size, bold,
    corners, keysim, keyblend, volume (on sound objects).
  Expressions belong ONLY on animatable properties (listed per
  object type below). `from: 5+2;` is an ERROR — write `from: 7;`.
* Two kinds of time, do not mix them up:
    - clip `from:`/`to:` are SOURCE seconds (into the raw file)
    - object `start:`/`end:`/`at:` are FINAL seconds — unless the
      object has `during: <clip>;`, which makes them CLIP-LOCAL
      (start:0 = the moment that clip begins). Prefer during: —
      it's immune to timeline math mistakes.
* A def must appear ABOVE its first use. Calling an undefined
  effect, or with the wrong number of arguments, is an error.
* An effect may only assign properties its target has (a def that
  sets `scale` cannot be called on a clip — clips have zoom).
* Unknown property names are errors. There are no transitions,
  masks or filters beyond what is documented here — build such
  looks from the primitives (see RECIPES).

--------------------------------------------------------------------
EFFECT DEFINITIONS — your toolbox, built by you
--------------------------------------------------------------------
def <name>(<params...>) {
  <prop>: <expression>;
  ...
}

Inside a def, each line assigns a property expression. `v` means
"the property's previous value", so effects COMPOSE — always write
`v + ...` or `v * ...` unless you deliberately replace the value:

  def shake(amp) {
    dx: v + rand(0.004*(amp));      # rand(s) = uniform in [-s, s]
    dy: v + rand(0.004*(amp));
  }
  def popin(d) {
    scale: v * backout(ramp(0, d)); # grow into place with overshoot
    opacity: v * ramp(0, d*0.6);
  }
  def punch(amt, d) {
    zoom: v * (1 + (amt)*spike(ramp(0, d)));
  }

Wrap parameters in parentheses inside expressions — (amp), (amt) —
so argument arithmetic can't break precedence. Arguments are
expressions themselves: `shake(t/5);` grows over the clip;
`punch(0.5, 0.6);` is a fixed hit. Effects apply top-down; later
assignments see earlier ones through `v`.

--------------------------------------------------------------------
OBJECTS
--------------------------------------------------------------------
clip <name> {                  # a span of RAW SOURCE footage
  src: 1;                      # source number from section 1
  from: 12.0; to: 18.5;        # SOURCE seconds (plain numbers!)
  speed: 1.0;                  # optional; duration = (to-from)/speed
  <clip props / effect calls>
}
  Clip ANIMATABLE properties: zoom (1 = none), rot (deg), hue (deg
  shift), sat (1 = normal, 0 = gray), bright (0 = normal, -1..1),
  dx, dy (frame displacement, fraction of frame), volume (this
  clip's own audio, 1 = normal).
  A clip's `t` runs 0 -> its duration; `dur` is that duration.
  speed changes duration: 2s of source at speed 0.5 plays for 4s.

media <name> {                 # image or video overlay
  path: "logo.png";
  during: intro;               # OR start:/end: in FINAL seconds
  start: 0.5; end: 3;          # with during:, these are clip-local
  x: 0.5; y: 0.5; scale: 0.25; # center position + width fraction
  key: green;                  # chroma key (+ keysim:, keyblend:)
  corners: 0.2;                # rounded corner mask, 0..0.5
  glow: #FF69B4; layer: 5;
  <props / effect calls>
}
  Media ANIMATABLE properties: x, y, scale, rot, opacity, hue,
  sat, bright. Video files play their frames from the moment the
  object starts (audio ignored). Omit start/end with during: to
  cover the whole clip.

text <name> {
  content: "ROUND 2";
  font: impact; size: 64; color: #FFD700; bold: 1; glow: #FF00FF;
  during/start/end/x/y/layer like media.
}
  Text ANIMATABLE properties: x, y, opacity — NOT scale, NOT rot.
  Fonts: impact, arial, georgia, comic, times, courier, consolas,
  verdana, tahoma, gabriola, segoe.

sound <name> {
  path: "boom.wav";
  during: kill1; at: 0.1;      # at: FINAL secs, clip-local w/ during:
  volume: 0.8;                 # plain number on sounds
}

settings {
  motionblur: med;   # ONLY if the user explicitly asked for motion
                     # blur — it multiplies render time severely.
                     # No request in the overview/notes = leave it out.
  fadein: 0.5; fadeout: 1.0;   # from/to black at the ends
  musicstart: 4.0;             # first tapped beat lands here
}

timeline { intro; kill1; kill2; outro; }
  Clips play back to back in this order. A clip's FINAL start time
  is the sum of the durations before it. Every declared clip should
  appear here (unused clips are ignored with a warning).

--------------------------------------------------------------------
EXPRESSIONS — the heart of the system
--------------------------------------------------------------------
Any ANIMATABLE property can be a number or an expression evaluated
every frame:
  t    seconds since this object/clip started (0 at its start)
  f    frames since it started
  T    absolute seconds in the final video
  dur  this object's duration in seconds
  v    the property's previous value (for composing)
Functions: sin cos tan abs floor ceil trunc mod pow sqrt exp log
min max clip(x,lo,hi) if(cond,a,b) lt lte gt gte eq between sgn
hypot random, plus these helpers:
  rand(s)          uniform random in [-s, s], new every frame
  ramp(a, b)       0 -> 1 linearly as t goes from a to b (clamped
                   outside, so it holds 0 before and 1 after)
  ease(p) easein(p) easeout(p) backin(p) backout(p) spike(p) smooth(p)
                   easing curves for a 0..1 progress p
                   (backout overshoots then settles; spike = _/\_)

The pattern for almost everything: CURVE(ramp(start, end)) scaled
and added/multiplied onto a property. Entrances use ramp(0, d);
exits use ramp(dur-d, dur). Examples:
  fade in 0.4s, out 0.5s:
    opacity: v * ramp(0,0.4) * (1-ramp(dur-0.5,dur));
  springy drop from above:
    y: 0.2 - 0.25*(1-backout(ramp(0,0.5)));
  slow drift right:        x: 0.5 + 0.02*smooth(ramp(0,dur));
  wobble:                  rot: v + 4*sin(2*PI*1.5*t);
  strobe flicker:          opacity: if(lt(mod(t*12,1),0.5),1,0.35);
  beat pulse (0.5s beat):  sat: 1 + 0.5*pow(sin(PI*mod(t*2,1)),8);

--------------------------------------------------------------------
RECIPES — for WHEN THE USER ASKS for these looks
--------------------------------------------------------------------
(These are how-tos, not suggestions. Use one only when the overview
or a marker note calls for that kind of look.)
* Kill punch-in:   def punch(a,d){ zoom: v*(1+(a)*spike(ramp(0,d))); }
                   then `punch(0.5, 0.6);` on the clip, timed so the
                   spike peak (middle of d) sits on the kill.
* Handheld cam:    dx/dy with small rand() — 0.002-0.006 amplitude.
* Dip to black between two clips (there is no transition op):
                   on clip A: bright: v - smooth(ramp(dur-0.3,dur));
                   on clip B: bright: v - (1-smooth(ramp(0,0.3)));
* Slow-mo aftermath: a separate clip with speed:0.5 right after the
                   kill clip, maybe sat/bright graded.
* Glitch moment:   hue: v + rand(40); dx: v + rand(0.01); over a
                   short clip, or gated with if(between(t,a,b),...).
* Watermark:       media, small scale, opacity:0.5, absolute
                   start/end covering the video, low layer.
* Pop-in logo:     popin() from the defs example above.
* Zoom creep:      zoom: v*(1 + 0.08*smooth(ramp(0,dur)));
Compose your own — that is the point of the language.

--------------------------------------------------------------------
RULES
--------------------------------------------------------------------
1. Output ONLY the program in one code block, first line `!!!`.
2. YOU own the CUT: choose spans, order, pacing. Build the video
   around the MARKERS — they are the moments that matter. Dead air
   between markers is yours to drop.
3. EFFECTS ARE OPT-IN. The user's overview and marker notes are
   your entire creative brief for effects. If nothing asks for
   zooms, shakes, grades, pops or any other effect, deliver a CLEAN
   edit: clips and a timeline, zero effects — exactly like EXAMPLE
   A below. Never decorate on your own initiative — an unrequested
   zoom is a bug, not flair. ENFORCEMENT: every def you write must
   carry a comment quoting the user words that requested it, e.g.
   `# requested: "zoom spike for every kill"`. If you cannot quote
   the overview or a note, you may not write the effect.
4. NEVER use `text` objects unless the user's overview/notes ask
   for text, a title, or a caption.
5. NEVER use `motionblur` unless the user explicitly asked for
   motion blur — it multiplies render time severely.
6. Only reference media files from section 2 by exact name; only
   source numbers from section 1. Clip from/to must lie inside the
   source's available seconds.
7. Static properties take plain numbers; expressions only on
   animatable properties. Every statement ends with `;`.
8. Effects must be defined above their first use, called with the
   exact parameter count, and only set properties their target has.
9. Taste: when effects ARE requested, a few strong, motivated moves
   beat constant noise. Match the energy of the footage and the
   user's overview.

COMMON MISTAKES — double-check before you reply:
  [ ] forgot the timeline { } block (the video would be empty)
  [ ] effects the user never asked for (zoom/shake/grades/pops)
  [ ] expression on a static prop (from/to/start/end/speed/at/...)
  [ ] `=` instead of `:`, or a missing semicolon
  [ ] scale/rot on a text object (text animates x, y, opacity only)
  [ ] absolute start/end used where during: times were intended
  [ ] effect called before its def, or with wrong argument count
  [ ] media file name that isn't exactly in section 2
  [ ] motionblur or text without the user asking
)SPEC";

static std::string BaseName(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string GeneratePromptScript(const Project& p, const std::vector<TrimClipV2>& clips,
                                 const std::vector<double>& srcDurations,
                                 const std::string& overview,
                                 const std::vector<MediaFile>& media,
                                 const MusicConfig& music, int W, int H, double fps) {
    if (fps < 10 || fps > 240) fps = 30;
    std::ostringstream ss;
    char buf[320];

    ss << "====================================================================\n";
    ss << "AI VIDEO DIRECTOR REQUEST — project \"" << p.name << "\"\n";
    ss << "====================================================================\n\n";
    ss << "You are the editor AND director of this video. You get raw footage,\n";
    ss << "markers and ideas; you reply with one " << p.name << ".edit program in the\n";
    ss << "AIVE_SCRIPT language (spec below) that composes the entire video —\n";
    ss << "the timeline, the pacing, the effects, all of it. Full control.\n\n";

    if (!overview.empty()) {
        ss << "--------------------------------------------------------------------\n";
        ss << "0. THE USER'S OVERVIEW — the vision, in their words\n";
        ss << "--------------------------------------------------------------------\n";
        ss << overview << "\n\n";
        ss << "Everything serves this vision; when in doubt, the overview wins.\n\n";
    }

    ss << "--------------------------------------------------------------------\n";
    ss << "1. RAW FOOTAGE (refer to these as src: 1, 2, ...)\n";
    ss << "--------------------------------------------------------------------\n";
    for (size_t i = 0; i < clips.size(); i++) {
        double d = i < srcDurations.size() ? srcDurations[i] : 0;
        std::snprintf(buf, sizeof(buf), "  source %d: \"%s\" — %.3f s available\n",
                      (int)i + 1, BaseName(clips[i].path).c_str(), d);
        ss << buf;
    }
    std::snprintf(buf, sizeof(buf), "\nOutput: %dx%d, %.6g fps.\n\n", W, H, fps);
    ss << buf;
    int nMarkers = 0;
    for (const auto& c : clips) nMarkers += (int)c.markers.size();
    if (nMarkers > 0) {
        ss << "MARKERS — the moments the user flagged (SOURCE seconds). These are\n";
        ss << "your anchor points: build the clips around them. Whatever the notes\n";
        ss << "ask for happens exactly there:\n";
        for (size_t i = 0; i < clips.size(); i++) {
            for (const auto& m : clips[i].markers) {
                std::snprintf(buf, sizeof(buf), "  source %d @ %.3f", (int)i + 1, m.t);
                ss << buf;
                if (!m.note.empty()) ss << ": \"" << m.note << "\"";
                if (!m.media.empty()) ss << "  [use media: \"" << m.media << "\" here]";
                ss << "\n";
            }
        }
        ss << "\n";
    } else {
        ss << "No markers were placed — pick the strongest moments yourself.\n\n";
    }

    ss << "--------------------------------------------------------------------\n";
    ss << "2. MEDIA LIBRARY (files in the project's media folder)\n";
    ss << "--------------------------------------------------------------------\n";
    int nListed = 0;
    for (const auto& m : media) {
        if (m.kind == "image") {
            std::snprintf(buf, sizeof(buf), "  image  \"%s\"  %dx%d px\n", m.name.c_str(),
                          m.probed ? m.info.w : 0, m.probed ? m.info.h : 0);
        } else if (m.kind == "audio") {
            std::snprintf(buf, sizeof(buf), "  sound  \"%s\"  %.2f s\n", m.name.c_str(),
                          m.probed ? m.info.duration : 0.0);
        } else if (m.kind == "video") {
            std::snprintf(buf, sizeof(buf),
                          "  video  \"%s\"  %.2f s, %dx%d (media overlay or sound source)\n",
                          m.name.c_str(), m.probed ? m.info.duration : 0.0,
                          m.probed ? m.info.w : 0, m.probed ? m.info.h : 0);
        } else {
            continue;
        }
        ss << buf;
        nListed++;
    }
    if (nListed == 0)
        ss << "  (none — no media/sound objects available)\n";
    ss << "\n";

    ss << "--------------------------------------------------------------------\n";
    ss << "3. BACKGROUND MUSIC & AUDIO\n";
    ss << "--------------------------------------------------------------------\n";
    if (music.enabled()) {
        std::string mname = BaseName(music.path);
        std::snprintf(buf, sizeof(buf),
                      "Background music: \"%s\" (%.1f s, volume %.2f%s) is mixed under the\n"
                      "whole video automatically. Do NOT add it yourself.\n",
                      mname.c_str(), music.duration, music.volume, music.loop ? ", looped" : "");
        ss << buf;
        if (!music.beats.empty()) {
            ss << "\nBEATS the user tapped (track seconds): ";
            for (size_t i = 0; i < music.beats.size(); i++) {
                std::snprintf(buf, sizeof(buf), "%s%.2f", i ? ", " : "", music.beats[i]);
                ss << buf;
            }
            ss << "\nRelative to beat 1: ";
            for (size_t i = 0; i < music.beats.size(); i++) {
                std::snprintf(buf, sizeof(buf), "%s+%.2f", i ? ", " : "",
                              music.beats[i] - music.beats[0]);
                ss << buf;
            }
            ss << "\nBEAT SYNC: anchor beat 1 to your first big moment with\n";
            ss << "`musicstart: <final sec>;` in settings — then beat k lands at\n";
            ss << "that time + (beat_k - beat_1). Since YOU choose every clip's\n";
            ss << "span and length, size your clips so markers/kills LAND on the\n";
            ss << "beats. That is the sync: action on beats, not transitions.\n";
        }
    } else {
        ss << "No background music is set. You may lay a music file from the\n";
        ss << "media library under the video with a `sound` object.\n";
    }
    if (music.autobalance)
        ss << "The final mix is loudness-normalized automatically.\n";
    ss << "\n";

    ss << kScriptSpec;

    ss << "\n--------------------------------------------------------------------\n";
    ss << "EXAMPLE A — the DEFAULT shape of a reply\n";
    ss << "overview was: \"trim to the good moments, a bit before each kill\n";
    ss << "and a little after, nothing fancy\" -> clips + timeline, NOTHING else:\n";
    ss << "--------------------------------------------------------------------\n";
    ss << "!!!\n";
    ss << "AIVE_SCRIPT v1\n";
    ss << "# markers were at src1 14.2 / 31.9 and src2 8.5 - each clip shows\n";
    ss << "# ~2s before the kill and ~1.5s after, per the overview\n";
    ss << "clip kill1 { src:1; from:12.2; to:15.7; }\n";
    ss << "clip kill2 { src:1; from:29.9; to:33.4; }\n";
    ss << "clip kill3 { src:2; from:6.5; to:10.0; }\n";
    ss << "timeline { kill1; kill2; kill3; }\n";
    ss << "\n";
    ss << "--------------------------------------------------------------------\n";
    ss << "EXAMPLE B — effects ONLY because this overview asked for them\n";
    ss << "overview was: \"zoom spike on every kill, handheld shake, slow-mo\n";
    ss << "after the last kill, pop the logo in during the intro\":\n";
    ss << "--------------------------------------------------------------------\n";
    ss << "!!!\n";
    ss << "AIVE_SCRIPT v1\n";
    ss << "# requested: \"handheld shake\"\n";
    ss << "def shake(amp) {\n";
    ss << "  dx: v + rand(0.004*(amp));\n";
    ss << "  dy: v + rand(0.004*(amp));\n";
    ss << "}\n";
    ss << "# requested: \"zoom spike on every kill\"\n";
    ss << "def killpunch() {\n";
    ss << "  zoom: v * (1 + 0.5*spike(ramp(0, 0.6)));\n";
    ss << "}\n";
    ss << "clip intro { src:1; from:0; to:5.2; shake(1); }\n";
    ss << "clip kill1 { src:1; from:11.4; to:16.0; killpunch(); shake(1); }\n";
    ss << "# requested: \"slow-mo after the last kill\"\n";
    ss << "clip slowmo { src:1; from:16.0; to:18.0; speed:0.5; }\n";
    ss << "# requested: \"pop the logo in during the intro\"\n";
    ss << "media logo {\n";
    ss << "  path:\"logo.png\"; during:intro; x:0.9; y:0.1; scale:0.12;\n";
    ss << "  opacity: v*backout(ramp(0,0.4));\n";
    ss << "}\n";
    ss << "timeline { intro; kill1; slowmo; }\n";
    ss << "\n";
    ss << "--------------------------------------------------------------------\n";
    ss << "YOUR TASK\n";
    ss << "--------------------------------------------------------------------\n";
    ss << "Direct this video. Choose the footage worth showing (markers first)\n";
    ss << "and compose the timeline. Add ONLY the effects the overview and notes\n";
    ss << "literally ask for — if they only ask for cutting, your whole program\n";
    ss << "is clips + timeline, like EXAMPLE A. Reply with ONLY the program in\n";
    ss << "one code block starting with !!!.\n";
    return ss.str();
}
