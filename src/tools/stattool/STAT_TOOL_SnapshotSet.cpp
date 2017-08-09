//
// Created by paul on 21.03.2017.
//

#include "STAT_TOOL_SnapshotSet.hpp"

StatToolSnapshotSet::StatToolSnapshotSet (const std::string &filename, const std::string &alias) : filename (filename),
  alias (alias)
{
}

StatToolSnapshot *
StatToolSnapshotSet::getSnapshotBySeconds (unsigned int seconds)
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
StatToolSnapshotSet::getSnapshotIndexBySeconds (unsigned int seconds)
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
StatToolSnapshotSet::getIndicesOfSnapshotsByArgument (const char *argument, int &index1, int &index2)
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

StatToolSnapshot *
StatToolSnapshotSet::getSnapshotByArgument (const char *argument)
{
  char alias[MAX_FILE_NAME_SIZE];
  unsigned int minutes1;
  minutes1 = 0;

  sscanf (argument, "%[^(](%d)", alias, &minutes1);

  if (strcmp (alias, this->alias.c_str()) != 0)
    {
      return NULL;
    }

  return getSnapshotBySeconds (minutes1);
}

StatToolSnapshotSet::~StatToolSnapshotSet()
{
  for (unsigned int i = 0; i < snapshots.size(); i++)
    {
      delete snapshots[i];
    }
}