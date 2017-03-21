#include <stdio.h>
#include <perfmon_base.h>
#include "statistics_analyzer.h"

int n_stat_values = 0;
PERF_FILE perf_files[MAX_PERF_FILES];
int num_of_loaded_files = 0;

void print_timestamp (struct tm timestamp)
{
    char time_str[80];
    strftime (time_str, 80, "%a %B %d %H:%M:%S %Y", &timestamp);
    printf("%s\n", time_str);
}

void diff_stats(UINT64 *stats1, UINT64 *stats2, UINT64 *stats_res) {
    int i;

    for (i = 0; i < n_stat_values; i++) {
	stats_res[i] = (UINT64)difference((long long)stats1[i], (long long)stats2[i]);
    }
}

void diff_and_print_stats (UINT64 *stats1, UINT64 *stats2, FILE *stream)
{
    int i;
    UINT64 *diff_stat = (UINT64*)malloc(sizeof(UINT64) * n_stat_values);

    for (i = 0; i < n_stat_values; i++) {
	diff_stat[i] = (UINT64)difference((long long)stats1[i], (long long)stats2[i]);
    }

    print_statistics(diff_stat, stream);
    free(diff_stat);
}

void print_statistics (UINT64 *stats_ptr, FILE *stream)
{
    int i;
    const char *s;
    for (i = 0; i < PSTAT_COUNT; i++)
    {
	if (pstat_Metadata[i].valtype == PSTAT_COMPLEX_VALUE)
	{
	    break;
	}

	int offset = pstat_Metadata[i].start_offset;

	if (pstat_Metadata[i].valtype != PSTAT_COMPUTED_RATIO_VALUE)
	{
	    if (pstat_Metadata[i].valtype != PSTAT_COUNTER_TIMER_VALUE)
	    {
		if (stats_ptr[offset] != 0) {
		    fprintf(stream, "%-29s = %10lld\n", pstat_Metadata[i].stat_name,
			    (long long) stats_ptr[offset]);
		}
	    }
	    else
	    {
		perfmon_print_timer_to_file (stream, i, stats_ptr);
	    }
	}
	else
	{
	    if (stats_ptr[offset] != 0) {
		fprintf(stream, "%-29s = %10.2f\n", pstat_Metadata[i].stat_name,
			(long long) stats_ptr[offset] / 100.0f);
	    }
	}
    }

    for (; i < PSTAT_COUNT; i++)
    {
	fprintf (stream, "%s:\n", pstat_Metadata[i].stat_name);
	pstat_Metadata[i].f_dump_in_file (stream, &(stats_ptr[pstat_Metadata[i].start_offset]));
    }
}

void print_statistics_in_table_form (PERF_STAT *first_stats, PERF_STAT *second_stats, FILE *stream)
{
    int i;
    const char *s;
    char timestamp1[20], timestamp2[20];

    UINT64 *stats1 = first_stats->raw_stats;
    UINT64 *stats2 = second_stats->raw_stats;

    strftime (timestamp1, 80, "%H:%M:%S", &first_stats->timestamp);
    strftime (timestamp2, 80, "%H:%M:%S", &second_stats->timestamp);
    printf("\t\t\t\t\t\t\t      %s \t   %s \t  diff\n", timestamp1, timestamp2);

    for (i = 0; i < PSTAT_COUNT; i++)
    {
	if (pstat_Metadata[i].valtype == PSTAT_COMPLEX_VALUE)
	{
	    break;
	}

	int offset = pstat_Metadata[i].start_offset;

	if (pstat_Metadata[i].valtype != PSTAT_COMPUTED_RATIO_VALUE)
	{
	    if (pstat_Metadata[i].valtype != PSTAT_COUNTER_TIMER_VALUE)
	    {
		if ( stats1[offset] == 0 && stats2[offset] == 0) {
		    continue;
		}

		fprintf (stream, "%-58s | %10lld | %10lld | %10lld\n", pstat_Metadata[i].stat_name,
			 (long long) stats1[offset],
			 (long long) stats2[offset],
			 difference((long long)stats1[offset], (long long)stats2[offset]));
	    }
	    else
	    {
		perfmon_compare_timer(stream, i, stats1, stats2);
	    }
	}
	else
	{
	    if (stats1[offset] == 0 && stats2[offset] == 0) {
		continue;
	    }

	    fprintf (stream, "%-58s | %10.2f | %10.2f | %10.2f\n", pstat_Metadata[i].stat_name,
		     (long long) stats1[offset] / 100.0f,
		     (long long) stats2[offset] / 100.0f,
		     difference((long long)stats1[offset], (long long)stats2[offset])/100.0f);
	}
    }

    for (; i < PSTAT_COUNT; i++)
    {
	fprintf (stream, "\n%s:\n", pstat_Metadata[i].stat_name);
	pstat_Metadata[i].f_dump_diff_in_file (stream, &(stats1[pstat_Metadata[i].start_offset]), &(stats2[pstat_Metadata[i].start_offset]));
    }
}

