#include "AssetReferenceRepair.h"

#include <engine/assets/AssetIdentity.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace {
namespace fs = std::filesystem;
std::string Lower(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value; }
std::string Slashes(std::string value) { std::replace(value.begin(), value.end(), '\\', '/'); return value; }
std::string Read(const fs::path& path) { std::ifstream in(path, std::ios::binary); return in ? std::string(std::istreambuf_iterator<char>(in), {}) : std::string{}; }
bool IsAuthored(const fs::path& path) {
    static const std::vector<std::string> extensions = {".scene", ".hud", ".btgraph", ".particle", ".particlefx", ".3dgcharacter", ".3dgclip", ".3dggraph", ".3dgmat", ".3dgshader", ".3dgragdoll", ".3dgretarget", ".3dgability", ".3dgprefab", ".3dgweather", ".3dgbuilding", ".3dgroad", ".3dgscatter", ".3dgbiome", ".3dgdaynight", ".3dgcave", ".3dgfence", ".3dgdestruction", ".3dginteraction", ".3dgportal", ".3dgquest", ".3dgdialogue", ".3dgitem", ".3dgcombat", ".3dgspawn", ".3dgsaveprofile", ".3dgikrig", ".3dgpose", ".3dgequipment", ".3dgvs", ".3dgloc", ".3dgworld"};
    const std::string ext = Lower(path.extension().string()); return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}
bool LooksLikeAssetPath(const std::string& value) {
    const std::string ext = Lower(fs::path(value).extension().string());
    return value.find('/') != std::string::npos || value.find('\\') != std::string::npos
        || ext.rfind(".3dg", 0) == 0 || ext == ".particle" || ext == ".particlefx" || ext == ".hud" || ext == ".scene" || ext == ".btgraph";
}
std::string VirtualFor(const fs::path& path, const fs::path& root) {
    std::error_code ec; auto relative = fs::relative(path, root, ec); if (ec || relative.empty() || *relative.begin() == "..") return {};
    return engine::AssetRegistry::NormalizeVirtualPath("/Game/" + relative.generic_string());
}
fs::path Resolve(const std::string& value, const fs::path& root) {
    fs::path path(value); if (path.is_absolute()) return path;
    std::string clean = Slashes(value); if (clean.rfind("/Game/", 0) == 0) clean.erase(0, 6);
    else if (clean.rfind("Game/", 0) == 0) clean.erase(0, 5);
    else if (clean.rfind("Content/", 0) == 0) clean.erase(0, 8);
    return root / fs::path(clean);
}
std::string ReplacementStyle(const std::string& oldValue, const fs::path& replacement, const fs::path& root) {
    if (fs::path(oldValue).is_absolute()) return replacement.string();
    std::error_code ec; const std::string relative = fs::relative(replacement, root, ec).generic_string();
    if (oldValue.rfind("/Game/", 0) == 0) return "/Game/" + relative;
    if (oldValue.rfind("Game/", 0) == 0) return "Game/" + relative;
    if (oldValue.rfind("Content/", 0) == 0) return "Content/" + relative;
    return relative;
}
bool ParseHeaderId(const fs::path& path, engine::AssetHandle* id) {
    if (!IsAuthored(path)) return false; std::ifstream in(path); std::string magic, token; int version = 0;
    return static_cast<bool>(in >> magic >> version >> token) && engine::AssetHandle::Parse(token, id);
}
std::string Timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream out; out << std::put_time(&local, "%Y%m%d_%H%M%S"); return out.str();
}
}

const char* AssetReferenceRepair::KindName(Kind kind) { switch (kind) { case Kind::MissingPath:return "Missing path"; case Kind::MismatchedId:return "Mismatched ID"; case Kind::StaleRegistryPath:return "Moved asset"; case Kind::MissingDependency:return "Missing dependency"; case Kind::MissingSource:return "Missing import source"; } return "Issue"; }

