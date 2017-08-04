//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_COMMANDEXECUTOR_H
#define CUBRID_COMMANDEXECUTOR_H

#include "config.h"

#include <string>
#include <vector>
#include <sstream>

#include "perf_metadata.h"
#include "STAT_TOOL_ErrorManager.hpp"

class CommandExecutor
{
  public:
    CommandExecutor (std::string &wholeCommand);
    virtual ErrorManager::ErrorCode parseCommandAndInit() = 0;
    virtual ErrorManager::ErrorCode execute() = 0;
    virtual void printUsage() = 0;
    virtual ~CommandExecutor ();
  protected:
    std::vector<std::string> arguments;
    std::vector<std::string> possibleOptions;
};

#endif //CUBRID_COMMANDEXECUTOR_H
