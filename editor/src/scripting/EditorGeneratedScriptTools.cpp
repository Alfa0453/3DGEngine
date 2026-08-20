#include "EditorGeneratedScriptTools.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace EditorGeneratedScriptTools {
namespace {

struct Registration {
    std::string className;
    bool behaviorTree = false;
};

bool IsClassName(const std::string& name) {
    if (name.empty()
        || (!std::isalpha(static_cast<unsigned char>(name.front()))
            && name.front() != '_')) {
        return false;
    }
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    });
}

bool WriteText(const std::filesystem::path& path,
               const std::string& text,
               std::string* error) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error) *error = "Could not create " + path.parent_path().string()
                + ": " + ec.message();
            return false;
        }
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        if (error) *error = "Could not write " + path.string();
        return false;
    }
    output << text;
    if (!output) {
        if (error) *error = "Writing failed for " + path.string();
        return false;
    }
    return true;
}

// Only writes when the content differs, so regenerating an unchanged header (e.g. on every
// editor launch) doesn't bump its timestamp and force a needless script rebuild.
bool WriteTextIfChanged(const std::filesystem::path& path,
                        const std::string& text,
                        std::string* error) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        std::ifstream existing(path, std::ios::binary);
        if (existing) {
            const std::string current((std::istreambuf_iterator<char>(existing)),
                                      std::istreambuf_iterator<char>());
            if (current == text) return true;
        }
    }
    return WriteText(path, text, error);
}

const char* TemplateFilename(BehaviorTreeTemplate scriptTemplate) {
    switch (scriptTemplate) {
    case BehaviorTreeTemplate::Task:      return "TaskTemplate.h";
    case BehaviorTreeTemplate::Decorator: return "DecoratorTemplate.h";
    case BehaviorTreeTemplate::Service:   return "ServiceTemplate.h";
    }
    return "";
}

const char* TemplatePlaceholder(BehaviorTreeTemplate scriptTemplate) {
    switch (scriptTemplate) {
    case BehaviorTreeTemplate::Task:      return "MyTask";
    case BehaviorTreeTemplate::Decorator: return "MyDecorator";
    case BehaviorTreeTemplate::Service:   return "MyService";
    }
    return "";
}

bool ReadTemplate(BehaviorTreeTemplate scriptTemplate,
                  std::string* source,
                  std::string* error) {
    const std::filesystem::path relative =
        std::filesystem::path("editor") / "btscripts" / "templates"
        / TemplateFilename(scriptTemplate);
    const std::filesystem::path candidates[] = {
#ifdef THREEDG_BT_SCRIPT_TEMPLATE_DIR
        std::filesystem::path(THREEDG_BT_SCRIPT_TEMPLATE_DIR)
            / TemplateFilename(scriptTemplate),
#endif
        std::filesystem::current_path() / relative,
        std::filesystem::current_path().parent_path() / relative
    };
    for (const std::filesystem::path& path : candidates) {
        std::ifstream input(path, std::ios::binary);
        if (!input) continue;
        *source = std::string(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>());
        return true;
    }
    if (error) {
        *error = "Could not find " + std::string(TemplateFilename(scriptTemplate))
            + " in editor/btscripts/templates.";
    }
    return false;
}

bool IsGameModuleRoot(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path / "CMakeLists.txt", ec)
        && std::filesystem::is_regular_file(
            path / "include" / "game" / "GameModule.h", ec);
}

std::filesystem::path FindGameModuleRoot(
    const std::filesystem::path& contentRoot) {
#ifdef THREEDG_GAME_MODULE_DIR
    const std::filesystem::path configured(THREEDG_GAME_MODULE_DIR);
    if (IsGameModuleRoot(configured)) return configured;
#endif
    std::error_code ec;
    const std::filesystem::path starts[] = {
        std::filesystem::absolute(contentRoot, ec),
        std::filesystem::current_path(ec)
    };
    for (std::filesystem::path start : starts) {
        for (int depth = 0; depth < 10 && !start.empty(); ++depth) {
            if (IsGameModuleRoot(start)) return start;
            if (IsGameModuleRoot(start / "game")) return start / "game";
            const std::filesystem::path parent = start.parent_path();
            if (parent == start) break;
            start = parent;
        }
    }
    return {};
}

std::filesystem::path ProjectRootFor(const std::filesystem::path& scriptRoot) {
    return scriptRoot.parent_path().parent_path();
}

