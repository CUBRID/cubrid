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

/**
 * 
 * CUBRID Configuration parameter constants for cubrid.conf and cm.conf and
 * cubrid_broker.conf
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public interface ConfConstants {

	//database common section parameter
	public static String service_section = "[service]";
	public static String service_section_name = "service";
	public static String service = "service";
	public static String server = "server";
	public static String parameterTypeClient = "client";
	public static String parameterTypeServer = "server";
	public static String parameterTypeBoth = "client,server";
	public static String parameterTypeUtility = "utility only";
	public static String common_section = "[common]";
	public static String common_section_name = "common";
	public static String auto_restart_server = "auto_restart_server";
	public static String checkpoint_interval_in_mins = "checkpoint_interval_in_mins";
	public static String cubrid_port_id = "cubrid_port_id";
	public static String data_buffer_pages = "data_buffer_pages";
	public static String deadlock_detection_interval_in_secs = "deadlock_detection_interval_in_secs";
	public static String isolation_level = "isolation_level";
	public static String java_stored_procedure = "java_stored_procedure";
	public static String lock_escalation = "lock_escalation";
	public static String lock_timeout_in_secs = "lock_timeout_in_secs";
	public static String log_buffer_pages = "log_buffer_pages";
	public static String max_clients = "max_clients";
	public static String replication = "replication";
	public static String sort_buffer_pages = "sort_buffer_pages";
	public static String pthread_scope_process = "pthread_scope_process";
	public static String async_commit = "async_commit";
	public static String backup_volume_max_size_bytes = "backup_volume_max_size_bytes";
	public static String block_ddl_statement = "block_ddl_statement";
	public static String block_nowhere_statement = "block_nowhere_statement";
	public static String call_stack_dump_activation_list = "call_stack_dump_activation_list";
	public static String call_stack_dump_deactivation_list = "call_stack_dump_deactivation_list";
	public static String call_stack_dump_on_error = "call_stack_dump_on_error";
	public static String compactdb_page_reclaim_only = "compactdb_page_reclaim_only";
	public static String compat_numeric_division_scale = "compat_numeric_division_scale";
	public static String compat_primary_key = "compat_primary_key";
	public static String csql_history_num = "csql_history_num";
	public static String db_hosts = "db_hosts";
	public static String dont_reuse_heap_file = "dont_reuse_heap_file";
	public static String error_log = "error_log";
	public static String file_lock = "file_lock";
	public static String garbage_collection = "garbage_collection";
	public static String group_commit_interval_in_msecs = "group_commit_interval_in_msecs";
	public static String ha_mode = "ha_mode";
	public static String hostvar_late_binding = "hostvar_late_binding";
	public static String index_scan_in_oid_order = "index_scan_in_oid_order";
	public static String index_scan_oid_buffer_pages = "index_scan_oid_buffer_pages";
	public static String insert_execution_mode = "insert_execution_mode";
	public static String intl_mbs_support = "intl_mbs_support";
	public static String lock_timeout_message_type = "lock_timeout_message_type";
	public static String max_plan_cache_entries = "max_plan_cache_entries";
	public static String max_query_cache_entries = "max_query_cache_entries";
	public static String media_failure_support = "media_failure_support";
	public static String oracle_style_empty_string = "oracle_style_empty_string";
	public static String oracle_style_outerjoin = "oracle_style_outerjoin";
	public static String query_cache_mode = "query_cache_mode";
	public static String query_cache_size_in_pages = "query_cache_size_in_pages";
	public static String single_byte_compare = "single_byte_compare";
	public static String temp_file_max_size_in_pages = "temp_file_max_size_in_pages";
	public static String temp_file_memory_size_in_pages = "temp_file_memory_size_in_pages";
	public static String temp_volume_path = "temp_volume_path";
	public static String thread_stacksize = "thread_stacksize";
	public static String unfill_factor = "unfill_factor";
	public static String volume_extension_path = "volume_extension_path";

	//Transaction isolation level
	public static String TRAN_SERIALIZABLE = "TRAN_SERIALIZABLE";
	public static String TRAN_REP_CLASS_REP_INSTANCE = "TRAN_REP_CLASS_REP_INSTANCE";
	public static String TRAN_REP_CLASS_COMMIT_INSTANCE = "TRAN_REP_CLASS_COMMIT_INSTANCE";
	public static String TRAN_REP_CLASS_UNCOMMIT_INSTANCE = "TRAN_REP_CLASS_UNCOMMIT_INSTANCE";
	public static String TRAN_COMMIT_CLASS_COMMIT_INSTANCE = "TRAN_COMMIT_CLASS_COMMIT_INSTANCE";
	public static String TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE = "TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE";

	public static String[][] dbBaseParameters = {
			{ data_buffer_pages, "25000", parameterTypeServer },
			{ sort_buffer_pages, "16", parameterTypeServer },
			{ log_buffer_pages, "50", parameterTypeServer },
			{ lock_escalation, "100000", parameterTypeServer },
			{ lock_timeout_in_secs, "-1", parameterTypeServer },
			{ deadlock_detection_interval_in_secs, "1", parameterTypeServer },
			{ checkpoint_interval_in_mins, "1000", parameterTypeServer },
			{ isolation_level, "\"" + TRAN_REP_CLASS_UNCOMMIT_INSTANCE + "\"",
					parameterTypeServer },
			{ cubrid_port_id, "1523", parameterTypeClient },
			{ max_clients, "50", parameterTypeServer },
			{ auto_restart_server, "no", parameterTypeServer },
			{ replication, "no", parameterTypeServer },
			{ java_stored_procedure, "yes", parameterTypeServer }

	};
	public static String[][] dbAdvancedParameters = {
			{ async_commit, "bool(yes|no)", "no", parameterTypeServer },
			{ backup_volume_max_size_bytes, "int(v>=32*1024)", "-1",
					parameterTypeServer },
			{ block_ddl_statement, "bool(yes|no)", "no", parameterTypeClient },
			{ block_nowhere_statement, "bool(yes|no)", "no",
					parameterTypeClient },
			{ call_stack_dump_activation_list, "string", "", parameterTypeBoth },
			{ call_stack_dump_deactivation_list, "string", "",
					parameterTypeBoth },
			{ call_stack_dump_on_error, "bool(yes|no)", "no", parameterTypeBoth },
			{ compactdb_page_reclaim_only, "int", "0", parameterTypeUtility },
			{ compat_numeric_division_scale, "bool(yes|no)", "no",
					parameterTypeBoth },
			{ compat_primary_key, "bool(yes|no)", "no", parameterTypeClient },
			{ csql_history_num, "int(v>=1&&v<=200)", "50", parameterTypeClient },
			{ db_hosts, "string", "", parameterTypeClient },
			{ dont_reuse_heap_file, "bool(yes|no)", "no", parameterTypeServer },
			{ error_log, "string", "cubrid.err", parameterTypeBoth },
			{ file_lock, "bool(yes|no)", "yes", parameterTypeServer },
			{ garbage_collection, "bool(yes|no)", "no", parameterTypeClient },
			{ group_commit_interval_in_msecs, "int(v>=0)", "0",
					parameterTypeServer },
			{ ha_mode, "string(on|off)", "off", parameterTypeServer },
			{ hostvar_late_binding, "bool(yes|no)", "no", parameterTypeClient },
			{ index_scan_in_oid_order, "bool(yes|no)", "no",
					parameterTypeClient },
			{ index_scan_oid_buffer_pages, "int(v>=1&&v<=16)", "4",
					parameterTypeServer },
			{ insert_execution_mode, "int(v>=1&&v<=7)", "1",
					parameterTypeClient },
			{ intl_mbs_support, "bool(yes|no)", "no", parameterTypeClient },
			{ lock_timeout_message_type, "int(v>=0&&v<=2)", "0",
					parameterTypeServer },
			{ max_plan_cache_entries, "int", "1000", parameterTypeBoth },
			{ max_query_cache_entries, "int", "-1", parameterTypeServer },
			{ media_failure_support, "bool(yes|no)", "yes", parameterTypeServer },
			{ oracle_style_empty_string, "bool(yes|no)", "no",
					parameterTypeClient },
			{ oracle_style_outerjoin, "bool(yes|no)", "no", parameterTypeClient },
			{ pthread_scope_process, "bool(yes|no)", "yes", parameterTypeServer },
			{ query_cache_mode, "int(v>=0&&v<=2)", "0", parameterTypeServer },
			{ query_cache_size_in_pages, "int", "-1", parameterTypeServer },
			{ single_byte_compare, "bool(yes|no)", "no", parameterTypeServer },
			{ temp_file_max_size_in_pages, "int", "-1", parameterTypeServer },
			{ temp_file_memory_size_in_pages, "int(v>=0&&v<=20)", "4",
					parameterTypeServer },
			{ temp_volume_path, "string", "", parameterTypeServer },
			{ thread_stacksize, "int(v>=64*1024)", "100*1024",
					parameterTypeServer },
			{ unfill_factor, "float(v>=0&&v<=0.3)", "0.1", parameterTypeServer },
			{ volume_extension_path, "string", "", parameterTypeServer } };

	//manager parameter
	public static String cm_port = "cm_port";
	public static String monitor_interval = "monitor_interval";
	public static String allow_user_multi_connection = "allow_user_multi_connection";
	public static String auto_start_broker = "auto_start_broker";
	public static String execute_diag = "execute_diag";
	public static String server_long_query_time = "server_long_query_time";
	public static String CM_TARGET = "cm_target";
	public static String[][] cmParameters = { { cm_port, "int", "8001" },
			{ monitor_interval, "int", "5" },
			{ allow_user_multi_connection, "string", "YES" },
			{ auto_start_broker, "string", "YES" },
			{ execute_diag, "string", "OFF" },
			{ server_long_query_time, "int", "10" },
			{ CM_TARGET, "string", "broker,server" } };

	//broker parameter
	public static String broker_section = "[broker]";
	public static String broker_sectionName = "broker";
	public static String MASTER_SHM_ID = "MASTER_SHM_ID";
	public static String ADMIN_LOG_FILE = "ADMIN_LOG_FILE";
	public static String SERVICE = "SERVICE";
	public static String BROKER_PORT = "BROKER_PORT";
	public static String MIN_NUM_APPL_SERVER = "MIN_NUM_APPL_SERVER";
	public static String MAX_NUM_APPL_SERVER = "MAX_NUM_APPL_SERVER";
	public static String APPL_SERVER_SHM_ID = "APPL_SERVER_SHM_ID";
	//	public static String APPL_SERVER_MAX_SIZE = "APPL_SERVER_MAX_SIZE";
	public static String LOG_DIR = "LOG_DIR";
	public static String ERROR_LOG_DIR = "ERROR_LOG_DIR";
	public static String SQL_LOG = "SQL_LOG";
	public static String TIME_TO_KILL = "TIME_TO_KILL";
	public static String SESSION_TIMEOUT = "SESSION_TIMEOUT";
	public static String KEEP_CONNECTION = "KEEP_CONNECTION";
	public static String ACCESS_LIST = "ACCESS_LIST";
	public static String ACCESS_LOG = "ACCESS_LOG";
	public static String APPL_SERVER_PORT = "APPL_SERVER_PORT";
	//	public static String APPL_SERVER = "APPL_SERVER";
	public static String LOG_BACKUP = "LOG_BACKUP";
	public static String SQL_LOG_MAX_SIZE = "SQL_LOG_MAX_SIZE";
	public static String MAX_STRING_LENGTH = "MAX_STRING_LENGTH";
	public static String SOURCE_ENV = "SOURCE_ENV";
	public static String STATEMENT_POOLING = "STATEMENT_POOLING";
	public static String LONG_QUERY_TIME = "LONG_QUERY_TIME";
	public static String LONG_TRANSACTION_TIME = "LONG_TRANSACTION_TIME";

	public static String[][] brokerParameters = {
			{ MASTER_SHM_ID, "int", "30001" },
			{ ADMIN_LOG_FILE, "string", "log/broker/cubrid_broker.log" },
			{ SERVICE, "string(ON|OFF)", "ON" },
			{ BROKER_PORT, "int(1024~65535)", "" },
			{ MIN_NUM_APPL_SERVER, "int", "5" },
			{ MAX_NUM_APPL_SERVER, "int", "40" },
			{ APPL_SERVER_SHM_ID, "int(1024~65535)", "" },
			//	{ APPL_SERVER_MAX_SIZE, "int", "20" },
			{ LOG_DIR, "string", "log/broker/sql_log" },
			{ ERROR_LOG_DIR, "string", "log/broker/error_log" },
			{ SQL_LOG, "string(ON|OFF|ERROR|NOTICE|TIMEOUT)", "ON" },
			{ TIME_TO_KILL, "int", "120" }, { SESSION_TIMEOUT, "int", "300" },
			{ KEEP_CONNECTION, "string(ON|OFF|AUTO)", "AUTO" },

			{ STATEMENT_POOLING, "string(ON|OFF)", "ON" },
			{ LONG_QUERY_TIME, "int", "60" },
			{ LONG_TRANSACTION_TIME, "int", "60" },
			{ SQL_LOG_MAX_SIZE, "int", "100000" },
			{ LOG_BACKUP, "string(ON|OFF)", "OFF" },
			{ SOURCE_ENV, "string", "cubrid.env" },
			{ MAX_STRING_LENGTH, "int", "-1" },
			{ APPL_SERVER_PORT, "int", "" },
			//	{ APPL_SERVER, "string(CAS)", "CAS" },
			{ ACCESS_LOG, "string(ON|OFF)", "ON" },
			{ ACCESS_LIST, "string", "" } };

}
