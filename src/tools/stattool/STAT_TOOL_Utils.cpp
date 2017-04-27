//
// Created by paul on 21.03.2017.
//

#include "STAT_TOOL_Utils.hpp"

std::vector<StatToolSnapshotSet *> Utils::loadedSets;

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
  first = second = NULL;
  char *aliasCopy = strdup (alias);

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