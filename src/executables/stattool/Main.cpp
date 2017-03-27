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

void print_plot_usage ()
{
  printf ("usage: plot <OPTIONS>\n\nvalid options:\n");
  printf ("\t-a <alias1, alias2...>\n");
  printf ("\t-i <INTERVAL>\n");
  printf ("\t-v <VARIABLE>\n");
  printf ("\t-f <PLOT FILENAME>\n");
}

int main (int argc, char **argv)
{
  bool quit = false;
  char command[MAX_COMMAND_SIZE];
  char *str;
  std::vector<StatisticsFile *> files;
  metadata_initialize();
  init_name_offset_assoc();
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
      else if (strcmp (str, "show") == 0)
        {
          int end = total_num_stat_vals;
          char *argument;
          std::vector<StatisticsFile::Snapshot *> snapshots;

          argument = strtok (NULL, " ");
          if (argument != NULL && strcmp (argument, "-c") == 0)
            {
              end = pstat_Metadata[PSTAT_PBX_FIX_COUNTERS].start_offset;
              argument = strtok (NULL, " ");
            }
          printf ("%-50s", " ");
          while (argument != NULL)
            {
              printf ("%15s", argument);
              if (strchr (argument, '/') != 0)
                {
                  StatisticsFile::Snapshot *snapshot1 = NULL, *snapshot2 = NULL;
                  char splitAliases[MAX_COMMAND_SIZE];
                  char *firstAlias, *secondAlias, *savePtr;

                  strcpy (splitAliases, argument);
                  firstAlias = strtok_r (splitAliases, "/", &savePtr);
                  secondAlias = strtok_r (NULL, " ", &savePtr);
                  for (unsigned int i = 0; i < files.size(); i++)
                    {
                      if ((snapshot1 = files[i]->getSnapshotByArgument (firstAlias)) != NULL)
                        {
                          break;
                        }
                    }

                  for (unsigned int i = 0; i < files.size(); i++)
                    {
                      if ((snapshot2 = files[i]->getSnapshotByArgument (secondAlias)) != NULL)
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
                  for (unsigned int i = 0; i < files.size(); i++)
                    {
                      if ((snapshot = files[i]->getSnapshotByArgument (argument)) != NULL)
                        {
                          snapshots.push_back (snapshot);
                          break;
                        }
                    }
                }
              argument = strtok (NULL, " ");
            }
          printf ("\n");
          for (int i = 0; i < end; i++)
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
        }
      else if (strcmp (str, "plot") == 0)
        {
          int index1 = -1, index2 = -1;
          char *argument = NULL;
          FILE *gnuplotPipe;
          std::string cmd = "";
          std::string plotCmd = "";
          bool advance;
          std::vector<std::pair<int, std::pair<int, int> > > plotData;
          std::vector<std::string> aliases;
          std::string interval = "";
          std::string variable = "";
          std::string plotFilename = "";

          argument = strtok (NULL, " ");
          while (argument != NULL)
            {
              advance = true;
              if (strcmp (argument, "-a") == 0)
                {
                  do
                    {
                      argument = strtok (NULL, " ");
                      if (argument == NULL || strcmp (argument, "-i") == 0 || strcmp (argument, "-v") == 0 ||
                          strcmp (argument, "-f") == 0)
                        {
                          advance = false;
                          break;
                        }
                      aliases.push_back (std::string (argument));
                    }
                  while (1);
                }
              else if (strcmp (argument, "-v") == 0)
                {
                  argument = strtok (NULL, " ");
                  if (argument == NULL)
                    {
                      print_plot_usage ();
                      break;
                    }
                  variable = std::string (argument);
                }
              else if (strcmp (argument, "-i") == 0)
                {
                  argument = strtok (NULL, " ");
                  if (argument == NULL)
                    {
                      print_plot_usage ();
                      break;
                    }
                  interval = std::string (argument);
                }
              else if (strcmp (argument, "-f") == 0)
                {
                  argument = strtok (NULL, " ");
                  if (argument == NULL)
                    {
                      print_plot_usage ();
                      break;
                    }
                  plotFilename = std::string (argument);
                }
              if (advance)
                {
                  argument = strtok (NULL, " ");
                }
            }

          if (variable.length () == 0 || aliases.size() == 0)
            {
              print_plot_usage ();
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
          cmd += variable;
          cmd += "\"";
          fprintf (gnuplotPipe, "%s\n", cmd.c_str());
          fprintf (gnuplotPipe, "set key outside\n");
          fprintf (gnuplotPipe, "set terminal png size 1080, 640\n");
          if (plotFilename.length () == 0)
            {
              fprintf (gnuplotPipe, "set output \"plot.png\"\n");
            }
          else
            {
              fprintf (gnuplotPipe, "set output \"");
              fprintf (gnuplotPipe, "%s", plotFilename.c_str());
              fprintf (gnuplotPipe, ".png\"\n");
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
  free (pstat_Nameoffset);

  return 0;
}