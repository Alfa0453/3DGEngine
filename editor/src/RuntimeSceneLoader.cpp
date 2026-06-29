#include "RuntimeSceneLoader.h"

#include <fstream>

bool RuntimeSceneLoader::Load(const std::string &path, Scene *scene, std::string *error)
{
    if (!scene) {
        if (error) {
            *error = "Runtime scene output pointer was null.";
        }
        return false;
    }

    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "Could not open runtime scene file for reading.";
        }
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "3DGRuntimeScene" || version != 1) {
        if (error) {
            *error = "Runtime scene file has an unknown format.";
        }
        return false;
    }

    Scene loaded;
    std::string recordType;
    while (in >> recordType) {
        if (!recordType.empty() && recordType[0] == '#') {
            std::string restOfLine;
            std::getline(in, restOfLine);
            continue;
        }

        if (recordType != "entity") {
            std::string restOfLine;
            std::getline(in, restOfLine);
            continue;
        }

        Entity entity;
        in >> entity.primitive >> entity.name
           >> entity.position.x >> entity.position.y >> entity.position.z
           >> entity.scale.x >> entity.scale.y >> entity.scale.z
           >> entity.rotation.w >> entity.rotation.x >> entity.rotation.y >> entity.rotation.z
           >> entity.color.r >> entity.color.g >> entity.color.b;

        if (!in) {
            if (error) {
                *error = "Runtime scene contains an invalid entity record.";
            }
            return false;
        }

        loaded.entities.push_back(entity);
    }

    *scene = loaded;
    return true;
}