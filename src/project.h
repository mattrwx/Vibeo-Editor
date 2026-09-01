#pragma once
// Project folder model: {root}\{name}\ contains {name}.trim, media\,
// {name}.prompt, {name}.edit, temp\, {name}_final.mp4
#include "ffmpeg.h"
#include <string>
#include <vector>
#include <cstdint>

// One contiguous span of a source video between two cut marks.
struct TrimSection {
    double a = 0, b = 0;
    bool keep = true;
    std::string note;    // user note about this section (kept sections)
    std::string trans;   // user note about the transition AFTER this section
};

// A frame-exact moment the user flagged inside a source video.
struct TrimMarker {
    double t = 0;        // source-local time
    std::string note;
    std::string media;   // optional attached media file name (in media/)
};

// A source video divided into sections by cut marks (sections cover [0..dur]).
struct TrimClipV2 {
    std::string path;    // absolute source path (utf8)
    std::vector<TrimSection> secs;
    std::vector<TrimMarker> markers;
};

// Flat render segment: one kept span of one source file.
struct TrimClip {
    std::string path;    // absolute source path (utf8)
    double in = 0, out = 0;
};

struct MediaFile {
    std::string name;      // file name only, e.g. "boom.wav"
    std::string fullPath;
    uint64_t sizeBytes = 0;
    std::string kind;      // "image" | "audio" | "video" | "other"
    bool probed = false;
    MediaInfo info;
};

struct Project {
    std::string dir;   // absolute project folder (utf8)
    std::string name;  // folder name == project name

    std::string TrimFilePath() const   { return dir + "\\" + name + ".trim"; }
    std::string MusicFilePath() const  { return dir + "\\" + name + ".music"; }
    std::string PromptFilePath() const { return dir + "\\" + name + ".prompt"; }
    std::string EditFilePath() const   { return dir + "\\" + name + ".edit"; }
    std::string MediaDir() const       { return dir + "\\media"; }
    std::string TempDir() const        { return dir + "\\temp"; }
    std::string OutputPath() const     { return dir + "\\" + name + "_final.mp4"; }
};

// Background music laid under the whole video, chosen in its own step.
struct MusicConfig {
    std::string path;          // absolute path; empty = no music
    double volume = 0.20;
    bool loop = true;          // loop if shorter than the video
    bool autobalance = true;   // loudness-normalize the final mix
    double duration = 0;       // probed length (informational)
    std::vector<double> beats; // user-tapped beat/drop times (seconds into the track)
    bool enabled() const { return !path.empty(); }
};

std::string MediaKindFromExt(const std::string& fileName);

// Named font -> "C:/Windows/Fonts/xxx.ttf"; "" if the name is unknown.
std::string FontFileFor(const std::string& name, bool bold);

bool SaveMusicFile(const Project& p, const MusicConfig& m, std::string* err);
bool LoadMusicFile(const Project& p, MusicConfig& m);   // false if no .music file

bool SaveTrimFile(const Project& p, const std::vector<TrimClipV2>& clips,
                  const std::string& overview, std::string* err);
// Understands both the v2 section format and the old v1 in/out format.
bool LoadTrimFile(const Project& p, std::vector<TrimClipV2>& clips,
                  std::string* overview, std::string* err);

// List files in media\ (top level only). Does not probe.
std::vector<MediaFile> ScanMediaDir(const Project& p);

// Build the full prompt text. Media entries should already be probed where possible.
std::string GeneratePrompt(const Project& p, const std::vector<TrimClipV2>& clips,
                           const std::string& overview,
                           const std::vector<MediaFile>& media, const MusicConfig& music,
                           int W, int H, double fps);

// v2 prompt for the AIVE_SCRIPT model: raw sources + markers + ideas; the AI
// composes the whole video. srcDurations pairs with clips (probed lengths).
std::string GeneratePromptScript(const Project& p, const std::vector<TrimClipV2>& clips,
                                 const std::vector<double>& srcDurations,
                                 const std::string& overview,
                                 const std::vector<MediaFile>& media,
                                 const MusicConfig& music, int W, int H, double fps);

bool ReadTextFile(const std::string& path, std::string& out);
bool WriteTextFile(const std::string& path, const std::string& text, std::string* err);
