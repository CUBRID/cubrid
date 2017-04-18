//
// Created by paul on 21.03.2017.
//

#include "STAT_TOOL_StatisticsFile.hpp"

StatisticsFile::StatisticsFile (const std::string &filename, const std::string &alias) : filename (filename),
  alias (alias)
{
}

ErrorManager::ErrorCode
StatisticsFile::readFileAndInit ()
{
  FILE *binary_fp = NULL;
  struct tm *timestamp;
  char strTime[80];
  INT64 seconds, unpacked_seconds;
  time_t epochSeconds;

  binary_fp = fopen (filename.c_str(), "rb");
  if (binary_fp == NULL)
    {
      ErrorManager::printErrorMessage (ErrorManager::OPEN_FILE_ERROR, "Filename: " + filename);
      return ErrorManager::OPEN_FILE_ERROR;
    }

  fread (&seconds, sizeof (INT64), 1, binary_fp);
  OR_GET_INT64 (&seconds, &unpacked_seconds);
  relativeEpochSeconds = (time_t)unpacked_seconds;
  timestamp = localtime (&relativeEpochSeconds);
  if (timestamp == NULL)
    {
      ErrorManager::printErrorMessage (ErrorManager::MISSING_TIMESTAMP_ERROR, "");
      return ErrorManager::MISSING_TIMESTAMP_ERROR;
    }
  relativeTimestamp = *timestamp;

  strftime (strTime, 80, "%a %B %d %H:%M:%S %Y", &relativeTimestamp);
  printf ("Relative Timestamp = %s\n", strTime);

  while (fread (&seconds, sizeof (INT64), 1, binary_fp) > 0)
    {
      char *unpacked_stats = (char *)malloc (sizeof (UINT64) * (size_t)Utils::getNStatValues());
      OR_GET_INT64 (&seconds, &unpacked_seconds);
      epochSeconds = (time_t)unpacked_seconds;
      Snapshot *snapshot = new Snapshot (epochSeconds);
      fread (unpacked_stats, sizeof (UINT64), (size_t)Utils::getNStatValues(), binary_fp);
      perfmon_unpack_stats (unpacked_stats, snapshot->rawStats);
      snapshots.push_back (snapshot);
    }

  fclose (binary_fp);
  return ErrorManager::NO_ERRORS;
}

Snapshot *
StatisticsFile::getSnapshotBySeconds (unsigned int seconds)
{
  unsigned int i, j, mid;

  i = 0;
  j = (unsigned int)snapshots.size() - 1;

  if ((mktime (&snapshots[0]->timestamp) - relativeEpochSeconds) >= seconds)
    {
      return snapshots[0];
    }

  while (j-i > 1)
    {
      mid = (i+j) / 2;
      time_t sec = (mktime (&snapshots[mid]->timestamp) - relativeEpochSeconds);
      if (sec > seconds)
        {
          j = mid;
        }
      else if (sec < seconds)
        {
          i = mid;
        }
      else if (sec == seconds)
        {
          return snapshots[mid];
        }
    }

  return snapshots[j];
}

int
StatisticsFile::getSnapshotIndexBySeconds (unsigned int seconds)
{
  unsigned int i, j, mid;

  i = 0;
  j = (unsigned int)snapshots.size() - 1;

  if ((mktime (&snapshots[0]->timestamp) - relativeEpochSeconds) >= seconds)
    {
      return 0;
    }

  while (j-i > 1)
    {
      mid = (i+j) / 2;
      time_t sec = (mktime (&snapshots[mid]->timestamp) - relativeEpochSeconds);
      if (sec > seconds)
        {
          j = mid;
        }
      else if (sec < seconds)
        {
          i = mid;
        }
      else if (sec == seconds)
        {
          return mid;
        }
    }

  return j;
}

void
StatisticsFile::getIndicesOfSnapshotsByArgument (const char *argument, int &index1, int &index2)
{
  char diffArgument[32];
  char alias[MAX_FILE_NAME_SIZE];
  unsigned int seconds1, seconds2;
  int tmp;

  index1 = 0;
  index2 = (int)snapshots.size() - 1;

  sscanf (argument, "%[^(]%[^)]", alias, diffArgument);

  if (strcmp (alias, this->alias.c_str()) != 0)
    {
      index1 = -1;
      index2 = -1;
      return;
    }

  if (strlen (diffArgument) == 0)
    {
      return;
    }

  if (strchr (diffArgument, '-') != NULL)
    {
      sscanf (diffArgument, "(%d-%d)", &seconds1, &seconds2);
      index1 = getSnapshotIndexBySeconds (seconds1);
      index2 = getSnapshotIndexBySeconds (seconds2);

      if (index1 > index2)
        {
          tmp = index2;
          index2 = index1;
          index1 = tmp;
        }
      return;
    }
  else
    {
      sscanf (diffArgument, "(%d", &seconds1);
      index1 = 0;
      index2 = getSnapshotIndexBySeconds (seconds1);
      return;
    }
}

Snapshot *
StatisticsFile::getSnapshotByArgument (const char *argument)
{
  char diffArgument[32];
  char alias[MAX_FILE_NAME_SIZE];
  unsigned int minutes1, minutes2;
  minutes1 = minutes2 = 0;

  sscanf (argument, "%[^(]%[^)]", alias, diffArgument);

  if (strcmp (alias, this->alias.c_str()) != 0)
    {
      return NULL;
    }

  if (strchr (diffArgument, '-') != NULL)
    {
      sscanf (diffArgument, "(%d-%d", &minutes1, &minutes2);
      Snapshot *s1 = getSnapshotBySeconds (minutes1);
      Snapshot *s2 = getSnapshotBySeconds (minutes2);
      return s1->difference (s2);
    }
  else
    {
      sscanf (diffArgument, "(%d", &minutes1);
      return getSnapshotBySeconds (minutes1);
    }
}

StatisticsFile::~StatisticsFile()
{
  for (unsigned int i = 0; i < snapshots.size(); i++)
    {
      delete snapshots[i];
    }
}