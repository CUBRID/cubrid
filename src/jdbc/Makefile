JDBC_SRC_DIR = .

include $(JDBC_SRC_DIR)/mk_def
include $(JDBC_SRC_DIR)/make_common

SRCS =	$(JDBC_SRCS)

all:	$(CLASS_DIR)/$(DRIVER)

#DEBUG_FLAG =

$(CLASS_DIR)/$(DRIVER):	$(SRCS)
	mkdir -p $(CLASS_DIR)
	$(JAVAC) $(DEBUG_FLAG) -d $(CLASS_DIR) $(SRCS)
	$(MAKE_JAR)

include obj_rule

clean:
	/bin/rm -rf $(CLASS_DIR)
	touch $(JDBC_SRC_DIR)/*.pjava
