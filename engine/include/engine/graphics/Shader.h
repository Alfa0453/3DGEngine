#pragma once

#include <string>
#include <unordered_map>

// Forward declarations of GLM types only — keeps the full (heavy) GLM headers
// out of this header. The .cpp includes the real <glm/glm.hpp>.
#include <glm/fwd.hpp>

namespace engine {

// Compiles a vertex + fragment shader into a linked GPU program and owns its
// lifetime (RAII). Because it owns an OpenGL program object, it is move-only:
// copying would risk two objects deleting the same GPU resource.
//
// On any compile or link error the constructor throws std::runtime_error with
// the driver's info log attached, so shader bugs surface as readable messages
// instead of a silently blank screen.
class Shader {
public:
    // Build directly from GLSL source strings.
    Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // Read both files from disk and build the shader. Throws if a file is
    // missing or fails to compile/link.
    static Shader FromFiles(const std::string& vertexPath, const std::string& fragmentPath);

    void Bind() const;  // make this the active program (glUseProgram)
    void Unbind() const;  // unbind any program

    // Uniform setters. The set grows as milestones need it; matrix/vector
    // setters (requiring GLM) arrive in Milestone 3.
    void SetInt(const std::string& name, int value);
    void SetFloat(const std::string& name, float value);
    void SetVec2(const std::string& name, const glm::vec2& value);
    void SetVec3(const std::string& name, const glm::vec3& value);
    void SetMat3(const std::string& name, const glm::mat3& value);
    void SetMat4(const std::string& name, const glm::mat4& value);

    unsigned int ID() const { return m_id; }

private:
    // Looks up (and caches) a uniform's location. Locations never change for a
    // linked program, so caching avoids a GL query every frame.
    int UniformLocation(const std::string& name);

    // Compiles one stage (vertex or fragment); throws on failure.
    static unsigned int CompileStage(unsigned int type, const std::string& src);

    unsigned int m_id = 0;  // the OpenGL program object handle
    std::unordered_map<std::string, int> m_uniformCache;
};

} // namespace engine