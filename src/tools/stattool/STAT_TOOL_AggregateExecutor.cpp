//
// Created by paul on 04.04.2017.
//

#include "STAT_TOOL_AggregateExecutor.hpp"

#define NAME_CMD "-n"
#define DIM_CMD "-d"
#define INDEX_CMD "-i"
#define ALIAS_CMD "-a"
#define FILENAME_CMD "-f"
#define DEFAULT_PLOT_FILENAME "aggregate_plot"

AggregateExecutor::AggregateExecutor (std::string &wholeCommand,
                                      std::vector<StatisticsFile *> &files) : CommandExecutor (wholeCommand, files)
{
  fixedDimension = -1;
  statIndex = PSTAT_BASE;
  statName = "";
  plotFilename = "";
  file = NULL;

  possibleOptions.push_back (NAME_CMD);
  possibleOptions.push_back (DIM_CMD);
  possibleOptions.push_back (ALIAS_CMD);
  possibleOptions.push_back (FILENAME_CMD);
}

ErrorManager::ErrorCode
AggregateExecutor::parseCommandAndInit ()
{
#if !defined (WINDOWS)
  gnuplotPipe = popen ("gnuplot", "w");
#else
  gnuplotPipe = _popen ("gnuplot.exe", "w");
#endif
  if (gnuplotPipe == NULL)
    {
      ErrorManager::printErrorMessage (ErrorManager::OPEN_PIPE_ERROR, "");
      return ErrorManager::OPEN_PIPE_ERROR;
    }

  for (unsigned int i = 0; i < arguments.size(); i++)
    {
      if (arguments[i].compare (NAME_CMD) == 0)
        {
          if (i+1 < arguments.size())
            {
              statName = arguments[i + 1];
            }
          else
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR, "You must provide a name!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
        }
      else if (arguments[i].compare (DIM_CMD) == 0)
        {
          if (i+1 < arguments.size())
            {
              char *endptr;
              const char *str = arguments[i+1].c_str();
              fixedDimension = (int) strtol (str, &endptr, 10);
              if (endptr == str)
                {
                  fixedDimension = -1;
                  fixedDimensionStr = arguments[i+1];
                }
            }
          else
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR, "You must provide a dimension!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
        }
      else if (arguments[i].compare (ALIAS_CMD) == 0)
        {
          if (i+1 < arguments.size())
            {
              for (unsigned f = 0; f < files.size(); f++)
                {
                  if (files[i]->getAlias().compare (arguments[i+1]) == 0)
                    {
                      file = files[i];
                      break;
                    }
                }
            }
          else
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR, "You must provide an alias!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
        }
      else if (arguments[i].compare (FILENAME_CMD) == 0)
        {
          if (i+1 < arguments.size())
            {
              plotFilename = arguments[i+1];
            }
          else
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR, "You must provide a plot filename!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
        }
    }

  if (plotFilename.length () == 0)
    {
      plotFilename = DEFAULT_PLOT_FILENAME;
    }

  if (statName.length() == 0)
    {
      return ErrorManager::MISSING_ARGUMENT_ERROR;
    }

  for (int i = 0; i < perfmeta_Stat_count; i++)
    {
      if (strcmp (pstat_Metadata[i].stat_name, statName.c_str()) == 0)
        {
          statIndex = (PERF_STAT_ID) i;
          if (fixedDimension == -1)
            {
              for (int j = 0; j < pstat_Metadata[statIndex].complexp->size; j++)
                {
                  if (fixedDimensionStr.compare (pstat_Metadata[statIndex].complexp->dimensions[j]->alias) == 0)
                    {
                      fixedDimension = j;
                      break;
                    }
                }
            }
          break;
        }
    }

  if (fixedDimension == -1)
    {
      ErrorManager::printErrorMessage (ErrorManager::INVALID_ARGUMENT_ERROR, "dimension (-d)");
      return ErrorManager::INVALID_ARGUMENT_ERROR;
    }

  if (statIndex == -1)
    {
      ErrorManager::printErrorMessage (ErrorManager::INVALID_ARGUMENT_ERROR, "name (-n)");
      return ErrorManager::INVALID_ARGUMENT_ERROR;
    }

  if (file == NULL)
    {
      ErrorManager::printErrorMessage (ErrorManager::INVALID_ARGUMENT_ERROR, "alias (-a)");
      return ErrorManager::INVALID_ARGUMENT_ERROR;
    }

  aggregateName = "";
  aggregateName += pstat_Metadata[statIndex].stat_name;

  for (int i = 0; i < pstat_Metadata[statIndex].complexp->size; i++)
    {
      if (fixedDimension == i)
        {
          aggregateName += "[x]";
        }
      else
        {
          aggregateName += "[*]";
        }
    }

  return ErrorManager::NO_ERRORS;
}

