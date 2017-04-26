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

extern "C" {
#include <perf_metadata.h>
#include <string.h>
#include <porting.h>
}

std::vector<StatToolSnapshotSet *> stattool_Loaded_sets;

void
init ()
{
  perfmeta_init();
}

void
final ()
{
  stattool_Loaded_sets.clear ();
  perfmeta_final ();
}

void
print_help ()
{
  printf ("Available commands:\n");
  printf ("\t-load <filename> <alias>\n");
  printf ("\t-plot <OPTIONS>\n\t\tvalid options:\n");
  printf ("\t\t-a <alias1, alias2...>\n");
  printf ("\t\t-i <INTERVAL>\n");
  printf ("\t\t-v <VARIABLE>\n");
  printf ("\t\t-f <PLOT FILENAME>\n");
  printf ("\t-show <alias[(X-Y)][/alias[(Z-W)]]>...\n");
  printf ("\t-aggregate <OPTIONS>\n\t\tvalid options:\n");
  printf ("\t\t-a <alias>\n");
  printf ("\t\t-n <name>\n");
  printf ("\t\t-d <fixed dimension>\n");
  printf ("\t\t-f <plot filename> DEFAULT: aggregate_plot.jpg\n");
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
    executor = new LoadExecutor (arguments, stattool_Loaded_sets);
  }
  else if (commandKeyword.compare ("show") == 0)
  {
    executor = new ShowExecutor (arguments, stattool_Loaded_sets);
  }
  else if (commandKeyword.compare ("plot") == 0)
  {
    executor = new PlotExecutor (arguments, stattool_Loaded_sets);
  }
  else if (commandKeyword.compare ("aggregate") == 0)
  {
    executor = new AggregateExecutor (arguments, stattool_Loaded_sets);
  }
  else if (commandKeyword.compare ("help") == 0)
  {
    print_help ();
    executor = NULL;
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

      if (error != ErrorManager::NO_ERRORS || executor == NULL) {
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
