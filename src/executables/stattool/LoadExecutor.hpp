//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_LOADEXECUTOR_H
#define CUBRID_LOADEXECUTOR_H

#include "CommandExecutor.hpp"

class LoadExecutor : public CommandExecutor
{
  public:
    LoadExecutor (std::string &wholeCommand, std::vector<StatisticsFile *> &files);
    bool parseCommandAndInit();
    bool execute();
    ~LoadExecutor();
  private:
    std::string filename, alias;
};

#endif //CUBRID_LOADEXECUTOR_H
