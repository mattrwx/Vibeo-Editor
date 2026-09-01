#pragma once
// Small utilities: UTF conversion, child processes, dialogs, background jobs.
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);

// ---------------------------------------------------------------- processes
struct ProcResult {
    bool started = false;
    int exitCode = -1;
    std::vector<uint8_t> out;   // raw stdout bytes
    std::string err;            // stderr as text
};
// Run a child process hidden, capture stdout (binary) and stderr (text).
ProcResult RunProcess(const std::vector<std::wstring>& args);

// Streaming variant: onLine is called for every line of stdout as it arrives.
// If onStarted is provided, the *caller* takes ownership of the process HANDLE
// passed to it (for cancellation via TerminateProcess) and must CloseHandle it
// after this function returns. Returns exit code, or -1 if launch failed.
int RunProcessStream(const std::vector<std::wstring>& args,
                     const std::function<void(const std::string& line)>& onLine,
                     std::string* errOut,
                     const std::function<void(void* hProcess)>& onStarted = nullptr);

// ---------------------------------------------------------------- dialogs / shell
bool OpenVideoFilesDialog(void* hwndOwner, std::vector<std::string>& outPaths);
bool OpenAudioFileDialog(void* hwndOwner, std::string& outPath);
bool OpenAnyMediaFilesDialog(void* hwndOwner, std::vector<std::string>& outPaths);
bool OpenFolderDialog(void* hwndOwner, std::string& outPath);
void OpenInExplorer(const std::string& path);   // open folder window
void ShellOpen(const std::string& path);        // open file with default app

// ---------------------------------------------------------------- background jobs
// One worker thread executing queued jobs in order.
void JobsPush(std::function<void()> fn);
void JobsShutdown();

// Queue a function to run on the main (UI) thread at the start of a frame.
void PostToMainThread(std::function<void()> fn);
void DrainMainThreadQueue();

// ---------------------------------------------------------------- misc
std::string FormatTime(double seconds);   // m:ss.mmm
double NowSeconds();
std::string ToLower(std::string s);