ErrorManager::ErrorCode
AggregateExecutor::execute ()
{
  std::string cmd = "";
  std::vector<Snapshot *> snapshotsForAggregation = file->getSnapshots ();
  std::vector<std::string> dataLines;
  UINT64 agg_vals[PERFBASE_DIMENSION_MAX_SIZE];

  fprintf (gnuplotPipe, "set terminal jpeg size 1080, 640\n");
  fprintf (gnuplotPipe, "set yrange [0:10<*]\n");
  fprintf (gnuplotPipe, "set xlabel \"time(s)\"\n");
  fprintf (gnuplotPipe, "set ylabel \"aggregate value\"\n");

  cmd += "set output '";
  cmd += plotFilename;
  cmd += ".jpg'";
  fprintf (gnuplotPipe, "%s\n", cmd.c_str());
  cmd = "";
  fprintf (gnuplotPipe, "set key below\n");
  fprintf (gnuplotPipe, "set grid y\n");

  cmd += "plot for [i=2:";
  std::stringstream ss;
  ss << pstat_Metadata[statIndex].complexp->dimensions[fixedDimension]->size + 1;
  cmd += ss.str();
  cmd += ":1] \"-\" using 1:(sum [col=i:";
  cmd += ss.str();
  cmd += "] column(col)) title columnheader(i-1) with filledcurves x1";

  fprintf (gnuplotPipe, "%s\n", cmd.c_str());
  for (unsigned int i = 0; i < snapshotsForAggregation.size(); i++)
    {
      cmd = "";
      perfbase_aggregate_complex (statIndex, snapshotsForAggregation[i]->rawStats, fixedDimension, agg_vals);
      time_t seconds = mktime (&snapshotsForAggregation[i]->timestamp) - file->getRelativeSeconds ();
      std::stringstream ss2;
      ss2 << seconds;
      cmd += ss2.str();

      for (int j = 0; j < pstat_Metadata[statIndex].complexp->dimensions[fixedDimension]->size; j++)
        {
          std::stringstream ss3;
          ss3 << agg_vals[j];
          cmd += " ";
          cmd += ss3.str();
        }
      dataLines.push_back (cmd);
    }

  std::string title = "";
  for (int i = 0; i < pstat_Metadata[statIndex].complexp->dimensions[fixedDimension]->size; i++)
    {
      std::string tmp (aggregateName);
      std::size_t index = tmp.find ("[x]");
      tmp[index+1] = (char) (i+'0');
      title += tmp;
      title += " ";
    }

  for (int i = 0; i < pstat_Metadata[statIndex].complexp->dimensions[fixedDimension]->size; i++)
    {
      fprintf (gnuplotPipe, "%s\n", title.c_str());
      for (unsigned int j = 0; j < dataLines.size(); j++)
        {
          fprintf (gnuplotPipe, "%s\n", dataLines[j].c_str());
        }
      fprintf (gnuplotPipe, "e\n");
    }

#if !defined (WINDOWS)
  pclose (gnuplotPipe);
#else
  _pclose (gnuplotPipe);
#endif

  return ErrorManager::NO_ERRORS;
}

void
AggregateExecutor::printUsage ()
{
  printf ("usage: aggregate <OPTIONS>\n\nvalid options:\n");
  printf ("\t-a <alias>\n");
  printf ("\t-n <name>\n");
  printf ("\t-d <fixed dimension>\n");
  printf ("\t-f <plot filename> DEFAULT: aggregate_plot.jpg\n");
}

AggregateExecutor::~AggregateExecutor ()
{

}
