#include "STAT_TOOL_ErrorManager.hpp"

void ErrorManager::printErrorMessage (ErrorCode ec, std::string userMessage)
{
  if (ec < NUM_ERRORS)
    {
      std::cout << errorsMap[ec].message << " " << userMessage << std::endl;
    }
}

const ErrorManager::ErrorMessage ErrorManager::errorsMap[] = {{NO_ERRORS, ""},
  {OPEN_FILE_ERROR, "Unable to open file!"},
  {CREATE_FILE_ERROR, "Unable to create file!"},
  {OPEN_PIPE_ERROR, "Unable to open pipe!"},
  {MISSING_ARGUMENT_ERROR, "Missing argument!"},
  {NOT_ENOUGH_ARGUMENTS_ERROR, "Not enough arguments were provided!"},
  {INVALID_COMMAND_ERROR, "The provided command is invalid!"},
  {MISSING_TIMESTAMP_ERROR, "Timestamp was not found in the provided file!"},
  {INVALID_ALIASES_ERROR, "Invalid provided aliases!"},
  {INVALID_ARGUMENT_ERROR, "The folowing argument is invalid: "}
};