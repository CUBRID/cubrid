//
// Created by paul on 28.03.2017.
//

#include "STAT_TOOL_LoadExecutor.hpp"

LoadExecutor::LoadExecutor (std::string &wholeCommand,
                            std::vector<StatToolSnapshotSet *> &files) : CommandExecutor (wholeCommand, files)
{
}

ErrorManager::ErrorCode
LoadExecutor::parseCommandAndInit()
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

ErrorManager::ErrorCode
LoadExecutor::readFileAndInit(StatToolSnapshotSet *set)
{
  FILE *binary_fp = NULL;
  struct tm *timestamp;
  char strTime[80];
  INT64 seconds, unpacked_seconds;
  time_t epochSeconds;

  binary_fp = fopen (filename.c_str(), "rb");
  if (binary_fp == NULL)
  {
    ErrorManager::printErrorMessage (ErrorManager::OPEN_FILE_ERROR, "Filename: " + filename);
    return ErrorManager::OPEN_FILE_ERROR;
  }

  fread (&seconds, sizeof (INT64), 1, binary_fp);
  OR_GET_INT64 (&seconds, &unpacked_seconds);
  time_t relativeSeconds = (time_t)unpacked_seconds;
  set->setRelativeSeconds (relativeSeconds);
  timestamp = localtime (&relativeSeconds);
  if (timestamp == NULL)
  {
    ErrorManager::printErrorMessage (ErrorManager::MISSING_TIMESTAMP_ERROR, "");
    return ErrorManager::MISSING_TIMESTAMP_ERROR;
  }
  set->setRelativeTimestamp (*timestamp);

  strftime (strTime, 80, "%a %B %d %H:%M:%S %Y", timestamp);
  printf ("Relative Timestamp = %s\n", strTime);

  while (fread (&seconds, sizeof (INT64), 1, binary_fp) > 0)
  {
    char *unpacked_stats = (char *)malloc (sizeof (UINT64) * (size_t) perfmeta_get_values_count ());
    OR_GET_INT64 (&seconds, &unpacked_seconds);
    epochSeconds = (time_t)unpacked_seconds;
    StatToolSnapshot *snapshot = new StatToolSnapshot (epochSeconds);
    fread (unpacked_stats, sizeof (UINT64), (size_t) perfmeta_get_values_count (), binary_fp);
    perfmon_unpack_stats (unpacked_stats, snapshot->rawStats);
    set->getSnapshots().push_back (snapshot);
  }

  fclose (binary_fp);
  return ErrorManager::NO_ERRORS;
}

ErrorManager::ErrorCode
LoadExecutor::execute()
{
  StatToolSnapshotSet *newFile = new StatToolSnapshotSet (std::string (filename), std::string (alias));
  ErrorManager::ErrorCode errorCode = readFileAndInit(newFile);

  if (errorCode == ErrorManager::NO_ERRORS)
    {
      files.push_back (newFile);
    }

  return errorCode;
}

void
LoadExecutor::printUsage()
{
  printf ("usage: load <filename> <alias>\n");
}

LoadExecutor::~LoadExecutor()
{

}