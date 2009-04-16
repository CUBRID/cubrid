CC = gcc34
CUBRID_SO = cubrid_broker.so
OFILES = cubrid_broker.o broker_config.o broker_filename.o broker_util.o broker_shm.o broker_error.o
SRCS = cubrid_broker.c

COLLECTD_SRC_DIR = /home/user/collectd-4.0.4
CUBRID_INCLUDE = -I../../src/base -I../../include -I../../build
CUBRID_BROKER_SRC_DIR = ../../src/broker
CUBRID_SRC_BUILD_FLAGS = -D_UC_ADMIN_SO_ -DDIAG_DEVEL
CUBRID_LIBRARY = -L/home/user/cubrid/lib -lcubridcs

#all: $(CUBRID_SO)
all: a.out

$(CUBRID_SO): $(OFILES)
	$(CC) -m32 -g -o $@ $(OFILES) -shared $(CUBRID_LIBRARY)

cubrid_broker.o: cubrid_broker.c
	$(CC) -m32 -g -DHAVE_CONFIG_H $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(COLLECTD_SRC_DIR)/src -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -Wall -Werror  -g -O2 -MT cubrid_broker.o -MD -MP -MF cubrid_broker.Tpo -c cubrid_broker.c  -fPIC -DPIC -o cubrid_broker.o

broker_shm.o: $(CUBRID_BROKER_SRC_DIR)/broker_shm.c
	$(CC) -m32 -g -o $@ $< -c -g $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -DCAS -DUC_ERROR_DEF_FILE="\"dummy.h\""

%.o: $(CUBRID_BROKER_SRC_DIR)/%.c
	$(CC) -m32 -g -o $@ $< -c -g $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE)

a.out: a.c $(CUBRID_SO)
	cp $(CUBRID_SO) libcubrid_broker.so
	$(CC) -m32 -g a.c -o a.out -L. -lcubrid_broker $(CUBRID_LIBRARY)
	
clean:
	/bin/rm $(OFILES) $(CUBRID_SO) *.Tpo
