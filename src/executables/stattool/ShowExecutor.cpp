//
// Created by paul on 28.03.2017.
//

#include "ShowExecutor.hpp"

ShowExecutor::ShowExecutor (std::string &wholeCommand,
                            std::vector<StatisticsFile *> &files) : CommandExecutor (wholeCommand, files),
  showComplex (false),
  end (total_num_stat_vals)
{
  possibleOptions.push_back (SHOW_COMPLEX_CMD);
}

bool ShowExecutor::parseCommandAndInit()
{
  for (unsigned int i = 0; i < arguments.size(); i++)
    {
      if (std::find (possibleOptions.begin (), possibleOptions.end (), arguments[i]) != possibleOptions.end())
        {
          if (arguments[i].compare (SHOW_COMPLEX_CMD) == 0)
            {
              showComplex = true;
            }
        }
      else
        {
          snapshotsStr.push_back (arguments[i]);
        }
    }

  for (unsigned int i = 0; i < snapshotsStr.size(); i++)
    {
      if (snapshotsStr[i].find ("/") != std::string::npos)
        {
          StatisticsFile::Snapshot *snapshot1 = NULL, *snapshot2 = NULL;
          char splitAliases[MAX_COMMAND_SIZE];
          char *firstAlias, *secondAlias, *savePtr;

          strcpy (splitAliases, snapshotsStr[i].c_str());
          firstAlias = strtok_r (splitAliases, "/", &savePtr);
          secondAlias = strtok_r (NULL, " ", &savePtr);
          for (unsigned int j = 0; j < files.size(); j++)
            {
              if ((snapshot1 = files[j]->getSnapshotByArgument (firstAlias)) != NULL)
                {
                  break;
                }
            }

          for (unsigned int j = 0; j < files.size(); j++)
            {
              if ((snapshot2 = files[j]->getSnapshotByArgument (secondAlias)) != NULL)
                {
                  break;
                }
            }

          if (snapshot1 && snapshot2)
            {
              snapshots.push_back (snapshot1->divide (snapshot2));
            }
        }
      else
        {
          StatisticsFile::Snapshot *snapshot;
          for (unsigned int j = 0; j < files.size(); j++)
            {
              if ((snapshot = files[j]->getSnapshotByArgument (snapshotsStr[i].c_str())) != NULL)
                {
                  snapshots.push_back (snapshot);
                  break;
                }
            }
        }
    }
  return true;
}

bool ShowExecutor::execute()
{
  printf ("%-50s", "");
  for (unsigned int i = 0; i < arguments.size(); i++)
    {
      if (std::find (possibleOptions.begin (), possibleOptions.end (), arguments[i]) == possibleOptions.end())
        {
          printf ("%15s", arguments[i].c_str ());
        }
    }
  printf ("\n");

  for (int i = 0; i < (showComplex ? total_num_stat_vals : pstat_Metadata[PSTAT_PBX_FIX_COUNTERS].start_offset); i++)
    {
      bool show = false;
      for (unsigned int j = 0; j < snapshots.size(); j++)
        {
          if (snapshots[j]->rawStats[i] != 0)
            {
              show = true;
            }
        }

      if (show)
        {
          printf ("%-50s", pstat_Nameoffset[i].name);
          for (unsigned int j = 0; j < snapshots.size (); j++)
            {
              printf ("%15lld", (long long) snapshots[j]->rawStats[i]);
            }
          printf ("\n");
        }
    }
  return true;
}

ShowExecutor::~ShowExecutor()
{

}