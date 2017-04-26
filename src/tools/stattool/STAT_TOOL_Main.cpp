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

void
init ()
{
  perfmeta_init();
}

void
final ()
{
  files.clear ();
  perfmeta_final ();
}

ErrorManager::ErrorCode
process_command (const std::string& command, CommandExecutor *&executor, bool& quit)
{
  std::string::size_type spacePosition;
  std::string commandKeyword;
  std::string arguments;

  spacePosition = command.find (" ");
  commandKeyword = command.substr (0, spacePosition);
  arguments = command.substr (spacePosition + 1);

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
    ErrorManager::printErrorMessage (ErrorManager::INVALID_COMMAND_ERROR, "The command is: " + command);
    return ErrorManager::INVALID_COMMAND_ERROR;
  }

  return ErrorManager::NO_ERRORS;
}

int
main (int argc, char **argv)
{
  bool quit = false;
  char buffer[MAX_COMMAND_SIZE];
  std::string command;
  CommandExecutor *executor = NULL;
  ErrorManager::ErrorCode error;

  init();

  while (!quit)
    {
      fgets (buffer, MAX_COMMAND_SIZE, stdin);
      buffer[strlen (buffer) - 1] = '\0';         /* eliminates the ending "\n" that fgets adds */
      if (strlen (buffer) == 0)
        {
          continue;
        }
      command = std::string(buffer);
      error = process_command (command, executor, quit);

      if (error != ErrorManager::NO_ERRORS) {
	continue;
      }

      if (quit) {
	break;
      }

      error = executor->parseCommandAndInit ();
      if (error != ErrorManager::NO_ERRORS) {
	executor->printUsage ();
      }
      (void) executor->execute ();

      if (executor != NULL) {
	delete executor;
      }
    }

  final();
  return 0;
}
