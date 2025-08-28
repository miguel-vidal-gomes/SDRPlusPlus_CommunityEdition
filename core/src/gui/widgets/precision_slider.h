#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <cstdio>

namespace ImGui {
    /**
     * Enhanced slider with keyboard input capability and better UX
     * Header-only implementation for immediate compatibility
     */
    
    enum PrecisionSliderMode {
        PRECISION_SLIDER_MODE_SLIDER_ONLY,    // Traditional slider with CTRL+Click hint
        PRECISION_SLIDER_MODE_HYBRID,         // Slider + small input field side by side
        PRECISION_SLIDER_MODE_AUTO_SWITCH     // Automatically switch based on interaction
    };
    
    /**
     * Helper function to show informative tooltip for precision sliders
     */
    inline void ShowPrecisionSliderTooltip(const char* description = nullptr) {
        if (description != nullptr) {
            SetTooltip("%s\n\nSlider: Quick mouse adjustments\nKeyboard: Precise value entry\nMousewheel: Fine adjustments", description);
        } else {
            SetTooltip("Slider: Quick mouse adjustments\nKeyboard: Precise value entry\nMousewheel: Fine adjustments");
        }
    }
    
    /**
     * Enhanced SliderFloat with precision input capabilities
     */
    inline bool PrecisionSliderFloat(
        const char* label, 
        float* v, 
        float v_min, 
        float v_max, 
        const char* format = "%.3f", 
        ImGuiSliderFlags flags = 0,
        PrecisionSliderMode mode = PRECISION_SLIDER_MODE_HYBRID,
        bool showInputField = true
    ) {
        bool value_changed = false;
        
        // Generate unique ID for input component
        std::string input_label = std::string("##precision_input_") + label;
        
        // Calculate layout
        float available_width = GetContentRegionAvail().x;
        float input_width = showInputField ? 80.0f : 0.0f;
        float slider_width = available_width - input_width - (showInputField ? 4.0f : 0.0f);
        
        switch (mode) {
            case PRECISION_SLIDER_MODE_SLIDER_ONLY: {
                // Traditional slider with enhanced tooltip
                SetNextItemWidth(available_width);
                value_changed = SliderFloat(label, v, v_min, v_max, format, flags);
                
                if (IsItemHovered()) {
                    ShowPrecisionSliderTooltip("CTRL+Click for keyboard input");
                }
                break;
            }
            
            case PRECISION_SLIDER_MODE_HYBRID: {
                // Slider + Input field side by side
                
                // Slider component
                SetNextItemWidth(slider_width);
                if (SliderFloat(label, v, v_min, v_max, format, flags)) {
                    value_changed = true;
                }
                
                if (showInputField) {
                    SameLine();
                    
                    // Input field component
                    SetNextItemWidth(input_width);
                    float temp_value = *v;
                    if (InputFloat(input_label.c_str(), &temp_value, 0.0f, 0.0f, format)) {
                        // Clamp the input value to valid range if requested
                        if (flags & ImGuiSliderFlags_AlwaysClamp) {
                            temp_value = ImClamp(temp_value, v_min, v_max);
                        }
                        *v = temp_value;
                        value_changed = true;
                    }
                    
                    // Tooltip for the input field
                    if (IsItemHovered()) {
                        SetTooltip("Direct keyboard input\nValues outside range allowed unless clamped");
                    }
                }
                
                // Combined tooltip when hovering the slider
                if (IsItemHovered(ImGuiHoveredFlags_RootWindow)) {
                    ShowPrecisionSliderTooltip();
                }
                break;
            }
            
            case PRECISION_SLIDER_MODE_AUTO_SWITCH: {
                // Auto-switch between slider and input based on interaction
                static bool use_input_mode = false;
                
                if (IsKeyPressed(ImGuiKey_Enter) || IsKeyPressed(ImGuiKey_Escape)) {
                    use_input_mode = false;
                }
                
                if (use_input_mode) {
                    // Input mode
                    SetNextItemWidth(available_width);
                    float temp_value = *v;
                    if (InputFloat(label, &temp_value, 0.0f, 0.0f, format, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        if (flags & ImGuiSliderFlags_AlwaysClamp) {
                            temp_value = ImClamp(temp_value, v_min, v_max);
                        }
                        *v = temp_value;
                        value_changed = true;
                        use_input_mode = false; // Return to slider mode after input
                    }
                } else {
                    // Slider mode
                    SetNextItemWidth(available_width);
                    value_changed = SliderFloat(label, v, v_min, v_max, format, flags);
                    
                    // Switch to input mode on double-click or CTRL+Click
                    if (IsItemHovered() && (IsMouseDoubleClicked(0) || (IsMouseClicked(0) && GetIO().KeyCtrl))) {
                        use_input_mode = true;
                    }
                }
                
                // Helpful tooltip
                if (IsItemHovered() && !use_input_mode) {
                    ShowPrecisionSliderTooltip("Double-click or CTRL+Click for keyboard input");
                }
                break;
            }
        }
        
        return value_changed;
    }
    
    /**
     * Enhanced SliderInt with precision input capabilities
     */
    inline bool PrecisionSliderInt(
        const char* label, 
        int* v, 
        int v_min, 
        int v_max, 
        const char* format = "%d", 
        ImGuiSliderFlags flags = 0,
        PrecisionSliderMode mode = PRECISION_SLIDER_MODE_HYBRID,
        bool showInputField = true
    ) {
        bool value_changed = false;
        
        // Generate unique ID for input component
        std::string input_label = std::string("##precision_input_") + label;
        
        // Calculate layout
        float available_width = GetContentRegionAvail().x;
        float input_width = showInputField ? 80.0f : 0.0f;
        float slider_width = available_width - input_width - (showInputField ? 4.0f : 0.0f);
        
        switch (mode) {
            case PRECISION_SLIDER_MODE_SLIDER_ONLY: {
                SetNextItemWidth(available_width);
                value_changed = SliderInt(label, v, v_min, v_max, format, flags);
                
                if (IsItemHovered()) {
                    ShowPrecisionSliderTooltip("CTRL+Click for keyboard input");
                }
                break;
            }
            
            case PRECISION_SLIDER_MODE_HYBRID: {
                // Slider component
                SetNextItemWidth(slider_width);
                if (SliderInt(label, v, v_min, v_max, format, flags)) {
                    value_changed = true;
                }
                
                if (showInputField) {
                    SameLine();
                    
                    // Input field component
                    SetNextItemWidth(input_width);
                    int temp_value = *v;
                    if (InputInt(input_label.c_str(), &temp_value)) {
                        if (flags & ImGuiSliderFlags_AlwaysClamp) {
                            temp_value = ImClamp(temp_value, v_min, v_max);
                        }
                        *v = temp_value;
                        value_changed = true;
                    }
                    
                    // Tooltip handling left to calling code
                }
                
                // Let external code handle tooltips normally via IsItemHovered()
                break;
            }
            
            case PRECISION_SLIDER_MODE_AUTO_SWITCH: {
                static bool use_input_mode = false;
                
                if (IsKeyPressed(ImGuiKey_Enter) || IsKeyPressed(ImGuiKey_Escape)) {
                    use_input_mode = false;
                }
                
                if (use_input_mode) {
                    SetNextItemWidth(available_width);
                    int temp_value = *v;
                    if (InputInt(label, &temp_value, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        if (flags & ImGuiSliderFlags_AlwaysClamp) {
                            temp_value = ImClamp(temp_value, v_min, v_max);
                        }
                        *v = temp_value;
                        value_changed = true;
                        use_input_mode = false;
                    }
                } else {
                    SetNextItemWidth(available_width);
                    value_changed = SliderInt(label, v, v_min, v_max, format, flags);
                    
                    if (IsItemHovered() && (IsMouseDoubleClicked(0) || (IsMouseClicked(0) && GetIO().KeyCtrl))) {
                        use_input_mode = true;
                    }
                }
                
                // Let external code handle tooltips normally via IsItemHovered()
                break;
            }
        }
        
        return value_changed;
    }
    
} // namespace ImGui
