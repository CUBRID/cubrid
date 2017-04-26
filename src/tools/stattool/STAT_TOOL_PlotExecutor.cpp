//
// Created by paul on 28.03.2017.
//

#include "STAT_TOOL_PlotExecutor.hpp"

#define DEFAULT_PLOT_FILENAME "plot"
#define ALIAS_CMD "-a"
#define VARIABLE_CMD "-v"
#define PLOT_FILENAME_CMD "-f"
#define INTERVAL_CMD "-i"

PlotExecutor::PlotExecutor (std::string &wholeCommand,
                            std::vector<StatToolSnapshotSet *> &files) : CommandExecutor (wholeCommand, files)
{
  possibleOptions.push_back (ALIAS_CMD);
  possibleOptions.push_back (VARIABLE_CMD);
  possibleOptions.push_back (PLOT_FILENAME_CMD);
  possibleOptions.push_back (INTERVAL_CMD);
}

ErrorManager::ErrorCode
PlotExecutor::parseCommandAndInit()
{
  int index1 = -1, index2 = -1;
  bool hasFilename = false;
  bool hasAlias = false, hasVariable = false;
  bool first = true;

  for (unsigned int i = 0; i < arguments.size(); i++)
    {
      if (arguments[i].compare (ALIAS_CMD) == 0)
        {
          hasAlias = true;
        }
      if (arguments[i].compare (VARIABLE_CMD) == 0)
        {
          hasVariable = true;
        }
    }

  if (!hasAlias || !hasVariable)
    {
      ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR, "");
      return ErrorManager::MISSING_ARGUMENT_ERROR;
    }

  for (unsigned int i = 0; i < arguments.size(); i++)
    {
      if (arguments[i].compare (ALIAS_CMD) == 0)
        {
          i++;
          while (i < arguments.size()
                 && std::find (possibleOptions.begin (), possibleOptions.end (), arguments[i]) == possibleOptions.end())
            {
              aliases.push_back (arguments[i]);
              i++;
            }
          i--;
          if (aliases.size() == 0)
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR,
                                               "No aliases were found!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
        }
      else if (arguments[i].compare (VARIABLE_CMD) == 0)
        {
          if (!hasArgument (i))
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR,
                                               "You must provide a variable!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
          variable = arguments[i+1];
          i++;
        }
      else if (arguments[i].compare (INTERVAL_CMD) == 0)
        {
          if (!hasArgument (i))
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR,
                                               "You must provide an interval!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
          interval = arguments[i+1];
          i++;
        }
      else if (arguments[i].compare (PLOT_FILENAME_CMD) == 0)
        {
          if (!hasArgument (i))
            {
              ErrorManager::printErrorMessage (ErrorManager::MISSING_ARGUMENT_ERROR,
                                               "You must provide a plot filename!!");
              return ErrorManager::MISSING_ARGUMENT_ERROR;
            }
          hasFilename = true;
          plotFilename = arguments[i+1];
          i++;
        }
    }

  if (!hasFilename)
    {
      plotFilename = std::string (DEFAULT_PLOT_FILENAME);
    }

  for (unsigned int i = 0; i < aliases.size(); i++)
    {
      std::string arg = "";
      arg += aliases[i] + interval;

      for (unsigned int j = 0; j < files.size(); j++)
        {
          files[j]->getIndicesOfSnapshotsByArgument (arg.c_str (), index1, index2);

          if (index1 != -1 && index2 != -1)
            {
              plotData.push_back (std::make_pair (j, std::make_pair (index1, index2)));
              if (first)
                {
                  first = false;
                  plotCmd += "plot '-' with lines title \"";
                  plotCmd += arg;
                  plotCmd += "\", ";
                }
              else
                {
                  plotCmd += "'-' with lines title \"";
                  plotCmd += arg;
                  plotCmd += "\", ";
                }
              break;
            }
        }
    }
  if (first)
    {
      ErrorManager::printErrorMessage (ErrorManager::INVALID_ALIASES_ERROR, "");
      return ErrorManager::INVALID_ALIASES_ERROR;
    }

  return ErrorManager::NO_ERRORS;
}

ErrorManager::ErrorCode
PlotExecutor::execute()
{
  std::string cmd = "";

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

  cmd += "set xlabel \"Time(s)\"";
  fprintf (gnuplotPipe, "%s\n", cmd.c_str());
  cmd = "";
  cmd += "set ylabel \"";
  cmd += variable;
  cmd += "\"";
  fprintf (gnuplotPipe, "%s\n", cmd.c_str());
  fprintf (gnuplotPipe, "set key outside\n");
  fprintf (gnuplotPipe, "set terminal png size 1080, 640\n");
  fprintf (gnuplotPipe, "set output \"");
  fprintf (gnuplotPipe, "%s", plotFilename.c_str());
  fprintf (gnuplotPipe, ".png\"\n");

  fprintf (gnuplotPipe, "%s\n", plotCmd.c_str ());
  for (unsigned int i = 0; i < plotData.size(); i++)
    {
      for (int j = plotData[i].second.first; j <= plotData[i].second.second; j++)
        {
          time_t seconds = files[plotData[i].first]->getSnapshots()[j]->getSeconds ()
                           -files[plotData[i].first]->getRelativeSeconds();
          UINT64 value = files[plotData[i].first]->getSnapshots ()[j]->getStatValueFromName (variable.c_str ());
          fprintf (gnuplotPipe, "%ld %lld\n", seconds, (long long) value);
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

bool
PlotExecutor::hasArgument (unsigned int i)
{
  return i != arguments.size() - 1 &&
         std::find (possibleOptions.begin (), possibleOptions.end (), arguments[i+1]) == possibleOptions.end();
}

void
PlotExecutor::printUsage()
{
  printf ("usage: plot <OPTIONS>\n\nvalid options:\n");
  printf ("\t-a <alias1, alias2...>\n");
  printf ("\t-i <INTERVAL>\n");
  printf ("\t-v <VARIABLE>\n");
  printf ("\t-f <PLOT FILENAME>\n");
}

PlotExecutor::~PlotExecutor()
{

}
