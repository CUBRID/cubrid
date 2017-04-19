#include <iostream>
#include "STAT_TOOL_Utils.hpp"
#include "STAT_TOOL_StatisticsFile.hpp"
#include "STAT_TOOL_ShowExecutor.hpp"
#include "STAT_TOOL_PlotExecutor.hpp"
#include "STAT_TOOL_LoadExecutor.hpp"
#include "STAT_TOOL_ErrorManager.hpp"
#include "STAT_TOOL_AggregateExecutor.hpp"
#if defined (WINDOWS)
#include <windows.h>
#endif

extern "C" {
#include <perf_metadata.h>
#include <string.h>
#include <porting.h>
}

std::vector<StatisticsFile *> files;

int
main (int argc, char **argv)
{
  bool quit = false;
  char command[MAX_COMMAND_SIZE];
  int n_stat_values = perfmeta_init();
  Utils::setNStatValues (n_stat_values);

  do
    {
      std::string commandStr;
      CommandExecutor *executor = NULL;
      std::string::size_type spacePosition;
      std::string commandKeyword;
      std::string arguments;

      fgets (command, MAX_COMMAND_SIZE, stdin);
      command[strlen (command) - 1] = '\0';         /* eliminates the ending "\n" that fgets adds */
      if (strlen (command) == 0)
        {
          continue;
        }
      commandStr = std::string (command);
      spacePosition = commandStr.find (" ");

      commandKeyword = commandStr.substr (0, spacePosition);
      arguments = commandStr.substr (spacePosition + 1);

      if (commandKeyword.compare ("load") == 0)
        {
          executor = new LoadExecutor (arguments, files);
        }
      else if (commandKeyword.compare ("show") == 0)
        {
          executor = new ShowExecutor (arguments, files);
        }
      else if (commandKeyword.compare ("plot") == 0)
        {
          executor = new PlotExecutor (arguments, files);
        }
      else if (commandKeyword.compare ("aggregate") == 0)
        {
          executor = new AggregateExecutor (arguments, files);
        }
      else if (commandKeyword.compare ("quit") == 0)
        {
          quit = true;
        }
      else
        {
          ErrorManager::printErrorMessage (ErrorManager::INVALID_COMMAND_ERROR,
                                           "The command is: " + commandStr);
          continue;
        }

      if (!quit)
        {
          ErrorManager::ErrorCode errorCode = executor->parseCommandAndInit ();
          if (errorCode == ErrorManager::NO_ERRORS)
            {
              executor->execute ();
            }
          else
            {
              executor->printUsage ();
            }
          delete executor;
        }
    }
  while (!quit);

  for (unsigned int i = 0; i < files.size(); i++)
    {
      delete (files[i]);
    }
  perfmeta_final ();

  return 0;
}
