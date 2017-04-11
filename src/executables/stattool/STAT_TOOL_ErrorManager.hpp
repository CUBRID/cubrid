//
// Created by paul on 29.03.2017.
//

#ifndef CUBRID_ERRORMANAGER_H
#define CUBRID_ERRORMANAGER_H

#define MAX_LINE_LEN 256

#include <vector>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>

class ErrorManager
{
  public:
    enum ErrorCode
    {
      NO_ERRORS = 0,
      OPEN_FILE_ERROR,
      CREATE_FILE_ERROR,
      OPEN_PIPE_ERROR,
      MISSING_ARGUMENT_ERROR,
      NOT_ENOUGH_ARGUMENTS_ERROR,
      INVALID_COMMAND_ERROR,
      MISSING_TIMESTAMP_ERROR,
      INVALID_ALIASES_ERROR,
      INVALID_ARGUMENT_ERROR,
      NUM_ERRORS
    };

    struct ErrorMessage
    {
      ErrorCode code;
      const char *message;
    };

    static const ErrorMessage errorsMap[];
    static void printErrorMessage (ErrorCode ec, std::string userMessage);
};

#endif //CUBRID_ERRORMANAGER_H
