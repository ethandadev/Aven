#pragma once

#include <filesystem>

namespace rynax::editor {

// rynax-editor <project> --check [--strict]: checks scripts, scenes and file references without a
// window and prints problems as "file:line:col: error: ...". Returns the process exit code.
int runProjectCheck(const std::filesystem::path& projectDir, bool strict);

} // namespace rynax::editor
