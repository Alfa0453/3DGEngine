#include "engine/ui/Hud.h"

#include "engine/assets/AssetReference.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/graphics/TextRenderer.h"
#include "engine/graphics/Shader.h"
#include "engine/physics/PhysicsComponents.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace engine {

// ---------------------------------------------------------------------------
// Document helpers
// ---------------------------------------------------------------------------
HudWidget& HudDocument::Add(HudWidgetType type) {
    HudWidget w;
    w.id = nextId++;
    w.type = type;
    switch (type) {
        case HudWidgetType::Panel:  w.name = "Panel";  w.size = {320.0f, 120.0f}; break;
        case HudWidgetType::Text:   w.name = "Text";   w.size = {260.0f, 32.0f};  w.text = "New Text"; break;
        case HudWidgetType::Bar:    w.name = "Bar";    w.size = {260.0f, 26.0f};  w.binding = HudBinding::HealthFraction; break;
        case HudWidgetType::Button: w.name = "Button"; w.size = {200.0f, 52.0f};  w.text = "Button";
                                    w.color = {0.20f, 0.42f, 0.75f, 0.92f}; break;
        case HudWidgetType::Image:  w.name = "Image";  w.size = {128.0f, 128.0f};
                                    w.color = {1.0f, 1.0f, 1.0f, 1.0f}; break;   // white tint = true colours
    }
    widgets.push_back(w);
    return widgets.back();
}

void HudDocument::Remove(int index) {
    if (index >= 0 && index < static_cast<int>(widgets.size())) {
        widgets.erase(widgets.begin() + index);
    }
}

