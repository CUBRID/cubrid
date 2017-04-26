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
  perfmeta_init ();
}

void
UnitTests::destroyTestEnvironment ()
{
  perfmeta_final ();
}

bool
UnitTests::testLoad_BadFile()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string command = "invalid_file.bin a";
  LoadExecutor *executor = new LoadExecutor (command, files);

  if (executor->parseCommandAndInit () != ErrorManager::NO_ERRORS)
    {
      return false;
    }

  return executor->execute () == ErrorManager::OPEN_FILE_ERROR;
}

bool
UnitTests::testLoad_GoodFile()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string command = "good.bin a";
  LoadExecutor *executor = new LoadExecutor (command, files);

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
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          return files[0]->getAlias ().compare (alias) == 0;
        }
    }
}

bool
UnitTests::testLoad_GoodFile_checkSnapshots ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          bool check = true;
          for (unsigned int i = 0; i < files[0]->getSnapshots ().size(); i++)
            {
              check &= (mktime (&files[0]->getSnapshots ()[i]->timestamp) - files[0]->getRelativeSeconds () == SECONDS_GAP * (i+1));
            }
          return files[0]->getSnapshots ().size() == NUM_OF_SNAPSHOTS && check;
        }
    }
}

bool
UnitTests::testLoad_noAlias ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string command = "file.bin ";
  LoadExecutor *executor = new LoadExecutor (command, files);

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
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  StatToolSnapshot *snapshot;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          bool check = true;
          snapshot = files[0]->getSnapshotBySeconds (150);
          check &= mktime (&snapshot->timestamp) - files[0]->getRelativeSeconds () == 200;
          snapshot = files[0]->getSnapshotBySeconds (0);
          check &= mktime (&snapshot->timestamp) - files[0]->getRelativeSeconds () == SECONDS_GAP;
          snapshot = files[0]->getSnapshotBySeconds (10000);
          check &= mktime (&snapshot->timestamp) - files[0]->getRelativeSeconds () == NUM_OF_SNAPSHOTS * SECONDS_GAP;
          return check;
        }
    }
}

bool
UnitTests::testStatFileClass_Test2()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  int index;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          bool check = true;
          index = files[0]->getSnapshotIndexBySeconds (150);
          check &= (mktime (&files[0]->getSnapshots ()[index]->timestamp) - files[0]->getRelativeSeconds ()) == 200;
          index = files[0]->getSnapshotIndexBySeconds (0);
          check &= (mktime (&files[0]->getSnapshots ()[index]->timestamp) - files[0]->getRelativeSeconds ()) == SECONDS_GAP;
          index = files[0]->getSnapshotIndexBySeconds (100000);
          check &= (mktime (&files[0]->getSnapshots ()[index]->timestamp) - files[0]->getRelativeSeconds ()) == SECONDS_GAP *
                   NUM_OF_SNAPSHOTS;
          return check;
        }
    }
}

bool
UnitTests::testStatFileClass_Test3()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  StatToolSnapshot *snapshot1, *snapshot2, *snapshot3;
  int index1, index2;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          bool check = true;
          files[0]->getIndicesOfSnapshotsByArgument ("b(100)", index1, index2);
          check &= (index1 == -1 && index2 == -1);
          files[0]->getIndicesOfSnapshotsByArgument ("a(150)", index1, index2);
          check &= (mktime (&files[0]->getSnapshots ()[index2]->timestamp) - files[0]->getRelativeSeconds ()) == 200;
          check &= (index1 == 0);
          files[0]->getIndicesOfSnapshotsByArgument ("a(150-670)", index1, index2);
          check &= (mktime (&files[0]->getSnapshots ()[index1]->timestamp) - files[0]->getRelativeSeconds ()) == 200;
          check &= (mktime (&files[0]->getSnapshots ()[index2]->timestamp) - files[0]->getRelativeSeconds ()) == 700;

          snapshot1 = files[0]->getSnapshotByArgument ("a(420)");
          check &= (mktime (&snapshot1->timestamp) - files[0]->getRelativeSeconds ()) == 500;

          snapshot1 = files[0]->getSnapshotByArgument ("a(120)");
          snapshot2 = files[0]->getSnapshotByArgument ("a(410)");
          snapshot3 = files[0]->getSnapshotByArgument ("a(120-410)");

          for (int i = 0; i < perfmeta_get_values_count (); i++)
            {
              if (snapshot3->rawStats[i] != snapshot1->rawStats[i] - snapshot2->rawStats[i])
                {
                  check = false;
                  break;
                }
            }

          return check;
        }
    }
}

bool
UnitTests::testPlot_MissingArguments ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          command = "-a b";
          PlotExecutor *plotExecutor = new PlotExecutor (command, files);
          bool check =  plotExecutor->parseCommandAndInit () == ErrorManager::MISSING_ARGUMENT_ERROR;
          delete plotExecutor;
          return check;
        }
    }
}

bool
UnitTests::testPlot_InvalidAlias ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          command = "-a b -i (100) -v Num_file_iowrites";
          PlotExecutor *plotExecutor = new PlotExecutor (command, files);
          bool check = plotExecutor->parseCommandAndInit () == ErrorManager::INVALID_ALIASES_ERROR;
          delete plotExecutor;
          return check;
        }
    }
}

bool
UnitTests::testPlot_CreatePlot ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          struct stat st;
          bool check1, check2;
          command = "-a a -i (0-700) -v Num_file_iowrites";
          PlotExecutor *plotExecutor = new PlotExecutor (command, files);
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
}

bool
UnitTests::testShow_InvalidAlias ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          bool check;
          command = "b(100) c d(100-300) e f x";
          ShowExecutor *showExecutor = new ShowExecutor (command, files);
          check = showExecutor->parseCommandAndInit () == ErrorManager::INVALID_ALIASES_ERROR;
          delete showExecutor;
          return check;
        }
    }
}

bool
UnitTests::testShow_HappyPath ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          bool check;
          command = "a a(100) a(100-500)";
          ShowExecutor *showExecutor = new ShowExecutor (command, files);
          check = showExecutor->parseCommandAndInit () == ErrorManager::NO_ERRORS;
          delete showExecutor;
          return check;
        }
    }
}

bool
UnitTests::testAggregate_MissingArguments ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          bool check;
          command = "-n Num_data_page_fix_ext -d 4";
          AggregateExecutor *aggregateExecutor = new AggregateExecutor (command, files);
          check = aggregateExecutor->parseCommandAndInit () == ErrorManager::INVALID_ARGUMENT_ERROR;
          delete aggregateExecutor;
          return check;
        }
    }
}

bool
UnitTests::testAggregate_HappyPath ()
{
  std::vector<StatToolSnapshotSet *> files;
  std::string filename = "good.bin";
  std::string alias = "a";
  std::string command = filename + " " + alias;
  const int NUM_OF_SNAPSHOTS = 10;
  const int SECONDS_GAP = 100;
  LoadExecutor *executor = new LoadExecutor (command, files);

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
      if (files.size () != 1)
        {
          return false;
        }
      else
        {
          struct stat st;
          bool check1, check2;
          command = "-a a -n Num_data_page_fix_ext -d 4";
          AggregateExecutor *aggregateExecutor = new AggregateExecutor (command, files);
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