std::filesystem::path ProjectScriptListPath(const std::filesystem::path& scriptRoot) {
    return ProjectRootFor(scriptRoot) / "Intermediate" / "Scripts" / "EditorScripts.list";
}

bool WriteRegistrations(const std::filesystem::path& listPath,
                        const std::filesystem::path& scriptRoot,
                        std::vector<Registration> registrations,
                        std::string* error) {
    registrations.erase(std::remove_if(registrations.begin(), registrations.end(),
        [&](const Registration& registration) {
            std::error_code ec;
            return !std::filesystem::is_regular_file(
                scriptRoot / (registration.className + ".h"), ec);
        }), registrations.end());
    std::sort(registrations.begin(), registrations.end(),
        [](const Registration& a, const Registration& b) {
            return a.className < b.className;
        });

    std::ostringstream list;
    list << "# Generated script registrations: `gameplay ClassName` or `bt ClassName`.\n"
         << "# This file is maintained by the editor. Empty lines and comments are ignored.\n";
    for (const Registration& registration : registrations) {
        list << (registration.behaviorTree ? "bt " : "gameplay ")
             << registration.className << '\n';
    }
    if (!WriteText(listPath, list.str(), error)) return false;

    std::ostringstream registry;
    registry << "#pragma once\n\n"
             << "#include <engine/gameplay/Script.h>\n"
             << "#include <engine/ai/BtScript.h>\n"
             << "#include <memory>\n";
    for (const Registration& registration : registrations) {
        std::error_code ec;
        const std::filesystem::path header = std::filesystem::absolute(
            scriptRoot / (registration.className + ".h"), ec).lexically_normal();
        registry << "#include \"" << header.generic_string() << "\"\n";
    }
    registry << "\n// Generated by the editor. Changes are replaced when scripts are created.\n"
             << "inline void RegisterEditorGeneratedScripts(engine::ScriptRegistry& scripts) {\n";
    bool hasGameplay = false;
    for (const Registration& registration : registrations) {
        if (registration.behaviorTree) continue;
        hasGameplay = true;
        registry << "    scripts.Register(\"" << registration.className
                 << "\", [] { return std::make_unique<" << registration.className
                 << ">(); });\n";
    }
    if (!hasGameplay) registry << "    (void)scripts;\n";
    registry << "}\n\n"
             << "inline void RegisterEditorGeneratedBtScripts(engine::ai::BtScriptRegistry& scripts) {\n";
    bool hasBehaviorTree = false;
    for (const Registration& registration : registrations) {
        if (!registration.behaviorTree) continue;
        hasBehaviorTree = true;
        registry << "    scripts.Register(\"" << registration.className
                 << "\", [] { return std::make_unique<" << registration.className
                 << ">(); });\n";
    }
    if (!hasBehaviorTree) registry << "    (void)scripts;\n";
    registry << "}\n";
    const std::filesystem::path generatedDir = listPath.parent_path();
    if (!WriteTextIfChanged(generatedDir / "ProjectGeneratedScripts.h",
                            registry.str(), error)) return false;

    std::ostringstream module;
    module << "// Generated by 3DG Editor. Do not edit.\n"
           << "#include \"ProjectGeneratedScripts.h\"\n\n"
           << "#include <engine/gameplay/ScriptModule.h>\n\n"
           << "#if defined(_WIN32)\n"
           << "#define THREEDG_SCRIPT_EXPORT extern \"C\" __declspec(dllexport)\n"
           << "#else\n"
           << "#define THREEDG_SCRIPT_EXPORT extern \"C\"\n"
           << "#endif\n\n"
           << "THREEDG_SCRIPT_EXPORT void RegisterScriptModule(\n"
           << "    engine::ScriptRegistry& scripts, engine::ai::BtScriptRegistry& bt) {\n"
           << "    RegisterEditorGeneratedScripts(scripts);\n"
           << "    RegisterEditorGeneratedBtScripts(bt);\n"
           << "}\n\n"
           << "THREEDG_SCRIPT_EXPORT std::uint32_t Get3DGScriptApiVersion() {\n"
           << "    return engine::kScriptModuleApiVersion;\n"
           << "}\n";
    return WriteTextIfChanged(generatedDir / "ProjectScriptModule.cpp",
                              module.str(), error);
}

