//
// Created by paul on 28.03.2017.
//

#include "STAT_TOOL_LoadExecutor.hpp"

LoadExecutor::LoadExecutor (std::string &wholeCommand,
                            std::vector<StatisticsFile *> &files) : CommandExecutor (wholeCommand, files)
{
}

ErrorManager::ErrorCode LoadExecutor::parseCommandAndInit()
{
  if (arguments.size() < 2)
    {
      ErrorManager::printErrorMessage (ErrorManager::NOT_ENOUGH_ARGUMENTS_ERROR, "");
      return ErrorManager::NOT_ENOUGH_ARGUMENTS_ERROR;
    }

  filename = arguments[0];
  alias = arguments[1];

  return ErrorManager::NO_ERRORS;
}

ErrorManager::ErrorCode LoadExecutor::execute()
{
  StatisticsFile *newFile = new StatisticsFile (std::string (filename), std::string (alias));

  ErrorManager::ErrorCode errorCode = newFile->readFileAndInit();
  if (errorCode == ErrorManager::NO_ERRORS)
    {
      files.push_back (newFile);
    }

  return errorCode;
}

void LoadExecutor::printUsage()
{
  printf ("usage: load <filename> <alias>\n");
}

LoadExecutor::~LoadExecutor()
{

}