int binary_search_stat(int minutes, PERF_FILE *file){
    int seconds = minutes*60;
    time_t relative_epoch_seconds = mktime(&file->relative_timestamp);
    int i, j, mid;

    i = 0;
    j = file->num_of_snapshots-1;

    if ((mktime(&file->stats[0].timestamp) - relative_epoch_seconds) >= seconds){
	return 0;
    }

    while (j-i > 1){
	mid = (i+j)/2;
	time_t sec = (mktime(&file->stats[mid].timestamp)-relative_epoch_seconds);
	if (sec > seconds) {
	    j = mid;
	} else if (sec < seconds) {
	    i = mid;
	} else if (sec == seconds) {
	    return mid;
	}
    }

    return j;
}

void init_stats(PERF_FILE *pf, char *filename, char *alias) {
    int current_stats_size = STARTING_STATS_SIZE;
    FILE *binary_fp;
    struct tm timestamp;
    char time_str[80];

    pf->num_of_snapshots = 0;
    strcpy(pf->filename, filename);
    strcpy(pf->alias, alias);
    binary_fp = fopen(filename, "rb");

    if (binary_fp == NULL) {
	fprintf(stderr, "The provided file doesn't exist or I don't have permission to open it!");
	return;
    } else {
	fread(&pf->relative_timestamp, sizeof(struct tm), 1, binary_fp);
	pf->stats = (PERF_STAT*)malloc(sizeof(PERF_STAT) * current_stats_size);
    }

    strftime (time_str, 80, "%a %B %d %H:%M:%S %Y", &pf->relative_timestamp);
    printf("Relative Timestamp = %s\n", time_str);

    while (fread(&timestamp, sizeof(struct tm), 1, binary_fp) > 0)
    {
	if (pf->num_of_snapshots == current_stats_size) {
	    pf->stats = realloc(pf->stats, sizeof(PERF_STAT) * (current_stats_size+5));
	    current_stats_size += 5;
	}
	pf->stats[pf->num_of_snapshots].raw_stats = (UINT64*)malloc(sizeof(UINT64) * n_stat_values);
	pf->stats[pf->num_of_snapshots].timestamp = timestamp;
	fread(pf->stats[pf->num_of_snapshots].raw_stats, sizeof(UINT64), (size_t)n_stat_values, binary_fp);
	pf->num_of_snapshots++;
    }

    pf->stats = realloc(pf->stats, sizeof(PERF_STAT) * pf->num_of_snapshots);
    fclose(binary_fp);
}

