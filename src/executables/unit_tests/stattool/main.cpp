#include <iostream>
#include "UnitTests.hpp"

#define SIMPLE_TEST(id, name, f_test) {id, name, f_test, UnitTests::simpleCleanUp}

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
  void (*f_clean) (void);
};

Test tests[] =
{
  SIMPLE_TEST (LOAD_BAD_FILE, "LOAD_BAD_FILE", UnitTests::testLoad_BadFile),
  SIMPLE_TEST (LOAD_GOOD_FILE, "LOAD_GOOD_FILE", UnitTests::testLoad_GoodFile),
  SIMPLE_TEST (LOAD_NO_ALIAS, "LOAD_NO_ALIAS", UnitTests::testLoad_noAlias),
  SIMPLE_TEST (LOAD_GOOD_FILE_CHECK_ALIAS, "LOAD_CHECK_ALIAS", UnitTests::testLoad_GoodFile_checkAlias),
  SIMPLE_TEST (LOAD_GOOD_FILE_CHECK_SNAPSHOTS, "LOAD_CHECK_SNAPSHOTS", UnitTests::testLoad_GoodFile_checkSnapshots),
  SIMPLE_TEST (STATFILE_CLASS_TEST1, "STATFILE_CLASS_TEST1", UnitTests::testStatFileClass_Test1),
  SIMPLE_TEST (STATFILE_CLASS_TEST2, "STATFILE_CLASS_TEST2", UnitTests::testStatFileClass_Test2),
  SIMPLE_TEST (STATFILE_CLASS_TEST3, "STATFILE_CLASS_TEST3", UnitTests::testStatFileClass_Test3),
  SIMPLE_TEST (PLOT_MISSING_ARGUMENTS, "PLOT_MISSING_ARGUMENTS", UnitTests::testPlot_MissingArguments),
  SIMPLE_TEST (PLOT_INVALID_ALIAS, "PLOT_INVALID_ALIAS", UnitTests::testPlot_InvalidAlias),
  SIMPLE_TEST (PLOT_HAPPY_PATH, "PLOT_HAPPY_PATH", UnitTests::testPlot_CreatePlot),
  SIMPLE_TEST (SHOW_INVALID_ALIAS, "SHOW_INVALID_ALIAS", UnitTests::testShow_InvalidAlias),
  SIMPLE_TEST (SHOW_HAPPY_PATH, "SHOW_HAPPY_PATH", UnitTests::testShow_HappyPath),
  SIMPLE_TEST (AGGREGATE_MISSING_ARGUMENTS, "AGGREGATE_MISSING_ARGUMENTS", UnitTests::testAggregate_MissingArguments),
  SIMPLE_TEST (AGGREGATE_HAPPY_PATH, "AGGREGATE_HAPPY_PATH", UnitTests::testAggregate_HappyPath)
};

int
main (int argc, char **argv)
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
      tests[i].f_clean();
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