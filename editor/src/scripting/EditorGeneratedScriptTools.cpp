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

} // namespace

bool RegisterScript(const std::filesystem::path& gameRoot,
                    const std::filesystem::path& scriptRoot,
                    const std::string& className,
                    bool behaviorTree,
                    std::string* error) {
    const std::filesystem::path listPath = gameRoot / "EditorScripts.list";
    std::vector<Registration> registrations;
    {
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
    }
    registrations.erase(std::remove_if(registrations.begin(), registrations.end(),
        [&](const Registration& registration) {
            return registration.className == className;
        }), registrations.end());
    registrations.push_back({className, behaviorTree});
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
    const std::filesystem::path includeBase = scriptRoot.parent_path();
    for (const Registration& registration : registrations) {
        std::error_code ec;
        const std::filesystem::path header = std::filesystem::absolute(
            scriptRoot / (registration.className + ".h"), ec).lexically_normal();
        std::filesystem::path includePath = std::filesystem::relative(header, includeBase, ec);
        if (ec) includePath = header;
        registry << "#include \"" << includePath.generic_string() << "\"\n";
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
    return WriteText(gameRoot / "include" / "game" / "EditorGeneratedScripts.h",
                     registry.str(), error);
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
    if (gameRoot.empty()) {
        if (error) {
            *error = "Could not find the shared game module. Expected a game folder "
                "with CMakeLists.txt and include/game/GameModule.h near the editor.";
        }
        return false;
    }

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
