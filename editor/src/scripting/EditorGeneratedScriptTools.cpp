#include "EditorGeneratedScriptTools.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace EditorGeneratedScriptTools {
namespace {

struct Registration {
    std::string className;
    bool behaviorTree = false;
    std::string cxxTypeName;
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

std::string StripCppCommentsAndStrings(const std::string& source) {
    enum class State { Code, LineComment, BlockComment, String, Character };
    State state = State::Code;
    bool escaped = false;
    std::string result(source.size(), ' ');
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        const char next = i + 1 < source.size() ? source[i + 1] : '\0';
        if (state == State::Code) {
            if (c == '/' && next == '/') { state = State::LineComment; ++i; continue; }
            if (c == '/' && next == '*') { state = State::BlockComment; ++i; continue; }
            if (c == '"') { state = State::String; escaped = false; continue; }
            if (c == '\'') { state = State::Character; escaped = false; continue; }
            result[i] = c;
        } else if (state == State::LineComment) {
            if (c == '\n') { state = State::Code; result[i] = c; }
        } else if (state == State::BlockComment) {
            if (c == '*' && next == '/') { state = State::Code; ++i; }
        } else {
            if (!escaped && ((state == State::String && c == '"')
                || (state == State::Character && c == '\''))) {
                state = State::Code;
            }
            escaped = !escaped && c == '\\';
            if (c == '\n') { state = State::Code; result[i] = c; escaped = false; }
        }
    }
    return result;
}

bool DetectScriptDeclaration(const std::string& source, const std::string& className,
                             bool* behaviorTree, std::string* cxxTypeName) {
    const std::string code = StripCppCommentsAndStrings(source);
    const std::regex declaration(
        "(?:class|struct)\\s+" + className
        + "\\s*(?:final\\s*)?:\\s*public\\s+"
          "(engine::ai::BtScript|engine::Script)\\b");
    std::smatch match;
    if (!std::regex_search(code, match, declaration)) return false;
    if (behaviorTree) *behaviorTree = match[1].str() == "engine::ai::BtScript";
    std::vector<std::string> scopes;
    const std::string prefix = code.substr(0, static_cast<std::size_t>(match.position()));
    const std::regex scopeToken(
        "namespace\\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\\s*\\{|[{}]");
    for (std::sregex_iterator it(prefix.begin(), prefix.end(), scopeToken), end;
         it != end; ++it) {
        const std::string token = it->str();
        if (token.front() == '}') {
            if (!scopes.empty()) scopes.pop_back();
        } else if (token.front() == '{') {
            scopes.emplace_back();
        } else {
            scopes.push_back((*it)[1].str());
        }
    }
    std::string qualified;
    for (const std::string& scope : scopes) {
        // A script nested in a class/function or anonymous namespace cannot be
        // named safely from the generated module translation unit.
        if (scope.empty()) return false;
        if (!qualified.empty()) qualified += "::";
        qualified += scope;
    }
    if (!qualified.empty()) qualified += "::";
    qualified += className;
    if (cxxTypeName) *cxxTypeName = std::move(qualified);
    return true;
}

std::string NormalizeLineEndings(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            normalized.push_back('\n');
        } else {
            normalized.push_back(text[i]);
        }
    }
    return normalized;
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
    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = "Could not write " + path.string();
        return false;
    }
    output << NormalizeLineEndings(text);
    output.flush();
    if (!output) {
        if (error) *error = "Writing failed for " + path.string();
        output.close();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    output.close();
#if defined(_WIN32)
    if (!MoveFileExW(temporary.wstring().c_str(), path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = "Could not replace " + path.string();
        std::filesystem::remove(temporary, ec);
        return false;
    }
#else
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        if (error) *error = "Could not replace " + path.string() + ": " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
#endif
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
            if (NormalizeLineEndings(current) == NormalizeLineEndings(text)) return true;
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

std::filesystem::path FindScriptHeader(const std::filesystem::path& scriptRoot,
                                       const std::string& className,
                                       bool* ambiguous = nullptr) {
    if (ambiguous) *ambiguous = false;
    std::filesystem::path found;
    std::error_code ec;
    if (!std::filesystem::is_directory(scriptRoot, ec)) return {};
    for (std::filesystem::recursive_directory_iterator it(
             scriptRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string extension = it->path().extension().string();
        if ((extension != ".h" && extension != ".hpp")
            || it->path().stem().string() != className) continue;
        if (!found.empty()) {
            if (ambiguous) *ambiguous = true;
            return {};
        }
        found = it->path();
    }
    return found;
}

bool WriteRegistrations(const std::filesystem::path& listPath,
                        const std::filesystem::path& scriptRoot,
                        std::vector<Registration> registrations,
                        std::string* error) {
    bool ambiguousHeader = false;
    registrations.erase(std::remove_if(registrations.begin(), registrations.end(),
        [&](const Registration& registration) {
            bool ambiguous = false;
            const std::filesystem::path header = FindScriptHeader(
                scriptRoot, registration.className, &ambiguous);
            if (ambiguous) {
                ambiguousHeader = true;
                if (error) *error = "More than one script header is named "
                    + registration.className + ". Use unique script class/file names.";
            }
            return header.empty();
        }), registrations.end());
    if (ambiguousHeader) return false;
    std::sort(registrations.begin(), registrations.end(),
        [](const Registration& a, const Registration& b) {
            return a.className < b.className;
        });

    std::ostringstream list;
    list << "# Generated script registrations: `gameplay ClassName` or `bt ClassName`.\n"
         << "# This file is maintained by the editor. Empty lines and comments are ignored.\n";
    for (const Registration& registration : registrations) {
        list << (registration.behaviorTree ? "bt " : "gameplay ")
             << registration.className;
        if (!registration.cxxTypeName.empty()
            && registration.cxxTypeName != registration.className)
            list << ' ' << registration.cxxTypeName;
        list << '\n';
    }
    std::ostringstream registry;
    registry << "#pragma once\n\n"
             << "#include <engine/gameplay/Script.h>\n"
             << "#include <engine/ai/BtScript.h>\n"
             << "#include <memory>\n";
    for (const Registration& registration : registrations) {
        std::error_code ec;
        const std::filesystem::path header = std::filesystem::absolute(
            FindScriptHeader(scriptRoot, registration.className), ec).lexically_normal();
        registry << "#include \"" << header.generic_string() << "\"\n";
    }
    registry << "\n// Generated by the editor. Changes are replaced when scripts are created.\n"
             << "inline void RegisterEditorGeneratedScripts(engine::ScriptRegistry& scripts) {\n";
    bool hasGameplay = false;
    for (const Registration& registration : registrations) {
        if (registration.behaviorTree) continue;
        hasGameplay = true;
        const std::string& typeName = registration.cxxTypeName.empty()
            ? registration.className : registration.cxxTypeName;
        registry << "    scripts.Register(\"" << registration.className
                 << "\", [] { return std::make_unique<" << typeName
                 << ">(); });\n";
    }
    if (!hasGameplay) registry << "    (void)scripts;\n";
    registry << "}\n\n"
             << "inline void RegisterEditorGeneratedBtScripts(engine::ai::BtScriptRegistry& scripts) {\n";
    bool hasBehaviorTree = false;
    for (const Registration& registration : registrations) {
        if (!registration.behaviorTree) continue;
        hasBehaviorTree = true;
        const std::string& typeName = registration.cxxTypeName.empty()
            ? registration.className : registration.cxxTypeName;
        registry << "    scripts.Register(\"" << registration.className
                 << "\", [] { return std::make_unique<" << typeName
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
           << "#include <engine/gameplay/ScriptModuleAbi.h>\n\n"
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
           << "}\n\n"
           << "THREEDG_SCRIPT_EXPORT engine::script::ScriptModuleInfo Get3DGScriptModuleInfo() {\n"
           << "    engine::script::ScriptModuleInfo info;\n"
           << "    info.moduleBuildId = engine::script::StableId(__DATE__ \" \" __TIME__);\n"
           << "    return info;\n"
           << "}\n";
    if (!WriteTextIfChanged(generatedDir / "ProjectScriptModule.cpp",
                            module.str(), error)) return false;
    // Commit the source-of-truth list last. If either generated C++ file could not be
    // replaced, the prior list remains intact and the failed generation is retryable.
    return WriteTextIfChanged(listPath, list.str(), error);
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
        std::istringstream entry(line);
        std::string className;
        std::string cxxTypeName;
        entry >> className >> cxxTypeName;
        if (IsClassName(className)) {
            if (cxxTypeName.empty()) cxxTypeName = className;
            registrations.push_back({className, isBehaviorTree, cxxTypeName});
        }
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
    if (!className.empty() && !IsClassName(className)) {
        if (error) *error = "Invalid C++ script class name: " + className;
        return false;
    }
    const std::filesystem::path listPath = ProjectScriptListPath(scriptRoot);
    std::vector<Registration> registrations = SeedRegistrations(listPath, gameRoot);
    // An empty class name means "just regenerate from the current list" (used when
    // switching projects); otherwise add/refresh this class.
    if (!className.empty()) {
        registrations.erase(std::remove_if(registrations.begin(), registrations.end(),
            [&](const Registration& registration) {
                return registration.className == className;
            }), registrations.end());
        registrations.push_back({className, behaviorTree, className});
    }
    return WriteRegistrations(listPath, scriptRoot, std::move(registrations), error);
}

bool RegenerateGeneratedScripts(const std::filesystem::path& contentRoot,
                                std::string* error) {
    const std::filesystem::path scriptRoot = contentRoot / "Scripts";
    const std::filesystem::path listPath = ProjectScriptListPath(scriptRoot);
    std::vector<Registration> registrations;

    // Treat Content/Scripts as the source of truth. This registers valid script
    // headers copied in from an IDE as well as editor-created files, including
    // scripts organized in subfolders.
    std::error_code ec;
    if (std::filesystem::is_directory(scriptRoot, ec)) {
        for (std::filesystem::recursive_directory_iterator it(
                 scriptRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string extension = it->path().extension().string();
            if (extension != ".h" && extension != ".hpp") continue;
            const std::string className = it->path().stem().string();
            if (!IsClassName(className)) continue;
            std::ifstream source(it->path(), std::ios::binary);
            const std::string contents((std::istreambuf_iterator<char>(source)),
                                       std::istreambuf_iterator<char>());
            bool behaviorTree = false;
            std::string cxxTypeName;
            if (!DetectScriptDeclaration(
                    contents, className, &behaviorTree, &cxxTypeName)) continue;
            registrations.push_back({className, behaviorTree, std::move(cxxTypeName)});
        }
        if (ec) {
            if (error) *error = "Could not scan Content/Scripts: " + ec.message();
            return false;
        }
    }
    return WriteRegistrations(listPath, scriptRoot, std::move(registrations), error);
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
    bool createdHeader = false;
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
        createdHeader = true;
    }

    if (!RegisterScript(gameRoot, scriptRoot, className, true, error)) {
        if (createdHeader) {
            ec.clear();
            std::filesystem::remove(headerPath, ec);
            std::string ignored;
            RegenerateGeneratedScripts(contentRoot, &ignored);
        }
        return false;
    }
    if (createdPath) *createdPath = headerPath.lexically_normal().string();
    return true;
}

} // namespace EditorGeneratedScriptTools
