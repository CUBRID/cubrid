//
// Created by paul on 20.04.2017.
//

#include "UnitTests.hpp"

int UnitTests::stdoutBackupFd;
FILE *UnitTests::nullOut;
bool UnitTests::isStdoutEnabled = true;

void
UnitTests::initTestEnvironment ()
{
  Utils::init ();
}

void
UnitTests::destroyTestEnvironment ()
{
  Utils::final ();
}

bool
UnitTests::testLoad_BadFile()
{
  std::string command = "invalid_file.bin a";
  LoadExecutor *executor = new LoadExecutor (command);

  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  return executor->execute () == ErrorManager::OPEN_FILE_ERROR;
}

bool
UnitTests::testLoad_GoodFile()
{
  std::string command = "good.bin a";
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile ("good.bin", 1, 0);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile ("good.bin");
  delete executor;

  return code == ErrorManager::NO_ERRORS;
}

bool
UnitTests::testLoad_GoodFile_checkAlias ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, 1, 0);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          return Utils::loadedSets[0]->getAlias ().compare (alias) == 0;
        }
    }
}

bool
UnitTests::testLoad_GoodFile_checkSnapshots ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          bool check = true;
          for (unsigned int i = 0; i < Utils::loadedSets[0]->getSnapshots ().size(); i++)
            {
              check &= (mktime (&Utils::loadedSets[0]->getSnapshots ()[i]->timestamp) - Utils::loadedSets[0]->getRelativeSeconds () == SECONDS_GAP * (i+1));
            }
          return Utils::loadedSets[0]->getSnapshots ().size() == NUM_OF_SNAPSHOTS && check;
        }
    }
}

bool
UnitTests::testLoad_noAlias ()
{
  std::string command = "file.bin ";
  LoadExecutor *executor = new LoadExecutor (command);

  return executor->parseCommandAndInit () == ErrorManager::NOT_ENOUGH_ARGUMENTS_ERROR;
}

void
UnitTests::removeFile (const std::string &filename)
{
  int rc = remove (filename.c_str());
  assert (rc == 0);
}

void
UnitTests::createStatFile (const std::string &filename, int numOfSnapshots, int secondsGap)
{
  char *packed_stats;
  FILE *binFile = fopen (filename.c_str(), "wb");
  UINT64 *stats = NULL;
  time_t current_time = time (NULL);
  INT64 portable_time, swapped_time;
  assert (binFile != NULL);

  portable_time = (INT64) current_time;
  OR_PUT_INT64 (&swapped_time, &portable_time);
  fwrite (&swapped_time, sizeof (INT64), 1, binFile);

  for (int i = 0; i < numOfSnapshots; i++)
    {
      portable_time += secondsGap;
      OR_PUT_INT64 (&swapped_time, &portable_time);
      fwrite (&swapped_time, sizeof (INT64), 1, binFile);

      stats = generateRandomStats (10);

      packed_stats = (char *) malloc (sizeof (UINT64) * perfmeta_get_values_count ());
      perfmon_pack_stats (packed_stats, stats);
      fwrite (packed_stats, sizeof (UINT64), (size_t) perfmeta_get_values_count (), binFile);

      free (stats);
    }
}

bool
UnitTests::testStatFileClass_Test1()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  StatToolSnapshot *snapshot;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          bool check = true;
          snapshot = Utils::loadedSets[0]->getSnapshotBySeconds (150);
          check &= mktime (&snapshot->timestamp) - Utils::loadedSets[0]->getRelativeSeconds () == 200;
          snapshot = Utils::loadedSets[0]->getSnapshotBySeconds (0);
          check &= mktime (&snapshot->timestamp) - Utils::loadedSets[0]->getRelativeSeconds () == SECONDS_GAP;
          snapshot = Utils::loadedSets[0]->getSnapshotBySeconds (10000);
          check &= mktime (&snapshot->timestamp) - Utils::loadedSets[0]->getRelativeSeconds () == NUM_OF_SNAPSHOTS * SECONDS_GAP;
          return check;
        }
    }

  return false;
}

bool
UnitTests::testStatFileClass_Test2()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  int index;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          bool check = true;
          index = Utils::loadedSets[0]->getSnapshotIndexBySeconds (150);
          check &= (mktime (&Utils::loadedSets[0]->getSnapshots ()[index]->timestamp) - Utils::loadedSets[0]->getRelativeSeconds ()) == 200;
          index = Utils::loadedSets[0]->getSnapshotIndexBySeconds (0);
          check &= (mktime (&Utils::loadedSets[0]->getSnapshots ()[index]->timestamp) - Utils::loadedSets[0]->getRelativeSeconds ()) == SECONDS_GAP;
          index = Utils::loadedSets[0]->getSnapshotIndexBySeconds (100000);
          check &= (mktime (&Utils::loadedSets[0]->getSnapshots ()[index]->timestamp) - Utils::loadedSets[0]->getRelativeSeconds ()) == SECONDS_GAP *
                   NUM_OF_SNAPSHOTS;
          return check;
        }
    }
  return false;

}

