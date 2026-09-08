#include "SourceControlPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace {
struct Command { int exitCode = -1; std::string output; };
std::string Lower(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value; }
#if defined(_WIN32)
std::wstring Widen(const std::string& value) { if (value.empty()) return {}; const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0); std::wstring result(static_cast<std::size_t>(size), L'\0'); MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size); return result; }
std::string Narrow(const std::string& bytes) { return bytes; }
std::wstring Quote(std::wstring value) { std::wstring out = L"\""; unsigned slashes = 0; for (wchar_t c : value) { if (c == L'\\') { ++slashes; continue; } if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out += c; slashes = 0; continue; } out.append(slashes, L'\\'); slashes = 0; out += c; } out.append(slashes * 2, L'\\'); return out + L'\"'; }
Command RunGit(const std::filesystem::path& root, const std::vector<std::string>& args) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE}; HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return {};
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE; startup.hStdOutput = writePipe; startup.hStdError = writePipe; startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring command = L"git -C " + Quote(root.wstring()); for (const auto& arg : args) command += L" " + Quote(Widen(arg)); std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0'); PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, root.wstring().c_str(), &startup, &process); CloseHandle(writePipe); if (!launched) { CloseHandle(readPipe); return {}; }
    std::string output; char buffer[4096]; DWORD read = 0; while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read) output.append(buffer, read); WaitForSingleObject(process.hProcess, INFINITE); DWORD exitCode = 1; GetExitCodeProcess(process.hProcess, &exitCode); CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(readPipe); return {static_cast<int>(exitCode), Narrow(output)};
}
#else
Command RunGit(const std::filesystem::path&, const std::vector<std::string>&) { return {}; }
#endif
std::vector<std::string> Lines(const std::string& value) { std::vector<std::string> lines; std::istringstream in(value); std::string line; while (std::getline(in, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); lines.push_back(std::move(line)); } return lines; }
std::vector<std::string> Split(const std::string& value, char delimiter) { std::vector<std::string> out; std::string item; std::istringstream in(value); while (std::getline(in, item, delimiter)) out.push_back(item); return out; }
}

SourceControlPanel::~SourceControlPanel() { if (m_future.valid()) m_future.wait(); }

std::vector<std::string> SourceControlPanel::SelectedPaths() const { std::vector<std::string> out; for (const auto& file : m_files) if (file.selected) out.push_back(file.path); return out; }
bool SourceControlPanel::InActiveChangeList(const std::string& path) const { if (m_activeChangeList < 0 || m_activeChangeList >= static_cast<int>(m_changeLists.size())) return true; const auto& files = m_changeLists[static_cast<std::size_t>(m_activeChangeList)].files; return std::find(files.begin(), files.end(), path) != files.end(); }

void SourceControlPanel::Start(Action action, std::vector<std::string> paths, std::string value) {
    if (m_running || m_repoRoot.empty()) return; m_running = true; const auto root = m_repoRoot; const bool stagedDiff = m_showStagedDiff;
    m_future = std::async(std::launch::async, [root, action, paths = std::move(paths), value = std::move(value), stagedDiff] {
        WorkResult result; result.action = action; Command command;
        auto withPaths = [&](std::vector<std::string> args) { if (!paths.empty()) { args.push_back("--"); args.insert(args.end(), paths.begin(), paths.end()); } return args; };
        switch (action) {
        case Action::Diff: command = RunGit(root, withPaths(stagedDiff ? std::vector<std::string>{"diff", "--cached", "--no-ext-diff"} : std::vector<std::string>{"diff", "--no-ext-diff"})); result.diff = command.output; break;
        case Action::ShowCommit: command = RunGit(root, {"show", "--stat", "--oneline", "--decorate", value}); result.diff = command.output; break;
        case Action::Stage: command = RunGit(root, withPaths({"add"})); break;
        case Action::Unstage: command = RunGit(root, withPaths({"restore", "--staged"})); break;
        case Action::Commit: command = RunGit(root, {"commit", "-m", value}); break;
        case Action::SwitchBranch: command = RunGit(root, {"switch", value}); break;
        case Action::CreateBranch: command = RunGit(root, {"switch", "-c", value}); break;
        case Action::Refresh: command.exitCode = 0; break;
        }
        result.ok = command.exitCode == 0; result.message = command.output;
        if (action != Action::Diff && action != Action::ShowCommit && result.ok) { const auto status = RunGit(root, {"status", "--porcelain=v1", "--branch", "--untracked-files=all"}); const auto branches = RunGit(root, {"branch", "--format=%(HEAD)|%(refname:short)|%(upstream:short)|%(upstream:trackshort)"}); const auto history = RunGit(root, {"log", "-n", "100", "--date=short", "--pretty=format:%h%x1f%an%x1f%ad%x1f%s"}); result.status = status.output; result.branches = branches.output; result.history = history.output; result.ok = status.exitCode == 0 && branches.exitCode == 0; }
        return result;
    });
}

