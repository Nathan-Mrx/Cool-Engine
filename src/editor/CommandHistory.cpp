#include "CommandHistory.h"

std::vector<std::unique_ptr<Command>> CommandHistory::s_Commands;
int CommandHistory::s_CommandIndex = -1;

void CommandHistory::AddCommand(std::unique_ptr<Command> cmd) {
    // Si on fait une nouvelle action alors qu'on avait annulé des choses,
    // on efface le "futur" de l'historique (comme dans Word ou Photoshop)
    if (s_CommandIndex < (int)s_Commands.size() - 1) {
        s_Commands.erase(s_Commands.begin() + s_CommandIndex + 1, s_Commands.end());
    }
    
    s_Commands.push_back(std::move(cmd));
    s_CommandIndex++;

    // On limite l'historique à 100 actions pour ne pas exploser la RAM
    if (s_Commands.size() > 100) {
        s_Commands.erase(s_Commands.begin());
        s_CommandIndex--;
    }
}

void CommandHistory::Undo() {
    if (s_CommandIndex >= 0) {
        s_Commands[s_CommandIndex]->Undo();
        s_CommandIndex--;
    }
}

void CommandHistory::Redo() {
    if (s_CommandIndex < (int)s_Commands.size() - 1) {
        s_CommandIndex++;
        s_Commands[s_CommandIndex]->Execute();
    }
}

void CommandHistory::Clear() {
    s_Commands.clear();
    s_CommandIndex = -1;
}