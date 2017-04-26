//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_LOADEXECUTOR_H
#define CUBRID_LOADEXECUTOR_H

#include "STAT_TOOL_CommandExecutor.hpp"

class LoadExecutor : public CommandExecutor
{
  public:
    LoadExecutor (std::string &wholeCommand, std::vector<StatToolSnapshotSet *> &files);
    ErrorManager::ErrorCode parseCommandAndInit();
    ErrorManager::ErrorCode readFileAndInit(StatToolSnapshotSet *set);
    ErrorManager::ErrorCode execute();
    void printUsage();
    ~LoadExecutor();
  private:
    std::string filename, alias;
};

#endif //CUBRID_LOADEXECUTOR_H
