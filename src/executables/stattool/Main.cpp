#include <iostream>
#include "Utils.hpp"
#include "StatisticsFile.hpp"
#if defined (WINDOWS)
#include <windows.h>
#endif

extern "C" {
#include <perfmon_base.h>
#include <string.h>
#include <porting.h>
}

int main (int argc, char **argv)
{
  bool quit = false;
  char command[MAX_COMMAND_SIZE];
  char *str;
  std::vector<StatisticsFile *> files;
  metadata_initialize();
  Utils::setNStatValues (pstat_Global.n_stat_values);

  do
    {
      fgets (command, MAX_COMMAND_SIZE, stdin);
      command[strlen (command)-1] = '\0';
      str = strtok (command, " ");
      if (str == NULL)
        {
          continue;
        }

      if (strcmp (str, "load") == 0)
        {
          char *filename, *alias;
          filename = strtok (NULL, " ");
          alias = strtok (NULL, " ");

          if (!filename || !alias)
            {
              printf ("Usage: load <filename> <filename alias>");
            }
          else
            {
              StatisticsFile *newFile;
              newFile = new StatisticsFile (std::string (filename), std::string (alias));
              bool hasSucceded = newFile->readFileAndInit();
              if (hasSucceded)
              {
                files.push_back (newFile);
              }
            }
        }
      else if (strcmp (str, "compare") == 0)
        {
          char *argument1, *argument2, *outputFilename;
          FILE *outFp = NULL;
          StatisticsFile::Snapshot *s1 = NULL, *s2 = NULL;

          argument1 = strtok (NULL, " " );
          argument2 = strtok (NULL, " " );

          if (!argument1 || !argument2)
            {
              printf ("Usage: compare <alias1(minutes1[-minutes2])> <alias2(minutes1[-minutes2])>\n");
              continue;
            }

          outputFilename = strtok (NULL, " ");

          if (outputFilename != NULL)
            {
              outFp = fopen (outputFilename, "w");
            }
          else
            {
              outFp = stdout;
            }

          for (unsigned int i = 0; i < files.size() && ((s1 = files[i]->getSnapshotByArgument (argument1)) == NULL); i++);
          for (unsigned int i = 0; i < files.size() && ((s2 = files[i]->getSnapshotByArgument (argument2)) == NULL); i++);

          StatisticsFile::printInTableForm (s1, s2, outFp);

          if (outFp != stdout)
            {
              fclose (outFp);
            }

        }
      else if (strcmp (str, "print") == 0)
        {
          char *argument, *output_filename;
          FILE *out_fp = NULL;
          StatisticsFile::Snapshot *snapshot;

          argument = strtok (NULL, " ");

          if (!argument)
            {
              printf ("Usage: print <alias1(minutes1[-minutes2])>\n");
              continue;
            }

          output_filename = strtok (NULL, " ");

          if (output_filename != NULL)
            {
              out_fp = fopen (output_filename, "w");
            }
          else
            {
              out_fp = stdout;
            }

          for (unsigned int i = 0; i < files.size(); i++)
            {
              if ((snapshot = files[i]->getSnapshotByArgument (argument)) != NULL)
                {
                  snapshot->print (out_fp);
                  break;
                }
            }

          if (out_fp != stdout)
            {
              fclose (out_fp);
            }
        }
      else if (strcmp (str, "quit") == 0)
        {
          quit = true;
        }
      else if (strcmp (str, "plot") == 0)
        {
          int index1 = -1, index2 = -1;
          char *argument = NULL, *plottedVariable = NULL, *plot_filename = NULL;
          StatisticsFile *statisticsFile = NULL;
          FILE *gnuplotPipe;

          std::string cmd = "";

          argument = strtok (NULL, " ");
          plottedVariable = strtok (NULL, " " );
	  plot_filename = strtok (NULL, " ");

          if (!argument || !plottedVariable)
            {
              printf ("Usage: plot <alias(minutes1-minutes2)> <wanted variable to plot>\n");
              continue;
            }
          for (unsigned int i = 0; i < files.size(); i++)
            {
              files[i]->getIndicesOfSnapshotsByArgument (argument, index1, index2);
              if (index1 != -1 && index2 != -1)
                {
                  statisticsFile = files[i];
                  break;
                }
            }

          if (statisticsFile == NULL)
            {
              printf ("You must provide an existing alias!\n");
              continue;
            }

#if !defined (WINDOWS)
          gnuplotPipe = popen ("gnuplot", "w");
#else
          gnuplotPipe = _popen ("gnuplot.exe", "w");
#endif
          if (gnuplotPipe == NULL)
            {
              printf ("Unable to open pipe!\n");
              continue;
            }

          cmd += "set xlabel \"Time(s)\"";
          fprintf (gnuplotPipe, "%s\n", cmd.c_str());
          cmd = "";
          cmd += "set ylabel \"";
          cmd += plottedVariable;
          cmd += "\"";
          fprintf (gnuplotPipe, "%s\n", cmd.c_str());
          cmd = "";
          fprintf (gnuplotPipe, "set key outside\n");
          fprintf (gnuplotPipe, "set terminal png size 1080, 640\n");
	  if (plot_filename == NULL)
	    {
	      cmd += "set output \"./";
	      cmd += argument;
	      cmd += "_";
	      cmd += plottedVariable;
	      cmd += ".png\"";
	    }
	  else
	    {
	      cmd += "set output \"";
	      cmd += plot_filename;
	      cmd += ".png\"";
	    }
          fprintf (gnuplotPipe, "%s\n", cmd.c_str());
          cmd = "";
          cmd += "plot '-' with lines ";
          cmd += "title \"";
          cmd += argument;
          cmd += "_";
          cmd += plottedVariable;
          cmd += "\"";
          fprintf (gnuplotPipe, "%s\n", cmd.c_str());

          for (int i = index1; i <= index2; i++)
            {
              time_t seconds = mktime (&statisticsFile->getSnapshots()[i]->timestamp)-statisticsFile->getRelativeSeconds();
              UINT64 value = statisticsFile->getSnapshots()[i]->getStatusValueFromName (plottedVariable);
              fprintf (gnuplotPipe, "%ld %lld\n", seconds, (long long) value);
            }

          fprintf (gnuplotPipe, "e\n");
#if !defined (WINDOWS)
          pclose (gnuplotPipe);
#else
          _pclose (gnuplotPipe);
#endif
        }
      else
        {
          printf ("Invalid command!\n");
        }
    }
  while (!quit);

  for (unsigned int i = 0; i < files.size(); i++)
    {
      delete (files[i]);
    }

  return 0;
}