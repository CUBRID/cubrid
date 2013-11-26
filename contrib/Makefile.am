
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

EXTRA_DIST = php4 php5 adodotnet hibernate python ruby collectd-plugin msg

initdir = ${datadir}/init.d
dist_init_SCRIPTS = init.d/cubrid-ha init.d/cubrid

rpmdir = ${datadir}/rpm
dist_rpm_SCRIPTS = rpm/cubrid.csh rpm/cubrid.sh
dist_rpm_DATA = rpm/cubrid.spec

scriptsdir = ${datadir}/scripts
dist_scripts_SCRIPTS = \
	scripts/check_reserved.sql \
	scripts/broker_log_top.py \
	scripts/brokerstatus_to_csv.py \
	scripts/statdump_to_csv.py

hascriptsdir = ${datadir}/scripts/ha
dist_hascripts_SCRIPTS = scripts/ha/ha_make_slavedb.sh
dist_hascripts_DATA = scripts/ha/README

hafuncdir = ${hascriptsdir}/functions
dist_hafunc_SCRIPTS = \
	scripts/ha/functions/ha_check_copylog.sh \
	scripts/ha/functions/ha_check_environment.sh \
	scripts/ha/functions/ha_check_script.sh \
	scripts/ha/functions/ha_repl_copylog.sh \
	scripts/ha/functions/ha_repl_reset.sh \
	scripts/ha/functions/ha_repl_resume.sh \
	scripts/ha/functions/ha_repl_suspend.sh \
	scripts/ha/functions/ha_set_apply_info.sh

haexpdir = ${hascriptsdir}/expect
dist_haexp_SCRIPTS = \
	scripts/ha/expect/scp_from.exp \
	scripts/ha/expect/scp_to.exp \
	scripts/ha/expect/ssh.exp
	
hacommondir = ${hascriptsdir}/common
dist_hacommon_SCRIPTS = scripts/ha/common/common.sh