void AssetReferenceRepair::Scan(const engine::AssetRegistry& registry, const std::string& contentRoot) {
    m_findings.clear(); const fs::path root(contentRoot); std::error_code ec;
    std::unordered_map<std::string, std::vector<fs::path>> byLeaf;
    std::unordered_map<engine::AssetHandle, fs::path, engine::AssetHandleHash> authoredById;
    std::vector<fs::path> authored;
    if (fs::is_directory(root, ec)) for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; } if (!it->is_regular_file(ec)) continue; const fs::path path = it->path();
        byLeaf[Lower(path.filename().string())].push_back(path);
        if (IsAuthored(path)) { authored.push_back(path); engine::AssetHandle id; if (ParseHeaderId(path, &id)) authoredById[id] = path; }
    }
    for (const auto& entry : registry.Entries()) {
        const fs::path registered = root / fs::path(entry.virtualPath.rfind("/Game/", 0) == 0 ? entry.virtualPath.substr(6) : entry.virtualPath);
        if (!fs::is_regular_file(registered, ec)) {
            auto actual = authoredById.find(entry.id);
            Finding finding; finding.kind = Kind::StaleRegistryPath; finding.asset = entry.id; finding.ownerPath = registered.string(); finding.oldValue = entry.virtualPath;
            if (actual != authoredById.end()) { finding.replacement = VirtualFor(actual->second, root); finding.repairable = !finding.replacement.empty(); finding.selected = finding.repairable; finding.message = "Registry path is stale; the same asset ID exists at " + finding.replacement; }
            else finding.message = "Registry path no longer exists and no authored asset with this ID was found.";
            m_findings.push_back(std::move(finding));
        }
        if (!entry.sourcePath.empty() && !fs::exists(entry.sourcePath, ec)) m_findings.push_back({Kind::MissingSource, registered.string(), entry.sourcePath, {}, "Original import source cannot be found. Reimport or locate the source manually.", entry.id, 0, 0, false, false});
        for (auto dependency : entry.dependencies) if (!registry.Find(dependency)) m_findings.push_back({Kind::MissingDependency, registered.string(), dependency.ToString(), {}, "Dependency ID is absent from the registry.", entry.id, 0, 0, false, false});
    }
    const std::regex quoted("\\\"([^\\\"\\r\\n]*)\\\""); const std::regex followingId("^\\s+([0-9a-fA-F]{32}|-)");
    for (const fs::path& owner : authored) {
        const std::string data = Read(owner); if (data.empty() || data.size() > 32u * 1024u * 1024u) continue;
        for (std::sregex_iterator it(data.begin(), data.end(), quoted), end; it != end; ++it) {
            const std::smatch& match = *it; const std::string value = match[1].str(); if (!LooksLikeAssetPath(value)) continue;
            const fs::path resolved = Resolve(value, root); const bool exists = fs::is_regular_file(resolved, ec);
            if (!exists) {
                auto candidates = byLeaf.find(Lower(fs::path(value).filename().string()));
                Finding finding; finding.kind = Kind::MissingPath; finding.ownerPath = owner.string(); finding.oldValue = value; finding.offset = static_cast<std::size_t>(match.position(1)); finding.length = static_cast<std::size_t>(match.length(1));
                if (candidates != byLeaf.end() && candidates->second.size() == 1) { finding.replacement = ReplacementStyle(value, candidates->second.front(), root); finding.repairable = true; finding.selected = true; finding.message = "Broken path has one filename match in Content."; }
                else finding.message = candidates == byLeaf.end() ? "Referenced path does not exist and no filename match was found." : "Referenced path is missing and has multiple possible filename matches.";
                const bool uniqueMatch = finding.repairable;
                const fs::path matchedPath = uniqueMatch ? candidates->second.front() : fs::path{};
                m_findings.push_back(std::move(finding));
                if (uniqueMatch) {
                    const std::string candidateVirtual = VirtualFor(matchedPath, root);
                    const auto* target = candidateVirtual.empty() ? nullptr : registry.FindByPath(candidateVirtual);
                    const std::size_t after = static_cast<std::size_t>(match.position() + match.length());
                    const std::string tail = data.substr(after, std::min<std::size_t>(96, data.size() - after)); std::smatch idMatch;
                    if (target && std::regex_search(tail, idMatch, followingId) && idMatch[1].str() != target->id.ToString()) {
                        m_findings.push_back({Kind::MismatchedId, owner.string(), idMatch[1].str(), target->id.ToString(), "Recovered path resolves to this stable asset ID.", target->id, after + static_cast<std::size_t>(idMatch.position(1)), static_cast<std::size_t>(idMatch.length(1)), true, true});
                    }
                }
                continue;
            }
            const std::string virtualPath = VirtualFor(resolved, root); const auto* target = virtualPath.empty() ? nullptr : registry.FindByPath(virtualPath); if (!target) continue;
            const std::size_t after = static_cast<std::size_t>(match.position() + match.length()); const std::string tail = data.substr(after, std::min<std::size_t>(96, data.size() - after)); std::smatch idMatch;
            if (std::regex_search(tail, idMatch, followingId)) { const std::string oldId = idMatch[1].str(); if (oldId != target->id.ToString()) { Finding finding; finding.kind = Kind::MismatchedId; finding.ownerPath = owner.string(); finding.oldValue = oldId; finding.replacement = target->id.ToString(); finding.message = "Path resolves to a different stable asset ID."; finding.asset = target->id; finding.offset = after + static_cast<std::size_t>(idMatch.position(1)); finding.length = static_cast<std::size_t>(idMatch.length(1)); finding.repairable = true; finding.selected = true; m_findings.push_back(std::move(finding)); } }
        }
    }
    int repairable = 0; for (const auto& f : m_findings) if (f.repairable) ++repairable;
    m_summary = "Scanned " + std::to_string(authored.size()) + " authored assets; found " + std::to_string(m_findings.size()) + " issue(s), " + std::to_string(repairable) + " repairable.";
}

