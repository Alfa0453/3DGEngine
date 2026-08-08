#include "engine/graphics/Shader.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>     // glm::value_ptr — raw float* into a vec/mat

#include <cctype>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace engine {
namespace {

// Read an entire text file into a string. Throws if the file cannot be opened.
std::string ReadFile(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Shader: cannot open file '" + path + "'");
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

ShaderStage StageFromMessage(const std::string& message)
{
    if (message.find("vertex compile") != std::string::npos) return ShaderStage::Vertex;
    if (message.find("fragment compile") != std::string::npos) return ShaderStage::Fragment;
    if (message.find("program link") != std::string::npos) return ShaderStage::Link;
    if (message.find("cannot open file") != std::string::npos) return ShaderStage::File;
    return ShaderStage::Unknown;
}

int LineFromDriverMessage(const std::string& message)
{
    // Common driver formats include "0(17)" and "0:17". Treat this as a
    // best-effort hint; the full original message is always retained.
    for (std::size_t i = 0; i + 2 < message.size(); ++i)
    {
        if (message[i] != '(' && message[i] != ':') continue;
        std::size_t begin = i + 1;
        std::size_t end = begin;
        while (end < message.size()
            && std::isdigit(static_cast<unsigned char>(message[end])))
        {
            ++end;
        }
        if (end > begin)
        {
            try { return std::stoi(message.substr(begin, end - begin)); }
            catch (...) { return 0; }
        }
    }
    return 0;
}

void SetFailureReport(ShaderCompileReport& report, const std::string& message)
{
    report.success = false;
    report.diagnostics.clear();
    report.diagnostics.push_back({
        ShaderDiagnostic::Severity::Error,
        StageFromMessage(message),
        LineFromDriverMessage(message),
        message
    });
}

}   // anonymous namespace

int Shader::UniformLocation(const std::string &name)
{
    if (auto it = m_uniformCache.find(name); it != m_uniformCache.end())
    {
        return it->second;
    }
    const int loc = glGetUniformLocation(m_id, name.c_str());
    m_uniformCache[name] = loc;     // cache even -1 ("not found") to avoid re-querying
    return loc;
}

unsigned int Shader::CompileStage(unsigned int type, const std::string &src)
{
    const unsigned int shader = glCreateShader(type);
    const char* cstr = src.c_str();
    glShaderSource(shader, 1, &cstr, nullptr);
    glCompileShader(shader);

    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        int len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        glDeleteShader(shader);
        const char* stage = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
        throw std::runtime_error(std::string("Shader: ") + stage + " compile failed:\n" + log.data());
    }
    return shader;
}

