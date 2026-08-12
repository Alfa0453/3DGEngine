#include "engine/assets/ForgeMaterialImporter.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct Options {
    std::string source;
    std::string contentRoot;
    std::string destination;
    std::string editorAction = "refresh";
    bool json = false;
};

std::string EscapeJson(const std::string& value) {
    std::ostringstream output;
    for (unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20u) output << '?';
                else output << static_cast<char>(c);
        }
    }
    return output.str();
}

std::string SafeName(const std::string& value) {
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '_' || c == '-')
            result.push_back(static_cast<char>(c));
        else if (std::isspace(c) && !result.empty() && result.back() != '_')
            result.push_back('_');
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result.empty() ? "material" : result;
}

std::string SafeStem(const std::filesystem::path& path) {
    return SafeName(path.stem().string());
}

struct StagedSource {
    std::filesystem::path path;
    std::filesystem::path backup;
    bool copied = false;
    bool replaced = false;
};

bool StageSourcePackage(const std::filesystem::path& source,
                        const std::filesystem::path& destination,
                        StagedSource* staged, std::string* error) {
    staged->path = destination;
    std::error_code ec;
    const std::filesystem::path normalizedSource =
        std::filesystem::absolute(source, ec).lexically_normal();
    ec.clear();
    const std::filesystem::path normalizedDestination =
        std::filesystem::absolute(destination, ec).lexically_normal();
    if (!ec && normalizedSource == normalizedDestination) return true;

    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        *error = "Could not create Material Forge source cache: " + ec.message();
        return false;
    }
    const std::filesystem::path temporary = destination.string() + ".tmp";
    staged->backup = destination.string() + ".bak";
    std::filesystem::remove(temporary, ec);
    ec.clear();
    std::filesystem::copy_file(
        source, temporary, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        *error = "Could not stage Material Forge source package: " + ec.message();
        return false;
    }
    std::filesystem::remove(staged->backup, ec);
    ec.clear();
    if (std::filesystem::is_regular_file(destination, ec)) {
        ec.clear();
        std::filesystem::rename(destination, staged->backup, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            *error = "Could not preserve the previous Forge source package: " + ec.message();
            return false;
        }
        staged->replaced = true;
    }
    ec.clear();
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        const std::string message = ec.message();
        if (staged->replaced) {
            std::error_code restoreError;
            std::filesystem::rename(staged->backup, destination, restoreError);
        }
        std::filesystem::remove(temporary, ec);
        *error = "Could not commit Material Forge source package: " + message;
        return false;
    }
    staged->copied = true;
    return true;
}

void FinishStagedSource(const StagedSource& staged, bool success) {
    if (!staged.copied) return;
    std::error_code ec;
    if (success) {
        if (staged.replaced) std::filesystem::remove(staged.backup, ec);
        return;
    }
    std::filesystem::remove(staged.path, ec);
    if (staged.replaced) {
        ec.clear();
        std::filesystem::rename(staged.backup, staged.path, ec);
    }
}

bool WriteDeploySignal(const std::filesystem::path& content,
                       const engine::ForgeMaterialImportResult& result,
                       const std::string& operation,
                       const std::string& editorAction,
                       std::filesystem::path* signalPath,
                       std::string* requestId,
                       std::string* error) {
    *signalPath = content.parent_path() / "Intermediate" / "MaterialForge"
        / "deploy.signal";
    std::error_code ec;
    std::filesystem::create_directories(signalPath->parent_path(), ec);
    if (ec) {
        *error = "Material cooked, but the editor notification folder could not be created: "
            + ec.message();
        return false;
    }
    const std::filesystem::path temporary = signalPath->string() + ".tmp";
    *requestId = std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            *error = "Material cooked, but the editor notification could not be written.";
            return false;
        }
        output << "3DG_MATERIAL_FORGE_DEPLOY 2\n"
               << *requestId << '\n'
               << operation << '\n'
               << editorAction << '\n'
               << result.materialId.ToString() << '\n'
               << result.materialPath << '\n';
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, ec);
            *error = "Material cooked, but the editor notification could not be completed.";
            return false;
        }
    }
    std::filesystem::remove(*signalPath, ec);
    ec.clear();
    std::filesystem::rename(temporary, *signalPath, ec);
    if (ec) {
        const std::string message = ec.message();
        std::filesystem::remove(temporary, ec);
        *error = "Material cooked, but the editor notification could not be committed: "
            + message;
        return false;
    }
    return true;
}

bool ParseOptions(int argc, char** argv, Options* options, std::string* error) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--json") {
            options->json = true;
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            *error = "help";
            return false;
        }
        if (argument != "--source" && argument != "--content"
            && argument != "--destination" && argument != "--editor-action") {
            *error = "Unknown option: " + argument;
            return false;
        }
        if (++i >= argc) {
            *error = "Missing value after " + argument;
            return false;
        }
        if (argument == "--source") options->source = argv[i];
        else if (argument == "--content") options->contentRoot = argv[i];
        else if (argument == "--destination") options->destination = argv[i];
        else options->editorAction = argv[i];
    }
    if (options->source.empty() || options->contentRoot.empty()) {
        *error = "Both --source and --content are required.";
        return false;
    }
    if (options->destination.empty())
        options->destination = "Materials/" + SafeStem(options->source);
    if (options->editorAction != "refresh"
        && options->editorAction != "apply_selected") {
        *error = "--editor-action must be refresh or apply_selected.";
        return false;
    }
    return true;
}

