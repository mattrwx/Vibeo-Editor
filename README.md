# AI Video Editor

**You trim. Your AI writes the edit. FFmpeg renders it. No subscriptions, no API keys.**

A fast, folder-based video editor for Windows that outsources the creative editing to
*any* AI chatbot you already use (ChatGPT, Claude, Gemini, a local model — anything you
can paste text into). The app packages your footage, media, and instructions into one
prompt; the AI replies with a tiny `.edit` script; the app renders the final video.

![AI Video Editor](docs/screenshot.png)

## How it works

```
your raw videos ──> [1 Trim] ──> [2 Mark up] ──> [3 Media] ──> [4 Music] ──> [5 Prompt]
                                                                                 │
                                                                        copy-paste into your AI
                                                                                 │
final .mp4  <────────────────────── [6 Edit + Render]  <───────  AI replies with a .edit file
```

Everything lives in one project folder you can zip, move, or re-open later — the app
resumes at whatever step you left off.

## Requirements

- Windows 10/11 with DirectX 11
- **FFmpeg** (`ffmpeg.exe` + `ffprobe.exe`) — easiest install:

```bash
winget install Gyan.FFmpeg
```

then restart the app (or click **Re-detect**). FFmpeg can also live next to `aive.exe`
or in an `ffmpeg\bin\` folder beside it.

## Building from source

Toolchain: CMake + Ninja + clang++ (the MSYS2 `clang64` environment works out of the
box). Dear ImGui is vendored in `vendor/imgui` — no other dependencies.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Run `build\aive.exe`.

## Usage

### 1. Create a project

Pick a location and a project folder name, or **Open existing project folder** to
resume one — the app detects which step you were on from the files in the folder.

### 2. Trim

Add one or more raw videos (single track — they play back to back). Cut each video
into sections and choose what stays:

| Action | How |
|---|---|
| Scrub / preview | click or drag anywhere on the timeline |
| Add a cut mark | **Cut at playhead** button, or press **C** while hovering the timeline |
| Move a cut mark | drag the white marker |
| Remove a cut mark | **Remove cut at playhead** |
| Keep / remove a section | **right-click** the section (green bar = kept, red = removed) |
| Frame-accurate nudge | **-1s / -1f / +1f / +1s** buttons |
| Faster scrubbing | **Scrub res** dropdown (1/1 … 1/16 preview resolution) |

Reorder or delete whole clips in the list on the left. *Next* saves `{project}.trim`.

### 3. Mark up

Every **kept section** gets a note field, and every **transition** between kept
sections gets one too. This is where you direct the edit — everything you type is
handed to the AI as instructions:

> *"intro, me talking — tighten any dead air"*
> *transition: "add a whoosh and a quick zoom punch"*
> *"the beach shot — slow motion would be nice, add the title here"*

All fields are optional, but good notes are what make a good edit. Note: **the AI is
forbidden from adding text/titles unless one of your notes asks for it.**

### 4. Media

The app creates a `media\` folder and opens it in Explorer. Drop in anything the AI
may use: images/logos (PNG with transparency works best), sound effects, stingers.
The list refreshes live with image dimensions and sound durations.

### 5. Music

Optionally pick one background track — volume, loop-if-shorter, and an automatic
fade-out at the end. This screen also has **auto-balance** (loudness normalization of
the final mix, on by default) so music, effects and the original audio sit together
without anything blowing out.

### 6. Prompt → AI → Render

- The app generates `{project}.prompt` and **copies it to your clipboard**. It
  contains your timeline (with your notes), the media inventory, the music info, and
  the full `.edit` spec with examples.
- Paste it into your AI of choice. It replies with a `.edit` file.
- Back in the app: **Paste from clipboard**, **Validate** (clear, line-numbered
  errors — paste the errors back to your AI if you get any), then **Render**.

The result is `{project}_final.mp4` in the project folder, with a progress bar and
the full FFmpeg log while it renders.

## The .edit language

What the AI writes — one operation per line, `#` for comments:

```
AIVE_EDIT v1
cut from=0 to=1.2
speed from=45 to=60 rate=2.0
text "My Epic Day" start=0.8 end=3.5 x=0.5 y=0.18 size=64 color=#FFD700 font=impact bold=1 pop=both
overlay "logo.png" start=1 end=6 x=0.92 y=0.08 scale=0.10 opacity=0.6 pop=in
sound "whoosh.wav" at=3.4 volume=0.8
transition at=12 duration=0.6
zoom start=8 end=8.4 amount=1.6 mode=in
flicker start=8 end=8.5 frequency=10 amplitude=0.25
mute from=10 to=12
fadein duration=0.5
fadeout duration=1.0
```

| Op | What it does |
|---|---|
| `cut` | remove a span of the timeline |
| `speed` | speed up / slow down a span (0.25×–4×, pitch preserved) |
| `overlay` | image on top of the video, optional springy `pop=in\|out\|both` |
| `text` | styled text — 11 named Windows fonts, bold, color, pop animation |
| `sound` | mix a sound effect / audio file at a timestamp |
| `music` | (handled by the app's Music step — the AI is told not to add its own) |
| `transition` | dip to black/white for a chosen duration |
| `zoom` | eased zoom: `mode=in` (punch-in), `out`, or `pulse` — window length = speed |
| `flicker` | brightness oscillation with frequency + amplitude |
| `mute` | silence the original audio in a span |
| `fadein` / `fadeout` | fade from/to black at the ends |

All animations use easing curves (cubic ease for zooms, overshoot springs for pops) —
only the fades are linear. The full specification, with the BASE-vs-FINAL timeline
rules and worked examples, is embedded in every generated `.prompt`.

## Project folder layout

```
MyProject\
  MyProject.trim        your cut marks, keep/remove flags, and notes
  MyProject.music       background music choice + audio settings
  media\                images / sounds you provide to the AI
  MyProject.prompt      what you paste into the AI
  MyProject.edit        what the AI gave back (saved on Validate/Render)
  MyProject_final.mp4   the rendered result
  temp\                 intermediate files (auto-deleted on success)
```

Everything is plain text — you can hand-edit any of it.

## Tips

- **The notes are the steering wheel.** The AI only knows what your section and
  transition notes tell it. "boring part, speed through it" beats silence.
- Mixed resolutions and frame rates are fine — everything is normalized to the first
  clip's resolution at 30 fps. Clips without audio get a silent track automatically.
- You can iterate: tweak the `.edit` by hand (or ask the AI for changes), Validate,
  and Render again.
- Renders that use `zoom` take roughly twice as long — the whole video goes through
  a high-resolution zoom pass for smoothness.

## Troubleshooting

- **"FFmpeg not found" banner** — install FFmpeg (see Requirements), then restart or
  click Re-detect.
- **Validation errors** — they're line-numbered and specific (unknown media file,
  overlapping cuts, out-of-range time…). Paste them back to your AI and ask it to fix
  the `.edit`.
- **Render failed** — open the **FFmpeg log** section on the render screen for the
  exact command and error. Set the environment variable `AIVE_KEEP_TEMP=1` to keep
  the `temp\` intermediates for inspection.
