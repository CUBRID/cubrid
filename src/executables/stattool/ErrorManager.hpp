//
// Created by paul on 29.03.2017.
//

#ifndef CUBRID_ERRORMANAGER_H
#define CUBRID_ERRORMANAGER_H

#define MAX_LINE_LEN 256
#define MESSAGES_PATH "~/cubrid/error_messages/en_US"

#include <vector>
#include <tr1/unordered_map>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>

using namespace std::tr1;

class ErrorManager
{
  public:
    enum ErrorCode
    {
      NO_ERRORS = 0,
      OPEN_ERROR,
      CREATE_ERROR,
      ARGUMENT_ERROR,
      CMD_ERROR,
      NUM_ERRORS
    };

    enum ErrorMessage
    {
      FILE_MSG = 0,
      PIPE_MSG,
      MISSING_ARGUMENT_MSG,
      NOT_ENOUGH_ARGUMENTS_MSG,
      INVALID_COMMAND,
      MISSING_TIMESTAMP,
      INVALID_ALIASES
    };

    static void printErrorMessage (ErrorCode ec, ErrorMessage em, std::string userMessage);
  private:
    static std::vector<unordered_map<unsigned int, std::string>* > initMessages();
    const static std::vector<unordered_map<unsigned int, std::string>* > MESSAGES;
};

#endif //CUBRID_ERRORMANAGER_H