bool SafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const std::filesystem::path& part : path)
        if (part == "..") return false;
    return true;
}

void PrintUsage() {
    std::cout
        << "3DGMaterialCooker --source <material.3dgtexpack> "
           "--content <project Content> [--destination <relative folder>] "
           "[--editor-action <refresh|apply_selected>] [--json]\n";
}

void PrintJsonError(const std::string& error) {
    std::cout << "{\"schema\":\"3dg.material-cook-result/1.3\","
              << "\"success\":false,\"error\":\""
              << EscapeJson(error) << "\"}\n";
}

void PrintJsonResult(const engine::ForgeMaterialImportResult& result,
                     const std::filesystem::path& sourcePath,
                     const std::filesystem::path& signalPath,
                     const std::filesystem::path& acknowledgementPath,
                     const std::string& requestId,
                     const std::string& operation,
                     const std::string& editorAction) {
    std::cout << "{\"schema\":\"3dg.material-cook-result/1.3\","
              << "\"success\":true,"
              << "\"operation\":\"" << operation << "\","
              << "\"editor_action\":\"" << editorAction << "\","
              << "\"request_id\":\"" << EscapeJson(requestId) << "\","
              << "\"material_id\":\"" << result.materialId.ToString() << "\","
              << "\"source_path\":\"" << EscapeJson(sourcePath.string()) << "\","
              << "\"notification_path\":\"" << EscapeJson(signalPath.string()) << "\","
              << "\"acknowledgement_path\":\""
              << EscapeJson(acknowledgementPath.string()) << "\","
              << "\"material_path\":\"" << EscapeJson(result.materialPath) << "\","
              << "\"albedo_path\":\"" << EscapeJson(result.albedoMapPath) << "\","
              << "\"normal_path\":\"" << EscapeJson(result.normalMapPath) << "\","
              << "\"orm_path\":\"" << EscapeJson(result.metalRoughMapPath) << "\","
              << "\"height_path\":\"" << EscapeJson(result.heightMapPath) << "\","
              << "\"width\":" << result.width << ','
              << "\"height\":" << result.height << ','
              << "\"source_records\":" << result.sourceRecordCount << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, &options, &error)) {
        if (error == "help") {
            PrintUsage();
            return 0;
        }
        if (options.json) PrintJsonError(error);
        else {
            std::cerr << "3DGMaterialCooker: " << error << '\n';
            PrintUsage();
        }
        return 2;
    }

    std::error_code ec;
    const std::filesystem::path source =
        std::filesystem::absolute(options.source, ec).lexically_normal();
    if (ec || !std::filesystem::is_regular_file(source, ec))
        error = "Forge package was not found: " + options.source;
    ec.clear();
    const std::filesystem::path content =
        std::filesystem::absolute(options.contentRoot, ec).lexically_normal();
    if (error.empty() && (ec || !std::filesystem::is_directory(content, ec)))
        error = "Content folder was not found: " + options.contentRoot;
    const std::filesystem::path relativeDestination =
        std::filesystem::path(options.destination).lexically_normal();
    if (error.empty() && !SafeRelativePath(relativeDestination))
        error = "Destination must be a relative folder inside Content.";

    engine::ForgeTexturePackage package;
    if (error.empty()
        && !engine::LoadForgeTexturePackage(source.string(), &package, &error)) {
        // Validate before replacing the durable linked source.
    }
    const std::filesystem::path destination = content / relativeDestination;
    const std::string materialStem = SafeName(package.materialName);
    const bool updating = error.empty()
        && std::filesystem::is_regular_file(destination / (materialStem + ".3dgmat"), ec);
    const std::string operation = updating ? "updated" : "created";
    const std::filesystem::path linkedSource =
        content.parent_path() / "Intermediate" / "MaterialForge" / "Sources"
        / relativeDestination / (materialStem + ".3dgtexpack");
    StagedSource staged;
    if (error.empty()
        && !StageSourcePackage(source, linkedSource, &staged, &error)) {
        // Source cache supplied the actionable error.
    }

    engine::ForgeMaterialImportResult result;
    if (error.empty()
        && !engine::ImportForgeMaterialPackage(
            linkedSource.string(), destination.string(), content.string(),
            nullptr, &result, &error)) {
        FinishStagedSource(staged, false);
    } else if (error.empty()) {
        FinishStagedSource(staged, true);
    }
    std::filesystem::path signalPath;
    std::string requestId;
    if (error.empty()
        && !WriteDeploySignal(
            content, result, operation, options.editorAction,
            &signalPath, &requestId, &error)) {
        // Cooked assets remain valid even if notification failed.
    }
    if (!error.empty()) {
        if (options.json) PrintJsonError(error);
        else std::cerr << "3DGMaterialCooker: " << error << '\n';
        return 1;
    }

    if (options.json) {
        const std::filesystem::path acknowledgementPath =
            signalPath.parent_path() / "deploy.ack";
        PrintJsonResult(
            result, linkedSource, signalPath, acknowledgementPath,
            requestId, operation, options.editorAction);
    }
    else {
        std::cout << "Cooked material: " << result.materialPath << '\n'
                  << "Textures: " << result.albedoMapPath << ", "
                  << result.normalMapPath << ", "
                  << result.metalRoughMapPath << ", "
                  << result.heightMapPath << '\n';
    }
    return 0;
}
