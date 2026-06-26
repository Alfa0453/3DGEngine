#include "engine/graphics/Shader.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>     // glm::value_ptr — raw float* into a vec/mat

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
    const unsigned int vertexShader = CompileStage(GL_VERTEX_SHADER, vertexSrc);
    const unsigned int fragmentShader = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc);

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
    : m_id(other.m_id), m_uniformCache(std::move(other.m_uniformCache))
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
        other.m_id = 0;
    }
    return *this;
}

Shader Shader::FromFiles(const std::string &vertexPath, const std::string &fragmentPath)
{
    return Shader(ReadFile(vertexPath), ReadFile(fragmentPath));
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
    glUniform1i(UniformLocation(name), value);
}
void Shader::SetFloat(const std::string &name, float value)
{
    glUniform1f(UniformLocation(name), value);
}

void Shader::SetVec3(const std::string &name, const glm::vec3 &value)
{
    glUniform3fv(UniformLocation(name), 1, glm::value_ptr(value));
}
void Shader::SetMat3(const std::string &name, const glm::mat3 &value)
{
    glUniformMatrix3fv(UniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}
void Shader::SetMat4(const std::string &name, const glm::mat4 &value)
{
     // `transpose = GL_FALSE`: GLM stores matrices column-major, exactly what
    // OpenGL expects, so no transpose is needed.
    glUniformMatrix4fv(UniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

} // namespace engine
