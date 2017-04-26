//
// Created by paul on 21.03.2017.
//

#ifndef CUBRID_UTILS_H
#define CUBRID_UTILS_H

extern "C" {
#include <perf_metadata.h>
}

#include <vector>
#include <typeinfo>
#include "STAT_TOOL_Snapshot.hpp"
#include "STAT_TOOL_SnapshotSet.hpp"

#define MAX_COMMAND_SIZE 128
#define MAX_FILE_NAME_SIZE 128

class StatToolSnapshotSet;
class StatToolSnapshot;

class Utils
{
public:
    static StatToolSnapshot *findSnapshotInLoadedSets (const std::vector<StatToolSnapshotSet *> &sets, const char *alias);
    static void perfmeta_custom_dump_stats_in_table_form (const std::vector<StatToolSnapshot *>& snapshots, FILE * stream, int show_complex,
						     int show_zero);
private:
    static void perfmon_print_timer_to_file_in_table_form (FILE * stream, int stat_index, const std::vector<StatToolSnapshot *>& snapshots,
							    int show_zero, int show_header);
    static void perfmon_stat_dump_in_file_in_table_form (FILE * stream, PSTAT_METADATA * stat, const std::vector<StatToolSnapshot *>& snapshots,
							 int show_zeroes);
};


#endif //CUBRID_UTILS_H
