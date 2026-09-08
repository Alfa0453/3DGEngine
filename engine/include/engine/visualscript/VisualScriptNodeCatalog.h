#pragma once

// =============================================================================
// Visual Scripting — Pass 3 : cached, searchable node catalog (Phases 5/6/20).
//
// The palette / context-search / pin-drag filter render from THIS cache, not by
// walking the registry + reflection metadata every frame. Built once from the
// VisualNodeRegistry (which already carries category + pin metadata from Pass 1/2)
// and rebuilt only when explicitly invalidated. Editor-facing but engine-side and
// GL/ImGui-free, so it stays testable and keeps the panel a thin view.
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptTypes.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace engine::vs {

struct CatalogPin {
    std::string  name;
    PinDirection direction = PinDirection::Input;
    PinKind      kind = PinKind::Data;
    ValueType    type = ValueType::Float;
};

struct CatalogEntry {
    std::string typeId;
    std::string displayName;
    std::string category;
    bool        pure = false;
    bool        isEvent = false;
    std::vector<CatalogPin> pins;
    std::string keywords;    // lowercased "typeId displayName category" for fast search

    bool HasCompatibleCounterpart(ValueType sourceType, PinKind sourceKind,
                                  PinDirection sourceDir) const {
        // A drag from a source pin wants a node with a pin of the OPPOSITE direction it can join.
        const PinDirection want = sourceDir == PinDirection::Output ? PinDirection::Input
                                                                    : PinDirection::Output;
        for (const CatalogPin& p : pins) {
            if (p.direction != want || p.kind != sourceKind) continue;
            if (sourceKind == PinKind::Exec) return true;
            const ValueType from = sourceDir == PinDirection::Output ? sourceType : p.type;
            const ValueType to   = sourceDir == PinDirection::Output ? p.type : sourceType;
            if (DataTypesCompatible(from, to)) return true;
        }
        return false;
    }
};

class VisualScriptNodeCatalog {
public:
    static VisualScriptNodeCatalog& Instance() { static VisualScriptNodeCatalog c; return c; }

    // Build once; cheap no-op afterwards (Phase 20). Call Invalidate() after registering new nodes.
    void EnsureBuilt() { if (!m_built) Rebuild(); }
    void Invalidate() { m_built = false; }

    void Rebuild() {
        m_entries.clear();
        const VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
        for (const std::string& typeId : reg.TypeIds()) {
            const NodeDescriptor* d = reg.Find(typeId);
            if (!d) continue;
            CatalogEntry entry;
            entry.typeId = d->typeId;
            entry.displayName = d->displayName.empty() ? d->typeId : d->displayName;
            entry.category = d->category.empty() ? std::string("Misc") : d->category;
            entry.pure = d->pure;
            entry.isEvent = d->IsEvent();
            for (const NodePinDesc& p : d->pins)
                entry.pins.push_back({p.name, p.direction, p.kind, p.type});
            entry.keywords = Lower(entry.typeId + " " + entry.displayName + " " + entry.category);
            m_entries.push_back(std::move(entry));
        }
        std::sort(m_entries.begin(), m_entries.end(), [](const CatalogEntry& a, const CatalogEntry& b) {
            if (a.category != b.category) return a.category < b.category;
            return a.displayName < b.displayName;
        });
        m_built = true;
    }

    const std::vector<CatalogEntry>& Entries() { EnsureBuilt(); return m_entries; }

    // Phase 6: substring search over name/category/typeId. Empty query returns everything.
    std::vector<const CatalogEntry*> Search(const std::string& query) {
        EnsureBuilt();
        std::vector<const CatalogEntry*> out;
        const std::string q = Lower(query);
        for (const CatalogEntry& e : m_entries)
            if (q.empty() || e.keywords.find(q) != std::string::npos) out.push_back(&e);
        return out;
    }

    // Phase 6: pin-drag filter — only nodes that can join the dragged pin, optionally text-filtered.
    std::vector<const CatalogEntry*> SearchForPin(const std::string& query, ValueType sourceType,
                                                  PinKind sourceKind, PinDirection sourceDir) {
        std::vector<const CatalogEntry*> out;
        for (const CatalogEntry* e : Search(query))
            if (e->HasCompatibleCounterpart(sourceType, sourceKind, sourceDir)) out.push_back(e);
        return out;
    }

    // Sorted unique category list for the palette tree.
    std::vector<std::string> Categories() {
        EnsureBuilt();
        std::vector<std::string> cats;
        for (const CatalogEntry& e : m_entries)
            if (cats.empty() || cats.back() != e.category)
                if (std::find(cats.begin(), cats.end(), e.category) == cats.end())
                    cats.push_back(e.category);
        std::sort(cats.begin(), cats.end());
        return cats;
    }

private:
    static std::string Lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
    bool m_built = false;
    std::vector<CatalogEntry> m_entries;
};

} // namespace engine::vs
