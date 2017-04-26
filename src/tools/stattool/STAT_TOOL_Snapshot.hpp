//
// Created by paul on 30.03.2017.
//

#ifndef CUBRID_STAT_TOOL_SNAPSHOT_H
#define CUBRID_STAT_TOOL_SNAPSHOT_H

#include "STAT_TOOL_Utils.hpp"

extern "C"
{
#include <perf_metadata.h>
}

struct StatToolSnapshotFloat;

struct StatToolSnapshot
{
  UINT64 *rawStats;
  struct tm timestamp, secondTimeStamp;
  bool isDifference;

  StatToolSnapshot ();
  StatToolSnapshot (time_t seconds, time_t seconds2);
  StatToolSnapshot (time_t seconds);
  StatToolSnapshot (const StatToolSnapshot &other);
  StatToolSnapshot *difference (StatToolSnapshot *other);
  StatToolSnapshotFloat *divide (StatToolSnapshot *other);
  virtual void print(FILE *stream, int offset);
  time_t getSeconds ();
  UINT64 getStatValueFromName (const char *stat_name);
  virtual bool isStatZero(int index);
  virtual ~StatToolSnapshot ();
};

struct StatToolSnapshotFloat : public StatToolSnapshot
{
public:
    float *rawStatsFloat;

    StatToolSnapshotFloat() {
      rawStatsFloat = (float *) malloc (sizeof (float) * perfmeta_get_values_count ());
    }
    bool isStatZero(int index) {
      return rawStatsFloat[index] == 0;
    }
    void print(FILE *stream, int offset) {
      fprintf (stream, "%15.4f", rawStatsFloat[offset]);
    }
};

#endif //CUBRID_STAT_TOOL_SNAPSHOT_H
