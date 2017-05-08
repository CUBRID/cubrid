//
// Created by paul on 21.03.2017.
//

#include "STAT_TOOL_Utils.hpp"

std::vector<StatToolSnapshotSet *> Utils::loadedSets;
const std::vector<std::pair<std::string, std::string> > Utils::possibleArguments = Utils::initPossibleArguments();

std::vector<std::pair<std::string, std::string> > Utils::initPossibleArguments()
{
  std::vector<std::pair<std::string, std::string> > arguments;
  arguments.push_back (std::make_pair ("-c", "stattool -c filename_1 alias_1 ... filename_n alias_n \"command\""));

  return arguments;
}

void Utils::init () {
  perfmeta_init();
}
void Utils::final () {
  loadedSets.clear ();
  perfmeta_final ();
}

StatToolSnapshot *
Utils::findSnapshotInLoadedSets (const char *alias)
{
  StatToolSnapshot *snapshot = NULL, *secondSnapshot = NULL;
  char *first, *second;
  char *aliasCopy;
  first = second = NULL;

  if (alias == NULL) {
    return NULL;
  }
  aliasCopy = strdup (alias);

  if (strchr (alias, '-') != NULL)
    {
      first = strtok (aliasCopy, "-");
      second = strtok (NULL, " ");
    }
  else
    {
      first = aliasCopy;
    }

  for (unsigned int j = 0; j < loadedSets.size(); j++)
    {
      if ((snapshot = loadedSets[j]->getSnapshotByArgument (first)) != NULL)
        {
          break;
        }
    }

  if (!snapshot)
    {
      return NULL;
    }

  if (second != NULL)
    {
      for (unsigned int j = 0; j < loadedSets.size(); j++)
        {
          if ((secondSnapshot = loadedSets[j]->getSnapshotByArgument (second)) != NULL)
            {
              return snapshot->difference (secondSnapshot);
            }
        }
    }
  else
    {
      return snapshot;
    }

  free (aliasCopy);
  return NULL;
}

void
Utils::printHelp ()
{
  printf ("Available commands:\n");
  printf("\n%s", LoadExecutor::USAGE);
  printf("\n%s", PlotExecutor::USAGE);
  printf("\n%s", ShowExecutor::USAGE);
  printf("\n%s", AggregateExecutor::USAGE);
}

ErrorManager::ErrorCode
Utils::processArguments (char **argv, int argc, bool &quit)
{
  std::string command = "";
  ErrorManager::ErrorCode error = ErrorManager::NO_ERRORS;
  CommandExecutor *executor = NULL;

  if (argc < 3) {
    return ErrorManager::NOT_ENOUGH_ARGUMENTS_ERROR;
  }

  if (strcmp(argv[1], COMMAND_ARGUMENT) == 0)
  {
    quit = true;

    for (int i = 2; i < argc - 1; i+=2) {
      command = "";
      command += argv[i];
      command += " ";
      command += argv[i+1];
      LoadExecutor loadExecutor(command);
      error = loadExecutor.parseCommandAndInit ();
      if (error != ErrorManager::NO_ERRORS) {
	return error;
      } else {
	error = loadExecutor.execute ();
	if (error != ErrorManager::NO_ERRORS) {
	  return error;
	}
      }
    }

    error = Utils::processCommand (std::string(argv[argc-1]), executor, quit);
    if (error != ErrorManager::NO_ERRORS || executor == NULL)
    {
      return error;
    }

    error = executor->parseCommandAndInit ();
    if (error != ErrorManager::NO_ERRORS)
    {
      return error;
    }
    else
    {
      error = executor->execute ();
    }
  }

  return error;
}

ErrorManager::ErrorCode
Utils::processCommand (const std::string &command, CommandExecutor *&executor, bool &quit)
{
  std::string::size_type spacePosition;
  std::string commandKeyword;
  std::string arguments;

  spacePosition = command.find (" ");
  commandKeyword = command.substr (0, spacePosition);
  arguments = command.substr (spacePosition + 1);

  if (commandKeyword.compare ("load") == 0)
  {
    executor = new LoadExecutor (arguments);
  }
  else if (commandKeyword.compare ("show") == 0)
  {
    executor = new ShowExecutor (arguments);
  }
  else if (commandKeyword.compare ("plot") == 0)
  {
    executor = new PlotExecutor (arguments);
  }
  else if (commandKeyword.compare ("aggregate") == 0)
  {
    executor = new AggregateExecutor (arguments);
  }
  else if (commandKeyword.compare ("help") == 0)
  {
    printHelp ();
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