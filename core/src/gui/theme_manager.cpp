#include <json.hpp>
#include <gui/theme_manager.h>
#include <imgui_internal.h>
#include <utils/flog.h>
#include <filesystem>
#include <fstream>

bool ThemeManager::loadThemesFromDir(std::string path) {
    // // TEST JUST TO DUMP THE ORIGINAL THEME
    // auto& style = ImGui::GetStyle();
    // ImVec4* colors = style.Colors;

    // printf("\n\n");
    // for (auto [name, id] : IMGUI_COL_IDS) {
    //     ImVec4 col = colors[id];
    //     uint8_t r = 255 - (col.x * 255.0f);
    //     uint8_t g = 255 - (col.y * 255.0f);
    //     uint8_t b = 255 - (col.z * 255.0f);
    //     uint8_t a = col.w * 255.0f;
    //     printf("\"%s\": \"#%02X%02X%02X%02X\",\n", name.c_str(), r, g, b, a);
    // }
    // printf("\n\n");


    if (!std::filesystem::is_directory(path)) {
        flog::error("Theme directory doesn't exist: {0}", path);
        return false;
    }
    themes.clear();
    for (const auto& file : std::filesystem::directory_iterator(path)) {
        std::string _path = file.path().generic_string();
        if (file.path().extension().generic_string() != ".json") {
            continue;
        }
        loadTheme(_path);
    }
    return true;
}

bool ThemeManager::loadTheme(std::string path) {
    if (!std::filesystem::is_regular_file(path)) {
        flog::error("Theme file doesn't exist: {0}", path);
        return false;
    }

    // Load defaults in theme
    Theme thm;
    thm.author = "--";

    // Load JSON
    std::ifstream file(path.c_str());
    json data;
    file >> data;
    file.close();

    // Load theme name
    if (!data.contains("name")) {
        flog::error("Theme {0} is missing the name parameter", path);
        return false;
    }
    if (!data["name"].is_string()) {
        flog::error("Theme {0} contains invalid name field. Expected string", path);
        return false;
    }
    std::string name = data["name"];

    if (themes.find(name) != themes.end()) {
        flog::error("A theme named '{0}' already exists", name);
        return false;
    }

    // Load theme author if available
    if (data.contains("author")) {
        if (!data["author"].is_string()) {
            flog::error("Theme {0} contains invalid author field. Expected string", path);
            return false;
        }
        thm.author = data["author"];
    }

    // Iterate through all parameters and check their contents
    std::map<std::string, std::string> params = data;
    for (auto const& [param, val] : params) {
        if (param == "name" || param == "author") { continue; }

        // Exception for non-imgu colors
        if (param == "WaterfallBackground" || param == "ClearColor" || param == "FFTHoldColor") {
            if (val[0] != '#' || !std::all_of(val.begin() + 1, val.end(), ::isxdigit) || val.length() != 9) {
                flog::error("Theme {0} contains invalid {1} field. Expected hex RGBA color", path, param);
                return false;
            }
            continue;
        }

        bool isValid = false;

        // Check if it's a style parameter
        if (param == "WindowRounding" || param == "ChildRounding" || param == "FrameRounding" || 
            param == "GrabRounding" || param == "PopupRounding" || param == "ScrollbarRounding" || 
            param == "TabRounding" || param == "WindowPaddingX" || param == "WindowPaddingY" ||
            param == "FramePaddingX" || param == "FramePaddingY" || param == "ItemSpacingX" || 
            param == "ItemSpacingY" || param == "WindowBorderSize" || param == "FrameBorderSize") {
            // Style parameters should be valid numbers
            try {
                std::stof(val);
                isValid = true;
            } catch (const std::exception&) {
                flog::error("Theme {0} contains invalid {1} field. Expected numeric value", path, param);
                return false;
            }
        }

        // If param is a color, check that it's a valid RGBA hex value
        if (IMGUI_COL_IDS.find(param) != IMGUI_COL_IDS.end()) {
            if (val[0] != '#' || !std::all_of(val.begin() + 1, val.end(), ::isxdigit) || val.length() != 9) {
                flog::error("Theme {0} contains invalid {1} field. Expected hex RGBA color", path, param);
                return false;
            }
            isValid = true;
        }

        if (!isValid) {
            flog::error("Theme {0} contains unknown {1} field.", path, param);
            return false;
        }
    }

    thm.data = data;
    themes[name] = thm;

    return true;
}

