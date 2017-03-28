//
// Created by paul on 28.03.2017.
//

#include "LoadExecutor.hpp"

LoadExecutor::LoadExecutor (std::string &wholeCommand,
                            std::vector<StatisticsFile *> &files) : CommandExecutor (wholeCommand, files)
{
}

bool LoadExecutor::parseCommandAndInit()
{
  if (arguments.size() < 2)
    {
      return false;
    }

  filename = arguments[0];
  alias = arguments[1];

  return true;
}

bool LoadExecutor::execute()
{
  StatisticsFile *newFile = new StatisticsFile (std::string (filename), std::string (alias));

  bool hasSucceded = newFile->readFileAndInit();
  if (hasSucceded)
    {
      files.push_back (newFile);
    }

  return hasSucceded;
}

LoadExecutor::~LoadExecutor()
{

}