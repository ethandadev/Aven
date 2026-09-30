// Updates: when the editor starts, Rynax asks GitHub for its releases. When there's a newer one, a
// small "Update to 0.4.0" button shows in the menu bar (and on the start screen); nothing pops up.
// The Update window shows what's new, downloads this system's zip, checks it against the SHA-256
// GitHub lists for it, and unpacks it into .rynax-update/ beside the editor. The new files are
// swapped in when Rynax quits (or restarts from that window): a running program can be renamed on
// every system but not overwritten, so the old files move to .rynax-update/old, which the new
// version deletes when it starts. If anything can't be moved, everything is put back.
//
// Downloads use curl, which comes with Windows 10 and later, macOS and most Linux systems, so the
// editor needs no HTTPS code of its own. Only release downloads (a folder with "START HERE.txt")
// update themselves; a build from source just says there's a new version.
//
// Every download must come with its .sig: an Ed25519 signature, by the release key, of the file's
// name and SHA-256 (tools/release/update_key.py). The public key is built in (RYNAX_UPDATE_PUBLIC_KEY,
// set by the release workflow); a build without one doesn't install updates itself. So publishing a
// release on GitHub isn't enough to reach people's computers: it also takes the private key.
//
// Test builds (RYNAX_TEST_HOOKS, off for releases) also read RYNAX_UPDATE_URL, which replaces GitHub's
// release list (file:// links work then), and RYNAX_UPDATE_PUBLIC_KEY.

#include "editor.h"

#include "rynax/core/fs.h"
#include "rynax/core/log.h"
#include "rynax/core/update.h"
#include "rynax/core/zip.h"

#include <imgui.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace rynax::editor {

namespace {

constexpr const char* kReleases = "https://api.github.com/repos/ethandadev/Rynax/releases?per_page=20";
constexpr const char* kReleasePage = "https://github.com/ethandadev/Rynax/releases/latest";

const char* hostSystem() {
#if defined(_WIN32)
    return "windows-x64";
#elif defined(__APPLE__)
    return "macos-arm64";
#else
    return "linux-x64";
#endif
}

const char* editorProgram() {
#if defined(_WIN32)
    return "rynax-editor.exe";
#else
    return "rynax-editor";
#endif
}

// A test build's stand-in for GitHub (RYNAX_UPDATE_URL), or nothing.
const char* testReleases() {
#if RYNAX_TEST_HOOKS
    const char* custom = std::getenv("RYNAX_UPDATE_URL");
    return custom && *custom ? custom : nullptr;
#else
    return nullptr;
#endif
}

std::string releasesUrl() { return testReleases() ? testReleases() : kReleases; }

// The key release downloads are signed with (64 hex digits), or "" in builds made without one.
std::string updateKey() {
#if RYNAX_TEST_HOOKS
    if (const char* key = std::getenv("RYNAX_UPDATE_PUBLIC_KEY"))
        return key;
#endif
    return RYNAX_UPDATE_PUBLIC_KEY;
}

} // namespace

// How this copy of Rynax was installed, and so how it's updated.
struct UpdateInstall {
    stdfs::path root;       // what an update replaces: Rynax's folder, or Rynax.app (empty: not a release)
    stdfs::path work;       // downloads and the old version; on the same drive as root, so files can move
    std::string program;    // the editor, inside root
    std::string cantUpdate; // why this copy can't replace itself (empty: it can)
};

