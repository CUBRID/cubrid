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

struct Snapshot
{
  UINT64 *rawStats;
  struct tm timestamp, secondTimeStamp;
  bool isDifference;

  Snapshot (time_t seconds, time_t seconds2);
  Snapshot (time_t seconds);
  Snapshot (const Snapshot &other);
  Snapshot *difference (Snapshot *other);
  Snapshot *divide (Snapshot *other);
  time_t getSeconds ();
  UINT64 getStatValueFromName (const char *stat_name);
  ~Snapshot ();
};

#endif //CUBRID_STAT_TOOL_SNAPSHOT_H
