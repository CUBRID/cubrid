JAVAC = javac -target 1.5
NBENCH_VERSION =1.0.0
#DEBUG_FLAG = -Xlint -g
CLASSPATH = .:../lib/commons-collections.jar:../lib/commons-pool-1.1.jar:../lib/commons-dbcp-1.1.jar:../lib/ibatis-2.3.0.677.jar
NBENCH_SRC_DIR = ./nbench
CLASS_DIR = $(NBENCH_SRC_DIR)/class
DRIVER = nbench.$(NBENCH_VERSION).jar

MAKE_JAR = (cd $(CLASS_DIR) ; jar cf $(DRIVER) nbench)

NBENCH_SRCES = \
	$(NBENCH_SRC_DIR)/common/NBenchException.java \
	$(NBENCH_SRC_DIR)/common/BackendEngineClient.java \
	$(NBENCH_SRC_DIR)/common/Variable.java \
	$(NBENCH_SRC_DIR)/common/SampleValue.java \
	$(NBENCH_SRC_DIR)/common/ValueType.java \
	$(NBENCH_SRC_DIR)/common/BackendEngine.java \
	$(NBENCH_SRC_DIR)/common/Value.java \
	$(NBENCH_SRC_DIR)/common/helper/NValue.java \
	$(NBENCH_SRC_DIR)/common/helper/NVariable.java \
	$(NBENCH_SRC_DIR)/common/helper/MapVariableScope.java \
	$(NBENCH_SRC_DIR)/common/VariableScope.java \
	$(NBENCH_SRC_DIR)/common/NameAndType.java \
	$(NBENCH_SRC_DIR)/parse/Transaction.java \
	$(NBENCH_SRC_DIR)/parse/Mix.java \
	$(NBENCH_SRC_DIR)/parse/Step.java \
	$(NBENCH_SRC_DIR)/parse/ConditionItem.java \
	$(NBENCH_SRC_DIR)/parse/Condition.java \
	$(NBENCH_SRC_DIR)/parse/SampleSpace.java \
	$(NBENCH_SRC_DIR)/parse/WorkLoad.java \
	$(NBENCH_SRC_DIR)/parser/TypeMap.java \
	$(NBENCH_SRC_DIR)/report/NStat.java \
	$(NBENCH_SRC_DIR)/report/Reporter.java \
	$(NBENCH_SRC_DIR)/report/NTotalStat.java \
	$(NBENCH_SRC_DIR)/script/NScriptEngine.java \
	$(NBENCH_SRC_DIR)/script/parser/NValueNode.java \
	$(NBENCH_SRC_DIR)/script/parser/NTokenizer.java \
	$(NBENCH_SRC_DIR)/script/parser/NNodeVisitable.java \
	$(NBENCH_SRC_DIR)/script/parser/helper/DumpVisitor.java \
	$(NBENCH_SRC_DIR)/script/parser/NNode.java \
	$(NBENCH_SRC_DIR)/script/parser/NBinOpNode.java \
	$(NBENCH_SRC_DIR)/script/parser/NToken.java \
	$(NBENCH_SRC_DIR)/script/parser/NParser.java \
	$(NBENCH_SRC_DIR)/script/parser/NIdentifierNode.java \
	$(NBENCH_SRC_DIR)/script/parser/NNodeVisitor.java \
	$(NBENCH_SRC_DIR)/script/NCode.java \
	$(NBENCH_SRC_DIR)/parser/NBenchXMLParser.java \
	$(NBENCH_SRC_DIR)/parser/PNAT.java \
	$(NBENCH_SRC_DIR)/engine/NFrontEngine.java \
	$(NBENCH_SRC_DIR)/engine/NSampleValue.java \
	$(NBENCH_SRC_DIR)/engine/NContext.java \
	$(NBENCH_SRC_DIR)/engine/SampleVariableScope.java \
	$(NBENCH_SRC_DIR)/engine/FrameScope.java \
	$(NBENCH_SRC_DIR)/engine/helper/DummyBackendEngine.java \
	$(NBENCH_SRC_DIR)/engine/helper/TrSpec.java \
	$(NBENCH_SRC_DIR)/engine/helper/ActionSpec.java \
	$(NBENCH_SRC_DIR)/engine/sqlmap/SQLMapBackendEngine.java \
	$(NBENCH_SRC_DIR)/engine/sqlmap/SQLMapBackendEngineClient.java \
	$(NBENCH_SRC_DIR)/engine/jdbc/JDBCBackendEngineClient.java \
	$(NBENCH_SRC_DIR)/engine/jdbc/JDBCBackendEngine.java \
	$(NBENCH_SRC_DIR)/engine/cci/CCIBackendEngine.java \
	$(NBENCH_SRC_DIR)/engine/cci/CCIBackendEngineClient.java \
	$(NBENCH_SRC_DIR)/engine/cci/CCIDataSource.java \
	$(NBENCH_SRC_DIR)/engine/cci/CCIConnection.java \
	$(NBENCH_SRC_DIR)/engine/cci/CCIDriver.java \
	$(NBENCH_SRC_DIR)/engine/cci/CCIPreparedStatement.java \
	$(NBENCH_SRC_DIR)/engine/cci/CCIResultSet.java


SRCES = $(NBENCH_SRCES)

all: $(CLASS_DIR)/$(DRIVER)
	$(JAVAC) -classpath $(CLASSPATH) $(DEBUG_FLAG) NBench.java

$(CLASS_DIR)/$(DRIVER) : $(SRCES)
	mkdir -p $(CLASS_DIR)
	$(JAVAC) -classpath $(CLASSPATH) $(DEBUG_FLAG) -d $(CLASS_DIR) $(SRCES)
	$(MAKE_JAR)

release:
	mkdir release_
	cp $(CLASS_DIR)/$(DRIVER) release_
	cp -r ../lib release_
	cp NBench.class release_
	tar cvf nbench.$(NBENCH_VERSION).tar release_
	rm -rf release_
	mv nbench.$(NBENCH_VERSION).tar ../release

clean:
	/bin/rm -rf $(CLASS_DIR)
	/bin/rm -rf ./*.class
	/bin/rm -rf ./nbench.*.tar
