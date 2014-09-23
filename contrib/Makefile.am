
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
dist_hascripts_SCRIPTS = \
	scripts/ha/ha_make_slavedb.sh \
	../BUILD_NUMBER
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

EXTRA_DIST += systemtap/probes.d
systemtapdir = ${datadir}/systemtap/tapset
dist_systemtap_DATA = systemtap/cubrid_func.stp

systemtap_scriptsdir = ${datadir}/systemtap/tapset/scripts
dist_systemtap_scripts_DATA = \
	systemtap/scripts/buffer_access.stp \
	systemtap/scripts/global_time.stp \
	systemtap/scripts/io_stat.stp \
	systemtap/scripts/lock_time.stp \
	systemtap/scripts/obj_access.stp \
	systemtap/scripts/query_time.stp \
	systemtap/scripts/transaction.stp \
	systemtap/scripts/topcallstack/get_file_line.sh \
	systemtap/scripts/topcallstack/topcallstack \
	systemtap/scripts/topcallstack/topcallstack_helper.stp
