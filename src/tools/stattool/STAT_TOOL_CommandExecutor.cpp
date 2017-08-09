//
// Created by paul on 28.03.2017.
//

#include "STAT_TOOL_CommandExecutor.hpp"

CommandExecutor::CommandExecutor (std::string &wholeCommand)
{
  std::istringstream ss (wholeCommand);
  std::string token;

  while (std::getline (ss, token, ' '))
    {
      arguments.push_back (token);
    }
}

CommandExecutor::~CommandExecutor()
{

}