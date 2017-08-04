//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_PLOTEXECUTOR_H
#define CUBRID_PLOTEXECUTOR_H

#include "config.h"

#include <algorithm>

#include "STAT_TOOL_CommandExecutor.hpp"
#include "STAT_TOOL_Utils.hpp"

class PlotExecutor : public CommandExecutor
{
  public:
    static const char *USAGE;

    PlotExecutor (std::string &wholeCommand);
    ErrorManager::ErrorCode parseCommandAndInit();
    ErrorManager::ErrorCode execute();
    void printUsage();
    ~PlotExecutor();
  private:
    bool hasArgument (unsigned int i);

    std::vector<std::pair<int, std::pair<int, int> > > plotData;
    std::vector<std::string> aliases;
    FILE *gnuplotPipe;
    std::string interval, variable, plotFilename, plotCmd;
};

#endif //CUBRID_PLOTEXECUTOR_H
