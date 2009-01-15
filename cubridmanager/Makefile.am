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

SRC = $(top_srcdir)/src
CM_DIR = ${top_srcdir}/cubridmanager

PRODUCT_DIR = product
WIN32_DIR = win32
LINUX_DIR = cmclient
RCP_DIR = ${CM_DIR}/rcpfiles
CUBRIDMANAGER = cubridmanager
WIN32_DEPLOY = .
LINUX_DEPLOY = .

cmdir = ${prefix}/${CUBRIDMANAGER}

all: clean-am ant-build win32 linux

ant-build:
	ant all -buildfile ${CM_DIR}/build.xml -Dbasedir=. -Dcubridmanager.location=${CM_DIR} -Djdbc.file=../jdbc/JDBC-@BUILD_NUMBER@.jar

win32: ant-build
	mkdir -p ${WIN32_DEPLOY}
	cp -rf $(PRODUCT_DIR)/win32 $(WIN32_DEPLOY)

linux: ant-build
	mkdir -p ${LINUX_DEPLOY}
	cp -rf ${PRODUCT_DIR}/cmclient ${LINUX_DEPLOY}

clean-am: 
	ant clean -buildfile ${CM_DIR}/build.xml -Dbasedir=.
	rm -rf ${WIN32_DIR} ${LINUX_DIR} ${CUBRIDMANAGER}

install-exec-hook:
	test -z "$(cmdir)" || mkdir -p "$(cmdir)"
	rm -rf ${cmdir}/cmclient*
	cp -r cmclient* ${cmdir}
