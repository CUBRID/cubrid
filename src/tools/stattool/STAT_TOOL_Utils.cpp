//
// Created by paul on 21.03.2017.
//

#include "STAT_TOOL_Utils.hpp"

int Utils::nStatValues;

int
Utils::getNStatValues()
{
  return Utils::nStatValues;
}

void
Utils::setNStatValues (int n)
{
  Utils::nStatValues = n;
}