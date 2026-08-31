#pragma once
// Staged FFmpeg render pipeline:
//   1. trim + normalize each clip (resolution, 30fps, 48k stereo audio)
//   2. concat -> base.mp4
//   3. apply cut/speed -> base2.mp4 (skipped when there are none)
//   4. composite overlays/text/sounds/mutes/fades -> {name}_final.mp4
#include "project.h"
#include "edit.h"

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

    std::mutex m;            // guards the fields below
    std::string stage;       // human-readable stage label
    std::string error;
    std::string log;
    void* hProc = nullptr;   // HANDLE of the currently running ffmpeg

    std::thread worker;
};

// Starts the worker thread. clips/script/media/music are copied.
void StartRender(RenderState& st, const Project& p, std::vector<TrimClip> clips,
                 EditScript script, std::vector<MediaFile> media, MusicConfig music);
void CancelRender(RenderState& st);
// Cancel (if needed) and join the worker. Call before app exit.
void ShutdownRender(RenderState& st);
