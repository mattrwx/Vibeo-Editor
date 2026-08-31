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
};

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
    bool bold = false;     // Text
    std::string mode;      // Zoom: in|out|pulse; Transition: black|white
    std::string font;      // Text: named font ("" = default)
    std::string color = "#FFFFFF";
    std::string file;      // Overlay/Sound media file name (as found in media list)
    std::string text;      // Text content
};

struct EditScript {
    std::vector<EditOp> ops;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    double baseDur = 0;
    double finalDur = 0;   // after cut/speed
    bool Valid() const { return errors.empty() && !ops.empty(); }
};

EditScript ParseEdit(const std::string& text, double baseDuration,
                     const std::vector<MediaFile>& media);
