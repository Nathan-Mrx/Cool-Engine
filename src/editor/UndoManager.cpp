#include "UndoManager.h"

std::vector<std::unique_ptr<Transaction>> UndoManager::s_History;
int UndoManager::s_CurrentIndex = -1;
std::unique_ptr<Transaction> UndoManager::s_ActiveTransaction = nullptr;

void UndoManager::BeginTransaction(const std::string& name) {
    if (s_ActiveTransaction) {
        // Sécurité : si on oublie de fermer une transaction précédente, on la ferme proprement.
        EndTransaction();
    }
    s_ActiveTransaction = std::make_unique<Transaction>(name);
}

void UndoManager::EndTransaction() {
    if (!s_ActiveTransaction || s_ActiveTransaction->IsEmpty()) {
        s_ActiveTransaction = nullptr; // On ignore les transactions vides
        return; 
    }

    // Si on a annulé des actions puis qu'on refait une nouvelle action, on efface le "futur"
    if (s_CurrentIndex < static_cast<int>(s_History.size()) - 1) {
        s_History.erase(s_History.begin() + s_CurrentIndex + 1, s_History.end());
    }

    s_History.push_back(std::move(s_ActiveTransaction));
    s_CurrentIndex++;

    // Limitation stricte de la mémoire
    if (s_History.size() > MaxHistorySize) {
        s_History.erase(s_History.begin());
        s_CurrentIndex--;
    }

    s_ActiveTransaction = nullptr;
}

void UndoManager::CancelTransaction() {
    // Détruit la transaction en cours sans l'appliquer à l'historique
    s_ActiveTransaction = nullptr; 
}

void UndoManager::PushAction(std::unique_ptr<IUndoableAction> action) {
    if (s_ActiveTransaction) {
        s_ActiveTransaction->AddAction(std::move(action));
    }
}

void UndoManager::Undo() {
    if (s_ActiveTransaction) {
        // Si on fait Ctrl+Z au milieu d'un "drag" de souris, on annule l'action en cours
        CancelTransaction();
    }

    if (CanUndo()) {
        s_History[s_CurrentIndex]->Undo();
        s_CurrentIndex--;
    }
}

void UndoManager::Redo() {
    if (s_ActiveTransaction) {
        CancelTransaction();
    }

    if (CanRedo()) {
        s_CurrentIndex++;
        s_History[s_CurrentIndex]->Redo();
    }
}

void UndoManager::Clear() {
    s_History.clear();
    s_CurrentIndex = -1;
    s_ActiveTransaction = nullptr;
}

bool UndoManager::CanUndo() {
    return s_CurrentIndex >= 0;
}

bool UndoManager::CanRedo() {
    return s_CurrentIndex < static_cast<int>(s_History.size()) - 1;
}