//
// Created by paul on 21.03.2017.
//

#ifndef CUBRID_UTILS_H
#define CUBRID_UTILS_H

#include "config.h"

#include <vector>
#include <typeinfo>

#include "perf_metadata.h"
#include "STAT_TOOL_Snapshot.hpp"
#include "STAT_TOOL_SnapshotSet.hpp"
#include "STAT_TOOL_CommandExecutor.hpp"
#include "STAT_TOOL_ShowExecutor.hpp"
#include "STAT_TOOL_PlotExecutor.hpp"
#include "STAT_TOOL_LoadExecutor.hpp"
#include "STAT_TOOL_AggregateExecutor.hpp"

#define MAX_COMMAND_SIZE 128
#define MAX_FILE_NAME_SIZE 128
#define COMMAND_ARGUMENT "-c"

class StatToolSnapshotSet;
class StatToolColumnInterface;

class Utils
{
  public:
    static void init ();
    static void final ();
    static StatToolSnapshot *findSnapshotInLoadedSets (const char *alias);
    static void printHelp ();
    static ErrorManager::ErrorCode processCommand (const std::string &command, CommandExecutor *&executor, bool &quit);
    static ErrorManager::ErrorCode processArguments (char **argv, int argc, bool &quit);

    static std::vector<StatToolSnapshotSet *> loadedSets;
    static const std::vector<std::pair<std::string, std::string> > possibleArguments;
  private:
    static std::vector<std::pair<std::string, std::string> > initPossibleArguments();
};


#endif //CUBRID_UTILS_H
