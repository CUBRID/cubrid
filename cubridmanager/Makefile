
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; version 2 of the License. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
#

PRODUCT_DIR = product
OUT_DIR = out
SUN_DIR = sun
WIN32_DIR = win32
LINUX_DIR = linux
LINUX_GTK_DIR = linux.gtk
RCP_DIR = rcpfiles
CONFIG = configuration
CUBRIDMANAGER = cubridmanager
SUN_DEPLOY = $(SUN_DIR)/$(CUBRIDMANAGER)
WIN32_DEPLOY = $(WIN32_DIR)/$(CUBRIDMANAGER)
LINUX_DEPLOY = $(LINUX_DIR)/$(CUBRIDMANAGER)
LINUX_GTK_DEPLOY = $(LINUX_GTK_DIR)/$(CUBRIDMANAGER)
SUN_CONFIG = $(SUN_DEPLOY)/$(CONFIG)
WIN32_CONFIG = $(WIN32_DEPLOY)/$(CONFIG)
LINUX_CONFIG = $(LINUX_DEPLOY)/$(CONFIG)
LINUX_GTK_CONFIG = $(LINUX_GTK_DEPLOY)/$(CONFIG)

all: sun win32 linux linux_gtk

sun: common
	mkdir -p $(SUN_CONFIG)
	cp -rf $(PRODUCT_DIR)/* $(SUN_DEPLOY)
	cp $(RCP_DIR)/config.ini.sun $(SUN_CONFIG)/config.ini
	cp $(RCP_DIR)/launcher.sun $(SUN_DEPLOY)/.cmclient_bin
	cp $(RCP_DIR)/launcher.sh $(SUN_DEPLOY)/cubridmanager

win32: 
	ant win32
	mkdir -p $(WIN32_CONFIG)
	mkdir -p $(WIN32_DEPLOY)/lib
	cp -rf $(PRODUCT_DIR)/* $(WIN32_DEPLOY)
	cp $(RCP_DIR)/win32/config.ini  $(WIN32_CONFIG)/config.ini
	cp $(RCP_DIR)/win32/cubridmanager.exe $(WIN32_DEPLOY)

linux: common
	mkdir -p $(LINUX_CONFIG)
	cp -rf $(PRODUCT_DIR)/* $(LINUX_DEPLOY)
	cp $(RCP_DIR)/config.ini.linux $(LINUX_CONFIG)/config.ini
	cp $(RCP_DIR)/launcher.linux $(LINUX_DEPLOY)/.cmclient_bin
	cp $(RCP_DIR)/launcher.motif.sh $(LINUX_DEPLOY)/cubridmanager

linux_gtk: common
	mkdir -p $(LINUX_GTK_CONFIG)
	cp -rf $(PRODUCT_DIR)/* $(LINUX_GTK_DEPLOY)
	cp $(RCP_DIR)/config.ini.linux.gtk $(LINUX_GTK_CONFIG)/config.ini
	cp $(RCP_DIR)/launcher.linux.gtk $(LINUX_GTK_DEPLOY)/.cmclient_bin
	cp $(RCP_DIR)/libcairo.so.1 $(LINUX_GTK_DEPLOY)/libcairo.so.1
	cp $(RCP_DIR)/launcher.gtk.sh $(LINUX_GTK_DEPLOY)/cubridmanager

common:
	ant all

clean:
	/bin/rm -rf $(SUN_DIR) $(WIN32_DIR) $(LINUX_DIR) $(LINUX_GTK_DIR) $(PRODUCT_DIR) $(OUT_DIR)
