//
// Created by paul on 28.03.2017.
//

#include "STAT_TOOL_ShowExecutor.hpp"

ShowExecutor::ShowExecutor (std::string &wholeCommand,
                            std::vector<StatisticsFile *> &files) : CommandExecutor (wholeCommand, files),
  showComplex (false),
  showZeroes (false)
{
  possibleOptions.push_back (SHOW_COMPLEX_CMD);
  possibleOptions.push_back (SHOW_ZEROES_CMD);
}

ErrorManager::ErrorCode
ShowExecutor::parseCommandAndInit()
{
  std::vector<std::string> snapshotsStr;

  for (unsigned int i = 0; i < arguments.size(); i++)
    {
      if (std::find (possibleOptions.begin (), possibleOptions.end (), arguments[i]) != possibleOptions.end())
        {
          if (arguments[i].compare (SHOW_COMPLEX_CMD) == 0)
            {
              showComplex = true;
            }
          if (arguments[i].compare (SHOW_ZEROES_CMD) == 0)
            {
              showZeroes = true;
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
          Snapshot *snapshot1 = NULL, *snapshot2 = NULL;
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
              validSnapshots.push_back (snapshotsStr[i]);
            }
        }
      else
        {
          Snapshot *snapshot;
          for (unsigned int j = 0; j < files.size(); j++)
            {
              if ((snapshot = files[j]->getSnapshotByArgument (snapshotsStr[i].c_str())) != NULL)
                {
                  snapshots.push_back (snapshot);
                  validSnapshots.push_back (snapshotsStr[i]);
                  break;
                }
            }
        }
    }
  if (validSnapshots.size() == 0)
    {
      ErrorManager::printErrorMessage (ErrorManager::INVALID_ALIASES_ERROR, "No valid aliases found!");
      return ErrorManager::INVALID_ALIASES_ERROR;
    }

  return ErrorManager::NO_ERRORS;
}

ErrorManager::ErrorCode
ShowExecutor::execute()
{
  const UINT64 **allRawStats;
  assert (validSnapshots.size() == validSnapshots.size());

  allRawStats = (const UINT64 **)malloc (sizeof (UINT64 *) * validSnapshots.size());

  printf ("%-50s", "");
  for (unsigned int i = 0; i < validSnapshots.size(); i++)
    {
      printf ("%15s", validSnapshots[i].c_str ());
      allRawStats[i] = snapshots[i]->rawStats;
    }
  printf ("\n");

  perfmeta_custom_dump_stats_in_table_form (allRawStats, (int)validSnapshots.size(), NULL, (int) showComplex,
      (int) showZeroes);
  free (allRawStats);
  return ErrorManager::NO_ERRORS;
}

void
ShowExecutor::printUsage()
{
  printf ("usage: show <alias[(X-Y)][/alias[(Z-W)]]>...\n");
}

ShowExecutor::~ShowExecutor()
{

}