void parse_alias_argument(char *argument, PERF_STAT *stat) {
    char diff_argument[32];
    char alias[MAX_FILE_NAME_SIZE];
    int minutes1 = -1;
    int minutes2 = -1;
    int i;

    sscanf(argument, "%[^(]%[^)]", alias, diff_argument);
    if (strchr(diff_argument, '-') != NULL) {
	sscanf(diff_argument, "(%d-%d", &minutes1, &minutes2);
    } else {
	sscanf(diff_argument, "(%d", &minutes1);
    }

    PERF_FILE *current_file = NULL;

    for (i = 0; i < num_of_loaded_files; i++) {
	if (strcmp(alias, perf_files[i].alias) == 0) {
	    current_file = &perf_files[i];
	    break;
	}
    }

    if (current_file != NULL) {
	if (minutes1 != -1) {
	    if (minutes2 == -1) {
		int index = binary_search_stat(minutes1, current_file);
		for (i = 0; i < n_stat_values; i++) {
		    stat->raw_stats[i] = current_file->stats[index].raw_stats[i];
		}
		stat->timestamp = current_file->stats[index].timestamp;
	    } else {
		int index1 = binary_search_stat(minutes1, current_file);
		int index2 = binary_search_stat(minutes2, current_file);
		diff_stats(current_file->stats[index1].raw_stats, current_file->stats[index2].raw_stats, stat->raw_stats);
		stat->timestamp = current_file->stats[index1].timestamp;
	    }
	} else if (minutes1 == -1 && minutes2 == -1) {

	}
    }
}

int main (int argc, char *argv[]){
    int quit;
    const char *s;
    char command[MAX_COMMAND_SIZE];
    int i, j;
    char *str;

    quit = 0;
    n_stat_values = metadata_initialize();

    do {
	fgets(command, MAX_COMMAND_SIZE, stdin);
	command[strlen(command)-1] = '\0';
	str = strtok(command, " ");
	if (str == NULL) {
	    continue;
	}
	if (strcmp(str, "load") == 0) {
	    char *filename, *alias;
	    filename = strtok(NULL, " ");
	    alias = strtok(NULL, " ");

	    if (!filename || !alias) {
		printf("Usage: load <filename> <filename alias>");
	    } else {
		init_stats(&perf_files[num_of_loaded_files++], filename, alias);
	    }
	} else if (strcmp(str, "compare") == 0) {
	    char *first, *second, *output_filename;
	    FILE *out_fp = NULL;
	    PERF_STAT first_stats, second_stats;

	    first_stats.raw_stats = (UINT64*)malloc(sizeof(UINT64) * n_stat_values);
	    second_stats.raw_stats = (UINT64*)malloc(sizeof(UINT64) * n_stat_values);

	    first = strtok(NULL, " ");
	    second = strtok(NULL, " ");

	    if (!first || !second) {
		printf("Usage: compare <alias1(minutes1[-minutes2])> <alias2(minutes1[-minutes2])>\n");
		continue;
	    }

	    output_filename = strtok(NULL, " ");

	    if (output_filename != NULL) {
		out_fp = fopen(output_filename, "w");
	    } else {
		out_fp = stdout;
	    }

	    parse_alias_argument(first, &first_stats);
	    parse_alias_argument(second, &second_stats);

	    print_statistics_in_table_form(&first_stats, &second_stats, out_fp);

	    if (out_fp != stdout) {
		fclose(out_fp);
	    }
	} else if (strcmp(str, "print") == 0) {
	    char *first, *output_filename;
	    FILE *out_fp = NULL;
	    PERF_STAT stat;
	    char str_time[80];

	    stat.raw_stats = (UINT64*)malloc(sizeof(UINT64) * n_stat_values);
	    first = strtok(NULL, " ");

	    if (!first) {
		printf("Usage: print <alias1(minutes1[-minutes2])>\n");
		continue;
	    }

	    output_filename = strtok(NULL, " ");

	    if (output_filename != NULL) {
		out_fp = fopen(output_filename, "w");
	    } else {
		out_fp = stdout;
	    }

	    parse_alias_argument(first, &stat);
	    strftime (str_time, 80, "%a %B %d %H:%M:%S %Y", &stat.timestamp);
	    printf("Timestamp = %s\n", str_time);
	    print_statistics(stat.raw_stats, out_fp);

	    if (out_fp != stdout) {
		fclose(out_fp);
	    }
	} else if (strcmp(str, "quit") == 0) {
	    quit = 1;
	} else {
	    printf("Invalid command!\n");
	}
    }while(!quit);

    for (i = 0; i < num_of_loaded_files; i++) {
	for (j = 0; j < perf_files[i].num_of_snapshots; j++) {
	    free(perf_files[i].stats[j].raw_stats);
	}
    }

    return 0;
}