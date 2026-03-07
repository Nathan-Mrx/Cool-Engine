#pragma once
#include <imgui.h>
#include <string>

class UITheme {
public:
    static void Apply() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // --- COULEURS (Inspiré du Dark Theme Godot / Unreal 5) ---
        colors[ImGuiCol_Text]                   = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        
        // Backgrounds principaux
        colors[ImGuiCol_WindowBg]               = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        
        // Bordures
        colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        
        // Fonds des inputs (Inputs, Checkboxes, etc.)
        colors[ImGuiCol_FrameBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        
        // Titres des fenêtres
        colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        
        // Scrollbar
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        
        // Accent Color (Bleu moderne)
        colors[ImGuiCol_CheckMark]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        
        // Boutons
        colors[ImGuiCol_Button]                 = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.24f, 0.52f, 0.88f, 1.00f); // Hover Bleu
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); // Clic Bleu
        
        // Headers (CollapsingHeader, TreeNodes)
        colors[ImGuiCol_Header]                 = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        
        // Tabs (Onglets du Docking)
        colors[ImGuiCol_Tab]                    = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
        
        // Docking
        colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_HeaderActive];
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

        // --- GEOMETRIE (Flat Design, coins légérement arrondis) ---
        style.WindowRounding    = 6.0f;
        style.ChildRounding     = 4.0f;
        style.FrameRounding     = 3.0f;
        style.PopupRounding     = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding      = 3.0f;
        style.TabRounding       = 4.0f;

        style.WindowBorderSize  = 0.0f; // Pas de bordures de fenêtre !
        style.FrameBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;

        style.ItemSpacing       = ImVec2(8.0f, 6.0f);
        style.FramePadding      = ImVec2(6.0f, 4.0f);
        style.WindowPadding     = ImVec2(8.0f, 8.0f);
        style.ScrollbarSize     = 14.0f;
    }

    static void LoadFonts() {
        ImGuiIO& io = ImGui::GetIO();
        
        // Configuration de la police pour qu'elle soit super nette
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = true;

        // Charge la police Inter. ATTENTION au chemin relatif !
        // Change "assets/fonts..." par le vrai chemin vers ton dossier.
        io.Fonts->AddFontFromFileTTF("assets/fonts/Inter.ttf", 16.0f, &fontConfig);
        
        // Optionnel : Tu peux charger d'autres tailles/graisses et les sauvegarder dans une variable
        // ImFont* BoldFont = io.Fonts->AddFontFromFileTTF("assets/fonts/Inter-Bold.ttf", 16.0f, &fontConfig);
    }
};