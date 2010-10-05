#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

CC = gcc
CUBRID_SO = cubrid_broker.so 
CUBRID_HA_SO = cubrid_ha.so
CUBRID_STATDUMP_SO = cubrid_statdump.so
OFILES = cubrid_broker.o broker_config.o broker_filename.o broker_util.o broker_shm.o broker_error.o
OFILES_HA = cubrid_ha.o
OFILES_STATDUMP = cubrid_statdump.o
SRCS = cubrid_broker.c cubrid_ha.c cubrid_statdump.c

#########################################################
# change following parts manually
#

# choose one bit model & enable that part

## 32 bit model
#OPT_32bit = -m32
#BIT_MODEL=ARCH_32
##

## LP64 bit model
BIT_MODEL=ARCH_LP64
##

# enable if you want to build plugin in C_API_MODE
#OPT_C_API_MODE=-DC_API_MODE

# enable if you want to build plugin with collectd 4.3 
#OPT_COLLECTD_VERSION=-DCOLLECTD_43

#set below variable 
CUBRID_SRC_DIR = 
CUBRID_CONFIG_DIR = 
CUBRID_DIR = 
COLLECTD_SRC_DIR = 

#example)
#CUBRID_SRC_DIR = /home/myid/CUBRID_SRC
#CUBRID_CONFIG_DIR = /home/myid/CUBRID_SRC/build64-rel
#CUBRID_DIR = /home1/myid/CUBRID22
#COLLECTD_SRC_DIR = /home1/myid/collectd-4.8.1
#end

CUBRID_INCLUDE = -I$(CUBRID_SRC_DIR)/src/base -I$(CUBRID_SRC_DIR)/include -I$(CUBRID_CONFIG_DIR) -I$(CUBRID_SRC_DIR)/src/cci -I$(CUBRID_SRC_DIR)/src/compat -I$(CUBRID_SRC_DIR)/src/api -I$(CUBRID_SRC_DIR)/src/object -I$(CUBRID_SRC_DIR)/src/query -I$(CUBRID_SRC_DIR)/src/storage -I$(CUBRID_SRC_DIR)/src/thread -I$(CUBRID_SRC_DIR)/src/transaction -I$(CUBRID_SRC_DIR)/src/parser -I$(CUBRID_CONFIG_DIR)/external/include/lzo
CUBRID_BROKER_SRC_DIR = $(CUBRID_SRC_DIR)/src/broker
CUBRID_SRC_BUILD_FLAGS = -D_UC_ADMIN_SO_ -DDIAG_DEVEL -DLINUX $(OPT_C_API_MODE) -D$(BIT_MODEL) -DOR_HAVE_NTOHD -DOR_HAVE_HTOND -DFP_LAYOUT_NEED_NOTHING -DHAVE_GETPWNAM_R $(OPT_COLLECTD_VERSION)
CUBRID_LIBRARY = -L$(CUBRID_DIR)/lib -lcubridcs -lcascci

all: a.out b.out c.out

$(CUBRID_STATDUMP_SO): $(OFILES_STATDUMP)
	$(CC) $(OPT_32bit) -o $@ $(OFILES_STATDUMP) -shared $(CUBRID_LIBRARY)

$(CUBRID_HA_SO): $(OFILES_HA)
	$(CC) $(OPT_32bit) -o $@ $(OFILES_HA) -shared $(CUBRID_LIBRARY)

$(CUBRID_SO): $(OFILES)
	$(CC) $(OPT_32bit) -o $@ $(OFILES) -shared $(CUBRID_LIBRARY)

cubrid_broker.o: cubrid_broker.c
	$(CC) $(OPT_32bit) -DHAVE_CONFIG_H $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(COLLECTD_SRC_DIR)/src -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -Wall -Werror  -O2 -MT cubrid_broker.o -MD -MP -MF cubrid_broker.Tpo -c cubrid_broker.c  -fPIC -DPIC -o cubrid_broker.o

cubrid_ha.o: cubrid_ha.c
	$(CC) $(OPT_32bit) -DHAVE_CONFIG_H $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(COLLECTD_SRC_DIR)/src -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -Wall -Werror  -O2 -MT cubrid_ha.o -MD -MP -MF cubrid_ha.Tpo -c cubrid_ha.c  -fPIC -DPIC -o cubrid_ha.o

cubrid_statdump.o: cubrid_statdump.c
	$(CC) $(OPT_32bit) -DHAVE_CONFIG_H $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(COLLECTD_SRC_DIR)/src -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -Werror  -O2 -MT cubrid_statdump.o -MD -MP -MF cubrid_statdump.Tpo -c cubrid_statdump.c  -fPIC -DPIC -o cubrid_statdump.o

broker_shm.o: $(CUBRID_BROKER_SRC_DIR)/broker_shm.c
	$(CC) $(OPT_32bit) -o $@ $< -c $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -DCAS -DUC_ERROR_DEF_FILE="\"dummy.h\"" -fPIC

%.o: $(CUBRID_BROKER_SRC_DIR)/%.c
	$(CC) $(OPT_32bit) -o $@ $< -c $(CUBRID_SRC_BUILD_FLAGS) -I. -I$(CUBRID_BROKER_SRC_DIR) $(CUBRID_INCLUDE) -fPIC

a.out: a.c $(CUBRID_SO)
	cp $(CUBRID_SO) libcubrid_broker.so
	$(CC) $(OPT_32bit) a.c -o a.out -L. -lcubrid_broker $(CUBRID_LIBRARY)

b.out: a.c $(CUBRID_HA_SO)
	cp $(CUBRID_HA_SO) libcubrid_broker_ha.so
	$(CC) $(OPT_32bit) a.c -o b.out -L. -lcubrid_broker $(CUBRID_LIBRARY)

c.out: a.c $(CUBRID_STATDUMP_SO)
	cp $(CUBRID_STATDUMP_SO) libcubrid_broker_statdump.so
	$(CC) $(OPT_32bit) a.c -o c.out -L. -lcubrid_broker $(CUBRID_LIBRARY)
	
clean:
	/bin/rm $(OFILES) $(CUBRID_SO) $(OFILES_HA) $(CUBRID_HA_SO) $(CUBRID_STATDUMP_SO) libcubrid_broker.so libcubrid_broker_ha.so libcubrid_broker_statdump.so a.out b.out c.out *.Tpo
