#include <iostream>
#include "Utils.h"
#include "StatisticsFile.h"

extern "C" {
    #include <perfmon_base.h>
    #include <string.h>
}

int main (int argc, char **argv) {
    bool quit = false;
    char command[MAX_COMMAND_SIZE];
    char *str;
    std::vector<StatisticsFile*> files;
    Utils::setNStatValues(metadata_initialize());

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
		StatisticsFile *newFile;
		try{
		    newFile = new StatisticsFile(std::string(filename), std::string(alias));
		    files.push_back(newFile);
		} catch (FileNotFoundException& e) {
		    std::cout << e.what() << std::endl;
		}
	    }
	} else if (strcmp(str, "compare") == 0) {
	    char *argument1, *argument2, *outputFilename;
	    FILE *outFp = NULL;
	    StatisticsFile::Snapshot *s1 = NULL, *s2 = NULL;

	    argument1 = strtok(NULL, " " );
	    argument2 = strtok(NULL, " " );

	    if (!argument1 || !argument2) {
		printf("Usage: compare <alias1(minutes1[-minutes2])> <alias2(minutes1[-minutes2])>\n");
		continue;
	    }

	    outputFilename = strtok(NULL, " ");

	    if (outputFilename != NULL) {
		outFp = fopen(outputFilename, "w");
	    } else {
		outFp = stdout;
	    }

	    for(unsigned int i = 0; i < files.size() && ((s1 = files[i]->getSnapshotByArgument(argument1)) == NULL); i++);
	    for(unsigned int i = 0; i < files.size() && ((s2 = files[i]->getSnapshotByArgument(argument2)) == NULL); i++);

	    StatisticsFile::printInTableForm(s1, s2, outFp);

	    if (outFp != stdout) {
		fclose(outFp);
	    }

	} else if (strcmp(str, "print") == 0) {
	    char *argument, *output_filename;
	    FILE *out_fp = NULL;
	    char strTime[80];
	    StatisticsFile::Snapshot *snapshot;

	    argument = strtok(NULL, " ");

	    if (!argument) {
		printf("Usage: print <alias1(minutes1[-minutes2])>\n");
		continue;
	    }

	    output_filename = strtok(NULL, " ");

	    if (output_filename != NULL) {
		out_fp = fopen(output_filename, "w");
	    } else {
		out_fp = stdout;
	    }

	    for(unsigned int i = 0; i < files.size(); i++) {
		if ((snapshot = files[i]->getSnapshotByArgument(argument)) != NULL) {
		    snapshot->print(out_fp);
		    break;
		}
	    }

	    if (out_fp != stdout) {
		fclose(out_fp);
	    }
	} else if (strcmp(str, "quit") == 0) {
	    quit = true;
	} else {
	    printf("Invalid command!\n");
	}
    } while (!quit);

    for (unsigned int i = 0; i < files.size(); i++) {
	delete(files[i]);
    }

    return 0;
}