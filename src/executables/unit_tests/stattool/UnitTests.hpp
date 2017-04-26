//
// Created by paul on 20.04.2017.
//

#ifndef CUBRID_UNITTESTS_H
#define CUBRID_UNITTESTS_H

extern "C" {
#include <stdlib.h>
#include <stdio.h>
};

#include "../../../tools/stattool/STAT_TOOL_PlotExecutor.hpp"
#include "../../../tools/stattool/STAT_TOOL_LoadExecutor.hpp"
#include "stattool/STAT_TOOL_SnapshotSet.hpp"
#include "../../../tools/stattool/STAT_TOOL_ShowExecutor.hpp"
#include "../../../tools/stattool/STAT_TOOL_AggregateExecutor.hpp"
#include <vector>
#include <string>

#if !defined(WINDOWS)
extern "C" {
#include <unistd.h>
};
#define CROSS_DUP(fd) dup(fd);
#define CROSS_DUP2(fd, newfd) dup2(fd, newfd);
#define NULL_FILENAME "/dev/null"
#else
extern "C" {
#include <io.h>
};
#define CROSS_DUP(fd) _dup(fd);
#define CROSS_DUP2(fd, newfd) _dup2(fd, newfd);
#define NULL_FILENAME "NUL"
#endif

class UnitTests
{
  public:
    static void initTestEnvironment ();
    static void destroyTestEnvironment ();

    static UINT64 *generateRandomStats (long long max);
    static void createStatFile (const std::string &filename, int numOfSnapshots, int secondsGap);
    static void removeFile (const std::string &filename);

    static bool testLoad_BadFile ();
    static bool testLoad_GoodFile ();
    static bool testLoad_GoodFile_checkAlias ();
    static bool testLoad_noAlias ();
    static bool testLoad_GoodFile_checkSnapshots ();
    static bool testStatFileClass_Test1();
    static bool testStatFileClass_Test2();
    static bool testStatFileClass_Test3();
    static bool testPlot_MissingArguments ();
    static bool testPlot_InvalidAlias ();
    static bool testPlot_CreatePlot ();
    static bool testShow_InvalidAlias ();
    static bool testShow_HappyPath ();
    static bool testAggregate_MissingArguments ();
    static bool testAggregate_HappyPath ();

    static void disableStdout();
    static void enableStdout();

    static int stdoutBackupFd;
    static FILE *nullOut;
    static bool isStdoutEnabled;
};

#endif //CUBRID_UNITTESTS_H
