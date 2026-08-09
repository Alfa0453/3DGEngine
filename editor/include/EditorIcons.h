#pragma once

#include <imgui.h>

#include <string>

namespace editor::icons {

// Segoe MDL2 Assets semantic glyphs. The font is merged into ImGui's regular
// text font by ImGuiLayer on Windows. Every helper falls back to its text label
// when the icon font is unavailable, so editor controls remain usable.
inline constexpr ImWchar kProbeGlyph = 0xE8B7;
inline constexpr const char* Add = u8"\uE710";
inline constexpr const char* Archive = u8"\uE7B8";
inline constexpr const char* Back = u8"\uE72B";
inline constexpr const char* Code = u8"\uE943";
inline constexpr const char* Contact = u8"\uE77B";
inline constexpr const char* Copy = u8"\uE8C8";
inline constexpr const char* Cut = u8"\uE8C6";
inline constexpr const char* Delete = u8"\uE74D";
inline constexpr const char* Document = u8"\uE8A5";
inline constexpr const char* Download = u8"\uE896";
inline constexpr const char* Edit = u8"\uE70F";
inline constexpr const char* Folder = u8"\uE8B7";
inline constexpr const char* Image = u8"\uE9D2";
inline constexpr const char* Layers = u8"\uE81E";
inline constexpr const char* Leaf = u8"\uE8BE";
inline constexpr const char* Link = u8"\uE71B";
inline constexpr const char* Music = u8"\uE8D6";
inline constexpr const char* Open = u8"\uE8E5";
inline constexpr const char* Palette = u8"\uE790";
inline constexpr const char* Paste = u8"\uE77F";
inline constexpr const char* Play = u8"\uE768";
inline constexpr const char* Refresh = u8"\uE72C";
inline constexpr const char* Screen = u8"\uE91B";
inline constexpr const char* Settings = u8"\uE713";
inline constexpr const char* Star = u8"\uE735";
inline constexpr const char* Stop = u8"\uE71A";
inline constexpr const char* Save = u8"\uE74E";
inline constexpr const char* Up = u8"\uE70E";
inline constexpr const char* Video = u8"\uE714";
inline constexpr const char* World = u8"\uE909";

inline bool Available() {
    ImFont* font = ImGui::GetFont();
    return font && font->IsGlyphInFont(kProbeGlyph);
}

inline std::string Label(const char* icon, const char* text) {
    if (!Available()) return text ? std::string(text) : std::string();
    std::string result = icon ? icon : "";
    if (text && *text) {
        result += "  ";
        result += text;
    }
    return result;
}

inline std::string WindowLabel(const char* icon, const char* text) {
    const std::string visible = Label(icon, text);
    return visible + "###" + (text ? text : "");
}

inline std::string ControlLabel(const char* icon, const char* label) {
    if (!Available() || !label) return label ? std::string(label) : std::string();
    const std::string original(label);
    const std::size_t idStart = original.find("##");
    const std::string visible = original.substr(0, idStart);
    const std::string id = idStart == std::string::npos
        ? std::string() : original.substr(idStart);
    return std::string(icon ? icon : "") + "  " + visible + id;
}

inline bool LabeledButton(const char* icon, const char* label,
                          const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    const std::string decorated = ControlLabel(icon, label);
    return ImGui::Button(decorated.c_str(), size);
}

inline bool Button(const char* icon, const char* tooltip) {
    const std::string visible = Available() ? std::string(icon) : std::string(tooltip);
    const std::string id = visible + "##EditorIcon" + tooltip;
    const bool pressed = ImGui::Button(id.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

inline bool MenuItem(const char* icon, const char* text, bool enabled = true) {
    const std::string label = Label(icon, text);
    return ImGui::MenuItem(label.c_str(), nullptr, false, enabled);
}

inline bool MenuItem(const char* icon, const char* text, const char* shortcut,
                     bool selected, bool enabled = true) {
    const std::string label = Label(icon, text);
    return ImGui::MenuItem(label.c_str(), shortcut, selected, enabled);
}

} // namespace editor::icons
