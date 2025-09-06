#include <gui/menus/theme.h>
#include <gui/gui.h>
#include <core.h>
#include <gui/style.h>

namespace thememenu {
    int themeId;
    std::vector<std::string> themeNames;
    std::string themeNamesTxt;

    void init(std::string resDir) {
        // TODO: Not hardcode theme directory
        gui::themeManager.loadThemesFromDir(resDir + "/themes/");
        core::configManager.acquire();
        std::string selectedThemeName = core::configManager.conf["theme"];
        core::configManager.release();

        // Select theme by name, if not available, apply Dark theme
        themeNames = gui::themeManager.getThemeNames();
        auto it = std::find(themeNames.begin(), themeNames.end(), selectedThemeName);
        if (it == themeNames.end()) {
            it = std::find(themeNames.begin(), themeNames.end(), "Dark");
            selectedThemeName = "Dark";
        }
        themeId = std::distance(themeNames.begin(), it);
        applyTheme();

        // Apply scaling
        ImGui::GetStyle().ScaleAllSizes(style::uiScale);

        themeNamesTxt = "";
        for (auto name : themeNames) {
            themeNamesTxt += name;
            themeNamesTxt += '\0';
        }
    }

     void applyTheme() {
         gui::themeManager.applyTheme(themeNames[themeId]);
     }

    void draw(void* ctx) {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        
        // Check if we're using the Advanced theme for enhanced UI
        bool isAdvancedTheme = (themeId < themeNames.size() && themeNames[themeId] == "Advanced");
        
        if (isAdvancedTheme) {
            // Enhanced theme selector for Advanced theme - use standard ImGui but with enhanced styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            
            ImGui::Text("🎨 Visual Theme (Advanced Mode)");
            ImGui::SetNextItemWidth(menuWidth);
            
            if (ImGui::Combo("##theme_select_combo", &themeId, themeNamesTxt.c_str())) {
                applyTheme();
                core::configManager.acquire();
                core::configManager.conf["theme"] = themeNames[themeId];
                core::configManager.release(true);
            }
            
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            
            // Theme preview/info for Advanced theme
            if (themeNames[themeId] == "Advanced") {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                // Theme features showcase using standard ImGui components
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.8f, 1.0f, 1.0f));
                ImGui::Text("✨ Advanced Theme Features");
                ImGui::PopStyleColor();
                
                ImGui::BulletText("🔘 Smooth rounded corners (10px windows)");
                ImGui::BulletText("🎨 Professional cyan accent theme");
                ImGui::BulletText("📏 Refined padding and spacing");
                ImGui::BulletText("🖼️ Clean borders for subtle definition");
                ImGui::BulletText("📱 Comfortable, professional controls");
                
                ImGui::Spacing();
                
                // Demo components using standard ImGui
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.6f, 1.0f));
                ImGui::Text("🔧 Enhanced Controls:");
                ImGui::PopStyleColor();
                
                static bool demo_check = false;
                ImGui::Checkbox("Enhanced Checkbox", &demo_check);
                
                static float demo_slider = 0.75f;
                ImGui::SliderFloat("Smooth Slider", &demo_slider, 0.0f, 1.0f, "%.2f");
                
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                if (ImGui::Button("Modern Button", ImVec2(120, 32))) {
                    // Demo action
                }
                ImGui::PopStyleVar();
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                ImGui::TextWrapped("🌟 The Advanced theme provides a sophisticated, professional interface with refined rounded elements, elegant cyan accents, and carefully balanced spacing for a premium SDR experience.");
                ImGui::PopStyleColor();
            }
        } else {
            // Standard theme selector for other themes
            ImGui::LeftLabel("Theme");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::Combo("##theme_select_combo", &themeId, themeNamesTxt.c_str())) {
                applyTheme();
                core::configManager.acquire();
                core::configManager.conf["theme"] = themeNames[themeId];
                core::configManager.release(true);
            }
            
            // Show a hint about the Advanced theme
            if (std::find(themeNames.begin(), themeNames.end(), "Advanced") != themeNames.end()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "💡 Try the 'Advanced' theme for a modern interface!");
            }
        }
    }
}