bool ThemeManager::applyTheme(std::string name) {
    if (themes.find(name) == themes.end()) {
        flog::error("Unknown theme: {0}", name);
        return false;
    }

    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();

    // Set default modern style values (can be overridden by theme)
    if (name == "Advanced") {
        // Advanced theme - Sophisticated professional styling
        style.WindowRounding = 10.0f;     // Smooth rounded windows
        style.ChildRounding = 8.0f;       // Subtle rounded child windows
        style.FrameRounding = 6.0f;       // Professional rounded frames
        style.GrabRounding = 6.0f;        // Smooth sliders/scrollbars
        style.PopupRounding = 8.0f;       // Clean rounded popups
        style.ScrollbarRounding = 12.0f;  // Elegant scrollbars
        style.TabRounding = 6.0f;         // Clean tabs
        
        // Professional spacing - refined but not excessive
        style.WindowPadding = ImVec2(16.0f, 16.0f);    // Professional window padding
        style.FramePadding = ImVec2(12.0f, 8.0f);      // Comfortable frame padding
        style.ItemSpacing = ImVec2(12.0f, 8.0f);       // Clean spacing between items
        style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);   // Refined inner spacing
        style.IndentSpacing = 28.0f;                   // Professional indentation
        
        // Subtle borders for definition
        style.WindowBorderSize = 0.0f;    // Clean borderless windows
        style.ChildBorderSize = 1.0f;     // Subtle child borders
        style.PopupBorderSize = 1.0f;     // Clean popup borders
        style.FrameBorderSize = 0.0f;     // Clean frameless controls
        style.TabBorderSize = 0.0f;       // Clean borderless tabs
        
        // Professional visual elements
        style.ScrollbarSize = 16.0f;      // Standard scrollbars
        style.GrabMinSize = 12.0f;        // Professional grab handles
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f); // Center window titles
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);  // Center button text
        
        // Enhanced visual elements (note: WindowShadowSize not available in this ImGui version)
        // style.WindowShadowSize = 8.0f;  // Not available in current ImGui version
    } else {
        // Default modern style for other themes
        style.WindowRounding = 6.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 3.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 9.0f;
        style.TabRounding = 4.0f;
        
        // Modern padding and spacing
        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 22.0f;
        
        // Better borders and separators
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
    }

    ImVec4* colors = style.Colors;
    Theme thm = themes[name];

    uint8_t ret[4];
    std::map<std::string, std::string> params = thm.data;
    for (auto const& [param, val] : params) {
        if (param == "name" || param == "author") { continue; }

        if (param == "WaterfallBackground") {
            decodeRGBA(val, ret);
            waterfallBg = ImVec4((float)ret[0] / 255.0f, (float)ret[1] / 255.0f, (float)ret[2] / 255.0f, (float)ret[3] / 255.0f);
            continue;
        }

        if (param == "ClearColor") {
            decodeRGBA(val, ret);
            clearColor = ImVec4((float)ret[0] / 255.0f, (float)ret[1] / 255.0f, (float)ret[2] / 255.0f, (float)ret[3] / 255.0f);
            continue;
        }

        if (param == "FFTHoldColor") {
            decodeRGBA(val, ret);
            fftHoldColor = ImVec4((float)ret[0] / 255.0f, (float)ret[1] / 255.0f, (float)ret[2] / 255.0f, (float)ret[3] / 255.0f);
            continue;
        }

        // Handle style parameters
        if (param == "WindowRounding") { style.WindowRounding = std::stof(val); continue; }
        if (param == "ChildRounding") { style.ChildRounding = std::stof(val); continue; }
        if (param == "FrameRounding") { style.FrameRounding = std::stof(val); continue; }
        if (param == "GrabRounding") { style.GrabRounding = std::stof(val); continue; }
        if (param == "PopupRounding") { style.PopupRounding = std::stof(val); continue; }
        if (param == "ScrollbarRounding") { style.ScrollbarRounding = std::stof(val); continue; }
        if (param == "TabRounding") { style.TabRounding = std::stof(val); continue; }
        if (param == "WindowPaddingX") { style.WindowPadding.x = std::stof(val); continue; }
        if (param == "WindowPaddingY") { style.WindowPadding.y = std::stof(val); continue; }
        if (param == "FramePaddingX") { style.FramePadding.x = std::stof(val); continue; }
        if (param == "FramePaddingY") { style.FramePadding.y = std::stof(val); continue; }
        if (param == "ItemSpacingX") { style.ItemSpacing.x = std::stof(val); continue; }
        if (param == "ItemSpacingY") { style.ItemSpacing.y = std::stof(val); continue; }
        if (param == "WindowBorderSize") { style.WindowBorderSize = std::stof(val); continue; }
        if (param == "FrameBorderSize") { style.FrameBorderSize = std::stof(val); continue; }

        // If param is a color, check that it's a valid RGBA hex value
        if (IMGUI_COL_IDS.find(param) != IMGUI_COL_IDS.end()) {
            decodeRGBA(val, ret);
            colors[IMGUI_COL_IDS[param]] = ImVec4((float)ret[0] / 255.0f, (float)ret[1] / 255.0f, (float)ret[2] / 255.0f, (float)ret[3] / 255.0f);
            continue;
        }
    }

    return true;
}

