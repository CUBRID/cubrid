//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_SHOWEXECUTOR_H
#define CUBRID_SHOWEXECUTOR_H

#include "CommandExecutor.hpp"
#include <algorithm>

#define SHOW_COMPLEX_CMD "-c"

class ShowExecutor : public CommandExecutor
{
  public:
    ShowExecutor (std::string &wholeCommand, std::vector<StatisticsFile *> &files);
    bool parseCommandAndInit();
    bool execute();
    ~ShowExecutor();
  private:
    bool showComplex;
    std::vector<std::string> snapshotsStr;
    std::vector<StatisticsFile::Snapshot *> snapshots;
    int end;
};

#endif //CUBRID_SHOWEXECUTOR_H
