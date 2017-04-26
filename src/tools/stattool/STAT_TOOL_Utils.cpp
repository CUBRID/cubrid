//
// Created by paul on 21.03.2017.
//

#include "STAT_TOOL_Utils.hpp"

StatToolSnapshot *
Utils::findSnapshotInLoadedSets (const std::vector<StatToolSnapshotSet *> &sets, const char *alias)
{
  StatToolSnapshot *snapshot = NULL, *secondSnapshot = NULL;
  char *first, *second;
  first = second = NULL;
  char *aliasCopy = strdup(alias);

  if (strchr(alias, '-') != NULL)
  {
    first = strtok (aliasCopy, "-");
    second = strtok (NULL, " ");
  } else {
    first = aliasCopy;
  }

  for (unsigned int j = 0; j < sets.size(); j++)
  {
    if ((snapshot = sets[j]->getSnapshotByArgument (first)) != NULL) {
      break;
    }
  }

  if (!snapshot) {
    return NULL;
  }

  if (second != NULL) {
    for (unsigned int j = 0; j < sets.size(); j++)
    {
      if ((secondSnapshot = sets[j]->getSnapshotByArgument (second)) != NULL) {
	return snapshot->difference (secondSnapshot);
      }
    }
  } else {
    return snapshot;
  }

  free(aliasCopy);
  return NULL;
}

void
Utils::perfmeta_custom_dump_stats_in_table_form (const std::vector<StatToolSnapshot *>& snapshots, FILE * stream, int show_complex,
						int show_zero)
{
  int i, j, show;
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
	    snapshots[j]->print (stream, offset);
	  }
	  fprintf (stream, "\n");
	}
      }
      else
      {
	perfmon_print_timer_to_file_in_table_form (stream, i, snapshots, show_zero, 0);
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
	  if (typeid (*snapshots[j]) == typeid (StatToolSnapshotFloat)) {
	    fprintf (stream, "%15.2f", ((StatToolSnapshotFloat*)snapshots[j])->rawStatsFloat[offset] / 100);
	  } else {
	    fprintf (stream, "%15.2f", (float) snapshots[j]->rawStats[offset] / 100);
	  }
	}
	fprintf (stream, "\n");
      }
    }
  }

  for (; show_complex == 1 && i < PSTAT_COUNT; i++)
  {
    fprintf (stream, "%s:\n", pstat_Metadata[i].stat_name);
    perfmon_stat_dump_in_file_in_table_form (stream, &pstat_Metadata[i], snapshots, show_zero);
  }
}

/*
 * perfmon_print_timer_to_file_in_table_form - Print in a file multiple statistic values in table form (colums)
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
Utils::perfmon_print_timer_to_file_in_table_form (FILE * stream, int stat_index, const std::vector<StatToolSnapshot *>& snapshots,
						  int show_zero, int show_header)
{
  int offset = pstat_Metadata[stat_index].start_offset;
  int i;
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
      snapshots[i]->print (stream, PSTAT_COUNTER_TIMER_COUNT_VALUE (offset));
    }
    fprintf (stream, "\n");
  }

  if (show_timer_total != 0 || show_zero == 1)
  {
    fprintf (stream, "Total_time_%-39s", pstat_Metadata[stat_index].stat_name);
    for (i = 0; i < snapshots.size(); i++)
    {
      snapshots[i]->print (stream, PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset));
    }
    fprintf (stream, "\n");
  }

  if (show_timer_max != 0 || show_zero == 1)
  {
    fprintf (stream, "Max_time_%-41s", pstat_Metadata[stat_index].stat_name);
    for (i = 0; i < snapshots.size(); i++)
    {
      snapshots[i]->print (stream, PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset));
    }
    fprintf (stream, "\n");
  }

  if (show_timer_avg != 0 || show_zero == 1)
  {
    fprintf (stream, "Avg_time_%-41s", pstat_Metadata[stat_index].stat_name);
    for (i = 0; i < snapshots.size(); i++)
    {
      snapshots[i]->print (stream, PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset));
    }
    fprintf (stream, "\n");
  }
}

void
Utils::perfmon_stat_dump_in_file_in_table_form (FILE * stream, PSTAT_METADATA * stat, const std::vector<StatToolSnapshot *>& snapshots,
						int show_zeroes)
{
  int i, j;
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
      snapshots[j]->print (stream, i);
    }
    fprintf (stream, "\n");
  }
}
