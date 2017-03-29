//
// Created by paul on 29.03.2017.
//

#include "ErrorManager.hpp"

std::vector<unordered_map<unsigned int, std::string>* > ErrorManager::initMessages()
{
  char buf[MAX_LINE_LEN];
  FILE *msgFp = fopen (MESSAGES_PATH, "r");
  int currentHashmap = 0;
  std::vector<unordered_map<unsigned int, std::string>* > msgMap;
  unsigned int index;
  char msg[MAX_LINE_LEN];

  msgMap.reserve (NUM_ERRORS);

  while (fgets (buf, MAX_LINE_LEN, msgFp))
    {
      buf[strlen (buf) - 1] = '\0';
      if (buf[0] == '#')
        {
          sscanf (buf, "#%d", &currentHashmap);
          unordered_map<unsigned int, std::string> *map = new unordered_map<unsigned int, std::string>();
          msgMap[currentHashmap] = map;
        }
      else
        {
          sscanf (buf, "%d \"%[^\"]\"", &index, msg);
          /* msgMap[currentHashmap]->insert (std::make_pair<unsigned int, std::string> (index, std::string (msg)));
           *
           * does not compile with C++11
           *
           * http://en.cppreference.com/w/cpp/utility/pair/make_pair
           *
           * let's avoid using make_pair. */
        }
    }
  return msgMap;
}

const std::vector<unordered_map<unsigned int, std::string>* > ErrorManager::MESSAGES = initMessages ();

void ErrorManager::printErrorMessage (ErrorCode ec, ErrorMessage em, std::string userMessage)
{
  unordered_map<unsigned int, std::string>::const_iterator msgElement = MESSAGES[ec]->find (em);

  if (msgElement != MESSAGES[ec]->end())
    {
      std::cout << msgElement->second << " " << userMessage << std::endl;
    }
}
