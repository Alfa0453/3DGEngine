#pragma once

#include <initializer_list>
#include <vector>
#include <cstddef>

namespace engine {
// Describes one vertex attribute. For simplicity every attribute is a run of
// 32-bit floats, which covers everything we need: position, colour, normal, UV.
struct VertexAttribute {
    unsigned int componentCount;    // number of floats, e.g. 3 for a vec3
};

// Describes the in-memory layout of a single vertex as an ordered list of
// attributes. Attribute N is bound to shader location N. The stride (bytes per
// vertex) is computed for you from the attributes.
class VertexLayout {
public:
    VertexLayout(std::initializer_list<VertexAttribute> attributes)
        : m_attributes(attributes) 
    {
        for (const VertexAttribute& a : m_attributes)
        {
            m_stride += a.componentCount * sizeof(float);
        }
    }

    const std::vector<VertexAttribute>& Attributes() const { return m_attributes; }
    unsigned int Stride() const { return m_stride; }     // bytes per vertex

private:
    std::vector<VertexAttribute> m_attributes;
    unsigned int m_stride = 0;
};

}   // namespace engine