namespace {

UpdateInstall thisInstall() {
    std::error_code ec;
    stdfs::path dir = fs::executableDir();
    UpdateInstall in;
    if (std::getenv("FLATPAK_ID") || stdfs::exists("/.flatpak-info", ec)) {
        in.root = dir;
        in.cantUpdate = "Rynax was installed with Flatpak, which keeps its files read-only: download the new .flatpak "
                        "from the release page and open it to update.";
    } else if (dir.filename() == "MacOS" && stdfs::exists(dir.parent_path() / "Info.plist", ec)) {
        // Rynax.app/Contents/MacOS/rynax-editor: the update replaces Rynax.app's Contents.
        in.root = dir.parent_path().parent_path();
        in.work = in.root.parent_path() / ".rynax-update";
        in.program = "Contents/MacOS/rynax-editor";
        if (in.root.string().find("/AppTranslocation/") != std::string::npos)
            in.cantUpdate = "macOS is running Rynax from a temporary read-only copy, because it hasn't been moved since it "
                            "was downloaded. Drag Rynax into your Applications folder, open it from there, and update again.";
#if !defined(_WIN32)
        else if (access(in.root.parent_path().c_str(), W_OK) != 0) // e.g. still on the .dmg it came in
            in.cantUpdate = "Rynax can't update itself where it is (" + in.root.parent_path().string() +
                            "). Drag Rynax into your Applications folder, open it from there, and update again.";
#endif
    } else if (stdfs::exists(dir / "START HERE.txt", ec) && stdfs::exists(dir / editorProgram(), ec)) {
        in.root = dir; // a release zip (or the Windows installer, which installs the same files)
        in.work = dir / ".rynax-update";
        in.program = editorProgram();
    } else {
        in.cantUpdate = "This copy of Rynax was built from its source code, so it doesn't replace itself: pull the new "
                        "code, or download Rynax from the release page.";
    }
    if (in.cantUpdate.empty() && updateKey().size() != 64)
        in.cantUpdate = "This copy of Rynax was built without the key that proves an update really comes from Rynax's "
                        "release, so it doesn't install updates itself: download the new version from the release page.";
    return in;
}


stdfs::path findCurl() {
    std::error_code ec;
#if defined(_WIN32)
    // Part of Windows since Windows 10 (1803). The full path, so a curl.exe in the project folder can't stand in.
    const char* root = std::getenv("SystemRoot");
    stdfs::path system = stdfs::path(root ? root : "C:\\Windows") / "System32" / "curl.exe";
    if (stdfs::exists(system, ec))
        return system;
    const char sep = ';';
    const char* file = "curl.exe";
#else
    for (const char* p : {"/usr/bin/curl", "/bin/curl", "/usr/local/bin/curl", "/opt/homebrew/bin/curl"})
        if (stdfs::exists(p, ec))
            return p;
    const char sep = ':';
    const char* file = "curl";
#endif
    if (const char* path = std::getenv("PATH")) {
        std::stringstream ss(path);
        std::string dir;
        while (std::getline(ss, dir, sep))
            if (!dir.empty() && stdfs::path(dir).is_absolute() && stdfs::exists(stdfs::path(dir) / file, ec))
                return stdfs::path(dir) / file;
    }
    return {};
}

// Runs a program (no shell, no window) and waits for it. Returns its exit code; -1 if it couldn't
// start, -2 if `cancel` was set while it ran (it's stopped then).
int runProgram(const std::vector<std::string>& args, const std::atomic<bool>* cancel) {
#if defined(_WIN32)
    std::string line;
    for (auto& a : args) {
        if (a.find('"') != std::string::npos)
            return -1;
        std::string arg = a;
        size_t slashes = 0;
        while (slashes < arg.size() && arg[arg.size() - 1 - slashes] == '\\')
            ++slashes;
        arg.append(slashes, '\\'); // a backslash before the closing quote would escape it
        line += (line.empty() ? "\"" : " \"") + arg + "\"";
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(n > 0 ? n : 1), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wide.data(), n);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, wide.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    CloseHandle(pi.hThread);
    int result = 0;
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if (cancel && *cancel) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            result = -2;
            break;
        }
    }
    if (result == 0) {
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        result = static_cast<int>(code);
    }
    CloseHandle(pi.hProcess);
    return result;
