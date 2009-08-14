CC = gcc34
CUBRID_SO = cubrid_broker.so 
CUBRID_HA_SO = cubrid_ha.so
OFILES = cubrid_broker.o broker_config.o broker_filename.o broker_util.o broker_shm.o broker_error.o
OFILES_HA = cubrid_ha.o
SRCS = cubrid_broker.c cubrid_ha.c

COLLECTD_SRC_DIR = /home1/user/collectd/collectd-4.3.1
CUBRID_INCLUDE = -I../../src/base -I../../include -I../../build -I../../src/cci
CUBRID_BROKER_SRC_DIR = ../../src/broker
CUBRID_SRC_BUILD_FLAGS = -D_UC_ADMIN_SO_ -DDIAG_DEVEL
CUBRID_LIBRARY = -L$(CUBRID)/lib -lcubridcs -lcascci
#OPT_32bit = -m32

#all: $(CUBRID_SO)
all: a.out b.out

$(CUBRID_HA_SO): $(OFILES_HA)
	$(CC) $(OPT_32bit) -g -o $@ $(OFILES_HA) -shared $(CUBRID_LIBRARY)

$(CUBRID_SO): $(OFILES)
	$(CC) $(OPT_32bit) -g -o $@ $(OFILES) -shared $(CUBRID_LIBRARY)

cubrid_broker.o: cubrid_broker.c
	$(CC) $(OPT_32bit) -g -DHAVE_CONFIG_H $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(COLLECTD_SRC_DIR)/src -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -Wall -Werror  -g -O2 -MT cubrid_broker.o -MD -MP -MF cubrid_broker.Tpo -c cubrid_broker.c  -fPIC -DPIC -o cubrid_broker.o

cubrid_ha.o: cubrid_ha.c
	$(CC) $(OPT_32bit) -g -DHAVE_CONFIG_H $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(COLLECTD_SRC_DIR)/src -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -Wall -Werror  -g -O2 -MT cubrid_ha.o -MD -MP -MF cubrid_ha.Tpo -c cubrid_ha.c  -fPIC -DPIC -o cubrid_ha.o

broker_shm.o: $(CUBRID_BROKER_SRC_DIR)/broker_shm.c
	$(CC) $(OPT_32bit) -g -o $@ $< -c -g $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -DCAS -DUC_ERROR_DEF_FILE="\"dummy.h\"" -fPIC

%.o: $(CUBRID_BROKER_SRC_DIR)/%.c
	$(CC) $(OPT_32bit) -g -o $@ $< -c -g $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -fPIC

a.out: a.c $(CUBRID_SO)
	cp $(CUBRID_SO) libcubrid_broker.so
	$(CC) $(OPT_32bit) -g a.c -o a.out -L. -lcubrid_broker $(CUBRID_LIBRARY)

b.out: a.c $(CUBRID_HA_SO)
	cp $(CUBRID_HA_SO) libcubrid_broker_ha.so
	$(CC) $(OPT_32bit) -g a.c -o b.out -L. -lcubrid_broker $(CUBRID_LIBRARY)
	
clean:
	/bin/rm $(OFILES) $(OFILES_HA) $(CUBRID_SO) $(CUBRID_HA_SO) libcubrid_broker.so libcubrid_broker_ha.so a.out b.out *.Tpo
