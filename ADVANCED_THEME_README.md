# 🎨 SDR++ CE Advanced Theme

## Overview

The **Advanced Theme** brings a modern, professional interface to SDR++ Community Edition, inspired by contemporary application design trends. It features sleek dark aesthetics, smooth rounded corners, enhanced visual hierarchy, and custom UI components.

## 🌟 Features

### Visual Enhancements
- **Modern Color Palette**: Professional blue accent colors (#007ACC) with refined dark backgrounds
- **Smooth Rounded Corners**: Enhanced corner radius (8px windows, 6px frames, 12px scrollbars)
- **Refined Spacing**: Increased padding and margins for better visual breathing room
- **Subtle Gradients**: Modern progress bars and visual elements with gradient effects
- **Clean Typography**: Centered window titles and button text for professional appearance

### Custom UI Components

#### 1. ModernButton
```cpp
// Primary action button with enhanced styling
ImGui::ModernButton("Connect", ImVec2(120, 0), true);

// Secondary button
ImGui::ModernButton("Cancel", ImVec2(120, 0), false);
```

#### 2. ModernToggle
```cpp
// Modern toggle switch (replaces traditional checkboxes)
static bool enabled = false;
ImGui::ModernToggle("Enable Feature", &enabled);
```

#### 3. ModernCard
```cpp
// Card container with subtle shadow effects
if (ImGui::BeginModernCard("Settings")) {
    // Card content here
    ImGui::EndModernCard();
}
```

#### 4. ModernProgressBar
```cpp
// Gradient progress bar
ImGui::ModernProgressBar(0.75f, ImVec2(-1, 20), "75%");
```

#### 5. ModernSectionHeader
```cpp
// Enhanced section headers with separator lines
ImGui::ModernSectionHeader("Audio Settings");
```

#### 6. ModernTooltip
```cpp
// Enhanced tooltips with better styling
ImGui::ModernTooltip("This is a helpful tooltip with modern styling");
```

## 🚀 Installation

The Advanced theme is automatically included with SDR++ CE. To activate it:

1. Open SDR++ CE
2. Go to **Settings** → **Display** → **Theme**
3. Select **"Advanced"** from the dropdown
4. The interface will immediately update with the new styling

## 🎯 Theme Configuration

### Color Scheme
The Advanced theme uses a carefully crafted color palette:

- **Primary**: `#007ACC` (Microsoft Blue)
- **Primary Hover**: `#1BA1F2` (Lighter Blue)
- **Primary Active**: `#005A9E` (Darker Blue)
- **Background**: `#1E1E1E` (Dark Gray)
- **Surface**: `#2D2D30` (Medium Gray)
- **Border**: `#3A3A3C` (Light Gray)
- **Text**: `#FFFFFF` (White)
- **Text Disabled**: `#808080` (Gray)

### Enhanced Styling Properties
- **Window Rounding**: 8.0px
- **Frame Rounding**: 5.0px
- **Scrollbar Rounding**: 12.0px
- **Window Padding**: 15px
- **Frame Padding**: 10px × 6px
- **Item Spacing**: 10px × 8px

## 🔧 Customization

### Creating Custom Themes
You can create your own theme based on the Advanced theme:

1. Copy `root/res/themes/advanced.json` to a new file
2. Modify the color values (use RGBA hex format: `#RRGGBBAA`)
3. Save in the themes directory
4. Restart SDR++ CE to see your new theme

### Extending Modern Components
The modern UI components are defined in `core/src/gui/widgets/advanced_widgets.h`. You can:

- Add new custom components
- Modify existing component styling
- Create theme-specific variations

## 🎨 Design Philosophy

The Advanced theme follows these design principles:

### 1. **Visual Hierarchy**
- Clear distinction between primary and secondary actions
- Consistent spacing and alignment
- Logical information grouping with cards and sections

### 2. **Modern Aesthetics**
- Flat design with subtle depth through shadows
- Consistent rounded corners throughout
- Professional color palette with good contrast ratios

### 3. **Enhanced Usability**
- Larger touch targets for better interaction
- Clear visual feedback for interactive elements
- Intuitive toggle switches and modern controls

### 4. **Professional Appearance**
- Clean, uncluttered interface
- Consistent styling across all components
- Attention to detail in spacing and alignment

## 📊 Comparison with Default Theme

| Feature | Default Theme | Advanced Theme |
|---------|---------------|----------------|
| **Corner Radius** | 4-6px | 5-8px |
| **Padding** | Standard | Enhanced (+25%) |
| **Color Palette** | Basic Dark | Professional Blue |
| **Typography** | Left-aligned | Centered titles |
| **Components** | Standard ImGui | Custom modern widgets |
| **Visual Depth** | Flat | Subtle shadows/gradients |

## 🔮 Future Enhancements

Planned improvements for the Advanced theme:

- **Smooth Animations**: Fade transitions and micro-interactions
- **Theme Variants**: Light mode and additional color schemes  
- **Component Library**: Expanded set of modern UI components
- **Accessibility**: Enhanced contrast ratios and keyboard navigation
- **Customization Panel**: In-app theme editor with real-time preview

## 💡 Usage Tips

1. **Best Practices**: Use `ModernButton` for primary actions, standard buttons for secondary actions
2. **Card Layout**: Group related settings using `ModernCard` containers
3. **Progressive Disclosure**: Use `ModernSectionHeader` to organize complex interfaces
4. **Visual Feedback**: Leverage `ModernToggle` for boolean settings instead of checkboxes
5. **Information Hierarchy**: Use modern tooltips to provide contextual help without cluttering the interface

## 🐛 Known Issues

- Some third-party modules may not fully support the enhanced styling
- Animation system is not yet implemented (static styling only)
- Theme switching requires application restart for full effect

## 📝 Contributing

To contribute to the Advanced theme:

1. Test your changes with different screen resolutions
2. Ensure accessibility guidelines are followed
3. Maintain consistency with the existing design language
4. Document any new components or modifications

---

**The Advanced Theme transforms SDR++ CE into a modern, professional-grade application while maintaining all the powerful functionality you expect.**
