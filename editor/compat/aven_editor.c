/*
 * aven-editor: Rynax was called Aven before 0.6. Copies of Aven 0.4 and 0.5 update themselves by
 * unpacking the new version and starting "aven-editor" from it, and the shortcuts their installer
 * made point at aven-editor too. So releases keep this little program under that name: it starts
 * rynax-editor, next to it, with the same arguments, and that's all.
 */

#if defined(_WIN32)

#include <windows.h>

#include <stdlib.h>
#include <wchar.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine, int show) {
    (void)instance;
    (void)previous;
    (void)commandLine;
    (void)show;
    static wchar_t path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, 32768);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (n == 0 || n >= 32768 || !slash || (size_t)(slash - path) + 20 >= 32768)
        return 1;
    wcscpy(slash + 1, L"rynax-editor.exe");
    // The arguments exactly as given, after this program's own name.
    const wchar_t* args = GetCommandLineW();
    if (*args == L'"') {
        ++args;
        while (*args && *args != L'"')
            ++args;
        if (*args)
            ++args;
    } else {
        while (*args && *args != L' ' && *args != L'\t')
            ++args;
    }
    size_t size = wcslen(path) + wcslen(args) + 3;
    wchar_t* command = (wchar_t*)malloc(size * sizeof(wchar_t));
    if (!command)
        return 1;
    swprintf(command, size, L"\"%ls\"%ls", path, args);
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof startup);
    startup.cb = sizeof startup;
    BOOL started = CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process);
    free(command);
    if (!started)
        return 1;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

#else

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

int main(int argc, char** argv) {
    (void)argc;
    char self[PATH_MAX] = {0};
#if defined(__APPLE__)
    uint32_t size = sizeof self;
    if (_NSGetExecutablePath(self, &size) != 0)
        return 1;
#else
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) {
        if (!argv[0] || strlen(argv[0]) >= sizeof self)
            return 1;
        strcpy(self, argv[0]);
    }
#endif
    char* slash = strrchr(self, '/');
    char target[PATH_MAX];
    if (!slash || snprintf(target, sizeof target, "%.*s/rynax-editor", (int)(slash - self), self) >= (int)sizeof target)
        return 1;
    argv[0] = target;
    execv(target, argv);
    perror("aven-editor: couldn't start rynax-editor");
    return 127;
}

#endif
