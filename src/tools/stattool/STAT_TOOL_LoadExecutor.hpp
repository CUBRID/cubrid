//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_LOADEXECUTOR_H
#define CUBRID_LOADEXECUTOR_H

#include "config.h"

#include "STAT_TOOL_CommandExecutor.hpp"
#include "STAT_TOOL_SnapshotSet.hpp"

class StatToolSnapshotSet;

class LoadExecutor : public CommandExecutor
{
  public:
    static const char *USAGE;

    LoadExecutor (std::string &wholeCommand);
    ErrorManager::ErrorCode parseCommandAndInit();
    ErrorManager::ErrorCode readFileAndInit (StatToolSnapshotSet *set);
    ErrorManager::ErrorCode execute();
    void printUsage();
    ~LoadExecutor();
  private:
    std::string filename, alias;
};

#endif //CUBRID_LOADEXECUTOR_H