bool
UnitTests::testStatFileClass_Test3()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  StatToolSnapshot *snapshot1, *snapshot2, *snapshot3;
  int index1, index2;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          bool check = true;
          Utils::loadedSets[0]->getIndicesOfSnapshotsByArgument ("b(100)", index1, index2);
          check &= (index1 == -1 && index2 == -1);
          Utils::loadedSets[0]->getIndicesOfSnapshotsByArgument ("a(150)", index1, index2);
          check &= (mktime (&Utils::loadedSets[0]->getSnapshots ()[index2]->timestamp) - Utils::loadedSets[0]->getRelativeSeconds ()) == 200;
          check &= (index1 == 0);
          Utils::loadedSets[0]->getIndicesOfSnapshotsByArgument ("a(150-670)", index1, index2);
          check &= (mktime (&Utils::loadedSets[0]->getSnapshots ()[index1]->timestamp) - Utils::loadedSets[0]->getRelativeSeconds ()) == 200;
          check &= (mktime (&Utils::loadedSets[0]->getSnapshots ()[index2]->timestamp) - Utils::loadedSets[0]->getRelativeSeconds ()) == 700;

          snapshot1 = Utils::loadedSets[0]->getSnapshotByArgument ("a(420)");
          check &= (mktime (&snapshot1->timestamp) - Utils::loadedSets[0]->getRelativeSeconds ()) == 500;

          return check;
        }
    }
  return false;
}

bool
UnitTests::testPlot_MissingArguments ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          command = "-a b";
          PlotExecutor *plotExecutor = new PlotExecutor (command);
          bool check =  plotExecutor->parseCommandAndInit () == ErrorManager::MISSING_ARGUMENT_ERROR;
          delete plotExecutor;
          return check;
        }
    }
  return false;
}

bool
UnitTests::testPlot_InvalidAlias ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          command = "-a b -i (100) -v Num_file_iowrites";
          PlotExecutor *plotExecutor = new PlotExecutor (command);
          bool check = plotExecutor->parseCommandAndInit () == ErrorManager::INVALID_ALIASES_ERROR;
          delete plotExecutor;
          return check;
        }
    }
  return false;
}

bool
UnitTests::testPlot_CreatePlot ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          struct stat st;
          bool check1, check2;
          command = "-a a -i (0-700) -v Num_file_iowrites";
          PlotExecutor *plotExecutor = new PlotExecutor (command);
          check1 = plotExecutor->parseCommandAndInit () == ErrorManager::NO_ERRORS;
          if (check1)
            {
              check2 = plotExecutor->execute () == ErrorManager::NO_ERRORS;
              check2 &= stat ("plot.png", &st) == 0;
            }
          else
            {
              delete plotExecutor;
              return false;
            }
          removeFile ("plot.png");
          delete plotExecutor;
          return check2;
        }
    }
  return false;
}

bool
UnitTests::testShow_InvalidAlias ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          bool check;
          command = "b(100) c d(100-300) e f x";
          ShowExecutor *showExecutor = new ShowExecutor (command);
          check = showExecutor->parseCommandAndInit () == ErrorManager::INVALID_ALIASES_ERROR;
          delete showExecutor;
          return check;
        }
    }
  return false;
}

bool
UnitTests::testShow_HappyPath ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          bool check;
          command = "a a(100) a(100-500)";
          ShowExecutor *showExecutor = new ShowExecutor (command);
          check = showExecutor->parseCommandAndInit () == ErrorManager::NO_ERRORS;
          delete showExecutor;
          return check;
        }
    }
  return false;
}

bool
UnitTests::testAggregate_MissingArguments ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          bool check;
          command = "-n Num_data_page_fix_ext -d 4";
          AggregateExecutor *aggregateExecutor = new AggregateExecutor (command);
          check = aggregateExecutor->parseCommandAndInit () == ErrorManager::INVALID_ARGUMENT_ERROR;
          delete aggregateExecutor;
          return check;
        }
    }
  return false;
}

bool
UnitTests::testAggregate_HappyPath ()
{
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command);

  createStatFile (filename, NUM_OF_SNAPSHOTS, SECONDS_GAP);
  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  ErrorManager::ErrorCode code = executor->execute ();
  removeFile (filename);
  delete executor;

  if (code != ErrorManager::NO_ERRORS)
    {
      return false;
    }
  else
    {
      if (Utils::loadedSets.size () != 1)
        {
          return false;
        }
      else
        {
          struct stat st;
          bool check1, check2;
          command = "-a a -n Num_data_page_fix_ext -d 4";
          AggregateExecutor *aggregateExecutor = new AggregateExecutor (command);
          check1 = aggregateExecutor->parseCommandAndInit () == ErrorManager::NO_ERRORS;
          if (check1)
            {
              check2 = aggregateExecutor->execute () == ErrorManager::NO_ERRORS;
              check2 &= stat ("aggregate_plot.jpg", &st) == 0;
            }
          else
            {
              delete aggregateExecutor;
              return false;
            }
          removeFile ("aggregate_plot.jpg");
          delete aggregateExecutor;
          return check2;
        }
    }
  return false;
}

void
UnitTests::disableStdout()
{
  if (isStdoutEnabled)
    {
      stdoutBackupFd = CROSS_DUP (STDOUT_FILENO);
      fflush (stdout);
      nullOut = fopen (NULL_FILENAME, "w");
      CROSS_DUP2 (fileno (nullOut), STDOUT_FILENO);
      isStdoutEnabled = false;
    }
}

void
UnitTests::enableStdout()
{
  if (!isStdoutEnabled)
    {
      fflush (stdout);
      fclose (nullOut);
      CROSS_DUP2 (stdoutBackupFd, STDOUT_FILENO);
      close (stdoutBackupFd);
      isStdoutEnabled = true;
    }
}

UINT64 *
UnitTests::generateRandomStats (long long max)
{
  UINT64 *stats = (UINT64 *) malloc (perfmeta_get_values_memsize ());

  for (int i = 0; i < perfmeta_get_values_count (); i++)
    {
      stats[i] = (UINT64)rand() % max;
    }

  return stats;
}

void
UnitTests::simpleCleanUp () {
  Utils::loadedSets.clear ();
}