void SourceControlPanel::ParseSnapshot(const WorkResult& result) {
    m_files.clear(); for (const auto& line : Lines(result.status)) { if (line.rfind("##", 0) == 0 || line.size() < 4) continue; FileState file; file.index = line[0]; file.worktree = line[1]; file.path = line.substr(3); const auto arrow = file.path.find(" -> "); if (arrow != std::string::npos) file.path.erase(0, arrow + 4); if (file.path.size() >= 2 && file.path.front() == '"' && file.path.back() == '"') file.path = file.path.substr(1, file.path.size() - 2); file.conflict = file.index == 'U' || file.worktree == 'U' || (file.index == 'A' && file.worktree == 'A') || (file.index == 'D' && file.worktree == 'D'); m_files.push_back(std::move(file)); }
    m_branches.clear(); for (const auto& line : Lines(result.branches)) { auto fields = Split(line, '|'); if (fields.size() < 2) continue; Branch branch; branch.current = fields[0] == "*"; branch.name = fields[1]; if (fields.size() > 2) branch.upstream = fields[2]; if (fields.size() > 3) branch.tracking = fields[3]; if (branch.current) m_selectedBranch = static_cast<int>(m_branches.size()); m_branches.push_back(std::move(branch)); }
    m_commits.clear(); for (const auto& line : Lines(result.history)) { auto fields = Split(line, '\x1f'); if (fields.size() < 4) continue; m_commits.push_back({fields[0], fields[1], fields[2], fields[3]}); }
}