bool ThemeManager::decodeRGBA(std::string str, uint8_t out[4]) {
    if (str[0] != '#' || !std::all_of(str.begin() + 1, str.end(), ::isxdigit) || str.length() != 9) {
        return false;
    }
    out[0] = std::stoi(str.substr(1, 2), NULL, 16);
    out[1] = std::stoi(str.substr(3, 2), NULL, 16);
    out[2] = std::stoi(str.substr(5, 2), NULL, 16);
    out[3] = std::stoi(str.substr(7, 2), NULL, 16);
    return true;
}

std::vector<std::string> ThemeManager::getThemeNames() {
    std::vector<std::string> names;
    for (auto [name, theme] : themes) { names.push_back(name); }
    return names;
}

std::map<std::string, int> ThemeManager::IMGUI_COL_IDS = {
    { "Text", ImGuiCol_Text },
    { "TextDisabled", ImGuiCol_TextDisabled },
    { "WindowBg", ImGuiCol_WindowBg },
    { "ChildBg", ImGuiCol_ChildBg },
    { "PopupBg", ImGuiCol_PopupBg },
    { "Border", ImGuiCol_Border },
    { "BorderShadow", ImGuiCol_BorderShadow },
    { "FrameBg", ImGuiCol_FrameBg },
    { "FrameBgHovered", ImGuiCol_FrameBgHovered },
    { "FrameBgActive", ImGuiCol_FrameBgActive },
    { "TitleBg", ImGuiCol_TitleBg },
    { "TitleBgActive", ImGuiCol_TitleBgActive },
    { "TitleBgCollapsed", ImGuiCol_TitleBgCollapsed },
    { "MenuBarBg", ImGuiCol_MenuBarBg },
    { "ScrollbarBg", ImGuiCol_ScrollbarBg },
    { "ScrollbarGrab", ImGuiCol_ScrollbarGrab },
    { "ScrollbarGrabHovered", ImGuiCol_ScrollbarGrabHovered },
    { "ScrollbarGrabActive", ImGuiCol_ScrollbarGrabActive },
    { "CheckMark", ImGuiCol_CheckMark },
    { "SliderGrab", ImGuiCol_SliderGrab },
    { "SliderGrabActive", ImGuiCol_SliderGrabActive },
    { "Button", ImGuiCol_Button },
    { "ButtonHovered", ImGuiCol_ButtonHovered },
    { "ButtonActive", ImGuiCol_ButtonActive },
    { "Header", ImGuiCol_Header },
    { "HeaderHovered", ImGuiCol_HeaderHovered },
    { "HeaderActive", ImGuiCol_HeaderActive },
    { "Separator", ImGuiCol_Separator },
    { "SeparatorHovered", ImGuiCol_SeparatorHovered },
    { "SeparatorActive", ImGuiCol_SeparatorActive },
    { "ResizeGrip", ImGuiCol_ResizeGrip },
    { "ResizeGripHovered", ImGuiCol_ResizeGripHovered },
    { "ResizeGripActive", ImGuiCol_ResizeGripActive },
    { "Tab", ImGuiCol_Tab },
    { "TabHovered", ImGuiCol_TabHovered },
    { "TabActive", ImGuiCol_TabActive },
    { "TabUnfocused", ImGuiCol_TabUnfocused },
    { "TabUnfocusedActive", ImGuiCol_TabUnfocusedActive },
    { "PlotLines", ImGuiCol_PlotLines },
    { "PlotLinesHovered", ImGuiCol_PlotLinesHovered },
    { "PlotHistogram", ImGuiCol_PlotHistogram },
    { "PlotHistogramHovered", ImGuiCol_PlotHistogramHovered },
    { "TableHeaderBg", ImGuiCol_TableHeaderBg },
    { "TableBorderStrong", ImGuiCol_TableBorderStrong },
    { "TableBorderLight", ImGuiCol_TableBorderLight },
    { "TableRowBg", ImGuiCol_TableRowBg },
    { "TableRowBgAlt", ImGuiCol_TableRowBgAlt },
    { "TextSelectedBg", ImGuiCol_TextSelectedBg },
    { "DragDropTarget", ImGuiCol_DragDropTarget },
    { "NavHighlight", ImGuiCol_NavHighlight },
    { "NavWindowingHighlight", ImGuiCol_NavWindowingHighlight },
    { "NavWindowingDimBg", ImGuiCol_NavWindowingDimBg },
    { "ModalWindowDimBg", ImGuiCol_ModalWindowDimBg }
};