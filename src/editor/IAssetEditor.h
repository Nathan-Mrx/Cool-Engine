#pragma once
#include <imgui.h>
#include <functional>
#include <filesystem>
#include <memory>

class IAssetEditor {
public:
    virtual ~IAssetEditor() = default;

    // --- INJECTIONS CONTEXTUELLES ---
    // Ces fonctions sont appelées par l'EditorLayer quand le Menu est ouvert
    virtual void OnImGuiMenuFile() {}
    virtual void OnImGuiMenuEdit() {}
    virtual void OnImGuiMenuView() {}

    // --- RENDU PRINCIPAL ---
    virtual void OnImGuiRender(bool& isOpen) = 0;

    // --- COMMANDES GLOBALES ---
    virtual void Save() {}
    virtual void SaveAs() {}

    std::function<void(const std::filesystem::path&)> OnPathChangedCallback;
};