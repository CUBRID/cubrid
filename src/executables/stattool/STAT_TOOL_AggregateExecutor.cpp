//
// Created by paul on 04.04.2017.
//

#include "STAT_TOOL_AggregateExecutor.hpp"

AggregateExecutor::AggregateExecutor (std::string &wholeCommand,
                                      std::vector<StatisticsFile *> &files) : CommandExecutor (wholeCommand, files)
{
  fixedDimension = -1;
  fixedIndex = -1;
  statIndex = -1;
  statName = "";
  plotFilename = "";
  file = NULL;

  possibleOptions.push_back (NAME_CMD);
  possibleOptions.push_back (DIM_CMD);
  possibleOptions.push_back (INDEX_CMD);
  possibleOptions.push_back (ALIAS_CMD);
  possibleOptions.push_back (FILENAME_CMD);
}

ErrorManager::ErrorCode AggregateExecutor::parseCommandAndInit ()
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
              fixedDimension = atoi (arguments[i+1].c_str());
            }
          else
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR, "You must provide a dimension!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
        }
      else if (arguments[i].compare (INDEX_CMD) == 0)
        {
          if (i+1 < arguments.size())
            {
              fixedIndex = atoi (arguments[i+1].c_str());
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

  if (statName.length() == 0 || fixedDimension == -1 || fixedIndex == -1)
    {
      return ErrorManager::MISSING_ARGUMENT_ERROR;
    }

  for (int i = 0; i < pstat_Global.n_stat_values; i++)
    {
      if (strcmp (pstat_Metadata[i].stat_name, statName.c_str()) == 0)
        {
          statIndex = i;
          break;
        }
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

  for (int i = 0; i < pstat_Metadata[statIndex].dimensions; i++)
    {
      if (fixedDimension == i)
        {
          aggregateName += "[";
          std::stringstream ss;
          ss << fixedIndex;
          aggregateName += ss.str();
          aggregateName += "]";
        }
      else
        {
          aggregateName += "[*]";
        }
    }

  return ErrorManager::NO_ERRORS;
}

ErrorManager::ErrorCode AggregateExecutor::execute ()
{
  std::string cmd = "";
  std::vector<Snapshot *> snapshotsForAggregation = file->getSnapshots ();

  fprintf (gnuplotPipe, "set terminal jpeg giant font \"Helvetica\" 16\n");
  cmd += "set output '";
  cmd += plotFilename;
  cmd += ".jpg'";
  fprintf (gnuplotPipe, "%s\n", cmd.c_str());
  cmd = "";
  fprintf (gnuplotPipe, "set key outside\n");
  fprintf (gnuplotPipe, "set grid y\n");
  fprintf (gnuplotPipe, "set style data histograms\n");
  fprintf (gnuplotPipe, "set style histogram rowstacked\n");
  fprintf (gnuplotPipe, "set boxwidth 0.5\n");
  fprintf (gnuplotPipe, "set style fill solid 1.0 border -1\n");

  for (unsigned int i = 0; i < snapshotsForAggregation.size(); i++)
    {
      time_t seconds = mktime (&snapshotsForAggregation[i]->timestamp) - file->getRelativeSeconds ();
      if (i == 0)
        {
          cmd += "plot '-' using ";
        }
      else
        {
          cmd += "'' using ";
        }
      std::stringstream ss;
      ss << i+2;
      cmd += ss.str();
      cmd += ":xtic(1) t \"";
      std::stringstream ss2;
      ss2 << seconds;
      cmd += ss2.str();
      cmd += "\", ";
    }

  fprintf (gnuplotPipe, "%s\n", cmd.c_str());
  std::string line = "";
  line += aggregateName;
  for (unsigned int i = 0; i < snapshotsForAggregation.size(); i++)
    {
      int res = 0;
      aggregate_complex_data (&pstat_Metadata[statIndex],
                              snapshotsForAggregation[i]->rawStats,
                              fixedDimension,
                              fixedIndex,
                              &res,
                              0,
                              pstat_Metadata[statIndex].start_offset);
      std::stringstream ss;
      ss << res+100;
      line += " " + ss.str();
    }

  for (unsigned int i = 0; i < snapshotsForAggregation.size(); i++)
    {
      fprintf (gnuplotPipe, "%s\n", line.c_str ());
      fprintf (gnuplotPipe, "e\n");
    }

#if !defined (WINDOWS)
  pclose (gnuplotPipe);
#else
  _pclose (gnuplotPipe);
#endif

  return ErrorManager::NO_ERRORS;
}

void AggregateExecutor::printUsage ()
{
  printf ("usage: aggregate <OPTIONS>\n\nvalid options:\n");
  printf ("\t-a <alias>\n");
  printf ("\t-n <name>\n");
  printf ("\t-d <fixed dimension>\n");
  printf ("\t-i <fixed index>\n");
  printf ("\t-f <plot filename> DEFAULT: aggregate_plot.jpg\n");
}

AggregateExecutor::~AggregateExecutor ()
{

}