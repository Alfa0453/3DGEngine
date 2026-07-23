#pragma once

#include <string>

// Thin wrapper around the OS's native file dialogs. Implemented in NativeDialog.cpp
// so the Win32 headers stay isolated from the rest of the editor. On non-Windows
// platforms these return an empty string (no dialog available).
namespace editor {

// Show a native "pick a folder" dialog. Returns the chosen absolute path, or an
// empty string if the user cancelled (or no native dialog is available).
std::string PickFolderDialog(const std::string& title);

// Show a native "open file" dialog filtered to one extension (e.g. "3dgproject").
// Returns the chosen absolute file path, or an empty string if cancelled.
std::string OpenFileDialog(const std::string& title,
                           const std::string& filterName,
                           const std::string& filterExt);

} // namespace editor
