#
# Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution.
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

BUILD_HOME=`cd ${top_builddir} && pwd`
CM_DIR = ${top_srcdir}/cubridmanager/com.cubrid.cubridmanager.build
BUILD_DIR=${BUILD_HOME}/cubridmanager/com.cubrid.cubridmanager.build
JDBC_DIR=${BUILD_HOME}/jdbc

PRODUCT_DIR=${BUILD_DIR}/deploy
PRO_WIN=${PRODUCT_DIR}/win
PRO_L32=${PRODUCT_DIR}/linux32
PRO_L64=${PRODUCT_DIR}/linux64
PRO_PLG=${PRODUCT_DIR}/plugins
PRO_P33=${PRODUCT_DIR}/plugins/3.3
PRO_P34=${PRODUCT_DIR}/plugins/3.4

CM_RCP=cubridmanager
CM_PLG=plugins
RCP_INST=${prefix}/${CM_RCP}
PLG_INST=${prefix}/${CM_PLG}
PLG33_INST=${prefix}/${CM_PLG}/3.3/plugins
PLG34_INST=${prefix}/${CM_PLG}/3.4/plugins

all: clean-am ant-build win linux32 linux64 plugins

ant-build:
	ant all -buildfile ${CM_DIR}/build.xml -Dbuild.dir=${BUILD_DIR} -Dproduct.version=${BUILD_NUMBER} -Djdbc.file=${JDBC_DIR}/JDBC-@BUILD_NUMBER@.jar
	
win: ant-build
	mkdir -p ${PRODUCT_DIR}

linux32: ant-build
	mkdir -p ${PRODUCT_DIR}
	
linux64: ant-build
	mkdir -p ${PRODUCT_DIR}

plugins: ant-build
	mkdir -p ${PRODUCT_DIR}

clean-am: 
	ant clean -buildfile ${CM_DIR}/build.xml

install-exec-hook:
	test -z "$(RCP_INST)" || mkdir -p "$(RCP_INST)"  
	rm -rf ${RCP_INST}/*  
	
	if test "$(BIT_MODEL)" == "-m32"; then \
		cp -r ${PRO_L32}/* ${RCP_INST} ; \
	else \
		cp -r ${PRO_L64}/* ${RCP_INST} ; \
	fi 
		