AssetReferenceRepair::ApplyResult AssetReferenceRepair::Apply(engine::AssetRegistry& registry, const std::string& contentRoot) {
    ApplyResult result; const fs::path root(contentRoot); const fs::path projectRoot = root.parent_path(); const fs::path backup = projectRoot / "Saved" / "AssetReferenceRepair" / Timestamp(); result.backupRoot = backup.string(); std::error_code ec; const engine::AssetRegistry registryBefore = registry;
    std::unordered_map<std::string, std::vector<const Finding*>> edits;
    std::vector<const Finding*> registryMoves;
    for (const auto& finding : m_findings) if (finding.selected && finding.repairable) { if (finding.kind == Kind::StaleRegistryPath) registryMoves.push_back(&finding); else edits[finding.ownerPath].push_back(&finding); }
    struct Prepared { fs::path owner, backup; std::string data; int repairs = 0; }; std::vector<Prepared> prepared;
    for (auto& pair : edits) {
        const fs::path owner(pair.first); std::string data = Read(owner); if (data.empty()) { result.error = "Could not read asset before repair: " + owner.string(); return result; }
        auto& changes = pair.second; std::sort(changes.begin(), changes.end(), [](const Finding* a, const Finding* b) { return a->offset > b->offset; });
        int repairCount = 0; for (const Finding* finding : changes) { if (finding->offset + finding->length > data.size() || data.substr(finding->offset, finding->length) != finding->oldValue) { result.error = "Asset changed since scan; rescan before applying: " + owner.string(); return result; } data.replace(finding->offset, finding->length, finding->replacement); ++repairCount; }
        fs::path relative = fs::relative(owner, root, ec); if (ec) { ec.clear(); relative = owner.filename(); } prepared.push_back({owner, backup / relative, std::move(data), repairCount});
    }
    for (const auto& item : prepared) { fs::create_directories(item.backup.parent_path(), ec); if (ec) { result.error = "Could not create repair backup folder: " + ec.message(); return result; } fs::copy_file(item.owner, item.backup, fs::copy_options::overwrite_existing, ec); if (ec) { result.error = "Could not create repair backup: " + ec.message(); return result; } }
    std::vector<const Prepared*> written; auto rollback = [&] { for (const Prepared* item : written) { std::error_code ignored; fs::copy_file(item->backup, item->owner, fs::copy_options::overwrite_existing, ignored); } registry = registryBefore; };
    for (const auto& item : prepared) { std::ofstream out(item.owner, std::ios::binary | std::ios::trunc); out.write(item.data.data(), static_cast<std::streamsize>(item.data.size())); out.close(); if (!out) { result.error = "Could not write repaired asset: " + item.owner.string(); rollback(); return result; } written.push_back(&item); result.repaired += item.repairs; ++result.filesChanged; }
    for (const Finding* finding : registryMoves) { std::string error; if (!registry.Move(finding->asset, finding->replacement, &error)) { result.error = error; rollback(); return result; } ++result.repaired; }
    const fs::path registryPath(engine::AssetRegistry::DefaultRegistryPath(contentRoot)); std::string error; if (!registry.Save(registryPath.string(), &error)) { result.error = error; rollback(); std::string ignored; registryBefore.Save(registryPath.string(), &ignored); return result; }
    fs::create_directories(projectRoot / "Saved" / "Reports", ec); const fs::path report = projectRoot / "Saved" / "Reports" / ("AssetReferenceRepair_" + Timestamp() + ".txt"); std::ofstream log(report); log << "3DG Asset Reference Repair Report\n" << "Repaired: " << result.repaired << "\nFiles changed: " << result.filesChanged << "\nBackup: " << result.backupRoot << "\n\n"; for (const auto& finding : m_findings) if (finding.selected) log << KindName(finding.kind) << " | " << finding.ownerPath << " | " << finding.oldValue << " -> " << finding.replacement << '\n'; result.reportPath = report.string(); return result;
}
