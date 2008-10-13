DROP_DIR = drop
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
	cp -rf $(DROP_DIR)/* $(SUN_DEPLOY)
	cp $(RCP_DIR)/config.ini.sun $(SUN_CONFIG)/config.ini
	cp $(RCP_DIR)/launcher.sun $(SUN_DEPLOY)/.cmclient_bin
	cp $(RCP_DIR)/launcher.sh $(SUN_DEPLOY)/cubridmanager

win32: common
	mkdir -p $(WIN32_CONFIG)
	mkdir -p $(WIN32_DEPLOY)/lib
	cp -rf $(DROP_DIR)/* $(WIN32_DEPLOY)
	cp $(RCP_DIR)/config.ini.win32 $(WIN32_CONFIG)/config.ini
	cp $(RCP_DIR)/launcher.exe $(WIN32_DEPLOY)/cubridmanager.exe
	cp $(RCP_DIR)/CertProcess.dll     $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/GPKICertManager.dll $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/cub_readcert.dll  $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/gpkiapi.dll       $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/gpkiapi_jni.dll   $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/mfc42.dll         $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/mfc71.dll         $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/msvcr71.dll       $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/msvcrt.dll        $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/nsldap32v11.dll   $(WIN32_DEPLOY)/lib
	cp $(RCP_DIR)/upa_j.dll         $(WIN32_DEPLOY)/lib

linux: common
	mkdir -p $(LINUX_CONFIG)
	cp -rf $(DROP_DIR)/* $(LINUX_DEPLOY)
	cp $(RCP_DIR)/config.ini.linux $(LINUX_CONFIG)/config.ini
	cp $(RCP_DIR)/launcher.linux $(LINUX_DEPLOY)/.cmclient_bin
	cp $(RCP_DIR)/launcher.motif.sh $(LINUX_DEPLOY)/cubridmanager

linux_gtk: common
	mkdir -p $(LINUX_GTK_CONFIG)
	cp -rf $(DROP_DIR)/* $(LINUX_GTK_DEPLOY)
	cp $(RCP_DIR)/config.ini.linux.gtk $(LINUX_GTK_CONFIG)/config.ini
	cp $(RCP_DIR)/launcher.linux.gtk $(LINUX_GTK_DEPLOY)/.cmclient_bin
	cp $(RCP_DIR)/libcairo.so.1 $(LINUX_GTK_DEPLOY)/libcairo.so.1
	cp $(RCP_DIR)/launcher.gtk.sh $(LINUX_GTK_DEPLOY)/cubridmanager

common:
	ant dist

clean:
	/bin/rm -rf $(SUN_DIR) $(WIN32_DIR) $(LINUX_DIR) $(LINUX_GTK_DIR) $(DROP_DIR) $(OUT_DIR)
