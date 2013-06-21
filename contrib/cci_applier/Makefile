CC = gcc
SRCS = ./src/cci_applier.c

LIBS = -lnsl -lpthread
INC = -I./include

CFLAGS := $(CFLAGS) -g -Wall -w $(INC)
LDFLAGS = -L./lib

SLIBS = ./lib/libcascci.a ./lib/libregex38a.a

OBJS := $(SRCS:.c=.o)
TARGET = cci_applier

all : $(TARGET)

$(TARGET) : $(OBJS) $(SLIBS)
	g++ -o $@ $^ $(LIBS)

clean :
	rm -rf $(OBJS) $(TARGET)

