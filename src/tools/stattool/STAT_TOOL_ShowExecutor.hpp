//
// Created by paul on 28.03.2017.
//

#ifndef CUBRID_SHOWEXECUTOR_H
#define CUBRID_SHOWEXECUTOR_H

#include "config.h"

#include <algorithm>

#include "STAT_TOOL_ErrorManager.hpp"
#include "STAT_TOOL_CommandExecutor.hpp"
#include "STAT_TOOL_SnapshotSet.hpp"

class CommandExecutor;
class StatToolColumnInterface;

#define SHOW_COMPLEX_CMD "-c"
#define SHOW_ZEROES_CMD "-z"

class ShowExecutor : public CommandExecutor
{
  public:
    static const char *USAGE;

    ShowExecutor (std::string &wholeCommand);
    ErrorManager::ErrorCode parseCommandAndInit();
    ErrorManager::ErrorCode execute();
    void printUsage();
    ~ShowExecutor();
  private:
    bool showComplex, showZeroes;
    std::vector<std::string> validSnapshots;
    std::vector<StatToolColumnInterface *> snapshots;

    void customDumpStatsInTableForm (const std::vector<StatToolColumnInterface *> &snapshots, FILE *stream,
                                     int show_complex,
                                     int show_zero);
    void printTimerToFileInTableForm (FILE *stream, int stat_index,
                                      const std::vector<StatToolColumnInterface *> &snapshots,
                                      int show_zero, int show_header);
    void statDumpInFileInTableForm (FILE *stream, PSTAT_METADATA *stat,
                                    const std::vector<StatToolColumnInterface *> &snapshots,
                                    int show_zeroes);
};

#endif //CUBRID_SHOWEXECUTOR_H