std::vector<Registration> ReadRegistrations(const std::filesystem::path& listPath) {
    std::vector<Registration> registrations;
    std::ifstream input(listPath);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        bool isBehaviorTree = false;
        if (line.rfind("bt ", 0) == 0) {
            isBehaviorTree = true;
            line.erase(0, 3);
        } else if (line.rfind("gameplay ", 0) == 0) {
            line.erase(0, 9);
        }
        if (IsClassName(line)) registrations.push_back({line, isBehaviorTree});
    }
    return registrations;
}

// Reads the project's list, migrating one-time from the old shared game-module list when
// the per-project file doesn't exist yet. WriteRegistrations then filters the result to the
// headers actually present in this project's Scripts folder, so a new/empty project stays
// empty while an existing project keeps exactly its own scripts.
std::vector<Registration> SeedRegistrations(const std::filesystem::path& listPath,
                                            const std::filesystem::path& gameRoot) {
    std::error_code ec;
    if (std::filesystem::exists(listPath, ec)) return ReadRegistrations(listPath);
    const std::filesystem::path legacyProjectList =
        listPath.parent_path().parent_path().parent_path() / "EditorScripts.list";
    if (std::filesystem::exists(legacyProjectList, ec)) {
        return ReadRegistrations(legacyProjectList);
    }
    if (gameRoot.empty()) return {};
    return ReadRegistrations(gameRoot / "EditorScripts.list");
}

} // namespace

bool RegisterScript(const std::filesystem::path& gameRoot,
                    const std::filesystem::path& scriptRoot,
                    const std::string& className,
                    bool behaviorTree,
                    std::string* error) {
    const std::filesystem::path listPath = ProjectScriptListPath(scriptRoot);
    std::vector<Registration> registrations = SeedRegistrations(listPath, gameRoot);
    // An empty class name means "just regenerate from the current list" (used when
    // switching projects); otherwise add/refresh this class.
    if (!className.empty()) {
        registrations.erase(std::remove_if(registrations.begin(), registrations.end(),
            [&](const Registration& registration) {
                return registration.className == className;
            }), registrations.end());
        registrations.push_back({className, behaviorTree});
    }
    return WriteRegistrations(listPath, scriptRoot, std::move(registrations), error);
}

bool RegenerateGeneratedScripts(const std::filesystem::path& contentRoot,
                                std::string* error) {
    const std::filesystem::path gameRoot = FindGameModuleRoot(contentRoot);
    const std::filesystem::path scriptRoot = contentRoot / "Scripts";
    const std::filesystem::path listPath = ProjectScriptListPath(scriptRoot);
    return WriteRegistrations(listPath, scriptRoot,
                              SeedRegistrations(listPath, gameRoot), error);
}

std::filesystem::path GeneratedScriptDirectory(
    const std::filesystem::path& contentRoot) {
    return contentRoot.parent_path() / "Intermediate" / "Scripts";
}

std::filesystem::path ProjectScriptBinary(
    const std::filesystem::path& contentRoot) {
    return contentRoot.parent_path() / "Binaries" / "game_scripts.dll";
}

bool CreateBehaviorTreeScript(const std::filesystem::path& contentRoot,
                              const std::string& className,
                              BehaviorTreeTemplate scriptTemplate,
                              std::string* createdPath,
                              std::string* error) {
    if (!IsClassName(className)) {
        if (error) *error = "Enter a valid C++ class name.";
        return false;
    }
    const std::filesystem::path gameRoot = FindGameModuleRoot(contentRoot);
    const std::filesystem::path scriptRoot = contentRoot / "Scripts";
    std::error_code ec;
    std::filesystem::create_directories(scriptRoot, ec);
    if (ec) {
        if (error) *error = "Could not create Content/Scripts: " + ec.message();
        return false;
    }

    const std::filesystem::path headerPath = scriptRoot / (className + ".h");
    if (!std::filesystem::exists(headerPath, ec)) {
        std::string source;
        if (!ReadTemplate(scriptTemplate, &source, error)) return false;
        const std::string placeholder = TemplatePlaceholder(scriptTemplate);
        std::size_t offset = 0;
        while ((offset = source.find(placeholder, offset)) != std::string::npos) {
            source.replace(offset, placeholder.size(), className);
            offset += className.size();
        }
        if (!WriteText(headerPath, source, error)) return false;
    }

    if (!RegisterScript(gameRoot, scriptRoot, className, true, error)) return false;
    if (createdPath) *createdPath = headerPath.lexically_normal().string();
    return true;
}

} // namespace EditorGeneratedScriptTools
