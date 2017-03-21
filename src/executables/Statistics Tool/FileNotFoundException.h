//
// Created by paul on 21.03.2017.
//

#ifndef CUBRID_FILENOTFOUNDEXCEPTION_H
#define CUBRID_FILENOTFOUNDEXCEPTION_H

#include <exception>
#include <stdexcept>
#include <string>

class FileNotFoundException : public std::runtime_error{
public:
    FileNotFoundException(const std::string& message);
    virtual const char *what() const throw();
    ~FileNotFoundException() throw(){}
private:
    std::string message;
};


#endif //CUBRID_FILENOTFOUNDEXCEPTION_H
