#include "util.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <cstdio>

// ---------------------------------------------------------------- UTF
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string a(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), a.data(), n, nullptr, nullptr);
    return a;
}

// ---------------------------------------------------------------- processes
// Windows command-line quoting rules (see MS docs on CommandLineToArgvW).
static std::wstring BuildCmdLine(const std::vector<std::wstring>& args) {
    std::wstring cl;
    for (const auto& a : args) {
        if (!cl.empty()) cl += L' ';
        bool needQuote = a.empty() || a.find_first_of(L" \t\"") != std::wstring::npos;
        if (!needQuote) { cl += a; continue; }
        cl += L'"';
        size_t bs = 0;
        for (wchar_t c : a) {
            if (c == L'\\') { bs++; continue; }
            if (c == L'"') { cl.append(bs * 2 + 1, L'\\'); cl += L'"'; bs = 0; continue; }
            cl.append(bs, L'\\'); bs = 0;
            cl += c;
        }
        cl.append(bs * 2, L'\\');
        cl += L'"';
    }
    return cl;
}

struct ChildPipes {
    HANDLE outR = nullptr, errR = nullptr, hProc = nullptr, hThread = nullptr;
    bool ok = false;
};

static ChildPipes LaunchChild(const std::vector<std::wstring>& args) {
    ChildPipes cp;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE outW = nullptr, errW = nullptr;
    if (!CreatePipe(&cp.outR, &outW, &sa, 0)) return cp;
    if (!CreatePipe(&cp.errR, &errW, &sa, 0)) { CloseHandle(cp.outR); CloseHandle(outW); return cp; }
    SetHandleInformation(cp.outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(cp.errR, HANDLE_FLAG_INHERIT, 0);
    HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outW;
    si.hStdError = errW;
    si.hStdInput = nulIn;

    PROCESS_INFORMATION pi{};
    std::wstring cl = BuildCmdLine(args);
    BOOL okc = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(outW);
    CloseHandle(errW);
    if (nulIn && nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
    if (!okc) {
        CloseHandle(cp.outR); CloseHandle(cp.errR);
        cp.outR = cp.errR = nullptr;
        return cp;
    }
    cp.hProc = pi.hProcess;
    cp.hThread = pi.hThread;
    cp.ok = true;
    return cp;
}

ProcResult RunProcess(const std::vector<std::wstring>& args) {
    ProcResult r;
    ChildPipes cp = LaunchChild(args);
    if (!cp.ok) return r;
    r.started = true;

    std::thread errThread([&]() {
        char buf[4096];
        DWORD n;
        while (ReadFile(cp.errR, buf, sizeof(buf), &n, nullptr) && n > 0)
            r.err.append(buf, buf + n);
    });
    {
        uint8_t buf[65536];
        DWORD n;
        while (ReadFile(cp.outR, buf, sizeof(buf), &n, nullptr) && n > 0)
            r.out.insert(r.out.end(), buf, buf + n);
    }
    errThread.join();
    WaitForSingleObject(cp.hProc, INFINITE);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(cp.hProc, &code);
    r.exitCode = (int)code;
    CloseHandle(cp.outR); CloseHandle(cp.errR);
    CloseHandle(cp.hProc); CloseHandle(cp.hThread);
    return r;
}

int RunProcessStream(const std::vector<std::wstring>& args,
                     const std::function<void(const std::string& line)>& onLine,
                     std::string* errOut,
                     const std::function<void(void* hProcess)>& onStarted) {
    ChildPipes cp = LaunchChild(args);
    if (!cp.ok) {
        if (errOut) *errOut += "failed to launch process\n";
        return -1;
    }
    if (onStarted) onStarted(cp.hProc);

    std::string errAccum;
    std::thread errThread([&]() {
        char buf[4096];
        DWORD n;
        while (ReadFile(cp.errR, buf, sizeof(buf), &n, nullptr) && n > 0)
            errAccum.append(buf, buf + n);
    });
    {
        std::string pending;
        char buf[4096];
        DWORD n;
        while (ReadFile(cp.outR, buf, sizeof(buf), &n, nullptr) && n > 0) {
            pending.append(buf, buf + n);
            size_t pos;
            while ((pos = pending.find_first_of("\r\n")) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!line.empty() && onLine) onLine(line);
            }
        }
        if (!pending.empty() && onLine) onLine(pending);
    }
    errThread.join();
    WaitForSingleObject(cp.hProc, INFINITE);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(cp.hProc, &code);
    CloseHandle(cp.outR); CloseHandle(cp.errR);
    CloseHandle(cp.hThread);
    if (!onStarted) CloseHandle(cp.hProc);   // else caller owns hProc
    if (errOut) *errOut += errAccum;
    return (int)code;
}

// ---------------------------------------------------------------- dialogs / shell
bool OpenVideoFilesDialog(void* hwndOwner, std::vector<std::string>& outPaths) {
    outPaths.clear();
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    COMDLG_FILTERSPEC filters[] = {
        { L"Video files", L"*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.wmv;*.ts;*.mts;*.flv;*.3gp" },
        { L"All files", L"*.*" },
    };
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(L"Add raw videos");
    bool ok = false;
    if (SUCCEEDED(dlg->Show((HWND)hwndOwner))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items))) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; i++) {
                IShellItem* it = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &it))) {
                    PWSTR psz = nullptr;
                    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &psz))) {
                        outPaths.push_back(WideToUtf8(psz));
                        CoTaskMemFree(psz);
                    }
                    it->Release();
                }
            }
            items->Release();
            ok = !outPaths.empty();
        }
    }
    dlg->Release();
    return ok;
}

