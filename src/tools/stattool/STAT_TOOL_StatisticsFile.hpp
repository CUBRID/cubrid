//
// Created by paul on 21.03.2017.
//

#ifndef CUBRID_STATISTICSFILE_H
#define CUBRID_STATISTICSFILE_H

#include <vector>
#include <string>
#include <ctime>
#include "STAT_TOOL_Utils.hpp"
#include <iostream>
#include <assert.h>
#include "STAT_TOOL_ErrorManager.hpp"
#include "STAT_TOOL_Snapshot.hpp"

extern "C"
{
#include <stdio.h>
#include <stdlib.h>
#include <system.h>
}

class StatisticsFile
{
  public:
    StatisticsFile (const std::string &filename, const std::string &alias);
    ErrorManager::ErrorCode readFileAndInit ();
    Snapshot *getSnapshotBySeconds (unsigned int seconds);
    Snapshot *getSnapshotByArgument (const char *argument);
    void getIndicesOfSnapshotsByArgument (const char *argument, int &index1, int &index2);
    int getSnapshotIndexBySeconds (unsigned int minutes);

    std::vector < Snapshot * > &getSnapshots ()
    {
      return snapshots;
    }
    struct tm getRelativeTimestamp ()
    {
      return relativeTimestamp;
    }
    time_t getRelativeSeconds ()
    {
      return relativeEpochSeconds;
    }
    std::string &getFilename ()
    {
      return filename;
    }
    std::string &getAlias ()
    {
      return alias;
    }

    ~StatisticsFile ();
  private:
    std::vector < Snapshot * >snapshots;
    struct tm relativeTimestamp;
    std::string filename, alias;
    time_t relativeEpochSeconds;
};


#endif //CUBRID_STATISTICSFILE_H
