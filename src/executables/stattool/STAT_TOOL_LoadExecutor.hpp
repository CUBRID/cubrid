//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_LOADEXECUTOR_H
#define CUBRID_LOADEXECUTOR_H

#include "STAT_TOOL_CommandExecutor.hpp"

class LoadExecutor : public CommandExecutor
{
  public:
    LoadExecutor (std::string &wholeCommand, std::vector<StatisticsFile *> &files);
    ErrorManager::ErrorCode parseCommandAndInit();
    ErrorManager::ErrorCode execute();
    void printUsage();
    ~LoadExecutor();
  private:
    std::string filename, alias;
};

#endif //CUBRID_LOADEXECUTOR_H
