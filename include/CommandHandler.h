#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <string>

class CommandHandler{
    public:
        CommandHandler();

        std::string processCommand(const std::string& commandLine);
    
};

#endif