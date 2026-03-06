#pragma once

#include <string>
#include <vector>
#include <memory>

// Interface de base pour toute action atomique (Un composant modifié, une entité supprimée, etc.)
class IUndoableAction {
public:
    virtual ~IUndoableAction() = default;
    virtual void Undo() = 0;
    virtual void Redo() = 0;
};

// Une Transaction est un groupe cohérent d'actions
class Transaction {
public:
    explicit Transaction(std::string name) : m_Name(std::move(name)) {}

    void AddAction(std::unique_ptr<IUndoableAction> action) {
        m_Actions.push_back(std::move(action));
    }

    void Undo() {
        // Undo doit toujours se faire dans l'ordre inverse de l'exécution
        for (auto it = m_Actions.rbegin(); it != m_Actions.rend(); ++it) {
            (*it)->Undo();
        }
    }

    void Redo() {
        // Redo se fait dans l'ordre normal
        for (auto& action : m_Actions) {
            action->Redo();
        }
    }

    [[nodiscard]] const std::string& GetName() const { return m_Name; }
    [[nodiscard]] bool IsEmpty() const { return m_Actions.empty(); }

private:
    std::string m_Name;
    std::vector<std::unique_ptr<IUndoableAction>> m_Actions;
};

// Le gestionnaire global statique
class UndoManager {
public:
    static void BeginTransaction(const std::string& name);
    static void EndTransaction();
    static void CancelTransaction();

    // Ajoute une action à la transaction en cours
    static void PushAction(std::unique_ptr<IUndoableAction> action);

    static void Undo();
    static void Redo();
    static void Clear();

    [[nodiscard]] static bool CanUndo();
    [[nodiscard]] static bool CanRedo();

private:
    static std::vector<std::unique_ptr<Transaction>> s_History;
    static int s_CurrentIndex;
    static std::unique_ptr<Transaction> s_ActiveTransaction;
    
    static constexpr int MaxHistorySize = 100;
};