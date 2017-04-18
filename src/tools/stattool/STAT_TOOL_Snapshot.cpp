//
// Created by paul on 30.03.2017.
//

#include "STAT_TOOL_Snapshot.hpp"

Snapshot::Snapshot (time_t seconds, time_t seconds2)
{
  isDifference = true;
  this->timestamp = *localtime (&seconds);
  this->secondTimeStamp = *localtime (&seconds2);

  rawStats = (UINT64 *) malloc (sizeof (UINT64) * Utils::getNStatValues ());
}

Snapshot::Snapshot (time_t seconds)
{
  isDifference = false;
  memset (&secondTimeStamp, 0, sizeof (struct tm));

  rawStats = (UINT64 *) malloc (sizeof (UINT64) * Utils::getNStatValues ());
  this->timestamp = *localtime (&seconds);
}

Snapshot::Snapshot (const Snapshot &other)
{
  this->timestamp = other.timestamp;
  for (int i = 0; i < Utils::getNStatValues (); i++)
    {
      rawStats[i] = other.rawStats[i];
    }
}

Snapshot *
Snapshot::difference (Snapshot *other)
{
  Snapshot *newSnapshot = new Snapshot (this->getSeconds (), other->getSeconds ());

  for (int i = 0; i < Utils::getNStatValues (); i++)
    {
      newSnapshot->rawStats[i] = this->rawStats[i] - other->rawStats[i];
    }

  return newSnapshot;
}

Snapshot *
Snapshot::divide (Snapshot *other)
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

time_t
Snapshot::getSeconds ()
{
  return mktime (&this->timestamp);
}

UINT64
Snapshot::getStatusValueFromName (const char *stat_name)
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

void
Snapshot::print (FILE *stream)
{
  int i;
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

Snapshot::~Snapshot ()
{
  free (rawStats);
}
