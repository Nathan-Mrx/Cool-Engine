#pragma once
#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h> // La librairie magique !

namespace ed = ax::NodeEditor;

class MaterialEditorPanel {
public:
    MaterialEditorPanel();
    ~MaterialEditorPanel();

    void OnImGuiRender(bool& isOpen);

private:
    ed::EditorContext* m_Context = nullptr;
};