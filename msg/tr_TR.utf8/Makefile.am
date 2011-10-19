
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


tr_TR.utf8dir = @CUBRID_LOCALEDIR@/en_US
dist_tr_TR.utf8_DATA = cubrid.msg csql.msg utils.msg esql.msg syntax.txt
tr_TR.utf8_DATA = cubrid.cat csql.cat utils.cat esql.cat

GENCAT = $(top_builddir)/util/gencat

SUFFIXES = .msg.cat

.msg.cat:
	$(GENCAT) $@ $<

distclean-local:
	-rm -f $(tr_TR.utf8_DATA)
