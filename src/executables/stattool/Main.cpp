#include <iostream>
#include "Utils.hpp"
#include "StatisticsFile.hpp"
#include "ShowExecutor.hpp"
#include "PlotExecutor.hpp"
#include "LoadExecutor.hpp"
#include "ErrorManager.hpp"
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
  std::vector<StatisticsFile *> files;
  metadata_initialize();
  init_name_offset_assoc();
  Utils::setNStatValues (pstat_Global.n_stat_values);

  do
    {
      std::string commandStr;
      CommandExecutor *executor = NULL;
      std::string::size_type spacePosition;
      std::string commandKeyword;
      std::string arguments;

      fgets (command, MAX_COMMAND_SIZE, stdin);
      command[strlen (command) - 1] = '\0';
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
      else if (commandKeyword.compare ("quit") == 0)
        {
          quit = true;
        }
      else
        {
          ErrorManager::printErrorMessage (ErrorManager::CMD_ERROR, ErrorManager::INVALID_COMMAND,
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
  free (pstat_Nameoffset);

  return 0;
}
