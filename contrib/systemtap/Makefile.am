
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

AUTOMAKE_OPTIONS = foreign

if ENABLE_SYSTEMTAP
all-local: probes.d
	dtrace -C -h -s probes.d -o probes.h
	dtrace -C -G -s probes.d -o probes.o
	echo "Compile dtrace probe"
clean-local:
	-rm -rf probes.h probes.o $(TAPSET_DIR)/cubrid_func.stp
	-rm -rf $(prefix)/share/systemtap

install-data-local:
	cp cubrid_func.stp $(TAPSET_DIR)
	cp -R ../systemtap $(prefix)/share

endif