bool OpenAudioFileDialog(void* hwndOwner, std::string& outPath) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    COMDLG_FILTERSPEC filters[] = {
        { L"Audio files", L"*.mp3;*.wav;*.ogg;*.flac;*.m4a;*.aac;*.wma;*.opus" },
        { L"All files", L"*.*" },
    };
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(L"Choose background music");
    bool ok = false;
    if (SUCCEEDED(dlg->Show((HWND)hwndOwner))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &psz))) {
                outPath = WideToUtf8(psz);
                CoTaskMemFree(psz);
                ok = true;
            }
            it->Release();
        }
    }
    dlg->Release();
    return ok;
}

bool OpenFolderDialog(void* hwndOwner, std::string& outPath) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Choose folder");
    bool ok = false;
    if (SUCCEEDED(dlg->Show((HWND)hwndOwner))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &psz))) {
                outPath = WideToUtf8(psz);
                CoTaskMemFree(psz);
                ok = true;
            }
            it->Release();
        }
    }
    dlg->Release();
    return ok;
}

void OpenInExplorer(const std::string& path) {
    ShellExecuteW(nullptr, L"open", L"explorer.exe", Utf8ToWide(path).c_str(), nullptr, SW_SHOWNORMAL);
}
void ShellOpen(const std::string& path) {
    ShellExecuteW(nullptr, L"open", Utf8ToWide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- background jobs
namespace {
std::deque<std::function<void()>> g_jobs;
std::mutex g_jobsM;
std::condition_variable g_jobsCv;
std::thread g_jobsThread;
bool g_jobsStop = false;
bool g_jobsStarted = false;

void JobsThreadMain() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(g_jobsM);
            g_jobsCv.wait(lk, [] { return g_jobsStop || !g_jobs.empty(); });
            if (g_jobsStop && g_jobs.empty()) return;
            job = std::move(g_jobs.front());
            g_jobs.pop_front();
        }
        job();
    }
}

std::deque<std::function<void()>> g_mainQ;
std::mutex g_mainQM;
} // namespace

void JobsPush(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(g_jobsM);
        if (!g_jobsStarted) {
            g_jobsStarted = true;
            g_jobsThread = std::thread(JobsThreadMain);
        }
        g_jobs.push_back(std::move(fn));
    }
    g_jobsCv.notify_one();
}

void JobsShutdown() {
    {
        std::lock_guard<std::mutex> lk(g_jobsM);
        if (!g_jobsStarted) return;
        g_jobsStop = true;
        g_jobs.clear();
    }
    g_jobsCv.notify_one();
    if (g_jobsThread.joinable()) g_jobsThread.join();
}

void PostToMainThread(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(g_mainQM);
    g_mainQ.push_back(std::move(fn));
}

void DrainMainThreadQueue() {
    std::deque<std::function<void()>> q;
    {
        std::lock_guard<std::mutex> lk(g_mainQM);
        q.swap(g_mainQ);
    }
    for (auto& fn : q) fn();
}

// ---------------------------------------------------------------- misc
std::string FormatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = (int)seconds;
    int ms = (int)((seconds - total) * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; total += 1; }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d.%03d", total / 60, total % 60, ms);
    return buf;
}

double NowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string ToLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}
