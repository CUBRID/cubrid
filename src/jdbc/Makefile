JAVAC = javac

JDBC_SRC_DIR = .

CLASS_DIR = ./class

include $(JDBC_SRC_DIR)/make_common

SRCS =	$(JDBC_SRCS) 


all: 	$(CLASS_DIR)/$(DRIVER)


$(CLASS_DIR)/$(DRIVER):	$(SRCS) 
	mkdir -p $(CLASS_DIR)
	$(JAVAC) $(DEBUG_FLAG) -d $(CLASS_DIR) $(SRCS)
	$(MAKE_DRIVER_JAR)



$(JDBC_SRC_DIR)/cubrid/jdbc/jci/%.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/%.pjava
	@rm -f $@
	cpp $(CPP_OPT_DRIVER) $< $@
	@chmod 400 $@

$(JDBC_SRC_DIR)/cubrid/jdbc/driver/%.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/%.pjava
	@rm -f $@
	cpp $(CPP_OPT_DRIVER) $< $@
	@chmod 400 $@

clean:
	/bin/rm -rf $(CLASS_DIR)
	touch $(JDBC_SRC_DIR)/cubrid/jdbc/driver/*.pjava
	touch $(JDBC_SRC_DIR)/cubrid/jdbc/jci/*.pjava



