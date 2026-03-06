#pragma once

#include <functional>
#include <memory>
#include <vector>

// Interface de base pour toute action annulable
class Command {
public:
    virtual ~Command() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
};

// Implémentation générique utilisant des Lambdas (Parfait pour ImGui/ECS)
class ActionCommand : public Command {
public:
    ActionCommand(std::function<void()> undo, std::function<void()> redo)
        : m_Undo(std::move(undo)), m_Redo(std::move(redo)) {}

    void Execute() override { if (m_Redo) m_Redo(); }
    void Undo() override { if (m_Undo) m_Undo(); }

private:
    std::function<void()> m_Undo;
    std::function<void()> m_Redo;
};

// Le gestionnaire global de l'historique
class CommandHistory {
public:
    static void AddCommand(std::unique_ptr<Command> cmd);
    static void Undo();
    static void Redo();
    static void Clear();

private:
    static std::vector<std::unique_ptr<Command>> s_Commands;
    static int s_CommandIndex;
};