void SourceControlPanel::Poll() { if (!m_running || !m_future.valid() || m_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return; WorkResult result = m_future.get(); m_running = false; if (result.action == Action::Diff || result.action == Action::ShowCommit) m_diff = result.ok ? result.diff : result.message; else { if (result.ok) ParseSnapshot(result); m_message = result.ok ? (result.message.empty() ? "Source control updated." : result.message) : result.message; } }

void SourceControlPanel::LoadChangeLists() { m_changeLists.clear(); std::ifstream in(m_projectRoot / "Saved" / "SourceControlChangelists.txt"); std::string record; ChangeList* current = nullptr; while (in >> record) { if (record == "LIST") { ChangeList list; in >> std::quoted(list.name); m_changeLists.push_back(std::move(list)); current = &m_changeLists.back(); } else if (record == "FILE" && current) { std::string path; in >> std::quoted(path); current->files.push_back(std::move(path)); } } }
void SourceControlPanel::SaveChangeLists() { std::error_code ec; std::filesystem::create_directories(m_projectRoot / "Saved", ec); std::ofstream out(m_projectRoot / "Saved" / "SourceControlChangelists.txt"); for (const auto& list : m_changeLists) { out << "LIST " << std::quoted(list.name) << '\n'; for (const auto& path : list.files) out << "FILE " << std::quoted(path) << '\n'; } }

void SourceControlPanel::Draw(const std::filesystem::path& projectRoot, bool* open) {
    Poll(); if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::SourceControl), open)) { ImGui::End(); return; }
    if (!m_initialized || projectRoot != m_projectRoot) { m_projectRoot = projectRoot; const Command root = RunGit(projectRoot, {"rev-parse", "--show-toplevel"}); if (root.exitCode == 0) { auto lines = Lines(root.output); if (!lines.empty()) m_repoRoot = lines.front(); } else { m_repoRoot.clear(); m_message = root.output.empty() ? "Git was not found or this project is not inside a repository." : root.output; } LoadChangeLists(); m_initialized = true; if (!m_repoRoot.empty()) Start(Action::Refresh); }
    ImGui::Text("Repository: %s", m_repoRoot.empty() ? "(none)" : m_repoRoot.string().c_str()); ImGui::SameLine(); if (ImGui::Button("Refresh") && !m_repoRoot.empty()) Start(Action::Refresh); if (m_running) { ImGui::SameLine(); ImGui::TextDisabled("Working..."); }
    if (m_repoRoot.empty()) { ImGui::TextWrapped("%s", m_message.c_str()); ImGui::End(); return; }
    if (ImGui::BeginTabBar("SourceControlTabs")) {
        if (ImGui::BeginTabItem("Changes")) {
            const auto conflictCount = static_cast<int>(std::count_if(m_files.begin(), m_files.end(), [](const FileState& file) { return file.conflict; }));
            ImGui::Text("%d changed file(s)", static_cast<int>(m_files.size()));
            if (conflictCount > 0) { ImGui::SameLine(); ImGui::TextColored({1.0f, 0.3f, 0.2f, 1.0f}, "| %d conflict(s) require resolution", conflictCount); }
            ImGui::InputTextWithHint("##SourceFilter", "Filter changed files", m_filter.data(), m_filter.size()); ImGui::SameLine(); if (ImGui::Button("Select All")) for (auto& f : m_files) if (InActiveChangeList(f.path)) f.selected = true; ImGui::SameLine(); if (ImGui::Button("Clear")) for (auto& f : m_files) f.selected = false;
            const std::string filter = Lower(m_filter.data()); if (ImGui::BeginTable("ChangedFiles", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0, 280})) { ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 42); ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 70); ImGui::TableSetupColumn("Path"); ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthFixed, 90); ImGui::TableHeadersRow(); for (std::size_t i = 0; i < m_files.size(); ++i) { auto& file = m_files[i]; if (!filter.empty() && Lower(file.path).find(filter) == std::string::npos) continue; if (!InActiveChangeList(file.path)) continue; ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Checkbox("##Selected", &file.selected); ImGui::TableSetColumnIndex(1); if (file.conflict) ImGui::TextColored({1,.3f,.2f,1}, "CONFLICT"); else ImGui::Text("%c%c", file.index, file.worktree); ImGui::TableSetColumnIndex(2); if (ImGui::Selectable(file.path.c_str())) { for (auto& f : m_files) f.selected = false; file.selected = true; Start(Action::Diff, {file.path}); } ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(file.index != ' ' && file.index != '?' ? "Staged" : "Working"); ImGui::PopID(); } ImGui::EndTable(); }
            const auto paths = SelectedPaths(); if (ImGui::Button("Stage Selected") && !paths.empty()) Start(Action::Stage, paths); ImGui::SameLine(); if (ImGui::Button("Unstage Selected") && !paths.empty()) Start(Action::Unstage, paths); ImGui::SameLine(); ImGui::Checkbox("Staged Diff", &m_showStagedDiff); ImGui::SameLine(); if (ImGui::Button("View Diff") && !paths.empty()) Start(Action::Diff, paths);
            ImGui::InputTextWithHint("Commit Message", "Describe this change", m_commitMessage.data(), m_commitMessage.size()); ImGui::SameLine(); if (ImGui::Button("Commit Staged") && m_commitMessage[0] && !m_running) { Start(Action::Commit, {}, m_commitMessage.data()); m_commitMessage[0] = '\0'; }
            ImGui::SeparatorText("Diff"); ImGui::BeginChild("SourceDiff", {0, 220}, true, ImGuiWindowFlags_HorizontalScrollbar); ImGui::TextUnformatted(m_diff.empty() ? "Select a changed file or press View Diff." : m_diff.c_str()); ImGui::EndChild(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("History")) { if (ImGui::BeginTable("GitHistory", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0, 500})) { ImGui::TableSetupColumn("Commit", ImGuiTableColumnFlags_WidthFixed, 80); ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 90); ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthFixed, 140); ImGui::TableSetupColumn("Subject"); ImGui::TableHeadersRow(); for (std::size_t i = 0; i < m_commits.size(); ++i) { const auto& commit = m_commits[i]; ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); if (ImGui::Selectable(commit.hash.c_str(), m_selectedCommit == static_cast<int>(i))) { m_selectedCommit = static_cast<int>(i); Start(Action::ShowCommit, {}, commit.hash); } ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(commit.date.c_str()); ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(commit.author.c_str()); ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(commit.subject.c_str()); ImGui::PopID(); } ImGui::EndTable(); } ImGui::SeparatorText("Selected Commit"); ImGui::BeginChild("CommitDetails", {0, 180}, true, ImGuiWindowFlags_HorizontalScrollbar); ImGui::TextUnformatted(m_diff.empty() ? "Select a commit to inspect it." : m_diff.c_str()); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Branches")) { ImGui::InputTextWithHint("##NewBranch", "new branch name", m_newBranch.data(), m_newBranch.size()); ImGui::SameLine(); if (ImGui::Button("Create + Switch") && m_newBranch[0]) { Start(Action::CreateBranch, {}, m_newBranch.data()); m_newBranch[0] = '\0'; } for (std::size_t i = 0; i < m_branches.size(); ++i) { const auto& branch = m_branches[i]; ImGui::PushID(static_cast<int>(i)); const std::string label = std::string(branch.current ? "* " : "  ") + branch.name; if (ImGui::Selectable(label.c_str(), m_selectedBranch == static_cast<int>(i))) m_selectedBranch = static_cast<int>(i); ImGui::SameLine(); ImGui::TextDisabled("%s %s", branch.upstream.c_str(), branch.tracking.c_str()); ImGui::PopID(); } if (m_selectedBranch >= 0 && m_selectedBranch < static_cast<int>(m_branches.size()) && !m_branches[static_cast<std::size_t>(m_selectedBranch)].current && ImGui::Button("Switch to Selected")) Start(Action::SwitchBranch, {}, m_branches[static_cast<std::size_t>(m_selectedBranch)].name); ImGui::TextDisabled("Branch switching is refused by Git when it would overwrite local work."); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Change Lists")) { ImGui::TextDisabled("Editor change lists group files without changing Git's staging area."); if (ImGui::Selectable("All Changes", m_activeChangeList == -1)) m_activeChangeList = -1; for (std::size_t i = 0; i < m_changeLists.size(); ++i) { ImGui::PushID(static_cast<int>(i)); if (ImGui::Selectable((m_changeLists[i].name + " (" + std::to_string(m_changeLists[i].files.size()) + ")").c_str(), m_activeChangeList == static_cast<int>(i))) m_activeChangeList = static_cast<int>(i); ImGui::PopID(); } ImGui::InputTextWithHint("##ChangeListName", "new change list", m_newChangeList.data(), m_newChangeList.size()); ImGui::SameLine(); if (ImGui::Button("Create") && m_newChangeList[0]) { m_changeLists.push_back({m_newChangeList.data(), {}}); m_activeChangeList = static_cast<int>(m_changeLists.size() - 1); m_newChangeList[0] = '\0'; SaveChangeLists(); } if (m_activeChangeList >= 0 && m_activeChangeList < static_cast<int>(m_changeLists.size())) { auto& list = m_changeLists[static_cast<std::size_t>(m_activeChangeList)]; if (ImGui::Button("Add Selected Changed Files")) { for (const auto& path : SelectedPaths()) if (std::find(list.files.begin(), list.files.end(), path) == list.files.end()) list.files.push_back(path); SaveChangeLists(); } ImGui::SameLine(); if (ImGui::Button("Delete Change List")) { m_changeLists.erase(m_changeLists.begin() + m_activeChangeList); m_activeChangeList = -1; SaveChangeLists(); } for (std::size_t i = 0; i < list.files.size(); ++i) { ImGui::PushID(static_cast<int>(i)); ImGui::BulletText("%s", list.files[i].c_str()); ImGui::SameLine(); if (ImGui::SmallButton("Remove")) { list.files.erase(list.files.begin() + static_cast<std::ptrdiff_t>(i)); SaveChangeLists(); ImGui::PopID(); break; } ImGui::PopID(); } } ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    if (!m_message.empty()) { ImGui::Separator(); ImGui::TextWrapped("%s", m_message.c_str()); } ImGui::End();
}
