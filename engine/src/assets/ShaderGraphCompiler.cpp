#include "engine/assets/ShaderGraphCompiler.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace engine {
namespace {

std::string DefaultValue(ShaderValueType type)
{
    switch (type) {
    case ShaderValueType::Float: return "0.0";
    case ShaderValueType::Int: return "0";
    case ShaderValueType::Bool: return "false";
    case ShaderValueType::Vec2: return "vec2(0.0)";
    case ShaderValueType::Vec3: return "vec3(0.0)";
    case ShaderValueType::Vec4:
    case ShaderValueType::Color: return "vec4(0.0)";
    case ShaderValueType::Texture2D: return "0";
    }
    return "0.0";
}

bool NumericConstant(const std::string& text, float* value)
{
    if (text.empty()) return false;
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return false;
    if (value) *value = parsed;
    return true;
}

std::string FoldBinary(const std::string& left, const std::string& right, char operation)
{
    float a = 0.0f, b = 0.0f;
    if (!NumericConstant(left, &a) || !NumericConstant(right, &b)
        || (operation == '/' && b == 0.0f)) return {};
    float result = 0.0f;
    if (operation == '+') result = a + b;
    else if (operation == '-') result = a - b;
    else if (operation == '*') result = a * b;
    else result = a / b;
    std::ostringstream out;
    out << result;
    return out.str();
}

std::string UniformType(ShaderValueType type)
{
    switch (type) {
    case ShaderValueType::Float: return "float";
    case ShaderValueType::Int: return "int";
    case ShaderValueType::Bool: return "bool";
    case ShaderValueType::Vec2: return "vec2";
    case ShaderValueType::Vec3: return "vec3";
    case ShaderValueType::Vec4:
    case ShaderValueType::Color: return "vec4";
    case ShaderValueType::Texture2D: return "sampler2D";
    }
    return "float";
}

std::string ConvertExpression(
    std::string expression, ShaderValueType from, ShaderValueType to)
{
    if (from == to
        || (from == ShaderValueType::Color && to == ShaderValueType::Vec4)
        || (from == ShaderValueType::Vec4 && to == ShaderValueType::Color))
        return expression;
    if (from == ShaderValueType::Float) {
        if (to == ShaderValueType::Vec2) return "vec2(" + expression + ")";
        if (to == ShaderValueType::Vec3) return "vec3(" + expression + ")";
        if (to == ShaderValueType::Vec4 || to == ShaderValueType::Color)
            return "vec4(vec3(" + expression + "),1.0)";
    }
    if ((from == ShaderValueType::Color || from == ShaderValueType::Vec4)
        && to == ShaderValueType::Vec3)
        return "(" + expression + ").xyz";
    if (from == ShaderValueType::Vec3
        && (to == ShaderValueType::Vec4 || to == ShaderValueType::Color))
        return "vec4(" + expression + ",1.0)";
    if ((from == ShaderValueType::Vec2 || from == ShaderValueType::Vec3
         || from == ShaderValueType::Vec4 || from == ShaderValueType::Color)
        && to == ShaderValueType::Float)
        return "(" + expression + ").x";
    return expression;
}

} // namespace

