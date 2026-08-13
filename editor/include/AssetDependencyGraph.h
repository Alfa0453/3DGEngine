#pragma once

#include <engine/assets/AssetRegistry.h>

#include <string>
#include <unordered_map>
#include <vector>

class AssetDependencyGraph {
public:
    enum class IssueKind { Missing, Circular, DuplicateDependency, DuplicateContent, Unreferenced, StalePath };
    struct Issue {
        IssueKind kind = IssueKind::Missing;
        engine::AssetHandle asset;
        engine::AssetHandle related;
        std::string message;
    };
    struct Node {
        engine::AssetRegistryEntry asset;
        std::vector<engine::AssetHandle> outgoing;
        std::vector<engine::AssetHandle> incoming;
        std::vector<std::size_t> issues;
    };

    void Build(const engine::AssetRegistry& registry, const std::string& contentRoot = {});
    const Node* Find(engine::AssetHandle id) const;
    const std::vector<Node>& Nodes() const { return m_nodes; }
    const std::vector<Issue>& Issues() const { return m_issues; }
    std::vector<const Node*> Search(const std::string& text,
                                    engine::AssetType type = engine::AssetType::Unknown,
                                    bool issuesOnly = false) const;
    bool HasCircularDependency(engine::AssetHandle id) const;

private:
    void AddIssue(Issue issue);
    std::vector<Node> m_nodes;
    std::vector<Issue> m_issues;
    std::unordered_map<engine::AssetHandle, std::size_t, engine::AssetHandleHash> m_index;
};
