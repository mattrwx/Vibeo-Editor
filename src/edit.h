#pragma once
// Parser + validator for the AIVE_EDIT v1 format the AI produces.
#include "project.h"
#include <string>
#include <vector>

enum class OpType {
    Cut, Speed, Overlay, Text, Sound, Mute, FadeIn, FadeOut,
    Transition,   // dip to black/white centered on a time (a=at, b=duration)
    Zoom,         // eased zoom on the footage
    Flicker,      // brightness oscillation
    Animate,      // AI-built property animation targeting an id= overlay/text
    Spin,         // rotate the whole frame across a window
    MusicStart,   // anchor the first tapped beat at a FINAL time (a=at)
    MotionBlur,   // whole-video optical-flow motion blur (mode = low|med|high)
    Keyframe,     // primitive: set a property value at a time (a=t, v0=value)
    ExprAnim,     // primitive: drive a property with a formula of t (text=formula)
    ClipFx,       // base-video effect over a final window (animProp, a..b, text=expr)
};

bool ValidCurveName(const std::string& name);

// Whitelist-check an algebraic formula of t (numbers, + - * / ( ) , and a fixed
// function set). Returns false with a reason when anything else appears.
bool SanitizeExprFormula(const std::string& f, std::string* whyNot);

// Named easing curve -> ffmpeg expression in terms of progress expression P.
std::string CurveExprFor(const std::string& name, const std::string& P);

struct EditOp {
    OpType type;
    int line = 0;          // 1-based source line for error messages
    double a = 0, b = 0;   // from/start .. to/end (Sound: a=at; fades/Transition: b=duration)
    double rate = 1;       // Speed
    double x = 0.5, y = 0.5, scale = 0.25, opacity = 1;   // Overlay/Text position
    double volume = 1;     // Sound
    int size = 48;         // Text px
    double amount = 2;     // Zoom magnification
    double freq = 8;       // Flicker Hz
    double amp = 0.3;      // Flicker amplitude
    int pop = 0;           // Overlay/Text: 0 none, 1 in, 2 out, 3 both
    double popdur = 0.35;  // pop animation length (each side)
    double corners = 0;    // Overlay: rounded corners, fraction of smaller side [0..0.5]
    std::string glow;      // Overlay/Text: halo color "#RRGGBB" ("" = none)
    std::string keyColor;  // Overlay: chroma key color "#RRGGBB" ("" = none)
    double keySim = 0.25;  // chroma key similarity
    double keyBlend = 0.08;// chroma key edge blend
    std::string idName;    // Overlay/Text: id; Animate/Keyframe/ExprAnim: target id
    std::string animProp;  // Animate/Keyframe/ExprAnim: property name
    double v0 = 0, v1 = 0; // Animate: value range; Spin: v0=degrees; Keyframe: v0=value
    std::string curveName; // Animate/Spin/Keyframe easing curve
    int layer = 0;         // Overlay/Text: z-order (higher = on top)
    bool isObject = false; // declared via `object` (primitive system)
    double rot = 0;        // Overlay: static rotation in degrees
    bool bold = false;     // Text
    std::string mode;      // Zoom: in|out|pulse; Transition: black|white
    std::string font;      // Text: named font ("" = default)
    std::string color = "#FFFFFF";
    std::string file;      // Overlay/Sound media file name (as found in media list)
    std::string text;      // Text content
};

// One entry of the AI-built timeline: a span of a source file at a speed.
struct TimelineSeg {
    int srcIndex = 0;      // into the project's source list
    double from = 0, to = 0;
    double rate = 1;
    std::string clipName;
    double finalStart = 0; // computed: position in the final video
};

struct EditScript {
    std::vector<EditOp> ops;
    std::vector<TimelineSeg> timeline;    // the AI-composed video (script mode)
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> questions;   // critical questions the AI asked
    double baseDur = 0;
    double finalDur = 0;
    bool Valid() const { return errors.empty() && (!ops.empty() || !timeline.empty()); }
};

// music may be null; when given, musicstart is validated against its beats.
EditScript ParseEdit(const std::string& text, double baseDuration,
                     const std::vector<MediaFile>& media,
                     const MusicConfig* music = nullptr);
