
SRC_DIR=..\debug
DEST_DIR=cubrid
INSTALL_SCRIPT_DIR=.

all:
	cmd /C $(INSTALL_SCRIPT_DIR)\\install.bat "$(SRC_DIR)" "$(DEST_DIR)"

all_debug: all

clean:
	cmd /c rmdir $(DEST_DIR) /s /q

install_client:
	cmd /c $(INSTALL_SCRIPT_DIR)\\install_client.bat "$(SRC_DIR)" "$(DEST_DIR)"
