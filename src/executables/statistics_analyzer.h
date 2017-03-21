//
// Created by paul on 14.03.2017.
//

#ifndef CUBRID_STATISTICS_ANALYZER_H
#define CUBRID_STATISTICS_ANALYZER_H

#define STARTING_STATS_SIZE 5
#define MAX_COMMAND_SIZE 64
#define MAX_FILE_NAME_SIZE 128
#define MAX_PERF_FILES 32

typedef struct perf_stat PERF_STAT;
typedef struct perf_file PERF_FILE;
struct perf_stat {
    UINT64 *raw_stats;
    struct tm timestamp;
};

struct perf_file {
    PERF_STAT *stats;
    struct tm relative_timestamp;
    int num_of_snapshots;
    char filename[MAX_FILE_NAME_SIZE];
    char alias[MAX_FILE_NAME_SIZE];
};

void print_statistics (UINT64 *stats_ptr, FILE *stream);
void compare_and_print_stats (UINT64 *stats1, UINT64 *stats2, FILE *stream);
int binary_search_stat(int minutes, PERF_FILE *file);
void print_timestamp (struct tm timestamp);

#endif //CUBRID_STATISTICS_ANALYZER_H
