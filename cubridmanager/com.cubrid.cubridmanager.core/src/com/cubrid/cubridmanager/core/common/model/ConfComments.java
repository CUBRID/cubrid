/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */

package com.cubrid.cubridmanager.core.common.model;

import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.core.CubridManagerCorePlugin;
import com.cubrid.cubridmanager.core.common.log.LogUtil;

/**
 * 
 * This is message bundle classes and provide convenience methods for
 * manipulating messages,it provide comments for cubrid.conf and cm.conf and
 * cubrid_broker.conf
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ConfComments {

	private static final Logger logger = LogUtil.getLogger(ConfComments.class);
	static {
		NLS.initializeMessages(CubridManagerCorePlugin.PLUGIN_ID
				+ ".common.model.ConfComments", ConfComments.class);
	}

	/**
	 * 
	 * Get comments of parameter
	 * 
	 * @param parameter
	 * @return
	 */
	public static String getComments(String parameter) {
		try {
			if (parameter.equals(ConfConstants.common_section)) {
				parameter = "common_section";
			}
			if (parameter.equals(ConfConstants.service_section)) {
				parameter = "service_section";
			}
			return (String) ConfComments.class.getField(parameter + "_comments").get(
					new ConfComments());
		} catch (Exception e) {
			logger.error(e);
		}
		return "";
	}

	/**
	 * 
	 * Parse comments and provide comments multi line support
	 * 
	 * @param commentsList
	 * @param comments
	 */
	public static void addComments(List<String> commentsList, String comments) {
		String[] commentsArr = comments.split("\r\n");
		for (int i = 0; i < commentsArr.length; i++) {
			if (commentsArr[i].trim().indexOf("#") == 0) {
				commentsList.add(commentsArr[i]);
			} else if (commentsArr[i].trim().length() == 0) {
				commentsList.add("");
			} else {
				commentsList.add("#" + commentsArr[i]);
			}
		}
	}

	//CUBRID copyright comments
	public static String cubrid_copyright_comments;
	//database common section parameter
	public static String service_section_comments;
	public static String service_comments;
	public static String server_comments;
	public static String common_section_comments;
	public static String auto_restart_server_comments;
	public static String checkpoint_interval_in_mins_comments;
	public static String cubrid_port_id_comments;
	public static String data_buffer_pages_comments;
	public static String deadlock_detection_interval_in_secs_comments;
	public static String isolation_level_comments;
	public static String java_stored_procedure_comments;
	public static String lock_escalation_comments;
	public static String lock_timeout_in_secs_comments;
	public static String log_buffer_pages_comments;
	public static String max_clients_comments;
	public static String replication_comments;
	public static String sort_buffer_pages_comments;
	public static String pthread_scope_process_comments;
	public static String async_commit_comments;
	public static String backup_volume_max_size_bytes_comments;
	public static String block_ddl_statement_comments;
	public static String block_nowhere_statement_comments;
	public static String call_stack_dump_activation_list_comments;
	public static String call_stack_dump_deactivation_list_comments;
	public static String call_stack_dump_on_error_comments;
	public static String compactdb_page_reclaim_only_comments;
	public static String compat_numeric_division_scale_comments;
	public static String compat_primary_key_comments;
	public static String csql_history_num_comments;
	public static String db_hosts_comments;
	public static String dont_reuse_heap_file_comments;
	public static String error_log_comments;
	public static String file_lock_comments;
	public static String garbage_colleciton_comments;
	public static String group_commit_interval_in_msecs_comments;
	public static String hostvar_late_binding_comments;
	public static String index_scan_in_oid_order_comments;
	public static String index_scan_oid_buffer_pages_comments;
	public static String insert_execution_mode_comments;
	public static String intl_mbs_support_comments;
	public static String lock_timeout_message_type_comments;
	public static String max_plan_cache_entries_comments;
	public static String max_query_cache_entries_comments;
	public static String media_failure_support_comments;
	public static String oracle_style_empty_string_comments;
	public static String oracle_style_outerjoin_comments;
	public static String query_cache_mode_comments;
	public static String query_cache_size_in_pages_comments;
	public static String single_byte_compare_comments;
	public static String temp_file_max_size_in_pages_comments;
	public static String temp_file_memory_size_in_pages_comments;
	public static String temp_volume_path_comments;
	public static String thread_stack_size_comments;
	public static String unfill_factor_comments;
	public static String volume_extension_path_comments;

}