#else
    std::vector<std::string> copies = args;
    std::vector<char*> argv;
    for (auto& a : copies)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t quiet;
    posix_spawn_file_actions_init(&quiet);
    posix_spawn_file_actions_addopen(&quiet, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&quiet, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&quiet, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int spawned = posix_spawn(&pid, argv[0], &quiet, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&quiet);
    if (spawned != 0)
        return -1;
    int status = 0;
    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done < 0)
            return -1;
        if (cancel && *cancel) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            return -2;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// Fetches a link into a file with curl. Returns "" when it worked, else what went wrong in words.
std::string fetch(const std::string& url, const stdfs::path& to, bool big, const std::atomic<bool>* cancel) {
    stdfs::path curl = findCurl();
    if (curl.empty())
        return "Rynax uses curl to download, and it isn't installed here (on Linux: sudo apt install curl).";
    stdfs::path err = to.string() + ".log";
    // https only (file:// too in a test build with RYNAX_UPDATE_URL), also after redirects.
    const char* protocols = testReleases() ? "=https,file" : "=https";
    std::vector<std::string> args = {curl.string(), "--fail", "--silent", "--show-error", "--location", "--proto", protocols,
                                     "--proto-redir", protocols, "--retry", "2", "--connect-timeout", "20",
                                     "--user-agent", std::string("Rynax-Editor/") + RYNAX_VERSION, "--stderr", err.string(),
                                     "--output", to.string()};
    if (big) // give up on a stalled download (slower than 1 KB/s for a minute), not a slow one
        args.insert(args.end(), {"--speed-limit", "1024", "--speed-time", "60"});
    else
        args.insert(args.end(), {"--max-time", "30", "--header", "Accept: application/vnd.github+json"});
    args.push_back(url);
    int code = runProgram(args, cancel);
    std::string said = fs::readText(err).value_or("");
    std::error_code ec;
    stdfs::remove(err, ec);
    while (!said.empty() && (said.back() == '\n' || said.back() == '\r'))
        said.pop_back();
    if (size_t nl = said.rfind('\n'); nl != std::string::npos)
        said = said.substr(nl + 1);
    if (code == 0)
        return "";
    if (code == -2)
        return "Cancelled.";
    if (code == -1)
        return "Couldn't run curl (" + curl.string() + ").";
    if (code == 6 || code == 7 || code == 28 || code == 35)
        return "Couldn't reach GitHub. Are you online? (" + said + ")";
    if (code == 22 && said.find("403") != std::string::npos)
        return "GitHub said to wait a while before asking again (" + said + ").";
    return said.empty() ? "curl stopped with error " + std::to_string(code) + "." : said;
}

