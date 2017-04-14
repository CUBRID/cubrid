//
// Created by paul on 04.04.2017.
//

#ifndef CUBRID_STAT_TOOL_AGGREGATEEXECUTOR_H
#define CUBRID_STAT_TOOL_AGGREGATEEXECUTOR_H

#include "STAT_TOOL_CommandExecutor.hpp"
#include <vector>
#include <sstream>

extern "C" {
#include <stdlib.h>
}

#define NAME_CMD "-n"
#define DIM_CMD "-d"
#define INDEX_CMD "-i"
#define ALIAS_CMD "-a"
#define FILENAME_CMD "-f"
#define DEFAULT_PLOT_FILENAME "aggregate_plot"

class AggregateExecutor : public CommandExecutor
{
  public:
    AggregateExecutor (std::string &wholeCommand, std::vector<StatisticsFile *> &files);
    ErrorManager::ErrorCode parseCommandAndInit ();
    ErrorManager::ErrorCode execute ();
    void printUsage ();
    ~AggregateExecutor ();
  private:
    int fixedDimension, fixedIndex, statIndex;
    std::string statName, aggregateName, plotFilename;
    StatisticsFile *file;
    FILE *gnuplotPipe;
};
#endif //CUBRID_STAT_TOOL_AGGREGATEEXECUTOR_H
