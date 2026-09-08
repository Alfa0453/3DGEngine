#pragma once

#include <array>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

class SourceControlPanel {
public:
    SourceControlPanel() = default;
    ~SourceControlPanel();
    void Draw(const std::filesystem::path& projectRoot, bool* open);

private:
    struct FileState { std::string path; char index = ' '; char worktree = ' '; bool selected = false; bool conflict = false; };
    struct Branch { std::string name, upstream, tracking; bool current = false; };
    struct Commit { std::string hash, author, date, subject; };
    struct ChangeList { std::string name; std::vector<std::string> files; };
    enum class Action { Refresh, Diff, ShowCommit, Stage, Unstage, Commit, SwitchBranch, CreateBranch };
    struct WorkResult { Action action = Action::Refresh; bool ok = false; std::string status, branches, history, diff, message; };

    void Start(Action action, std::vector<std::string> paths = {}, std::string value = {});
    void Poll();
    void ParseSnapshot(const WorkResult& result);
    void LoadChangeLists();
    void SaveChangeLists();
    std::vector<std::string> SelectedPaths() const;
    bool InActiveChangeList(const std::string& path) const;

    std::filesystem::path m_projectRoot, m_repoRoot;
    std::future<WorkResult> m_future;
    std::vector<FileState> m_files;
    std::vector<Branch> m_branches;
    std::vector<Commit> m_commits;
    std::vector<ChangeList> m_changeLists;
    std::string m_diff, m_message;
    std::array<char, 256> m_commitMessage{};
    std::array<char, 128> m_newBranch{};
    std::array<char, 128> m_newChangeList{};
    std::array<char, 160> m_filter{};
    int m_selectedBranch = -1;
    int m_selectedCommit = -1;
    int m_activeChangeList = -1;
    bool m_running = false;
    bool m_showStagedDiff = false;
    bool m_initialized = false;
};
