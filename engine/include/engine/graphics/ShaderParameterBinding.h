#pragma once

#include "engine/assets/ShaderAsset.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Texture.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>

namespace engine {

struct ParsedShaderParameterValue {
    std::array<float, 4> numbers{{0.0f, 0.0f, 0.0f, 0.0f}};
    std::size_t count = 0;
    bool boolean = false;
};

inline ParsedShaderParameterValue ParseShaderParameterValue(std::string value)
{
    ParsedShaderParameterValue parsed;
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    parsed.boolean = lowered == "true" || lowered == "yes" || lowered == "on";
    // Graph defaults may be serialized either as "1, 0, 0" or as a GLSL-like
    // constructor such as "vec3(1, 0, 0)". Discard the constructor name before
    // numeric parsing so its dimension digit is never mistaken for a value.
    const std::size_t constructor = value.find('(');
    if (constructor != std::string::npos)
        value.erase(0, constructor + 1);
    for (char& c : value)
        if (c == ',' || c == '(' || c == ')') c = ' ';
    std::istringstream input(value);
    while (parsed.count < parsed.numbers.size()
           && input >> parsed.numbers[parsed.count])
        ++parsed.count;
    if (!parsed.boolean && parsed.count != 0)
        parsed.boolean = parsed.numbers[0] != 0.0f;
    return parsed;
}

// Upload one reflected graph parameter. Returns the next free texture unit;
// scalar/vector values leave it unchanged. A missing texture is deliberately
// ignored so callers can supply their own neutral fallback binding.
inline int UploadShaderParameter(
    Shader& shader, const std::string& displayName, int type,
    const std::string& value, const Texture* texture, int textureUnit)
{
    const std::string uniform = ShaderParameterUniformName(displayName);
    if (type == static_cast<int>(ShaderValueType::Texture2D)) {
        if (texture) {
            texture->Bind(static_cast<unsigned int>(textureUnit));
            shader.SetInt(uniform, textureUnit++);
        }
        return textureUnit;
    }

    const ParsedShaderParameterValue parsed = ParseShaderParameterValue(value);
    if (type == static_cast<int>(ShaderValueType::Int))
        shader.SetInt(uniform, parsed.count == 0
            ? 0 : static_cast<int>(parsed.numbers[0]));
    else if (type == static_cast<int>(ShaderValueType::Bool))
        shader.SetInt(uniform, parsed.boolean ? 1 : 0);
    else if (type == static_cast<int>(ShaderValueType::Vec2))
        shader.SetVec2(uniform, glm::vec2(parsed.numbers[0], parsed.numbers[1]));
    else if (type == static_cast<int>(ShaderValueType::Vec3))
        shader.SetVec3(uniform, glm::vec3(
            parsed.numbers[0], parsed.numbers[1], parsed.numbers[2]));
    else if (type == static_cast<int>(ShaderValueType::Vec4)
             || type == static_cast<int>(ShaderValueType::Color))
        shader.SetVec4(uniform, glm::vec4(
            parsed.numbers[0], parsed.numbers[1],
            parsed.numbers[2], parsed.numbers[3]));
    else
        shader.SetFloat(uniform, parsed.numbers[0]);
    return textureUnit;
}

} // namespace engine
