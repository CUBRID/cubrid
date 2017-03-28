//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_COMMANDEXECUTOR_H
#define CUBRID_COMMANDEXECUTOR_H

#include <string>
#include <vector>
#include <sstream>
#include "StatisticsFile.hpp"
#include "../../base/perfmon_base.h"

extern "C" {
#include <error_manager.h>
}

class CommandExecutor
{
  public:
    CommandExecutor (std::string &wholeCommand, std::vector<StatisticsFile *> &files);
    virtual ~CommandExecutor ();
    virtual bool parseCommandAndInit() = 0;
    virtual bool execute() = 0;
  protected:
    std::vector<std::string> arguments;
    std::vector<std::string> possibleOptions;
    std::vector<StatisticsFile *> &files;
};

#endif //CUBRID_COMMANDEXECUTOR_H
