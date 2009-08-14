
SRC_DIR=
DEST_DIR=
INSTALL_SCRIPT_DIR=.

all:
	cmd /C $(INSTALL_SCRIPT_DIR)\\install.bat "$(SRC_DIR)" "$(DEST_DIR)"

all_debug: all

clean:
	if exist $(DEST_DIR) cmd /c rmdir $(DEST_DIR) /s /q

install_client:
	cmd /c $(INSTALL_SCRIPT_DIR)\\install_client.bat "$(SRC_DIR)" "$(DEST_DIR)"
