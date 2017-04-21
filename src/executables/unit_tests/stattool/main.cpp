#include <iostream>
#include "UnitTests.hpp"

enum TestId
{
  LOAD_BAD_FILE = 0,
  LOAD_GOOD_FILE,
  LOAD_GOOD_FILE_CHECK_ALIAS,
  LOAD_GOOD_FILE_CHECK_SNAPSHOTS,
  LOAD_NO_ALIAS,
  PLOT_MISSING_ARGUMENTS,
  PLOT_INVALID_ALIAS,
  PLOT_HAPPY_PATH,
  SHOW_INVALID_ALIAS,
  SHOW_HAPPY_PATH,
  AGGREGATE_MISSING_ARGUMENTS,
  AGGREGATE_HAPPY_PATH,
  STATFILE_CLASS_TEST1,
  STATFILE_CLASS_TEST2,
  STATFILE_CLASS_TEST3,

  TESTS_COUNT
};

struct Test
{
  TestId id;
  const char *name;
  bool (*f_test) (void);
};

Test tests[] =
{
  {LOAD_BAD_FILE, "LOAD_BAD_FILE", UnitTests::testLoad_BadFile},
  {LOAD_GOOD_FILE, "LOAD_GOOD_FILE", UnitTests::testLoad_GoodFile},
  {LOAD_NO_ALIAS, "LOAD_NO_ALIAS", UnitTests::testLoad_noAlias},
  {LOAD_GOOD_FILE_CHECK_ALIAS, "LOAD_CHECK_ALIAS", UnitTests::testLoad_GoodFile_checkAlias},
  {LOAD_GOOD_FILE_CHECK_SNAPSHOTS, "LOAD_CHECK_SNAPSHOTS", UnitTests::testLoad_GoodFile_checkSnapshots},
  {STATFILE_CLASS_TEST1, "STATFILE_CLASS_TEST1", UnitTests::testStatFileClass_Test1},
  {STATFILE_CLASS_TEST2, "STATFILE_CLASS_TEST2", UnitTests::testStatFileClass_Test2},
  {STATFILE_CLASS_TEST3, "STATFILE_CLASS_TEST3", UnitTests::testStatFileClass_Test3},
  {PLOT_MISSING_ARGUMENTS, "PLOT_MISSING_ARGUMENTS", UnitTests::testPlot_MissingArguments},
  {PLOT_INVALID_ALIAS, "PLOT_INVALID_ALIAS", UnitTests::testPlot_InvalidAlias},
  {PLOT_HAPPY_PATH, "PLOT_HAPPY_PATH", UnitTests::testPlot_CreatePlot},
  {SHOW_INVALID_ALIAS, "SHOW_INVALID_ALIAS", UnitTests::testShow_InvalidAlias},
  {SHOW_HAPPY_PATH, "SHOW_HAPPY_PATH", UnitTests::testShow_HappyPath},
  {AGGREGATE_MISSING_ARGUMENTS, "AGGREGATE_MISSING_ARGUMENTS", UnitTests::testAggregate_MissingArguments},
  {AGGREGATE_HAPPY_PATH, "AGGREGATE_HAPPY_PATH", UnitTests::testAggregate_HappyPath}
};

int main (int argc, char **argv)
{
  std::vector<unsigned int> failedTests;
  UnitTests::initTestEnvironment ();

  UnitTests::disableStdout ();
  for (unsigned int i = 0; i < TESTS_COUNT; i++)
    {
      if (!tests[i].f_test())
        {
          failedTests.push_back (i);
        }
    }
  UnitTests::enableStdout ();

  if (failedTests.size () > 0)
    {
      for (unsigned int i = 0; i < failedTests.size(); i++)
        {
          std::cout << tests[failedTests[i]].name << " failed!" << std::endl;
        }
    }
  else
    {
      std::cout << "All tests passed!" << std::endl;
    }

  UnitTests::destroyTestEnvironment ();
  return 0;
}