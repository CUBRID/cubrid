#include <iostream>
#include "STAT_TOOL_Utils.hpp"
#include "STAT_TOOL_SnapshotSet.hpp"
#include "STAT_TOOL_ShowExecutor.hpp"
#include "STAT_TOOL_PlotExecutor.hpp"
#include "STAT_TOOL_LoadExecutor.hpp"
#include "STAT_TOOL_ErrorManager.hpp"
#include "STAT_TOOL_AggregateExecutor.hpp"
#if defined (WINDOWS)
#include <windows.h>
#endif

int
main (int argc, char **argv)
{
  bool quit = false;
  char buffer[MAX_COMMAND_SIZE];
  std::string command;
  CommandExecutor *executor = NULL;
  ErrorManager::ErrorCode error;

  Utils::init ();

  while (!quit)
    {
      fgets (buffer, MAX_COMMAND_SIZE, stdin);
      buffer[strlen (buffer) - 1] = '\0';         /* eliminates the ending "\n" that fgets adds */
      if (strlen (buffer) == 0)
        {
          continue;
        }
      command = std::string (buffer);
      error = Utils::processCommand (command, executor, quit);

      if (quit)
        {
          /* quit program */
          break;
        }

      if (error != ErrorManager::NO_ERRORS || executor == NULL)
        {
          /* error or nothing to execute */
          continue;
        }

      error = executor->parseCommandAndInit ();
      if (error != ErrorManager::NO_ERRORS)
        {
	  printf ("Usage: ");
	  executor->printUsage ();
        }
      else
        {
          (void) executor->execute ();
        }
    }

  if (executor != NULL)
    {
      delete executor;
    }

  Utils::final ();
  return 0;
}