std::string megabytes(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

// Release notes are Markdown. Shown here: headings, bullets (nested too) and paragraphs, with lines
// that continue a bullet joined up, [links](...) as their words, and the rest as plain text. They end
// where the release's download table starts.
void drawNotes(const std::string& notes, ImFont* bold) {
    struct Block {
        int kind = 0; // 0 paragraph, 1 heading, 2 bullet
        int depth = 0;
        std::string text;
    };
    std::vector<Block> blocks;
    bool open = false;
    std::stringstream ss(notes);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.find("<!-- rynax:download -->") != std::string::npos)
            break;
        size_t start = line.find_first_not_of(' ');
        if (start == std::string::npos || line.compare(start, 4, "<!--") == 0 || line[start] == '|') {
            open = false;
            continue;
        }
        std::string text = line.substr(start);
        if (text[0] == '#') {
            blocks.push_back({1, 0, text.substr(std::min(text.size(), text.find_first_not_of("# ")))});
            open = false;
        } else if (text.size() > 1 && (text[0] == '-' || text[0] == '*') && text[1] == ' ') {
            blocks.push_back({2, static_cast<int>(start / 2), text.substr(2)});
            open = true;
        } else if (open) {
            blocks.back().text += " " + text; // a wrapped line
        } else {
            blocks.push_back({0, 0, text});
            open = true;
        }
    }
    for (auto& b : blocks) {
        std::string& t = b.text;
        for (const char* mark : {"**", "`"}) // emphasis reads as clutter in plain text
            for (size_t at; (at = t.find(mark)) != std::string::npos;)
                t.erase(at, std::strlen(mark));
        for (size_t bracket = t.find('['); bracket != std::string::npos; bracket = t.find('[', bracket + 1)) {
            size_t close = t.find(']', bracket), end = std::string::npos;
            if (close != std::string::npos && close + 1 < t.size() && t[close + 1] == '(')
                end = t.find(')', close);
            if (end != std::string::npos) // (a lone [ or [words] stays as it is)
                t = t.substr(0, bracket) + t.substr(bracket + 1, close - bracket - 1) + t.substr(end + 1);
        }
        if (b.kind == 1) {
            ImGui::Spacing();
            ImGui::PushFont(bold);
            ImGui::TextWrapped("%s", t.c_str());
            ImGui::PopFont();
        } else if (b.kind == 2) {
            float indent = ui::px(18) * static_cast<float>(b.depth);
            if (indent > 0)
                ImGui::Indent(indent);
            ImGui::Bullet();
            ImGui::TextWrapped("%s", t.c_str());
            if (indent > 0)
                ImGui::Unindent(indent);
        } else {
            ImGui::TextWrapped("%s", t.c_str());
        }
    }
}

} // namespace

struct Editor::UpdateJob {
    enum Stage { Checking, UpToDate, Available, Downloading, Unpacking, Ready, Failed };
    std::atomic<int> stage{Checking};
    std::atomic<bool> cancel{false};
    bool manual = false;
    update::Release release; // written before stage becomes Available, then only read
    UpdateInstall install;   // where this copy is, and whether it can update itself
    stdfs::path part;        // the download while it comes in

    std::mutex mutex;
    std::string problem;

    void fail(const std::string& why) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            problem = why;
        }
        Log::warn("Update: ", why);
        stage = Failed;
    }
    std::string why() {
        std::lock_guard<std::mutex> lock(mutex);
        return problem;
    }
    bool busy() const { return stage == Checking || stage == Downloading || stage == Unpacking; }
};

