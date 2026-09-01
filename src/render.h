#pragma once
// Staged FFmpeg render pipeline:
//   1. render each timeline clip (trim + normalize + speed, source fps kept)
//   2. concat -> base.mp4
//   3. composite: clip effects, objects, sounds, music, fades -> final.mp4
#include "project.h"
#include "edit.h"
#include "script.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct RenderState {
    std::atomic<bool> running{ false };
    std::atomic<bool> done{ false };
    std::atomic<bool> success{ false };
    std::atomic<bool> cancelRequested{ false };
    std::atomic<float> progress{ 0.f };
    std::atomic<long long> frame{ 0 };     // current frame of the running ffmpeg
    std::atomic<double> outSec{ 0.0 };     // seconds of output produced so far

    std::mutex m;            // guards the fields below
    std::string stage;       // human-readable stage label
    std::string error;
    std::string log;
    void* hProc = nullptr;   // HANDLE of the currently running ffmpeg

    std::thread worker;
};

// Starts the worker thread. The script's timeline defines the video; sources
// are the project's raw videos in order. All arguments are copied.
void StartRender(RenderState& st, const Project& p, std::vector<ScriptSource> sources,
                 EditScript script, std::vector<MediaFile> media, MusicConfig music);
void CancelRender(RenderState& st);
// Cancel (if needed) and join the worker. Call before app exit.
void ShutdownRender(RenderState& st);
