//
// Created by paul on 28.03.2017.
//

#include "PlotExecutor.hpp"

PlotExecutor::PlotExecutor (std::string &wholeCommand,
                            std::vector<StatisticsFile *> &files) : CommandExecutor (wholeCommand, files)
{
  possibleOptions.push_back (ALIAS_CMD);
  possibleOptions.push_back (VARIABLE_CMD);
  possibleOptions.push_back (PLOT_FILENAME_CMD);
  possibleOptions.push_back (INTERVAL_CMD);
}

bool PlotExecutor::parseCommandAndInit()
{
  int index1, index2;
  bool hasFilename = false;
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
              return false;
            }
        }
      else if (arguments[i].compare (VARIABLE_CMD) == 0)
        {
          if (!hasArgument (i))
            {
              return false;
            }
          variable = arguments[i+1];
          i++;
        }
      else if (arguments[i].compare (INTERVAL_CMD) == 0)
        {
          if (!hasArgument (i))
            {
              return false;
            }
          interval = arguments[i+1];
          i++;
        }
      else if (arguments[i].compare (PLOT_FILENAME_CMD) == 0)
        {
          if (!hasArgument (i))
            {
              return false;
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

      if (i == 0)
        {
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

      for (unsigned int j = 0; j < files.size(); j++)
        {
          files[j]->getIndicesOfSnapshotsByArgument (arg.c_str (), index1, index2);

          if (index1 != -1 && index2 != -1)
            {
              plotData.push_back (std::make_pair (j, std::make_pair (index1, index2)));
            }
        }
    }

  return true;
}

bool PlotExecutor::execute()
{
  std::string cmd = "";

#if !defined (WINDOWS)
  gnuplotPipe = popen ("gnuplot", "w");
#else
  gnuplotPipe = _popen ("gnuplot.exe", "w");
#endif
  if (gnuplotPipe == NULL)
    {
      printf ("Unable to open pipe!\n");
      return false;
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
          UINT64 value = files[plotData[i].first]->getSnapshots()[j]->getStatusValueFromName (variable.c_str ());
          fprintf (gnuplotPipe, "%ld %lld\n", seconds, (long long) value);
        }

      fprintf (gnuplotPipe, "e\n");
    }

#if !defined (WINDOWS)
  pclose (gnuplotPipe);
#else
  _pclose (gnuplotPipe);
#endif

  return true;
}

bool PlotExecutor::hasArgument (unsigned int i)
{
  return i != arguments.size() - 1 &&
         std::find (possibleOptions.begin (), possibleOptions.end (), arguments[i+1]) == possibleOptions.end();
}


PlotExecutor::~PlotExecutor()
{

}