namespace {

// Everything in the work folder but the previous version (kept so "Go back" can restore it).
void tidyWork(const stdfs::path& work) {
    std::error_code ec;
    std::vector<stdfs::path> leftovers;
    for (auto& e : stdfs::directory_iterator(work, ec))
        if (e.path().filename() != "old" && e.path().filename() != "old-version.txt")
            leftovers.push_back(e.path());
    for (auto& p : leftovers)
        stdfs::remove_all(p, ec);
}

// Installed with the Setup program: Settings > Apps should show the version that's there now.
void setInstalledVersion(const stdfs::path& root, const std::string& version) {
#if defined(_WIN32)
    HKEY key = nullptr;
    const wchar_t* uninstall = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{6F6D3C2B-3E1A-4B8E-9E36-6A0B5F1D2C47}_is1";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, uninstall, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    wchar_t location[1024] = {};
    DWORD size = sizeof location - sizeof(wchar_t);
    std::error_code same;
    if (RegQueryValueExW(key, L"InstallLocation", nullptr, nullptr, reinterpret_cast<BYTE*>(location), &size) == ERROR_SUCCESS &&
        stdfs::equivalent(stdfs::path(location), root, same)) {
        std::wstring wide(version.begin(), version.end()); // digits, dots and letters
        RegSetValueExW(key, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(wide.c_str()),
                       static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
#else
    (void)root;
    (void)version;
#endif
}

std::string restartCommand(const UpdateInstall& in, const stdfs::path& project) {
#if defined(__APPLE__)
    // Through Launch Services, so it starts as the app (its name in the menu bar, its icon in the Dock).
    std::string command = "/usr/bin/open -n \"" + in.root.string() + "\" --args";
#else
    std::string command = "\"" + (in.root / in.program).string() + "\"";
#endif
    if (!project.empty())
        command += " \"" + project.string() + "\"";
    return command;
}

} // namespace

void Editor::startUpdater() {
    UpdateInstall install = thisInstall();
    if (!install.work.empty()) {
        // What an update left behind: the old version's files, or why it couldn't be installed.
        stdfs::path work = install.work;
        if (auto failed = fs::readText(work / "failed.txt"))
            notify("The update couldn't be installed: " + *failed, true);
        tidyWork(work); // the previous version stays, for Preferences > Updates > Go back
    }
    if (!options_.screenshot.empty())
        return;
    if (!prefs.lastVersion.empty() && update::isNewer(RYNAX_VERSION, prefs.lastVersion))
        notify(std::string("Rynax is updated to ") + RYNAX_VERSION + ". Have fun!");
    if (prefs.lastVersion != RYNAX_VERSION) {
        prefs.lastVersion = RYNAX_VERSION;
        prefs.save();
    }
    // Builds from source don't look on their own (Help > Check for Updates still does).
    if (prefs.checkUpdates && !install.root.empty())
        checkForUpdates(false);
}

void Editor::checkForUpdates(bool manual, bool wait) {
    if (manual)
        showUpdater_ = true;
    if (updateJob_ && (updateJob_->busy() || updateJob_->stage == UpdateJob::Ready))
        return; // already on it, or already downloaded
    auto job = std::make_shared<UpdateJob>();
    job->manual = manual;
    job->install = thisInstall();
    updateJob_ = job;
    bool betas = prefs.betaUpdates || !update::parseVersion(RYNAX_VERSION).pre.empty(); // beta testers keep getting betas
    std::string skipped = manual ? "" : prefs.skippedUpdate;
    auto work = [job, betas, skipped] {
        stdfs::path list = fs::userDataDir("Rynax Editor") / "releases.json";
        std::error_code ec;
        stdfs::create_directories(list.parent_path(), ec);
        std::string problem = fetch(releasesUrl(), list, false, nullptr);
        if (!problem.empty())
            return job->fail(problem);
        Json releases = Json::parse(fs::readText(list).value_or(""));
        stdfs::remove(list, ec);
        if (!releases.isArray())
            return job->fail("GitHub's answer didn't list any releases" +
                             (releases["message"].isString() ? " (" + releases["message"].asString() + ")." : std::string(".")));
        update::Release release;
        if (!update::newestRelease(releases, RYNAX_VERSION, hostSystem(), betas, release) || release.version == skipped) {
            job->stage = UpdateJob::UpToDate;
            return;
        }
        job->release = release;
        Log::info("Update: Rynax ", release.version, " is out (this is ", RYNAX_VERSION, ").");
        job->stage = UpdateJob::Available;
    };
    if (wait)
        work();
    else
        std::thread(work).detach();
}

void Editor::downloadUpdate(bool wait) {
    auto job = updateJob_;
    if (!job || job->busy() || job->stage == UpdateJob::Ready || job->release.download.empty() || !job->install.cantUpdate.empty())
        return;
    stdfs::path work = job->install.work;
    std::error_code ec;
    stdfs::create_directories(work, ec);
    tidyWork(work);
    bool writable = fs::writeText(work / "can-write", "yes");
    stdfs::remove(work / "can-write", ec);
    if (!writable) {
        job->fail("Rynax can't change its own folder (" + job->install.root.string() +
                  "). Download the new version from the release page and unzip it instead.");
        return;
    }
    job->part = work / (job->release.fileName + ".part");
    job->cancel = false;
    job->stage = UpdateJob::Downloading;
    auto run = [job, work] {
        const update::Release& r = job->release;
        std::string problem = fetch(r.download, job->part, true, &job->cancel);
        std::error_code e;
        if (job->cancel) {
            stdfs::remove(job->part, e);
            job->stage = UpdateJob::Available;
            return;
        }
        if (!problem.empty())
            return job->fail("The download stopped: " + problem);
        job->stage = UpdateJob::Unpacking;
        uint64_t size = stdfs::file_size(job->part, e);
        if (e)
            return job->fail("The download went missing before it could be checked. Try again.");
        if (r.size && size != r.size)
            return job->fail("The download came in cut short (" + megabytes(size) + " of " + megabytes(r.size) + "). Try again.");
        std::string sha = update::sha256File(job->part);
        if (sha.empty() || (!r.sha256.empty() && sha != r.sha256)) {
            stdfs::remove(job->part, e);
            return job->fail("The download doesn't match the release (its SHA-256 is different), so it wasn't installed. Try again.");
        }
        // The signature: made with the release key, which isn't on GitHub, so a release someone else
        // managed to publish (or change) is refused here.
        if (r.signatureUrl.empty()) {
            stdfs::remove(job->part, e);
            return job->fail("This release isn't signed, so it wasn't installed. Download it from the release page if you trust it.");
        }
        stdfs::path sigFile = work / (r.fileName + ".sig");
        problem = fetch(r.signatureUrl, sigFile, false, &job->cancel);
        std::string error;
        if (problem.empty() && !update::checkSignature(updateKey(), r.fileName, sha, fs::readText(sigFile).value_or(""), error))
            problem = error;
        stdfs::remove(sigFile, e);
        if (!problem.empty()) {
            stdfs::remove(job->part, e);
            Log::error("Update: refused Rynax ", r.version, ": ", problem);
            return job->fail("This download couldn't be confirmed as a real Rynax release (" + problem + "), so it wasn't installed.");
        }
        if (!zip::extract(job->part, work / "new", error))
            return job->fail("Couldn't unpack the download: " + error + ".");
        stdfs::remove(job->part, e);
        if (!stdfs::exists(work / "new" / job->install.program, e))
            return job->fail("The download doesn't have the editor in it, so it wasn't installed.");
        Log::info("Update: Rynax ", r.version, " is ready; it's installed when Rynax closes.");
        job->stage = UpdateJob::Ready;
    };
    if (wait)
        run();
    else
        std::thread(run).detach();
}

std::string Editor::previousVersion() const {
    UpdateInstall in = thisInstall();
    std::error_code ec;
    if (in.work.empty() || !stdfs::exists(in.work / "old", ec))
        return "";
    std::string version = fs::readText(in.work / "old-version.txt").value_or("");
    while (!version.empty() && std::isspace(static_cast<unsigned char>(version.back())))
        version.pop_back();
    return version;
}

void Editor::saveAndRestart() {
    // As "Save and close" does.
    if (playing_)
        stop();
    bool ok = true;
    if (hasProject()) {
        ok = !dirty_ || saveScene();
        ok = saveAllScripts() && ok;
    }
    if (pixel_.dirty)
        ok = savePixelImage() && ok;
    if (!ok)
        return; // (what couldn't be saved is said; restarting would lose it)
    updateRestart_ = true;
    quit_ = true;
}

std::string Editor::installPendingUpdate() {
    auto job = updateJob_;
    if (job && job->stage == UpdateJob::Downloading) {
        // Quitting mid-download: stop curl rather than leave it running without Rynax.
        job->cancel = true;
        for (int i = 0; i < 50 && job->stage == UpdateJob::Downloading; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::error_code ec;
    if (rollbackPending_) {
        // "Go back": the previous version's files swap back in, the same way an update goes in.
        UpdateInstall in = thisInstall();
        std::string previous = previousVersion();
        if (previous.empty() || in.root.empty() || in.work.empty())
            return "";
        std::string error;
        if (!update::swapIn(in.root, in.work / "old", in.work / "undone", error)) {
            Log::error("Update: couldn't go back to Rynax ", previous, ": ", error);
            fs::writeText(in.work / "failed.txt", error);
            return "";
        }
        stdfs::remove_all(in.work / "undone", ec);
        stdfs::remove_all(in.work / "old", ec);
        stdfs::remove(in.work / "old-version.txt", ec);
        setInstalledVersion(in.root, previous);
        Log::info("Update: went back to Rynax ", previous, ".");
        return updateRestart_ ? restartCommand(in, projectDir_) : "";
    }
    if (!job || job->stage != UpdateJob::Ready || !job->install.cantUpdate.empty())
        return "";
    stdfs::path work = job->install.work;
    stdfs::remove_all(work / "old", ec);
    std::string error;
    if (!update::swapIn(job->install.root, work / "new", work / "old", error)) {
        Log::error("Update: couldn't install Rynax ", job->release.version, ": ", error);
        fs::writeText(work / "failed.txt", error); // said when Rynax next starts
        return "";
    }
    fs::writeText(work / "old-version.txt", RYNAX_VERSION); // for "Go back to Rynax ..."
    Log::info("Update: installed Rynax ", job->release.version, ".");
    setInstalledVersion(job->install.root, job->release.version);
    return updateRestart_ ? restartCommand(job->install, projectDir_) : "";
}

bool Editor::updateAvailable() const {
    auto job = updateJob_;
    if (!job)
        return false;
    int stage = job->stage;
    return stage == UpdateJob::Available || stage == UpdateJob::Downloading || stage == UpdateJob::Unpacking ||
           stage == UpdateJob::Ready || (stage == UpdateJob::Failed && !job->release.version.empty());
}

std::string Editor::updateStatus() const {
    auto job = updateJob_;
    if (!job)
        return "none";
    const char* names[] = {"checking", "up to date", "available", "downloading", "unpacking", "ready", "failed"};
    int stage = job->stage; // (the release is written before the stage moves past Checking)
    std::string status = stage == UpdateJob::Checking || job->release.version.empty() ? names[stage]
                                                                                      : job->release.version + " " + names[stage];
    return stage == UpdateJob::Failed ? status + ": " + job->why() : status;
}

namespace {
std::string badgeLabel(bool ready, const std::string& version) {
    return ready ? "Restart to update" : "Update to " + version;
}
} // namespace

float Editor::updateBadgeWidth() const {
    if (!updateAvailable())
        return 0;
    return ImGui::CalcTextSize(badgeLabel(updateJob_->stage == UpdateJob::Ready, updateJob_->release.version).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
}

void Editor::drawUpdateBadge(bool small) {
    if (!updateAvailable())
        return;
    auto job = updateJob_;
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    std::string label = badgeLabel(job->stage == UpdateJob::Ready, job->release.version) + "##updatebadge";
    if (small ? ImGui::SmallButton(label.c_str()) : ImGui::Button(label.c_str()))
        showUpdater_ = true;
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A new version of Rynax is out. Click to see what's new.");
}

void Editor::drawUpdater() {
    if (!showUpdater_)
        return;
    auto job = updateJob_;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(ui::fitted({560, 460}), ImGuiCond_Appearing);
    if (!ImGui::Begin("Update Rynax", &showUpdater_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    const float buttonH = ui::px(32);
    int stage = job ? job->stage.load() : UpdateJob::UpToDate;
    // (the checking thread writes the release until the stage moves on)
    bool known = job && stage != UpdateJob::Checking && !job->release.version.empty();

    if (!job || stage == UpdateJob::Checking) {
        ImGui::TextUnformatted(job ? "Looking for a new version..." : "");
    } else if (stage == UpdateJob::UpToDate) {
        ImGui::PushFont(fonts.big);
        ImGui::TextUnformatted("You're up to date");
        ImGui::PopFont();
        ImGui::TextDisabled("Rynax %s is the newest%s version.", RYNAX_VERSION, prefs.betaUpdates ? "" : " released");
    } else if (stage == UpdateJob::Failed && !known) {
        ImGui::TextWrapped("Couldn't check for a new version.");
        ImGui::TextDisabled("%s", job->why().c_str());
    }

    if (known) {
        const update::Release& r = job->release;
        ImGui::PushFont(fonts.big);
        ImGui::Text("Rynax %s is here", r.version.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("You have %s%s", RYNAX_VERSION, r.beta ? "  ·  this one is a beta" : "");
        ImGui::Spacing();
        bool textLine = stage == UpdateJob::Failed || stage == UpdateJob::Ready || !job->install.cantUpdate.empty() || r.download.empty();
        float notesH = ImGui::GetContentRegionAvail().y - buttonH * (textLine ? 2.4f : 1.4f);
        ImGui::BeginChild("##notes", {0, std::max(notesH, ui::px(80))}, ImGuiChildFlags_Borders);
        if (r.notes.empty())
            ImGui::TextDisabled("No notes for this version; the release page may say more.");
        else
            drawNotes(r.notes, fonts.bold);
        ImGui::EndChild();

        if (stage == UpdateJob::Failed)
            ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", job->why().c_str());
        if (stage == UpdateJob::Downloading) {
            std::error_code ec;
            uint64_t got = stdfs::file_size(job->part, ec);
            if (ec)
                got = 0;
            float fraction = r.size ? std::min(1.0f, static_cast<float>(got) / static_cast<float>(r.size)) : 0.0f;
            std::string text = "Downloading  " + megabytes(got) + (r.size ? " of " + megabytes(r.size) : std::string());
            ImGui::ProgressBar(fraction, {ImGui::GetContentRegionAvail().x - ui::px(110), buttonH}, text.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {-1, buttonH}))
                job->cancel = true;
        } else if (stage == UpdateJob::Unpacking) {
            ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), {-1, buttonH}, "Checking and unpacking...");
        } else if (stage == UpdateJob::Ready) {
            ImGui::TextWrapped("Ready. Rynax %s goes in when you close Rynax, or now:", r.version.c_str());
            bool unsaved = (dirty_ && hasProject()) || pixel_.dirty;
            for (auto& t : tabs_)
                unsaved = unsaved || t->modified;
            if (ImGui::Button(unsaved ? "Save and restart" : "Restart now", {ui::px(180), buttonH}))
                saveAndRestart();
            ImGui::SameLine();
            if (ImGui::Button("Later", {ui::px(100), buttonH}))
                showUpdater_ = false;
        } else if (!job->install.cantUpdate.empty() || r.download.empty()) {
            ImGui::TextWrapped("%s", !job->install.cantUpdate.empty()
                                         ? job->install.cantUpdate.c_str()
                                         : "There's no download for this system in that release yet; the release page has what there is.");
            if (ImGui::Button("Open the release page", {ui::px(220), buttonH}))
                openExternal(r.page.empty() ? kReleasePage : r.page);
        } else {
            if (ImGui::Button(stage == UpdateJob::Failed ? "Try again" : "Download and install", {ui::px(200), buttonH}))
                downloadUpdate(false);
            ImGui::SameLine();
            if (ImGui::Button("Release page", {0, buttonH}))
                openExternal(r.page.empty() ? kReleasePage : r.page);
            ImGui::SameLine();
            if (ImGui::Button("Skip this version", {0, buttonH})) {
                prefs.skippedUpdate = r.version;
                prefs.save();
                updateJob_.reset();
                showUpdater_ = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stop mentioning %s. Help > Check for Updates still shows it.", r.version.c_str());
        }
    } else if (job && !job->busy()) {
        ImGui::Spacing();
        if (ImGui::Button("Check again", {ui::px(140), buttonH}))
            checkForUpdates(true);
        ImGui::SameLine();
        if (ImGui::Button("Close", {ui::px(100), buttonH}))
            showUpdater_ = false;
    }
    ImGui::End();
}

} // namespace rynax::editor
