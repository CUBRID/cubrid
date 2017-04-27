//
// Created by paul on 30.03.2017.
//

#include "STAT_TOOL_Snapshot.hpp"

StatToolSnapshot::StatToolSnapshot () : StatToolStatisticsColumn()
{
}

StatToolSnapshot::StatToolSnapshot (time_t seconds, time_t seconds2) : StatToolStatisticsColumn()
{
  isDifference = true;
  this->timestamp = *localtime (&seconds);
  this->secondTimeStamp = *localtime (&seconds2);

  rawStats = (UINT64 *) malloc (sizeof (UINT64) * perfmeta_get_values_count ());
}

StatToolSnapshot::StatToolSnapshot (time_t seconds) : StatToolStatisticsColumn()
{
  isDifference = false;
  memset (&secondTimeStamp, 0, sizeof (struct tm));

  rawStats = (UINT64 *) malloc (sizeof (UINT64) * perfmeta_get_values_count ());
  this->timestamp = *localtime (&seconds);
}

StatToolSnapshot::StatToolSnapshot (const StatToolSnapshot &other) : StatToolStatisticsColumn()
{
  this->timestamp = other.timestamp;
  perfmeta_copy_stats (rawStats, other.rawStats);
}

StatToolSnapshot *
StatToolSnapshot::difference (StatToolSnapshot *other)
{
  StatToolSnapshot *newSnapshot = new StatToolSnapshot (this->getSeconds (), other->getSeconds ());

  perfmeta_diff_stats (newSnapshot->rawStats, this->rawStats, other->rawStats);

  return newSnapshot;
}

StatToolFauxSnapshotWithFloatColumn *
StatToolSnapshot::divide (StatToolSnapshot *other)
{
  StatToolFauxSnapshotWithFloatColumn *newSnapshot = new StatToolFauxSnapshotWithFloatColumn ();

  for (int i = 0; i < perfmeta_get_values_count (); i++)
    {
      if (other->rawStats[i] == 0)
        {
          newSnapshot->rawStats[i] = 0;
        }
      else
        {
          newSnapshot->rawStats[i] = (this->rawStats[i] / (float)other->rawStats[i]);
        }
    }

  return newSnapshot;
}

time_t
StatToolSnapshot::getSeconds ()
{
  return mktime (&this->timestamp);
}

UINT64
StatToolSnapshot::getStatValueFromName (const char *stat_name)
{
  return perfmeta_get_stat_value_from_name (stat_name, rawStats);
}
