#
#  Copyright (C) 2008 Search Solution Corporation. 
#  Copyright (c) 2016 CUBRID Corporation.
# 
#  Redistribution and use in source and binary forms, with or without modification,
#  are permitted provided that the following conditions are met:
# 
#  - Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 
#  - Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 
#  - Neither the name of the <ORGANIZATION> nor the names of its contributors
#    may be used to endorse or promote products derived from this software without
#    specific prior written permission.
# 
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
#  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
#  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
#  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
#  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
#  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
#  OF SUCH DAMAGE.
# 

JDBC_SRC_DIR = .

BIN_DIR = bin
SRC_DIR = src

JDBC_BUILD_NUMBER := $(shell cat ../../BUILD_NUMBER)

VERSION_FILE = CUBRID-JDBC-$(JDBC_BUILD_NUMBER)
JDBC_DRIVER = JDBC-$(JDBC_BUILD_NUMBER)-cubrid.jar
JSP_DRIVER = jspserver.jar

all: src/$(VERSION_FILE) $(JDBC_DRIVER) $(JSP_DRIVER)

clean:
	ant -buildfile ../../jdbc/build.xml -Dbasedir=. -Dversion=$(JDBC_BUILD_NUMBER) clean
	ant -buildfile ../../java/build.xml -Dbasedir=. -Dversion=$(JDBC_BUILD_NUMBER) clean

src/$(VERSION_FILE):
	mkdir -p src
	touch $@

$(JDBC_DRIVER):
	ant -buildfile ../../jdbc/build.xml -Dbasedir=. -Dsrc=$(JDBC_SRC_DIR) -Dversion=$(JDBC_BUILD_NUMBER) dist-cubrid

$(JSP_DRIVER):
	ant -buildfile ../../java/build.xml -Dbasedir=. -Dsrc=$(JDBC_SRC_DIR) -Dversion=$(JDBC_BUILD_NUMBER) dist 