GeneratedShaderSource GenerateShaderSource(const ShaderAsset& asset, bool skinned)
{
    GeneratedShaderSource result;
    result.issues = ValidateShaderAsset(asset);
    if (ShaderAssetHasErrors(result.issues)) return result;

    std::unordered_map<std::uint64_t, const ShaderGraphNode*> nodes;
    std::unordered_map<std::uint64_t, const ShaderGraphPin*> pins;
    std::unordered_map<std::uint64_t, const ShaderGraphLink*> inputLinks;
    for (const auto& node : asset.nodes) nodes[node.id] = &node;
    for (const auto& pin : asset.pins) pins[pin.id] = &pin;
    for (const auto& link : asset.links) inputLinks[link.toPin] = &link;

    const char* outputType = asset.domain == ShaderDomain::PostProcess ? "PostProcessOutput"
        : asset.domain == ShaderDomain::Particle ? "ParticleOutput"
        : asset.domain == ShaderDomain::Unlit ? "UnlitOutput" : "SurfaceOutput";
    const ShaderGraphNode* output = nullptr;
    for (const auto& node : asset.nodes)
        if (node.type == outputType) { output = &node; break; }
    if (!output) return result;

    std::unordered_set<std::uint64_t> reachable;
    const auto visit = [&](const auto& self, std::uint64_t nodeId) -> void {
        if (!reachable.insert(nodeId).second) return;
        for (const auto& pin : asset.pins) {
            if (pin.nodeId != nodeId || !pin.input) continue;
            const auto linked = inputLinks.find(pin.id);
            if (linked == inputLinks.end()) continue;
            const auto sourcePin = pins.find(linked->second->fromPin);
            if (sourcePin != pins.end()) self(self, sourcePin->second->nodeId);
        }
    };
    visit(visit, output->id);
    result.reachableNodes.assign(reachable.begin(), reachable.end());
    std::sort(result.reachableNodes.begin(), result.reachableNodes.end());

    const bool postProcess = asset.domain == ShaderDomain::PostProcess;
    const bool particle = asset.domain == ShaderDomain::Particle;
    const bool unlit = asset.domain == ShaderDomain::Unlit;

    // Surface displacement must be evaluated in the vertex stage. Keep this
    // evaluator deliberately side-effect free and based on the same serialized
    // graph as the fragment evaluator. Unsupported fragment-only inputs resolve
    // to a safe zero displacement instead of producing an invalid program.
    std::unordered_map<std::uint64_t, std::string> vertexExpressions;
    const auto vertexExpression = [&](const auto& self,
                                      std::uint64_t nodeId) -> std::string {
        const auto cached = vertexExpressions.find(nodeId);
        if (cached != vertexExpressions.end()) return cached->second;
        const ShaderGraphNode& node = *nodes[nodeId];
        std::vector<std::string> inputs;
        for (const auto& pin : asset.pins) {
            if (pin.nodeId != nodeId || !pin.input) continue;
            const auto linked = inputLinks.find(pin.id);
            inputs.push_back(linked == inputLinks.end()
                ? DefaultValue(pin.type)
                : self(self, pins[linked->second->fromPin]->nodeId));
        }
        std::string value = node.value.empty() ? "0.0" : node.value;
        if (node.type == "UV") value = "aUV";
        else if (node.type == "LocalPosition") value = "local.xyz";
        else if (node.type == "WorldPosition")
            value = "(uModel*local).xyz";
        else if (node.type == "Normal") value = "normalize(localNormal)";
        else if (node.type == "Time") value = "uTime";
        else if (node.type == "DeltaTime") value = "uDeltaTime";
        else if (node.type.rfind("Parameter", 0) == 0)
            value = node.type == "ParameterTexture2D"
                ? "0.0" : ShaderParameterUniformName(node.name);
        else if ((node.type == "Add" || node.type == "Subtract"
                  || node.type == "Multiply" || node.type == "Divide")
                 && inputs.size() >= 2) {
            const char* op = node.type == "Add" ? "+"
                : node.type == "Subtract" ? "-"
                : node.type == "Multiply" ? "*" : "/";
            value = "(" + inputs[0] + op + inputs[1] + ")";
        } else if (node.type == "OneMinus" && !inputs.empty())
            value = "(1.0-" + inputs[0] + ")";
        else if (node.type == "Saturate" && !inputs.empty())
            value = "clamp(" + inputs[0] + ",0.0,1.0)";
        else if (node.type == "Min" && inputs.size() >= 2)
            value = "min(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Max" && inputs.size() >= 2)
            value = "max(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Clamp" && inputs.size() >= 3)
            value = "clamp(" + inputs[0] + "," + inputs[1] + ","
                + inputs[2] + ")";
        else if (node.type == "Power" && inputs.size() >= 2)
            value = "pow(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Absolute" && !inputs.empty())
            value = "abs(" + inputs[0] + ")";
        else if (node.type == "Noise" && !inputs.empty())
            value = "fract(sin(dot(" + inputs[0]
                + ",vec2(12.9898,78.233)))*43758.5453)";
        else if (node.type == "SampleTexture2D") value = "0.0";
        vertexExpressions[nodeId] = value;
        return value;
    };

    std::string displacement = "0.0";
    if (!postProcess && !particle && !unlit) {
        for (const auto& pin : asset.pins) {
            if (pin.nodeId != output->id || !pin.input
                || pin.name != "Displacement") continue;
            const auto linked = inputLinks.find(pin.id);
            if (linked == inputLinks.end()) break;
            const ShaderGraphPin* source = pins[linked->second->fromPin];
            displacement = ConvertExpression(
                vertexExpression(vertexExpression, source->nodeId),
                source->type, ShaderValueType::Float);
            break;
        }
    }

    std::ostringstream vertex;
    if (postProcess) {
        vertex << "#version 330 core\n"
            "layout(location=0) in vec2 aPosition;\n"
            "layout(location=1) in vec2 aUV;\n"
            "out vec2 vUV;\n"
            "void main(){vUV=aUV;gl_Position=vec4(aPosition,0.0,1.0);}\n";
    } else if (particle) {
        vertex << "#version 330 core\n"
            "layout(location=0) in vec2 aCorner;\n"
            "layout(location=1) in vec3 iCenter;\n"
            "layout(location=2) in float iSize;\n"
            "layout(location=3) in vec4 iColor;\n"
            "layout(location=4) in float iRotation;\n"
            "layout(location=5) in float iFrame;\n"
            "layout(location=6) in vec3 iVelocity;\n"
            "layout(location=7) in float iNormalizedAge;\n"
            "uniform mat4 uViewProjection; uniform vec3 uCameraRight; uniform vec3 uCameraUp;\n"
            "out vec2 vUV; out vec4 vParticleColor; out vec3 vParticleVelocity;\n"
            "out float vParticleSize; out float vParticleRotation; out float vParticleFrame;\n"
            "out float vParticleAge;\n"
            "void main(){float c=cos(iRotation),s=sin(iRotation);"
            "vec2 corner=mat2(c,-s,s,c)*aCorner;"
            "vec3 world=iCenter+(corner.x*uCameraRight+corner.y*uCameraUp)*iSize;"
            "gl_Position=uViewProjection*vec4(world,1.0);vUV=aCorner+0.5;"
            "vParticleColor=iColor;vParticleVelocity=iVelocity;vParticleSize=iSize;"
            "vParticleRotation=iRotation;vParticleFrame=iFrame;vParticleAge=iNormalizedAge;}\n";
    } else if (unlit) {
        vertex << "#version 330 core\n"
            "layout(location=0) in vec2 aPosition;\n"
            "layout(location=1) in vec2 aUV;\n"
            "uniform mat4 uProjection;\n"
            "out vec2 vUV;\n"
            "void main(){vUV=aUV;gl_Position=uProjection*vec4(aPosition,0.0,1.0);}\n";
    } else {
        vertex << "#version 330 core\n"
            "layout(location=0) in vec3 aPosition;\nlayout(location=1) in vec3 aNormal;\n"
            "layout(location=2) in vec2 aUV;\n";
        if (skinned)
            vertex << "layout(location=3) in ivec4 aBoneIds;\nlayout(location=4) in vec4 aBoneWeights;\n"
                "uniform mat4 uBones[128];\n";
        vertex << "uniform mat4 uModel;\nuniform mat4 uViewProjection;\n"
            "uniform float uTime;\nuniform float uDeltaTime;\n";
        for (const auto& parameter : asset.parameters)
            if (parameter.type != ShaderValueType::Texture2D)
                vertex << "uniform " << UniformType(parameter.type) << ' '
                    << ShaderParameterUniformName(parameter.name) << ";\n";
        vertex << "out vec3 vNormal;\nout vec3 vWorldPosition;"
            "\nout vec3 vLocalPosition;\nout vec2 vUV;\n"
            "void main(){";
        if (skinned)
            vertex << "mat4 skin=aBoneWeights.x*uBones[aBoneIds.x]+aBoneWeights.y*uBones[aBoneIds.y]+"
                "aBoneWeights.z*uBones[aBoneIds.z]+aBoneWeights.w*uBones[aBoneIds.w];"
                "vec4 local=skin*vec4(aPosition,1.0);vec3 localNormal=mat3(skin)*aNormal;";
        else
            vertex << "vec4 local=vec4(aPosition,1.0);vec3 localNormal=aNormal;";
        vertex << "local.xyz+=normalize(localNormal)*(" << displacement << ");"
            "vec4 w=uModel*local;vWorldPosition=w.xyz;vLocalPosition=local.xyz;"
            "vNormal=mat3(transpose(inverse(uModel)))*localNormal;vUV=aUV;"
            "gl_Position=uViewProjection*w;}\n";
    }

    std::ostringstream fragment;
    int line = 1;
    const auto emit = [&](const std::string& text, std::uint64_t nodeId = 0) {
        fragment << text << '\n';
        if (nodeId) result.fragmentLineNodes[line] = nodeId;
        ++line;
    };
    emit("#version 330 core");
    emit((postProcess || unlit) ? "in vec2 vUV;"
        : particle
            ? "in vec2 vUV; in vec4 vParticleColor; in vec3 vParticleVelocity;"
              " in float vParticleSize; in float vParticleRotation;"
              " in float vParticleFrame; in float vParticleAge;"
            : "in vec3 vNormal; in vec3 vWorldPosition; in vec3 vLocalPosition; in vec2 vUV;");
    emit("out vec4 FragColor;");
    if (postProcess) {
        emit("uniform sampler2D uSceneColor; uniform sampler2D uSceneDepth;");
        emit("uniform sampler2D uSceneNormal; uniform sampler2D uSceneVelocity;");
        emit("uniform vec2 uTexelSize; uniform float uExposure;");
        emit("uniform float uTime; uniform float uDeltaTime;");
    } else if (unlit) {
        emit("uniform vec4 uWidgetColor; uniform sampler2D uWidgetTexture;");
        emit("uniform int uUseWidgetTexture; uniform vec4 uClipRect;");
    } else if (!particle) {
        emit("uniform vec3 uLightDirection; uniform float uLightIntensity;");
        emit("uniform vec3 uCameraPosition; uniform vec4 uObjectColor;");
        emit("uniform float uTime; uniform float uDeltaTime;");
        emit("const float PI=3.14159265359;");
        emit("float DistributionGGX(vec3 N,vec3 H,float roughness){"
             "float a=roughness*roughness;float a2=a*a;"
             "float nDotH=max(dot(N,H),0.0);float d=nDotH*nDotH*(a2-1.0)+1.0;"
             "return a2/max(PI*d*d,0.000001);}");
        emit("float GeometrySchlickGGX(float nDotV,float roughness){"
             "float r=roughness+1.0;float k=(r*r)/8.0;"
             "return nDotV/max(nDotV*(1.0-k)+k,0.000001);}");
        emit("float GeometrySmith(vec3 N,vec3 V,vec3 L,float roughness){"
             "return GeometrySchlickGGX(max(dot(N,V),0.0),roughness)*"
             "GeometrySchlickGGX(max(dot(N,L),0.0),roughness);}");
        emit("vec3 FresnelSchlick(float cosTheta,vec3 f0){"
             "return f0+(1.0-f0)*pow(clamp(1.0-cosTheta,0.0,1.0),5.0);}");
        emit("vec3 ApplyTangentNormal(vec3 tangentNormal){"
             "vec3 baseN=normalize(vNormal);vec3 dp1=dFdx(vWorldPosition);"
             "vec3 dp2=dFdy(vWorldPosition);vec2 duv1=dFdx(vUV);vec2 duv2=dFdy(vUV);"
             "vec3 T=normalize(dp1*duv2.y-dp2*duv1.y);"
             "if(dot(T,T)<0.0001)T=normalize(abs(baseN.y)<0.999?cross(vec3(0,1,0),baseN):cross(vec3(1,0,0),baseN));"
             "vec3 B=normalize(cross(baseN,T));return normalize(mat3(T,B,baseN)*tangentNormal);}");
    }
    for (const auto& parameter : asset.parameters) {
        emit("uniform " + UniformType(parameter.type) + " "
             + ShaderParameterUniformName(parameter.name) + ";");
    }
    emit("void main(){");

    std::unordered_map<std::uint64_t, std::string> expressions;
    const auto expression = [&](const auto& self, std::uint64_t nodeId) -> std::string {
        const auto cached = expressions.find(nodeId);
        if (cached != expressions.end()) return cached->second;
        const ShaderGraphNode& node = *nodes[nodeId];
        auto inputs = [&]() {
            std::vector<std::string> values;
            for (const auto& pin : asset.pins) {
                if (pin.nodeId != nodeId || !pin.input) continue;
                const auto linked = inputLinks.find(pin.id);
                if (linked == inputLinks.end()) values.push_back(DefaultValue(pin.type));
                else values.push_back(self(self, pins[linked->second->fromPin]->nodeId));
            }
            return values;
        }();
        std::string value = node.value.empty() ? "0.0" : node.value;
        if (node.type == "UV" || node.type == "ScreenUV") value = "vUV";
        else if (node.type == "WidgetUV") value = "vUV";
        else if (node.type == "WidgetColor") value = "uWidgetColor";
        else if (node.type == "WidgetTexture")
            value = "(uUseWidgetTexture!=0?texture(uWidgetTexture,vUV):vec4(1.0))";
        else if (node.type == "ClipMask") value = "1.0";
        else if (node.type == "SignedDistance" && !inputs.empty())
            value = "length(" + inputs[0] + ")";
        else if (node.type == "ParticleColor") value = "vParticleColor";
        else if (node.type == "ParticleVelocity") value = "vParticleVelocity";
        else if (node.type == "ParticleSize") value = "vParticleSize";
        else if (node.type == "ParticleRotation") value = "vParticleRotation";
        else if (node.type == "ParticleFrame") value = "vParticleFrame";
        else if (node.type == "ParticleAge"
                 || node.type == "NormalizedLifetime") value = "vParticleAge";
        else if (node.type == "TrailCoordinates") value = "vUV";
        else if (node.type == "SoftDepth") value = "1.0";
        else if (node.type == "SceneColor") value = "texture(uSceneColor,vUV)";
        else if (node.type == "SceneDepth") value = "texture(uSceneDepth,vUV).r";
        else if (node.type == "SceneNormal")
            value = "texture(uSceneNormal,vUV).xyz";
        else if (node.type == "SceneVelocity")
            value = "texture(uSceneVelocity,vUV).xy";
        else if (node.type == "SceneColorSample" && !inputs.empty())
            value = "texture(uSceneColor," + inputs[0] + ")";
        else if (node.type == "SceneDepthSample" && !inputs.empty())
            value = "texture(uSceneDepth," + inputs[0] + ").r";
        else if (node.type == "SceneNormalSample" && !inputs.empty())
            value = "texture(uSceneNormal," + inputs[0] + ").xyz";
        else if (node.type == "SceneVelocitySample" && !inputs.empty())
            value = "texture(uSceneVelocity," + inputs[0] + ").xy";
        else if (node.type == "PixelOffset" && inputs.size() >= 2)
            value = "(" + inputs[0] + "+" + inputs[1] + "*uTexelSize)";
        else if (node.type == "TexelSize") value = "uTexelSize";
        else if (node.type == "Exposure") value = "uExposure";
        else if (node.type == "Normal") value = "normalize(vNormal)";
        else if (node.type == "WorldPosition") value = "vWorldPosition";
        else if (node.type == "LocalPosition") value = "vLocalPosition";
        else if (node.type == "Tangent") value = "vec3(1.0,0.0,0.0)";
        else if (node.type == "ViewDirection") value =
            (!postProcess && !particle && !unlit)
                ? "normalize(uCameraPosition-vWorldPosition)"
                : "vec3(0.0,0.0,1.0)";
        else if (node.type == "CameraPosition") value =
            (!postProcess && !particle && !unlit)
                ? "uCameraPosition" : "vec3(0.0)";
        else if (node.type == "ObjectColor")
            value = (!postProcess && !particle && !unlit)
                ? "uObjectColor" : "vec4(1.0)";
        else if (node.type == "VertexColor") value = "vec4(1.0)";
        else if (node.type == "Time") value =
            (postProcess || (!particle && !unlit)) ? "uTime" : "0.0";
        else if (node.type == "DeltaTime") value =
            (postProcess || (!particle && !unlit)) ? "uDeltaTime" : "0.0166667";
        else if (node.type.rfind("Parameter", 0) == 0)
            value = ShaderParameterUniformName(node.name);
        else if ((node.type == "Add" || node.type == "Subtract" || node.type == "Multiply"
                  || node.type == "Divide") && inputs.size() >= 2) {
            const char* op = node.type == "Add" ? "+" : node.type == "Subtract" ? "-"
                : node.type == "Multiply" ? "*" : "/";
            const std::string folded = FoldBinary(inputs[0], inputs[1], *op);
            value = folded.empty() ? "(" + inputs[0] + op + inputs[1] + ")" : folded;
        } else if (node.type == "OneMinus" && !inputs.empty()) value = "(1.0-" + inputs[0] + ")";
        else if (node.type == "Saturate" && !inputs.empty()) value = "clamp(" + inputs[0] + ",0.0,1.0)";
        else if (node.type == "Normalize" && !inputs.empty()) value = "normalize(" + inputs[0] + ")";
        else if (node.type == "Dot" && inputs.size() >= 2) value = "dot(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Min" && inputs.size() >= 2) value = "min(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Max" && inputs.size() >= 2) value = "max(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Clamp" && inputs.size() >= 3) value = "clamp(" + inputs[0] + "," + inputs[1] + "," + inputs[2] + ")";
        else if (node.type == "Power" && inputs.size() >= 2) value = "pow(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "SquareRoot" && !inputs.empty()) value = "sqrt(max(" + inputs[0] + ",0.0))";
        else if (node.type == "Absolute" && !inputs.empty()) value = "abs(" + inputs[0] + ")";
        else if (node.type == "Sign" && !inputs.empty()) value = "sign(" + inputs[0] + ")";
        else if (node.type == "Floor" && !inputs.empty()) value = "floor(" + inputs[0] + ")";
        else if (node.type == "Fraction" && !inputs.empty()) value = "fract(" + inputs[0] + ")";
        else if (node.type == "Modulo" && inputs.size() >= 2) value = "mod(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Compose" && inputs.size() >= 3) value = "vec3(" + inputs[0] + "," + inputs[1] + "," + inputs[2] + ")";
        else if ((node.type == "Split" || node.type == "ChannelMask") && !inputs.empty()) value = "(" + inputs[0] + ").x";
        else if (node.type == "Swizzle" && !inputs.empty()) value = "(" + inputs[0] + ").xyz";
        else if (node.type == "Cross" && inputs.size() >= 2) value = "cross(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Length" && !inputs.empty()) value = "length(" + inputs[0] + ")";
        else if (node.type == "Reflect" && inputs.size() >= 2) value = "reflect(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "Lerp" && inputs.size() >= 3) value = "mix(" + inputs[0] + "," + inputs[1] + "," + inputs[2] + ")";
        else if (node.type == "SampleTexture2D" && inputs.size() >= 2)
            value = inputs[0] == "0" ? "vec4(1.0)" : "texture(" + inputs[0] + "," + inputs[1] + ")";
        else if (node.type == "NormalMapDecode" && !inputs.empty())
            value = (!postProcess && !particle && !unlit)
                ? "ApplyTangentNormal(normalize((" + inputs[0] + ").xyz*2.0-1.0))"
                : "normalize((" + inputs[0] + ").xyz*2.0-1.0)";
        else if (node.type == "UVTransform" && inputs.size() >= 3)
            value = inputs[0] + "*" + inputs[1] + "+" + inputs[2];
        else if (node.type == "Remap" && inputs.size() >= 5)
            value = "mix(" + inputs[3] + "," + inputs[4] + ",(" + inputs[0] + "-"
                + inputs[1] + ")/max(" + inputs[2] + "-" + inputs[1] + ",0.00001))";
        else if (node.type == "Smoothstep" && inputs.size() >= 3)
            value = "smoothstep(" + inputs[1] + "," + inputs[2] + "," + inputs[0] + ")";
        else if (node.type == "Fresnel" && inputs.size() >= 2)
            value = "pow(1.0-clamp(dot(normalize(" + inputs[0] + "),normalize("
                + inputs[1] + ")),0.0,1.0),5.0)";
        else if (node.type == "Noise" && !inputs.empty())
            value = "fract(sin(dot(" + inputs[0] + ",vec2(12.9898,78.233)))*43758.5453)";
        else if (node.type == "Comparison" && inputs.size() >= 2)
            value = "(" + inputs[0] + ">" + inputs[1] + ")";
        else if (node.type == "Select" && inputs.size() >= 3)
            value = "(" + inputs[0] + "?" + inputs[1] + ":" + inputs[2] + ")";
        expressions[nodeId] = value;
        return value;
    };

    const auto outputValue = [&](const char* name, std::string defaultExpression,
                                 ShaderValueType targetType) {
        for (const auto& pin : asset.pins) {
            if (pin.nodeId != output->id || !pin.input || pin.name != name)
                continue;
            const auto linked = inputLinks.find(pin.id);
            if (linked == inputLinks.end()) return defaultExpression;
            const auto sourcePin = pins.find(linked->second->fromPin);
            if (sourcePin == pins.end()) return defaultExpression;
            return ConvertExpression(
                expression(expression, sourcePin->second->nodeId),
                sourcePin->second->type, targetType);
        }
        return defaultExpression;
    };

    const std::string color = outputValue(
        asset.domain == ShaderDomain::Surface ? "Base Color" : "Color",
        asset.domain == ShaderDomain::Surface ? "uObjectColor"
                                              : "vec4(0.68,0.32,0.12,1.0)",
        ShaderValueType::Color);
    for (const std::uint64_t id : result.reachableNodes)
        emit(" // node:" + std::to_string(id), id);
    emit(" vec4 graphColor=" + color + ";", output->id);
    if (asset.domain == ShaderDomain::Unlit
        || asset.domain == ShaderDomain::PostProcess
        || asset.domain == ShaderDomain::Particle)
        emit(" FragColor=graphColor;", output->id);
    else {
        const std::string emissive = outputValue(
            "Emissive", "vec3(0.0)", ShaderValueType::Vec3);
        const std::string roughness = outputValue(
            "Roughness", "0.5", ShaderValueType::Float);
        const std::string metallic = outputValue(
            "Metallic", "0.0", ShaderValueType::Float);
        const std::string normal = outputValue(
            "Normal", "normalize(vNormal)", ShaderValueType::Vec3);
        const std::string opacity = outputValue(
            "Opacity", "1.0", ShaderValueType::Float);
        const std::string alphaCutoff = outputValue(
            "Alpha Cutoff", "0.5", ShaderValueType::Float);
        const std::string clearcoat = outputValue(
            "Clearcoat", "0.0", ShaderValueType::Float);
        const std::string transmission = outputValue(
            "Transmission", "0.0", ShaderValueType::Float);
        const std::string subsurface = outputValue(
            "Subsurface", "0.0", ShaderValueType::Float);
        const std::string sheen = outputValue(
            "Sheen", "0.0", ShaderValueType::Float);
        const std::string anisotropy = outputValue(
            "Anisotropy", "0.0", ShaderValueType::Float);

        emit(" vec3 materialBaseColor=max(graphColor.rgb,vec3(0.0));",
             output->id);
        emit(" vec3 materialEmissive=max(" + emissive + ",vec3(0.0));",
             output->id);
        emit(" float materialRoughness=clamp(" + roughness
             + ",0.045,1.0);", output->id);
        emit(" float materialMetallic=clamp(" + metallic
             + ",0.0,1.0);", output->id);
        emit(" float materialOpacity=clamp(graphColor.a*(" + opacity
             + "),0.0,1.0);", output->id);
        emit(" float materialClearcoat=clamp(" + clearcoat
             + ",0.0,1.0);", output->id);
        emit(" float materialTransmission=clamp(" + transmission
             + ",0.0,1.0);", output->id);
        emit(" float materialSubsurface=clamp(" + subsurface
             + ",0.0,1.0);", output->id);
        emit(" float materialSheen=clamp(" + sheen
             + ",0.0,1.0);", output->id);
        emit(" float materialAnisotropy=clamp(" + anisotropy
             + ",-1.0,1.0);", output->id);
        if (asset.blendMode == 1)
            emit(" if(materialOpacity<clamp(" + alphaCutoff
                 + ",0.0,1.0))discard;", output->id);

        emit(" vec3 N=normalize(" + normal + ");", output->id);
        emit(" vec3 V=normalize(uCameraPosition-vWorldPosition);"
             "vec3 L=normalize(-uLightDirection);vec3 H=normalize(V+L);",
             output->id);
        emit(" float nDotL=max(dot(N,L),0.0);float nDotV=max(dot(N,V),0.0001);"
             "float hDotV=max(dot(H,V),0.0);", output->id);
        emit(" vec3 tangent=normalize(dFdx(vWorldPosition));"
             "float anisotropicAlignment=abs(dot(normalize(H-N*dot(H,N)),tangent));"
             "float anisotropicRoughness=clamp(materialRoughness*mix(1.0,"
             "mix(1.35,0.65,anisotropicAlignment),abs(materialAnisotropy)),0.045,1.0);",
             output->id);
        emit(" vec3 f0=mix(vec3(0.04),materialBaseColor,materialMetallic);"
             "float ndf=DistributionGGX(N,H,anisotropicRoughness);"
             "float geometry=GeometrySmith(N,V,L,anisotropicRoughness);"
             "vec3 fresnel=FresnelSchlick(hDotV,f0);", output->id);
        emit(" vec3 specular=(ndf*geometry*fresnel)/max(4.0*nDotV*nDotL,0.0001);"
             "vec3 diffuse=(vec3(1.0)-fresnel)*(1.0-materialMetallic)*"
             "materialBaseColor/PI;", output->id);
        emit(" float wrappedDiffuse=max((dot(N,L)+0.45)/1.45,0.0);"
             "float diffuseTerm=mix(nDotL,wrappedDiffuse,materialSubsurface);"
             "vec3 direct=(diffuse*diffuseTerm+specular*nDotL)*max(uLightIntensity,0.0);",
             output->id);
        emit(" float coatRoughness=mix(0.18,0.045,materialClearcoat);"
             "float coatD=DistributionGGX(N,H,coatRoughness);"
             "float coatG=GeometrySmith(N,V,L,coatRoughness);"
             "vec3 coatF=FresnelSchlick(hDotV,vec3(0.04));"
             "vec3 coat=materialClearcoat*(coatD*coatG*coatF)/"
             "max(4.0*nDotV*nDotL,0.0001)*nDotL*uLightIntensity;",
             output->id);
        emit(" float rim=pow(1.0-nDotV,5.0);"
             "vec3 sheenLight=materialBaseColor*materialSheen*rim*"
             "(0.25+0.75*nDotL)*uLightIntensity;", output->id);
        emit(" vec3 ambient=materialBaseColor*mix(0.03,0.08,materialSubsurface);"
             "vec3 transmitted=materialBaseColor*(0.08+0.92*pow(1.0-nDotV,2.0));"
             "vec3 lit=mix(ambient+direct+coat+sheenLight,transmitted,"
             "materialTransmission)+materialEmissive;", output->id);
        emit(" FragColor=vec4(max(lit,vec3(0.0)),materialOpacity);",
             output->id);
    }
    emit("}");
    result.vertex = vertex.str();
    result.fragment = fragment.str();
    result.success = true;
    return result;
}

} // namespace engine
