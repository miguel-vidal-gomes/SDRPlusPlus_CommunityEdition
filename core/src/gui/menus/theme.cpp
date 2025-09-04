#include <gui/menus/theme.h>
#include <gui/gui.h>
#include <core.h>
#include <gui/style.h>
#include <gui/widgets/advanced_widgets.h>

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
            // Modern section header for Advanced theme
            ImGui::ModernSectionHeader("Visual Theme");
            
            // Enhanced theme selector with modern styling
            ImGui::Text("Select Theme:");
            ImGui::SetNextItemWidth(menuWidth);
            
            // Use modern styling for combo
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
            
            if (ImGui::Combo("##theme_select_combo", &themeId, themeNamesTxt.c_str())) {
                applyTheme();
                core::configManager.acquire();
                core::configManager.conf["theme"] = themeNames[themeId];
                core::configManager.release(true);
            }
            
            ImGui::PopStyleVar(2);
            
            // Theme preview/info for Advanced theme
            if (themeNames[themeId] == "Advanced") {
                ImGui::Spacing();
                
                // Theme features showcase
                if (ImGui::BeginModernCard("Advanced Theme Features")) {
                    ImGui::Text("🎨 Modern Design Elements");
                    ImGui::BulletText("Rounded corners and smooth gradients");
                    ImGui::BulletText("Enhanced spacing and typography");
                    ImGui::BulletText("Professional color palette");
                    
                    ImGui::Spacing();
                    
                    // Demo of modern components
                    static bool demo_toggle = false;
                    ImGui::Text("Demo Components:");
                    ImGui::ModernToggle("Modern Toggle", &demo_toggle);
                    
                    ImGui::Spacing();
                    
                    static float demo_progress = 0.75f;
                    ImGui::Text("Progress Bar:");
                    ImGui::ModernProgressBar(demo_progress, ImVec2(-1, 20), "75%");
                    
                    ImGui::Spacing();
                    
                    if (ImGui::ModernButton("Primary Action", ImVec2(120, 0), true)) {
                        // Demo action
                    }
                    ImGui::SameLine();
                    if (ImGui::ModernButton("Secondary", ImVec2(120, 0), false)) {
                        // Demo action
                    }
                    
                    ImGui::EndModernCard();
                }
                
                // Theme info with tooltip
                ImGui::Spacing();
                ImGui::Text("ℹ️ Advanced Theme Active");
                ImGui::ModernTooltip("The Advanced theme provides a modern, professional interface\n"
                                   "with enhanced visual elements and improved usability.\n\n"
                                   "Features include:\n"
                                   "• Smooth rounded corners\n"
                                   "• Professional color scheme\n"
                                   "• Enhanced component styling\n"
                                   "• Better visual hierarchy");
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