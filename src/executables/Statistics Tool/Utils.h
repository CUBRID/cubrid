//
// Created by paul on 21.03.2017.
//

#ifndef CUBRID_UTILS_H
#define CUBRID_UTILS_H

#define MAX_COMMAND_SIZE 64
#define MAX_FILE_NAME_SIZE 128


class Utils {
public:
    static int getNStatValues();
    static void setNStatValues(int n);
private:
    static int nStatValues;
};


#endif //CUBRID_UTILS_H