void HudDocument::Clear() {
    widgets.clear();
    nextId = 1;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void HudWidgetRect(const HudWidget& w, const HudDocument& doc, int screenW, int screenH,
                   float& outX, float& outY, float& outW, float& outH) {
    const float s = static_cast<float>(screenH) / std::max(doc.designSize.y, 1.0f);
    outW = w.size.x * s;
    outH = w.size.y * s;

    const int idx = static_cast<int>(w.anchor);
    const int col = idx % 3;   // 0 left, 1 center, 2 right
    const int row = idx / 3;   // 0 top,  1 middle, 2 bottom

    const float ax = (col == 0) ? 0.0f : (col == 1) ? screenW * 0.5f : static_cast<float>(screenW);
    const float ay = (row == 0) ? 0.0f : (row == 1) ? screenH * 0.5f : static_cast<float>(screenH);

    outX = ax + w.offset.x * s;
    if (col == 1) outX -= outW * 0.5f;
    else if (col == 2) outX -= outW;

    outY = ay + w.offset.y * s;
    if (row == 1) outY -= outH * 0.5f;
    else if (row == 2) outY -= outH;
}

// ---------------------------------------------------------------------------
// Value formatting + binding resolution
// ---------------------------------------------------------------------------
namespace {

std::string FormatNumber(float v) {
    if (std::fabs(v - std::round(v)) < 0.001f) {
        return std::to_string(static_cast<long long>(std::llround(v)));
    }
    std::ostringstream os;
    os.precision(1);
    os << std::fixed << v;
    return os.str();
}

std::string Substitute(const std::string& tmpl, const std::string& value) {
    const std::string token = "{}";
    const auto pos = tmpl.find(token);
    if (pos == std::string::npos) {
        return value;   // no placeholder -> show the value alone
    }
    std::string out = tmpl;
    for (std::size_t p = out.find(token); p != std::string::npos; p = out.find(token, p)) {
        out.replace(p, token.size(), value);
        p += value.size();
    }
    return out;
}

std::string ResolveText(const HudWidget& w, const HudContext& ctx) {
    switch (w.binding) {
        case HudBinding::HealthText: {
            const std::string v = FormatNumber(ctx.health) + "/" + FormatNumber(ctx.maxHealth);
            return Substitute(w.text, v);
        }
        case HudBinding::NamedFloat: {
            auto it = ctx.floats.find(w.bindKey);
            const float v = it != ctx.floats.end() ? it->second : 0.0f;
            return Substitute(w.text, FormatNumber(v));
        }
        case HudBinding::NamedString: {
            auto it = ctx.strings.find(w.bindKey);
            return Substitute(w.text, it != ctx.strings.end() ? it->second : std::string());
        }
        case HudBinding::HealthFraction:
        case HudBinding::None:
        default:
            return w.text;
    }
}

float ResolveFraction(const HudWidget& w, const HudContext& ctx) {
    switch (w.binding) {
        case HudBinding::HealthFraction:
            return std::clamp(ctx.healthFraction, 0.0f, 1.0f);
        case HudBinding::NamedFloat: {
            auto it = ctx.floats.find(w.bindKey);
            const float v = it != ctx.floats.end() ? it->second : 0.0f;
            const float span = w.maxValue - w.minValue;
            if (std::fabs(span) < 1e-6f) return 0.0f;
            return std::clamp((v - w.minValue) / span, 0.0f, 1.0f);
        }
        default:
            return 1.0f;   // unbound bar previews full
    }
}

bool PointInRect(float px, float py, float x, float y, float w, float h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

} // namespace

std::string ResolveHudText(const HudWidget& widget, const HudContext& context) {
    return ResolveText(widget, context);
}

float ResolveHudFraction(const HudWidget& widget, const HudContext& context) {
    return ResolveFraction(widget, context);
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
HudDrawResult DrawHud(TextRenderer& text, const HudDocument& doc, const HudContext& ctx,
                      int screenW, int screenH) {
    HudDrawResult result;
    text.Begin(screenW, screenH);

    for (const HudWidget& w : doc.widgets) {
        if (!w.visible) continue;

        float x, y, ww, hh;
        HudWidgetRect(w, doc, screenW, screenH, x, y, ww, hh);
        const float s = static_cast<float>(screenH) / std::max(doc.designSize.y, 1.0f);
        const float effScale = std::max(w.textScale * s, 0.1f);
        auto drawRect = [&](float rx, float ry, float rw, float rh, const glm::vec4& color,
                            unsigned int texture = 0u) {
            if (w.customShader) {
                text.CustomRect(*const_cast<Shader*>(w.customShader), texture, rx, ry, rw, rh,
                                color, w.shaderParameters, w.shaderParameterTypes, w.shaderTextures);
            } else if (texture != 0) {
                text.Image(texture, rx, ry, rw, rh, glm::vec3(color), color.a);
            } else {
                text.FillRect(rx, ry, rw, rh, glm::vec3(color), color.a);
            }
        };

        switch (w.type) {
            case HudWidgetType::Panel:
                drawRect(x, y, ww, hh, w.color);
                break;

            case HudWidgetType::Image: {
                unsigned int tex = 0;
                if (!w.imageAsset.empty() && ctx.textureLookup) tex = ctx.textureLookup(w.imageAsset);
                drawRect(x, y, ww, hh, w.color, tex);
                break;
            }

            case HudWidgetType::Text: {
                const std::string str = ResolveHudText(w, ctx);
                text.Text(str, x, y, effScale, glm::vec3(w.color));
                break;
            }

            case HudWidgetType::Bar: {
                const float frac = ResolveHudFraction(w, ctx);
                drawRect(x, y, ww, hh, w.bgColor);
                if (frac > 0.0f) {
                    drawRect(x, y, ww * frac, hh, w.fillColor);
                }
                break;
            }

            case HudWidgetType::Button: {
                const bool hovered = ctx.cursorActive && PointInRect(ctx.cursorX, ctx.cursorY, x, y, ww, hh);
                glm::vec3 bg = glm::vec3(w.color);
                float alpha = w.color.a;
                if (hovered) {
                    bg = ctx.mousePressed ? bg * 0.8f : glm::min(bg * 1.25f, glm::vec3(1.0f));
                }
                drawRect(x, y, ww, hh, glm::vec4(bg, alpha));

                const std::string label = ResolveHudText(w, ctx);
                const float tw = text.Measure(label, effScale);
                const float th = TextRenderer::kGlyphPx * effScale;
                text.Text(label, x + (ww - tw) * 0.5f, y + (hh - th) * 0.5f, effScale, glm::vec3(0.97f));

                if (hovered && ctx.mousePressed && result.clickedWidget < 0) {
                    result.clickedWidget = w.id;
                    result.clickedAction = w.action;
                    result.clickedKey = w.bindKey;
                }
                break;
            }
        }
    }

    text.End();
    return result;
}

void DrawWorldHealthBars(TextRenderer& text, ecs::Registry& registry,
                         const glm::mat4& viewProjection,
                         int screenW, int screenH, ecs::Entity localPlayer) {
    if (screenW <= 0 || screenH <= 0) return;

    bool began = false;
    registry.view<ecs::Transform, Health>().each(
        [&](ecs::Entity entity, ecs::Transform& transform, Health& health) {
            if (entity == localPlayer || !health.alive || health.maxHp <= 0.0f) return;

            float top = 1.0f;
            if (const ecs::Collider* collider = registry.TryGet<ecs::Collider>(entity)) {
                switch (collider->shape) {
                    case ecs::ColliderShape::Sphere:
                        top = collider->radius;
                        break;
                    case ecs::ColliderShape::Capsule:
                        top = collider->halfHeight + collider->radius;
                        break;
                    case ecs::ColliderShape::Cylinder:
                    case ecs::ColliderShape::Cone:
                        top = collider->halfHeight;
                        break;
                    default:
                        top = collider->halfExtents.y;
                        break;
                }
            }

            const glm::vec4 clip = viewProjection
                * glm::vec4(transform.position + glm::vec3(0.0f, top + 0.28f, 0.0f), 1.0f);
            if (clip.w <= 0.001f) return;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f
                || ndc.x < -1.1f || ndc.x > 1.1f
                || ndc.y < -1.1f || ndc.y > 1.1f) return;

            if (!began) {
                text.Begin(screenW, screenH);
                began = true;
            }

            const float fraction =
                std::clamp(health.hp / health.maxHp, 0.0f, 1.0f);
            const float width = std::clamp(
                150.0f / std::sqrt(std::max(clip.w, 1.0f)), 72.0f, 124.0f);
            const float height = std::clamp(width * 0.095f, 7.0f, 11.0f);
            const float x = (ndc.x * 0.5f + 0.5f) * screenW - width * 0.5f;
            const float y = (1.0f - (ndc.y * 0.5f + 0.5f)) * screenH;

            const glm::vec3 low(0.90f, 0.12f, 0.08f);
            const glm::vec3 high(0.15f, 0.82f, 0.24f);
            const glm::vec3 fill = glm::mix(low, high, fraction);
            text.FillRect(x - 2.0f, y - 2.0f, width + 4.0f, height + 4.0f,
                          glm::vec3(0.02f), 0.92f);
            text.FillRect(x, y, width, height, glm::vec3(0.16f, 0.025f, 0.025f), 0.88f);
            if (fraction > 0.0f) {
                text.FillRect(x, y, width * fraction, height, fill, 0.98f);
            }
        });

    if (began) text.End();
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
const char* HudWidgetTypeName(HudWidgetType type) {
    switch (type) {
        case HudWidgetType::Panel:  return "Panel";
        case HudWidgetType::Text:   return "Text";
        case HudWidgetType::Bar:    return "Bar";
        case HudWidgetType::Button: return "Button";
        case HudWidgetType::Image:  return "Image";
    }
    return "?";
}

const char* HudAnchorName(HudAnchor a) {
    switch (a) {
        case HudAnchor::TopLeft:      return "Top Left";
        case HudAnchor::TopCenter:    return "Top Center";
        case HudAnchor::TopRight:     return "Top Right";
        case HudAnchor::MiddleLeft:   return "Middle Left";
        case HudAnchor::Center:       return "Center";
        case HudAnchor::MiddleRight:  return "Middle Right";
        case HudAnchor::BottomLeft:   return "Bottom Left";
        case HudAnchor::BottomCenter: return "Bottom Center";
        case HudAnchor::BottomRight:  return "Bottom Right";
    }
    return "?";
}

const char* HudBindingName(HudBinding b) {
    switch (b) {
        case HudBinding::None:           return "None";
        case HudBinding::HealthFraction: return "Health Fraction";
        case HudBinding::HealthText:     return "Health Text";
        case HudBinding::NamedFloat:     return "Named Float";
        case HudBinding::NamedString:    return "Named String";
    }
    return "?";
}

const char* HudButtonActionName(HudButtonAction a) {
    switch (a) {
        case HudButtonAction::None:        return "None";
        case HudButtonAction::ExitPlay:    return "Exit Play";
        case HudButtonAction::RestartPlay: return "Restart Play";
        case HudButtonAction::EmitEvent:   return "Emit Event";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Serialization (.hud text format v1)
// ---------------------------------------------------------------------------
namespace {

std::string SanitizeToken(const std::string& in) {
    if (in.empty()) return "~";
    std::string out = in;
    for (char& c : out) if (c == ' ' || c == '\t' || c == '\n') c = '_';
    return out;
}

std::string ReadToken(const std::string& tok) {
    return tok == "~" ? std::string() : tok;
}

} // namespace

bool HudDocument::Save(const std::string& path, std::string* error) {
    const std::string contentRoot = FindContentRootForAsset(path);
    AssetRegistry registry;
    std::string registryError;
    const std::string registryPath = contentRoot.empty()
        ? std::string() : AssetRegistry::DefaultRegistryPath(contentRoot);
    std::error_code ec;
    if (!registryPath.empty() && std::filesystem::exists(registryPath, ec)
        && !registry.Load(registryPath, &registryError)) {
        if (error) *error = "Could not load HUD asset registry: " + registryError;
        return false;
    }
    if (!assetId.Valid() && std::filesystem::is_regular_file(path, ec)) {
        std::ifstream existing(path);
        std::string magic, idText;
        int version = 0;
        if (existing >> magic >> version >> idText
            && magic == "3DG_HUD" && version >= 3)
            AssetHandle::Parse(idText, &assetId);
    }
    if (!assetId.Valid()) assetId = AssetHandle::Generate();
    std::vector<AssetHandle> dependencies;
    const auto capture = [&](std::string& assetPath, AssetHandle& id,
                             AssetType type) {
        if (contentRoot.empty() || assetPath.empty()) return;
        const AssetReference reference =
            MakeAssetReference(&registry, contentRoot, assetPath, type);
        if (reference.id.Valid()) id = reference.id;
        if (id.Valid()
            && std::find(dependencies.begin(), dependencies.end(), id)
                == dependencies.end())
            dependencies.push_back(id);
    };
    for (HudWidget& widget : widgets) {
        capture(widget.imageAsset, widget.imageAssetId, AssetType::Texture);
        capture(widget.shaderPath, widget.shaderAssetId, AssetType::Shader);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "Could not open '" + path + "' for writing";
        return false;
    }
    out << "3DG_HUD 3 " << assetId.ToString() << '\n';
    out << "design " << designSize.x << ' ' << designSize.y << '\n';
    out << "count " << widgets.size() << '\n';
    for (const HudWidget& w : widgets) {
        out << "w "
            << w.id << ' ' << w.parent << ' '
            << static_cast<int>(w.type) << ' '
            << static_cast<int>(w.anchor) << ' '
            << w.offset.x << ' ' << w.offset.y << ' '
            << w.size.x << ' ' << w.size.y << ' '
            << w.color.r << ' ' << w.color.g << ' ' << w.color.b << ' ' << w.color.a << ' '
            << w.bgColor.r << ' ' << w.bgColor.g << ' ' << w.bgColor.b << ' ' << w.bgColor.a << ' '
            << w.fillColor.r << ' ' << w.fillColor.g << ' ' << w.fillColor.b << ' ' << w.fillColor.a << ' '
            << w.textScale << ' '
            << static_cast<int>(w.binding) << ' '
            << w.minValue << ' ' << w.maxValue << ' '
            << static_cast<int>(w.action) << ' '
            << (w.visible ? 1 : 0) << ' '
            << SanitizeToken(w.name) << ' '
            << SanitizeToken(w.bindKey) << ' '
            << SanitizeToken(w.imageAsset) << '\n';
        out << "t " << w.text << '\n';
        out << "s " << std::quoted(w.shaderPath) << ' '
            << (w.imageAssetId.Valid() ? w.imageAssetId.ToString() : std::string("-")) << ' '
            << (w.shaderAssetId.Valid() ? w.shaderAssetId.ToString() : std::string("-")) << ' '
            << w.shaderParameters.size();
        for (const auto& [name, value] : w.shaderParameters) {
            const auto type = w.shaderParameterTypes.find(name);
            out << ' ' << std::quoted(name) << ' '
                << (type == w.shaderParameterTypes.end() ? 0 : type->second) << ' '
                << std::quoted(value);
        }
        out << '\n';
    }
    out << "ASSET_DEPS " << dependencies.size();
    for (AssetHandle dependency : dependencies)
        out << ' ' << dependency.ToString();
    out << '\n';
    if (!out) return false;
    out.close();
    if (!contentRoot.empty()) {
        AssetRegistryEntry entry;
        entry.id = assetId;
        entry.type = AssetType::Hud;
        entry.virtualPath = AssetRegistry::NormalizeVirtualPath(
            std::filesystem::relative(path, contentRoot, ec).generic_string());
        entry.importerVersion = 1;
        entry.dependencies = dependencies;
        if (ec || !registry.Register(std::move(entry), &registryError)
            || !registry.Save(registryPath, &registryError)) {
            if (error) *error = "HUD saved, but registration failed: "
                + (ec ? ec.message() : registryError);
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

bool HudDocument::Load(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Could not open '" + path + "'";
        return false;
    }

    HudDocument loaded;
    std::string line;
    int declaredCount = 0;
    int maxId = 0;
    int fileVersion = 1;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;

        if (tag == "3DG_HUD") {
            std::string idText;
            ls >> fileVersion >> idText;
            if (fileVersion < 3
                || !AssetHandle::Parse(idText, &loaded.assetId)) {
                if (error) *error = "Malformed HUD asset identity";
                return false;
            }
        } else if (tag == "hud") {
            ls >> fileVersion;
        } else if (tag == "design") {
            ls >> loaded.designSize.x >> loaded.designSize.y;
        } else if (tag == "count") {
            ls >> declaredCount;
        } else if (tag == "w") {
            HudWidget w;
            int type = 0, anchor = 0, binding = 0, action = 0, visible = 1;
            std::string nameTok = "~", keyTok = "~", imageTok = "~";
            ls >> w.id >> w.parent >> type >> anchor
               >> w.offset.x >> w.offset.y >> w.size.x >> w.size.y
               >> w.color.r >> w.color.g >> w.color.b >> w.color.a
               >> w.bgColor.r >> w.bgColor.g >> w.bgColor.b >> w.bgColor.a
               >> w.fillColor.r >> w.fillColor.g >> w.fillColor.b >> w.fillColor.a
               >> w.textScale >> binding >> w.minValue >> w.maxValue
               >> action >> visible >> nameTok >> keyTok >> imageTok;
            w.type    = static_cast<HudWidgetType>(std::clamp(type, 0, 4));
            w.anchor  = static_cast<HudAnchor>(std::clamp(anchor, 0, 8));
            w.binding = static_cast<HudBinding>(std::clamp(binding, 0, 4));
            w.action  = static_cast<HudButtonAction>(std::clamp(action, 0, 3));
            w.visible = visible != 0;
            w.name       = ReadToken(nameTok);
            w.bindKey    = ReadToken(keyTok);
            w.imageAsset = ReadToken(imageTok);
            maxId = std::max(maxId, w.id);

            // The next line holds the widget's text ("t <text>").
            std::string textLine;
            if (std::getline(in, textLine)) {
                if (textLine.rfind("t ", 0) == 0) w.text = textLine.substr(2);
                else if (textLine == "t") w.text.clear();
            }
            if (fileVersion >= 2) {
                std::string shaderLine;
                if (std::getline(in, shaderLine)) {
                    std::istringstream shaderStream(shaderLine);
                    std::string shaderTag;
                    std::size_t parameterCount = 0;
                    shaderStream >> shaderTag >> std::quoted(w.shaderPath);
                    if (fileVersion >= 3) {
                        std::string imageId, shaderId;
                        shaderStream >> imageId >> shaderId;
                        if ((imageId != "-"
                             && !AssetHandle::Parse(imageId, &w.imageAssetId))
                            || (shaderId != "-"
                                && !AssetHandle::Parse(
                                    shaderId, &w.shaderAssetId))) {
                            if (error) *error =
                                "Malformed HUD widget asset identity";
                            return false;
                        }
                    }
                    shaderStream >> parameterCount;
                    if (shaderTag != "s") {
                        if (error) *error = "Malformed HUD shader record";
                        return false;
                    }
                    for (std::size_t i = 0; i < parameterCount; ++i) {
                        std::string name;
                        std::string value;
                        int parameterType = 0;
                        shaderStream >> std::quoted(name) >> parameterType >> std::quoted(value);
                        w.shaderParameters[name] = value;
                        w.shaderParameterTypes[name] = parameterType;
                    }
                }
            }
            loaded.widgets.push_back(w);
        }
    }

    (void)declaredCount;
    loaded.nextId = maxId + 1;
    const std::string contentRoot = FindContentRootForAsset(path);
    AssetRegistry registry;
    std::string ignored;
    if (!contentRoot.empty()
        && registry.Load(
            AssetRegistry::DefaultRegistryPath(contentRoot), &ignored)) {
        for (HudWidget& widget : loaded.widgets) {
            if (widget.imageAssetId.Valid())
                widget.imageAsset = ResolveAssetReference(
                    &registry, contentRoot,
                    {widget.imageAssetId, widget.imageAsset},
                    AssetType::Texture);
            if (widget.shaderAssetId.Valid())
                widget.shaderPath = ResolveAssetReference(
                    &registry, contentRoot,
                    {widget.shaderAssetId, widget.shaderPath},
                    AssetType::Shader);
        }
    }
    *this = std::move(loaded);
    return true;
}

} // namespace engine
