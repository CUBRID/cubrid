//
// Created by paul on 21.03.2017.
//

#ifndef CUBRID_STATISTICSFILE_H
#define CUBRID_STATISTICSFILE_H

#include <vector>
#include <string>
#include <ctime>
#include "Utils.hpp"
#include <iostream>
#include <assert.h>
#include "ErrorManager.hpp"

extern "C"
{
#include <stdio.h>
#include <stdlib.h>
#include <system.h>
#include <perfmon_base.h>
}
class StatisticsFile
{
  public:
    struct Snapshot
    {
      UINT64 *rawStats;
      struct tm timestamp, secondTimeStamp;
      bool isDifference;

      Snapshot (time_t seconds, time_t seconds2)
      {
        isDifference = true;
        this->timestamp = *localtime (&seconds);
        this->secondTimeStamp = *localtime (&seconds2);

        rawStats = (UINT64 *) malloc (sizeof (UINT64) * Utils::getNStatValues ());
      }

      Snapshot (time_t seconds)
      {
        isDifference = false;
        memset (&secondTimeStamp, 0, sizeof (struct tm));

        rawStats = (UINT64 *) malloc (sizeof (UINT64) * Utils::getNStatValues ());
        this->timestamp = *localtime (&seconds);
      }

      Snapshot (const Snapshot &other)
      {
        this->timestamp = other.timestamp;
        for (int i = 0; i < Utils::getNStatValues (); i++)
          {
            rawStats[i] = other.rawStats[i];
          }
      }

      Snapshot *difference (Snapshot *other)
      {
        Snapshot *newSnapshot = new Snapshot (this->getSeconds (), other->getSeconds ());

        for (int i = 0; i < Utils::getNStatValues (); i++)
          {
            newSnapshot->rawStats[i] = this->rawStats[i] - other->rawStats[i];
          }

        return newSnapshot;
      }

      Snapshot *divide (Snapshot *other)
      {
        Snapshot *newSnapshot = new Snapshot (this->getSeconds (), other->getSeconds ());

        for (int i = 0; i < Utils::getNStatValues (); i++)
          {
            if (other->rawStats[i] == 0)
              {
                newSnapshot->rawStats[i] = 0;
              }
            else
              {
                newSnapshot->rawStats[i] = (UINT64) (100.0f * (float)this->rawStats[i] / (float)other->rawStats[i]);
              }
          }

        return newSnapshot;
      }

      time_t getSeconds ()
      {
        return mktime (&this->timestamp);
      }

      UINT64 getStatusValueFromName (const char *stat_name)
      {
        int i;

        for (i = 0; i < total_num_stat_vals; i++)
          {
            if (strcmp (pstat_Nameoffset[i].name, stat_name) == 0)
              {
                return rawStats[i];
              }
          }
        return 0;
      }

      void print (FILE *stream)
      {
        int i;
        const char *s;
        UINT64 *stats_ptr = this->rawStats;
        char strTime[80];

        if (isDifference)
          {
            strftime (strTime, 80, "%a %B %d %H:%M:%S %Y", &timestamp);
            printf ("First timestamp = %s\n", strTime);
            strftime (strTime, 80, "%a %B %d %H:%M:%S %Y", &secondTimeStamp);
            printf ("Second timestamp = %s\n", strTime);
          }
        else
          {
            strftime (strTime, 80, "%a %B %d %H:%M:%S %Y", &timestamp);
            printf ("Timestamp = %s\n", strTime);
          }

        for (i = 0; i < total_num_stat_vals; i++)
          {
            if (stats_ptr[i] != 0)
              {
                fprintf (stream, "%s = %lld\n", pstat_Nameoffset[i].name, (long long)stats_ptr[i]);
              }
          }
      }

      ~Snapshot ()
      {
        free (rawStats);
      }
    };

    StatisticsFile (const std::string &filename, const std::string &alias);
    ErrorManager::ErrorCode readFileAndInit ();
    Snapshot *getSnapshotBySeconds (unsigned int minutes);
    Snapshot *getSnapshotByArgument (const char *argument);
    void getIndicesOfSnapshotsByArgument (const char *argument, int &minutes1, int &minutes2);
    int getSnapshotIndexBySeconds (unsigned int minutes);
    static void printInTableForm (Snapshot *s1, Snapshot *s2, FILE *stream);

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
