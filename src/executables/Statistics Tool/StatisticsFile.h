//
// Created by paul on 21.03.2017.
//

#ifndef CUBRID_STATISTICSFILE_H
#define CUBRID_STATISTICSFILE_H

#include <vector>
#include <string>
#include <ctime>
#include "Utils.h"
#include <iostream>
#include <assert.h>
#include "FileNotFoundException.h"

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

    Snapshot (const Snapshot & other)
    {
      this->timestamp = other.timestamp;
      for (int i = 0; i < Utils::getNStatValues (); i++)
	{
	  rawStats[i] = other.rawStats[i];
	}
    }

    Snapshot *difference (Snapshot * other)
    {
      Snapshot *newSnapshot = new Snapshot (this->getSeconds (), other->getSeconds ());

      for (int i = 0; i < Utils::getNStatValues (); i++)
	{
	  newSnapshot->rawStats[i] = this->rawStats[i] - other->rawStats[i];
	}

      return newSnapshot;
    }

    time_t getSeconds ()
    {
      return mktime (&this->timestamp);
    }

    UINT64 getStatusValueFromName (char *stat_name)
    {
      int i;

      for (i = 0; i < PSTAT_COUNT; i++)
	{
	  if (strcmp (pstat_Metadata[i].stat_name, stat_name) == 0)
	    {
	      int offset = pstat_Metadata[i].start_offset;

	      switch (pstat_Metadata[i].valtype)
		{
		case PSTAT_ACCUMULATE_SINGLE_VALUE:
		case PSTAT_PEEK_SINGLE_VALUE:
		case PSTAT_COMPUTED_RATIO_VALUE:
		  return rawStats[offset];
		case PSTAT_COUNTER_TIMER_VALUE:
		  return rawStats[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)];
		case PSTAT_COMPLEX_VALUE:
		default:
		  assert (false);
		  break;
		}
	    }
	}
      return 0;
    }

    void print (FILE * stream)
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

      for (i = 0; i < PSTAT_COUNT; i++)
	{
	  if (pstat_Metadata[i].valtype == PSTAT_COMPLEX_VALUE)
	    {
	      break;
	    }

	  int offset = pstat_Metadata[i].start_offset;

	  if (pstat_Metadata[i].valtype != PSTAT_COMPUTED_RATIO_VALUE)
	    {
	      if (pstat_Metadata[i].valtype != PSTAT_COUNTER_TIMER_VALUE)
		{
		  if (stats_ptr[offset] != 0)
		    {
		      fprintf (stream, "%-29s = %10lld\n", pstat_Metadata[i].stat_name, (long long) stats_ptr[offset]);
		    }
		}
	      else
		{
		  perfmon_print_timer_to_file (stream, i, stats_ptr);
		}
	    }
	  else
	    {
	      if (stats_ptr[offset] != 0)
		{
		  fprintf (stream, "%-29s = %10.2f\n", pstat_Metadata[i].stat_name,
			   (long long) stats_ptr[offset] / 100.0f);
		}
	    }
	}

      for (; i < PSTAT_COUNT; i++)
	{
	  fprintf (stream, "%s:\n", pstat_Metadata[i].stat_name);
	  pstat_Metadata[i].f_dump_in_file (stream, &(stats_ptr[pstat_Metadata[i].start_offset]));
	}
    }

    ~Snapshot ()
    {
      free (rawStats);
    }
  };

  StatisticsFile (const std::string & filename, const std::string & alias);
  Snapshot *getSnapshotByMinutes (unsigned int minutes);
  Snapshot *getSnapshotByArgument (char *argument);
  void getIndicesOfSnapshotsByArgument (char *argument, int &minutes1, int &minutes2);
  int getSnapshotIndexByMinutes (unsigned int minutes);
  static void printInTableForm (Snapshot * s1, Snapshot * s2, FILE * stream);

  std::vector < Snapshot * >&getSnapshots ()
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
  std::string & getFilename ()
  {
    return filename;
  }
  std::string & getAlias ()
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
