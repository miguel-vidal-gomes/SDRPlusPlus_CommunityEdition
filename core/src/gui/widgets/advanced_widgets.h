#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <cmath>

namespace ImGui {
    
    /**
     * Advanced Theme Modern Button with subtle animations and better styling
     */
    inline bool ModernButton(const char* label, const ImVec2& size = ImVec2(0, 0), bool primary = false) {
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        
        // Get current theme colors
        ImVec4 button_color = primary ? 
            ImVec4(0.0f, 0.48f, 0.8f, 1.0f) :  // Primary blue
            style.Colors[ImGuiCol_Button];
        
        ImVec4 button_hovered = primary ? 
            ImVec4(0.11f, 0.63f, 0.95f, 1.0f) : // Lighter blue
            style.Colors[ImGuiCol_ButtonHovered];
        
        ImVec4 button_active = primary ?
            ImVec4(0.0f, 0.38f, 0.65f, 1.0f) :  // Darker blue
            style.Colors[ImGuiCol_ButtonActive];
        
        // Apply enhanced colors
        PushStyleColor(ImGuiCol_Button, button_color);
        PushStyleColor(ImGuiCol_ButtonHovered, button_hovered);
        PushStyleColor(ImGuiCol_ButtonActive, button_active);
        
        // Enhanced rounding for modern look
        PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
        
        bool result = Button(label, size);
        
        PopStyleVar(2);
        PopStyleColor(3);
        
        return result;
    }
    
    /**
     * Modern Card Container with subtle shadow effect
     */
    inline bool BeginModernCard(const char* label, bool* p_open = nullptr, ImGuiWindowFlags flags = 0) {
        // Enhanced styling for cards
        PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
        PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        
        // Subtle background color for depth
        ImVec4 bg_color = GetStyleColorVec4(ImGuiCol_WindowBg);
        bg_color.w = 0.95f; // Slightly transparent
        PushStyleColor(ImGuiCol_WindowBg, bg_color);
        
        // Enhanced child window with modern styling
        flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        
        bool result = BeginChild(label, ImVec2(0, 0), true, flags);
        
        return result;
    }
    
    inline void EndModernCard() {
        EndChild();
        PopStyleColor(1);
        PopStyleVar(3);
    }
    
    /**
     * Modern Toggle Switch (enhanced checkbox)
     */
    inline bool ModernToggle(const char* label, bool* v, const ImVec2& size = ImVec2(50, 25)) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return false;
        
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, nullptr, true);
        
        const float height = size.y;
        const float width = size.x;
        const float radius = height * 0.5f;
        
        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size_vec = ImVec2(width + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), height);
        const ImRect total_bb(pos, ImVec2(pos.x + size_vec.x, pos.y + size_vec.y));
        
        ItemSize(total_bb, style.FramePadding.y);
        if (!ItemAdd(total_bb, id)) return false;
        
        bool hovered, held;
        ImVec2 toggle_size = ImVec2(width, height);
        bool pressed = ButtonBehavior(ImRect(pos, ImVec2(pos.x + toggle_size.x, pos.y + toggle_size.y)), id, &hovered, &held);
        if (pressed) *v = !*v;
        
        // Animation value (you could store this per-widget for smooth animation)
        float t = *v ? 1.0f : 0.0f;
        
        // Colors
        ImU32 bg_col = *v ? 
            GetColorU32(ImVec4(0.0f, 0.48f, 0.8f, 1.0f)) :  // Active blue
            GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f));     // Inactive gray
            
        if (hovered) {
            ImVec4 col = ColorConvertU32ToFloat4(bg_col);
            col.x += 0.1f; col.y += 0.1f; col.z += 0.1f;
            bg_col = ColorConvertFloat4ToU32(col);
        }
        
        ImDrawList* draw_list = window->DrawList;
        
        // Background track
        draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg_col, radius);
        
        // Knob
        float knob_pos = pos.x + radius + t * (width - radius * 2.0f);
        ImU32 knob_col = GetColorU32(ImGuiCol_Text);
        draw_list->AddCircleFilled(ImVec2(knob_pos, pos.y + radius), radius - 2.0f, knob_col);
        
        // Label
        if (label_size.x > 0.0f) {
            RenderText(ImVec2(pos.x + width + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);
        }
        
        return pressed;
    }
    
    /**
     * Modern Progress Bar with gradient
     */
    inline void ModernProgressBar(float fraction, const ImVec2& size_arg = ImVec2(-1, 0), const char* overlay = nullptr) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return;
        
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        
        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), g.FontSize + style.FramePadding.y * 2.0f);
        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        
        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, 0)) return;
        
        // Clamp fraction
        fraction = ImSaturate(fraction);
        
        // Background
        ImU32 bg_col = GetColorU32(ImGuiCol_FrameBg);
        window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, 4.0f);
        
        // Progress bar with gradient
        if (fraction > 0.0f) {
            ImVec2 fill_br = ImVec2(ImLerp(bb.Min.x, bb.Max.x, fraction), bb.Max.y);
            
            // Gradient colors
            ImU32 col_start = GetColorU32(ImVec4(0.0f, 0.6f, 1.0f, 1.0f));  // Light blue
            ImU32 col_end = GetColorU32(ImVec4(0.0f, 0.4f, 0.8f, 1.0f));    // Darker blue
            
            window->DrawList->AddRectFilledMultiColor(bb.Min, fill_br, col_start, col_end, col_end, col_start);
        }
        
        // Overlay text
        if (overlay) {
            ImVec2 overlay_size = CalcTextSize(overlay);
            ImVec2 overlay_pos = ImVec2(bb.Min.x + (size.x - overlay_size.x) * 0.5f, bb.Min.y + (size.y - overlay_size.y) * 0.5f);
            RenderTextClipped(bb.Min, bb.Max, overlay, nullptr, &overlay_size, ImVec2(0.5f, 0.5f), &bb);
        }
    }
    
    /**
     * Modern Section Header with separator line
     */
    inline void ModernSectionHeader(const char* label) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return;
        
        const ImGuiStyle& style = GetStyle();
        const ImVec2 label_size = CalcTextSize(label);
        
        // Add some spacing before header
        Spacing();
        
        // Header text with enhanced styling
        PushFont(nullptr); // Use default font but we could use a bold variant
        PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
        
        Text("%s", label);
        
        PopStyleColor();
        PopFont();
        
        // Separator line
        ImVec2 pos = GetCursorScreenPos();
        float width = GetContentRegionAvail().x;
        
        ImDrawList* draw_list = window->DrawList;
        ImU32 col = GetColorU32(ImGuiCol_Separator);
        draw_list->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + width, pos.y), col, 1.0f);
        
        // Add spacing after header
        Spacing();
    }
    
    /**
     * Modern Tooltip with better styling
     */
    inline void ModernTooltip(const char* text) {
        if (IsItemHovered()) {
            PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
            PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.1f, 0.1f, 0.1f, 0.95f));
            
            SetTooltip("%s", text);
            
            PopStyleColor();
            PopStyleVar(2);
        }
    }
    
} // namespace ImGui
