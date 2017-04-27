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

struct StatToolFauxSnapshotWithFloatColumn;
struct StatToolColumnInterface;

struct StatToolColumnInterface
{
    virtual void printColumnValue (FILE * stream, int offset) = 0;
    virtual bool isStatZero (int index) = 0;
    virtual void printColumnValueForComputedRatio (FILE * stream, int offset) = 0;
};

template <class T>
struct StatToolStatisticsColumn : public StatToolColumnInterface
{
    T *rawStats;

    StatToolStatisticsColumn() {
      rawStats = (T *) malloc(sizeof(T) * perfmeta_get_values_count());
    }
    bool isStatZero (int index) {
      return rawStats[index] == 0;
    }
    virtual void printColumnValueForComputedRatio (FILE * stream, int offset) {
      fprintf (stream, "%15.4f", rawStats[offset] / 100.0f);
    }
    virtual void printColumnValue (FILE * stream, int offset) = 0;
    virtual ~StatToolStatisticsColumn ()
    {
      delete rawStats;
    }
};

struct StatToolSnapshot : public StatToolStatisticsColumn<UINT64>
{
  struct tm timestamp, secondTimeStamp;
  bool isDifference;

  StatToolSnapshot ();
  StatToolSnapshot (time_t seconds, time_t seconds2);
  StatToolSnapshot (time_t seconds);
  StatToolSnapshot (const StatToolSnapshot &other);
  StatToolSnapshot *difference (StatToolSnapshot *other);
  StatToolFauxSnapshotWithFloatColumn *divide (StatToolSnapshot *other);
  void printColumnValue (FILE *stream, int offset) {
    fprintf (stream, "%15lld", (long long) rawStats[offset]);
  }
  time_t getSeconds ();
  UINT64 getStatValueFromName (const char *stat_name);
};

struct StatToolFauxSnapshotWithFloatColumn : public StatToolStatisticsColumn<float>
{
  public:
    StatToolFauxSnapshotWithFloatColumn() : StatToolStatisticsColumn()
    {
    }
    void printColumnValue (FILE * stream, int offset)
    {
      fprintf (stream, "%15.4f", rawStats[offset]);
    }
    void printColumnValueForComputedRatio (FILE * stream, int offset) {
      printColumnValue (stream, offset);
    }
};

#endif //CUBRID_STAT_TOOL_SNAPSHOT_H