Shader::Shader(const std::string &vertexSrc, const std::string &fragmentSrc)
{
    unsigned int vertexShader = 0;
    unsigned int fragmentShader = 0;
    try
    {
        vertexShader = CompileStage(GL_VERTEX_SHADER, vertexSrc);
        fragmentShader = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc);
    }
    catch (...)
    {
        if (vertexShader) glDeleteShader(vertexShader);
        if (fragmentShader) glDeleteShader(fragmentShader);
        throw;
    }

    // Link the two stages into one program that the GPU can run.
    m_id = glCreateProgram();
    glAttachShader(m_id, vertexShader);
    glAttachShader(m_id, fragmentShader);
    glLinkProgram(m_id);

    int ok = 0;
    glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        int len = 0;
        glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
        glGetProgramInfoLog(m_id, len, nullptr, log.data());
        glDeleteProgram(m_id);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        throw std::runtime_error(std::string("Shader: program link failed:\n") + log.data());
    }

    // Once linked, the program holds its own copy of the compiled code, so the
    // individual stage objects are no longer needed.
    glDetachShader(m_id, vertexShader);
    glDetachShader(m_id, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

Shader::~Shader()
{
    if (m_id)
    {
        glDeleteProgram(m_id);
    }
}

Shader::Shader(Shader &&other) noexcept
    : m_id(other.m_id), m_uniformCache(std::move(other.m_uniformCache)),
      m_intValues(std::move(other.m_intValues)),
      m_floatValues(std::move(other.m_floatValues)),
      m_vectorValues(std::move(other.m_vectorValues)),
      m_vectorSizes(std::move(other.m_vectorSizes))
{
    other.m_id = 0; // leave the moved-from object harmless
}

Shader &Shader::operator=(Shader &&other) noexcept
{
    if (this != &other)
    {
        if (m_id)
        {
            glDeleteProgram(m_id);
        }
        m_id = other.m_id;
        m_uniformCache = std::move(other.m_uniformCache);
        m_intValues = std::move(other.m_intValues);
        m_floatValues = std::move(other.m_floatValues);
        m_vectorValues = std::move(other.m_vectorValues);
        m_vectorSizes = std::move(other.m_vectorSizes);
        other.m_id = 0;
    }
    return *this;
}

Shader Shader::FromFiles(const std::string &vertexPath, const std::string &fragmentPath)
{
    return Shader(ReadFile(vertexPath), ReadFile(fragmentPath));
}

std::unique_ptr<Shader> Shader::TryCompile(
    const std::string& vertexSrc,
    const std::string& fragmentSrc,
    ShaderCompileReport& report)
{
    try
    {
        auto shader = std::make_unique<Shader>(vertexSrc, fragmentSrc);
        report.success = true;
        report.diagnostics.clear();
        return shader;
    }
    catch (const std::exception& error)
    {
        SetFailureReport(report, error.what());
        return nullptr;
    }
    catch (...)
    {
        SetFailureReport(report, "Shader: unknown compile failure");
        return nullptr;
    }
}

std::unique_ptr<Shader> Shader::TryFromFiles(
    const std::string& vertexPath,
    const std::string& fragmentPath,
    ShaderCompileReport& report)
{
    try
    {
        return TryCompile(ReadFile(vertexPath), ReadFile(fragmentPath), report);
    }
    catch (const std::exception& error)
    {
        SetFailureReport(report, error.what());
        return nullptr;
    }
    catch (...)
    {
        SetFailureReport(report, "Shader: unknown file loading failure");
        return nullptr;
    }
}

void Shader::Bind() const
{
    glUseProgram(m_id);
}
void Shader::Unbind() const
{
    glUseProgram(0);
}
void Shader::SetInt(const std::string &name, int value)
{
    const int location = UniformLocation(name);
    if (location < 0) return;
    const auto it = m_intValues.find(location);
    if (it != m_intValues.end() && it->second == value) return;
    m_intValues[location] = value;
    glUniform1i(location, value);
}
void Shader::SetFloat(const std::string &name, float value)
{
    const int location = UniformLocation(name);
    if (location < 0) return;
    const auto it = m_floatValues.find(location);
    if (it != m_floatValues.end() && it->second == value) return;
    m_floatValues[location] = value;
    glUniform1f(location, value);
}

void Shader::SetVec2(const std::string &name, const glm::vec2 &value)
{
    const int location = UniformLocation(name);
    if (location < 0) return;
    std::array<float, 16> next{};
    next[0] = value.x; next[1] = value.y;
    const auto it = m_vectorValues.find(location);
    if (it != m_vectorValues.end() && m_vectorSizes[location] == 2
        && std::equal(next.begin(), next.begin() + 2, it->second.begin())) return;
    m_vectorValues[location] = next;
    m_vectorSizes[location] = 2;
    glUniform2fv(location, 1, glm::value_ptr(value));
}
void Shader::SetVec3(const std::string &name, const glm::vec3 &value)
{
    const int location = UniformLocation(name);
    if (location < 0) return;
    std::array<float, 16> next{};
    next[0] = value.x; next[1] = value.y; next[2] = value.z;
    const auto it = m_vectorValues.find(location);
    if (it != m_vectorValues.end() && m_vectorSizes[location] == 3
        && std::equal(next.begin(), next.begin() + 3, it->second.begin())) return;
    m_vectorValues[location] = next;
    m_vectorSizes[location] = 3;
    glUniform3fv(location, 1, glm::value_ptr(value));
}
void Shader::SetVec4(const std::string &name, const glm::vec4 &value)
{
    const int location = UniformLocation(name);
    if (location < 0) return;
    std::array<float, 16> next{};
    next[0] = value.x; next[1] = value.y; next[2] = value.z; next[3] = value.w;
    const auto it = m_vectorValues.find(location);
    if (it != m_vectorValues.end() && m_vectorSizes[location] == 4
        && std::equal(next.begin(), next.begin() + 4, it->second.begin())) return;
    m_vectorValues[location] = next;
    m_vectorSizes[location] = 4;
    glUniform4fv(location, 1, glm::value_ptr(value));
}
void Shader::SetMat3(const std::string &name, const glm::mat3 &value)
{
    const int location = UniformLocation(name);
    if (location < 0) return;
    std::array<float, 16> next{};
    std::copy_n(glm::value_ptr(value), 9, next.begin());
    const auto it = m_vectorValues.find(location);
    if (it != m_vectorValues.end() && m_vectorSizes[location] == 9
        && std::equal(next.begin(), next.begin() + 9, it->second.begin())) return;
    m_vectorValues[location] = next;
    m_vectorSizes[location] = 9;
    glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
}
void Shader::SetMat4(const std::string &name, const glm::mat4 &value)
{
     // `transpose = GL_FALSE`: GLM stores matrices column-major, exactly what
    // OpenGL expects, so no transpose is needed.
    const int location = UniformLocation(name);
    if (location < 0) return;
    std::array<float, 16> next{};
    std::copy_n(glm::value_ptr(value), 16, next.begin());
    const auto it = m_vectorValues.find(location);
    if (it != m_vectorValues.end() && m_vectorSizes[location] == 16
        && std::equal(next.begin(), next.end(), it->second.begin())) return;
    m_vectorValues[location] = next;
    m_vectorSizes[location] = 16;
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::SetMat4Array(const std::string& name, const glm::mat4* values, int count)
{
    if (!values || count <= 0) return;
    const int location = UniformLocation(name);
    if (location < 0) return;
    glUniformMatrix4fv(location, count, GL_FALSE, glm::value_ptr(values[0]));
}

} // namespace engine
