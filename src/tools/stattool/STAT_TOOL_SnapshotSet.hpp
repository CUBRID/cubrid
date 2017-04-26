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

class StatToolSnapshot;

class StatToolSnapshotSet
{
  public:
    StatToolSnapshotSet (const std::string &filename, const std::string &alias);
    StatToolSnapshot *getSnapshotBySeconds (unsigned int seconds);
    StatToolSnapshot *getSnapshotByArgument (const char *argument);
    void getIndicesOfSnapshotsByArgument (const char *argument, int &index1, int &index2);
    int getSnapshotIndexBySeconds (unsigned int minutes);

    std::vector < StatToolSnapshot * > &getSnapshots ()
    {
      return snapshots;
    }
    struct tm getRelativeTimestamp ()
    {
      return relativeTimestamp;
    }

    void setRelativeTimestamp (struct tm timestamp) {
      relativeTimestamp = timestamp;
    }

    time_t getRelativeSeconds ()
    {
      return relativeEpochSeconds;
    }

    void setRelativeSeconds (time_t seconds) {
      relativeEpochSeconds = seconds;
    }

    std::string &getFilename ()
    {
      return filename;
    }
    std::string &getAlias ()
    {
      return alias;
    }

    ~StatToolSnapshotSet ();
  private:
    std::vector < StatToolSnapshot * >snapshots;
    struct tm relativeTimestamp;
    std::string filename, alias;
    time_t relativeEpochSeconds;
};


#endif //CUBRID_STATISTICSFILE_H
