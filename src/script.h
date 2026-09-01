#pragma once
// AIVE_SCRIPT compiler: parses the AI-written script language (effect defs,
// objects, timeline) and compiles it into the render IR (EditScript).
#include "edit.h"
#include "project.h"

#include <string>
#include <vector>

struct ScriptSource {
    std::string path;      // absolute source video path
    double duration = 0;   // probed length (0 = unknown, skip range checks)
};

// sources = the project's raw videos in order (script refers to them by
// 1-based index via `src:`). fps is used for the `f` (frame) variable.
EditScript ParseScript(const std::string& text,
                       const std::vector<ScriptSource>& sources,
                       const std::vector<MediaFile>& media,
                       const MusicConfig* music, double fps);
