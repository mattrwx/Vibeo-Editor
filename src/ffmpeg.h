#pragma once
// Thin wrapper around ffmpeg.exe / ffprobe.exe (external processes).
#include <string>
#include <vector>
#include <cstdint>

struct MediaInfo {
    bool ok = false;
    std::string error;
    double duration = 0;      // seconds; 0 for still images
    int w = 0, h = 0;         // first video stream, if any
    double fps = 0;           // first video stream frame rate (0 = unknown)
    bool hasVideo = false;
    bool hasAudio = false;
};

// Locate ffmpeg/ffprobe: next to the exe, in .\ffmpeg\bin, or on PATH.
void DetectFfmpeg();
bool FfmpegAvailable();
bool FfprobeAvailable();
std::wstring FfmpegExe();    // full path or bare name usable with CreateProcess
std::wstring FfprobeExe();

// Blocking; call from a worker thread.
MediaInfo ProbeMedia(const std::string& path);

// Extract one frame at time t (seconds), scaled to targetW wide, as RGBA.
// fast=true snaps to the nearest keyframe (-noaccurate_seek -skip_frame nokey):
// far less decoding per seek, at the cost of frame accuracy.
// Blocking; call from a worker thread.
bool ExtractFrameRGBA(const std::string& path, double t, int targetW,
                      std::vector<uint8_t>& rgba, int& w, int& h, std::string* err,
                      bool fast = false);
