//
// Created by paul on 21.03.2017.
//

#include "FileNotFoundException.h"

FileNotFoundException::FileNotFoundException (const std::string &message) : runtime_error ("file not found"),
  message (message) {}

const char *FileNotFoundException::what() const throw()
{
  return message.c_str();
}