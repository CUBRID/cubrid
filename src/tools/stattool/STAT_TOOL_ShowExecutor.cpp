//
// Created by paul on 28.03.2017.
//

#include "STAT_TOOL_ShowExecutor.hpp"

const char *ShowExecutor::USAGE = "show <alias[(X-Y)][/alias[(Z-W)]]>...\n";

ShowExecutor::ShowExecutor (std::string &wholeCommand) : CommandExecutor (wholeCommand),
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
          StatToolSnapshot *snapshot1 = NULL, *snapshot2 = NULL;
          char splitAliases[MAX_COMMAND_SIZE];
          char *firstAlias, *secondAlias, *savePtr;
          unsigned int index1, index2;

          strcpy (splitAliases, snapshotsStr[i].c_str());
          firstAlias = strtok_r (splitAliases, "/", &savePtr);
          secondAlias = strtok_r (NULL, " ", &savePtr);

          sscanf (firstAlias, "#%d", &index1);
          sscanf (secondAlias, "#%d", &index2);

          if (index1 - 1 >= i || index2 - 1 >= i)
            {
              continue;
            }

          snapshot1 = Utils::findSnapshotInLoadedSets (snapshotsStr[index1-1].c_str());
          snapshot2 = Utils::findSnapshotInLoadedSets (snapshotsStr[index2-1].c_str());

          if (snapshot1 && snapshot2)
            {
              snapshots.push_back (snapshot1->divide (snapshot2));
              validSnapshots.push_back (snapshotsStr[i]);
            }
        }
      else if (snapshotsStr[i].find ("-") != std::string::npos)
        {
          StatToolSnapshot *snapshot1 = NULL, *snapshot2 = NULL;
          char splitAliases[MAX_COMMAND_SIZE];
          char *firstAlias, *secondAlias, *savePtr;

          strcpy (splitAliases, snapshotsStr[i].c_str());
          firstAlias = strtok_r (splitAliases, "-", &savePtr);
          secondAlias = strtok_r (NULL, " ", &savePtr);

          snapshot1 = Utils::findSnapshotInLoadedSets (firstAlias);
          snapshot2 = Utils::findSnapshotInLoadedSets (secondAlias);

          if (snapshot1 && snapshot2)
            {
              snapshots.push_back (snapshot1->difference (snapshot2));
              validSnapshots.push_back (snapshotsStr[i]);
            }
        }
      else
        {
          StatToolSnapshot *snapshot = NULL;
          snapshot = Utils::findSnapshotInLoadedSets (snapshotsStr[i].c_str());

          if (snapshot)
            {
              snapshots.push_back (snapshot);
              validSnapshots.push_back (snapshotsStr[i]);
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
  assert (validSnapshots.size() == snapshots.size());

  printf ("%-50s", "");
  for (unsigned int i = 0; i < validSnapshots.size(); i++)
    {
      printf ("%15s", validSnapshots[i].c_str ());
    }
  printf ("\n");

  customDumpStatsInTableForm (snapshots, NULL, (int) showComplex,
                              (int) showZeroes);
  return ErrorManager::NO_ERRORS;
}

void
ShowExecutor::printUsage()
{
  printf ("%s", USAGE);
}

void
ShowExecutor::customDumpStatsInTableForm (const std::vector<StatToolColumnInterface *> &snapshots, FILE *stream,
                                          int show_complex,
                                          int show_zero)
{
  unsigned int i, j;
  int show;
  int offset;

  if (stream == NULL)
  {
    stream = stdout;
  }

  for (i = 0; i < PSTAT_COUNT; i++)
  {
    offset = pstat_Metadata[i].start_offset;
    if (pstat_Metadata[i].valtype == PSTAT_COMPLEX_VALUE)
    {
      break;
    }

    if (pstat_Metadata[i].valtype != PSTAT_COMPUTED_RATIO_VALUE)
    {
      if (pstat_Metadata[i].valtype != PSTAT_COUNTER_TIMER_VALUE)
      {
        show = 0;
        for (j = 0; j < snapshots.size(); j++)
        {
          if (!snapshots[j]->isStatZero (offset))
          {
            show = 1;
          }
        }

        if (show == 1 || show_zero == 1)
        {
          fprintf (stream, "%-50s", pstat_Metadata[i].stat_name);
          for (j = 0; j < snapshots.size(); j++)
          {
            snapshots[j]->printColumnValue (stream, offset);
          }
          fprintf (stream, "\n");
        }
      }
      else
      {
        printTimerToFileInTableForm (stream, i, snapshots, show_zero, 0);
      }
    }
    else
    {
      show = 0;
      for (j = 0; j < snapshots.size(); j++)
      {
        if (!snapshots[j]->isStatZero (offset) != 0)
        {
          show = 1;
        }
      }
      if (show == 1 || show_zero == 1)
      {
        fprintf (stream, "%-50s", pstat_Metadata[i].stat_name);
        for (j = 0; j < snapshots.size(); j++)
        {
          snapshots[j]->printColumnValueForComputedRatio (stream, offset);
        }
        fprintf (stream, "\n");
      }
    }
  }

  for (; show_complex == 1 && i < PSTAT_COUNT; i++)
  {
    fprintf (stream, "%s:\n", pstat_Metadata[i].stat_name);
    statDumpInFileInTableForm (stream, &pstat_Metadata[i], snapshots, show_zero);
  }
}

/*
 * printTimerToFileInTableForm - Print in a file multiple statistic values in table form (colums)
 *
 * stream (in/out): input file
 * stat_index (in): statistic index
 * stats (in) : statistic values array
 * num_of_stats (in) : number of stats in array
 * show_zero (in) : show(1) or not(0) null values
 * show_header (in) : show(1) or not(0) the header
 * return: void
 *
 */
void
ShowExecutor::printTimerToFileInTableForm (FILE *stream, int stat_index,
                                           const std::vector<StatToolColumnInterface *> &snapshots,
                                           int show_zero, int show_header)
{
  int offset = pstat_Metadata[stat_index].start_offset;
  unsigned int i;
  int show_timer_count = 0, show_timer_total = 0, show_timer_max = 0, show_timer_avg = 0;

  assert (pstat_Metadata[stat_index].valtype == PSTAT_COUNTER_TIMER_VALUE);

  for (i = 0; i < snapshots.size(); i++)
  {
    if (!snapshots[i]->isStatZero (PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)))
    {
      show_timer_count = 1;
    }
    if (!snapshots[i]->isStatZero (PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)))
    {
      show_timer_total = 1;
    }
    if (!snapshots[i]->isStatZero (PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)))
    {
      show_timer_max = 1;
    }
    if (!snapshots[i]->isStatZero (PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)))
    {
      show_timer_avg = 1;
    }
  }

  if (show_header == 1)
  {
    fprintf (stream, "The timer values for %s are:\n", pstat_Metadata[stat_index].stat_name);
  }
  if (show_timer_count != 0 || show_zero == 1)
  {
    fprintf (stream, "Num_%-46s", pstat_Metadata[stat_index].stat_name);
    for (i = 0; i < snapshots.size(); i++)
    {
      snapshots[i]->printColumnValue (stream, PSTAT_COUNTER_TIMER_COUNT_VALUE (offset));
    }
    fprintf (stream, "\n");
  }

  if (show_timer_total != 0 || show_zero == 1)
  {
    fprintf (stream, "Total_time_%-39s", pstat_Metadata[stat_index].stat_name);
    for (i = 0; i < snapshots.size(); i++)
    {
      snapshots[i]->printColumnValue (stream, PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset));
    }
    fprintf (stream, "\n");
  }

  if (show_timer_max != 0 || show_zero == 1)
  {
    fprintf (stream, "Max_time_%-41s", pstat_Metadata[stat_index].stat_name);
    for (i = 0; i < snapshots.size(); i++)
    {
      snapshots[i]->printColumnValue (stream, PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset));
    }
    fprintf (stream, "\n");
  }

  if (show_timer_avg != 0 || show_zero == 1)
  {
    fprintf (stream, "Avg_time_%-41s", pstat_Metadata[stat_index].stat_name);
    for (i = 0; i < snapshots.size(); i++)
    {
      snapshots[i]->printColumnValue (stream, PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset));
    }
    fprintf (stream, "\n");
  }
}

void
ShowExecutor::statDumpInFileInTableForm (FILE *stream, PSTAT_METADATA *stat,
                                         const std::vector<StatToolColumnInterface *> &snapshots,
                                         int show_zeroes)
{
  int i;
  unsigned int j;
  int start_offset = stat->start_offset;
  int end_offset = stat->start_offset + stat->n_vals;

  assert (stream != NULL);
  for (i = start_offset; i < end_offset; i++)
  {
    int show = 0;

    for (j = 0; j < snapshots.size(); j++)
    {
      if (!snapshots[j]->isStatZero (i) != 0)
      {
        show = 1;
      }
    }

    if (show == 0 && show_zeroes == 0)
    {
      continue;
    }
    fprintf (stream, "%-50s", perfmeta_get_value_name (i));
    for (j = 0; j < snapshots.size(); j++)
    {
      snapshots[j]->printColumnValue (stream, i);
    }
    fprintf (stream, "\n");
  }
}


ShowExecutor::~ShowExecutor()
{

}
