//
// Created by paul on 30.03.2017.
//

#include "STAT_TOOL_Snapshot.hpp"

Snapshot::Snapshot (time_t seconds, time_t seconds2)
{
  isDifference = true;
  this->timestamp = *localtime (&seconds);
  this->secondTimeStamp = *localtime (&seconds2);

  rawStats = (UINT64 *) malloc (sizeof (UINT64) * perfmeta_get_Stat_count());
}

Snapshot::Snapshot (time_t seconds)
{
  isDifference = false;
  memset (&secondTimeStamp, 0, sizeof (struct tm));

  rawStats = (UINT64 *) malloc (sizeof (UINT64) * perfmeta_get_Stat_count());
  this->timestamp = *localtime (&seconds);
}

Snapshot::Snapshot (const Snapshot &other)
{
  this->timestamp = other.timestamp;
  perfmeta_copy_stats (rawStats, other.rawStats);
}

Snapshot *
Snapshot::difference (Snapshot *other)
{
  Snapshot *newSnapshot = new Snapshot (this->getSeconds (), other->getSeconds ());

  for (int i = 0; i < perfmeta_get_Stat_count(); i++)
    {
      newSnapshot->rawStats[i] = this->rawStats[i] - other->rawStats[i];
    }

  return newSnapshot;
}

Snapshot *
Snapshot::divide (Snapshot *other)
{
  Snapshot *newSnapshot = new Snapshot (this->getSeconds (), other->getSeconds ());

  for (int i = 0; i < perfmeta_get_Stat_count(); i++)
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

time_t
Snapshot::getSeconds ()
{
  return mktime (&this->timestamp);
}

UINT64
Snapshot::getStatValueFromName (const char *stat_name)
{
  return perfmeta_get_stat_value_from_name (stat_name, rawStats);
}

Snapshot::~Snapshot ()
{
  free (rawStats);
}
