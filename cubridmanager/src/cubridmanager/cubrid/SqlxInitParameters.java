package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.Messages;

public class SqlxInitParameters {
	private static ArrayList items = new ArrayList();

	public static ArrayList getSqlxInitParameters() {
		// items.add(new ParameterItem("api_trace_file", ParameterItem.tString,
		// "NULL", "", "", Messages.getString("SQLXDESC.API_TRACE_FILE"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("api_trace_mode", ParameterItem.tInteger,
		// "-1", "", "", Messages.getString("SQLXDESC.API_TRACE_MODE"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("audit_buffer_size",
		// ParameterItem.tInteger, "128", "64", "32768",
		// Messages.getString("SQLXDESC.AUDIT_BUFFER_SIZE"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("audit_flush_interval",
		// ParameterItem.tInteger, "30", "0", "",
		// Messages.getString("SQLXDESC.AUDIT_FLUSH_INTERVAL"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("audit_init_state",
		// ParameterItem.tInteger, "-1", "-1", "1",
		// Messages.getString("SQLXDESC.AUDIT_INIT_STATE"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("audit_set_all_on_new_classes",
		// ParameterItem.tBoolean, "0", "", "",
		// Messages.getString("SQLXDESC.AUDIT_SET_ALL_ON_NEW_CLASSES"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("audit_trail_dir", ParameterItem.tString,
		// "NULL", "", "", Messages.getString("SQLXDESC.AUDIT_TRAIL_DIR"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("audit_trail_max_size",
		// ParameterItem.tInteger, "100", "1", "1024",
		// Messages.getString("SQLXDESC.AUDIT_TRAIL_MAX_SIZE"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("auto_commit", ParameterItem.tBoolean,
		// "1", "", "", Messages.getString("SQLXDESC.AUTO_COMMIT"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("auto_volext_factor", ParameterItem.tFloat,
				"0", "0", "",
				Messages.getString("SQLXDESC.AUTO_VOLEXT_FACTOR"),
				ParameterItem.uServer));
		items.add(new ParameterItem("backup_iobuffer_size",
				ParameterItem.tInteger, "1", "1", "", Messages
						.getString("SQLXDESC.BACKUP_IOBUFFER_SIZE"),
				ParameterItem.uServer));
		items.add(new ParameterItem("backup_max_volume_size",
				ParameterItem.tInteger, "-1", "32768", "", Messages
						.getString("SQLXDESC.BACKUP_MAX_VOLUME_SIZE"),
				ParameterItem.uServer));
		items.add(new ParameterItem("call_stack_dump_disable",
				ParameterItem.tString, "", "", "", Messages
						.getString("SQLXDESC.CALL_STACK_DUMP_DISABLE"),
				ParameterItem.uBoth));
		items.add(new ParameterItem("call_stack_dump_enable",
				ParameterItem.tString, "", "", "", Messages
						.getString("SQLXDESC.CALL_STACK_DUMP_ENABLE"),
				ParameterItem.uBoth));
		items.add(new ParameterItem("call_stack_dump_on_error",
				ParameterItem.tBoolean, "0", "", "", Messages
						.getString("SQLXDESC.CALL_STACK_DUMP_ON_ERROR"),
				ParameterItem.uBoth));
		// items.add(new ParameterItem("catalog_oid_table_size",
		// ParameterItem.tInteger, "1024", "50", "",
		// Messages.getString("SQLXDESC.CATALOG_OID_TABLE_SIZE"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("check_deleted_objects",
		// ParameterItem.tInteger, "1", "", "",
		// Messages.getString("SQLXDESC.CHECK_DELETED_OBJECTS"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("checkpoint_interval",
				ParameterItem.tInteger, "144000", "10", "", Messages
						.getString("SQLXDESC.CHECKPOINT_INTERVAL"),
				ParameterItem.uServer));
		items.add(new ParameterItem("checkpoint_interval_minutes",
				ParameterItem.tInteger, "1440", "1", "", Messages
						.getString("SQLXDESC.CHECKPOINT_INTERVAL_MINUTES"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("class_representation_cache",
		// ParameterItem.tInteger, "100", "10", "",
		// Messages.getString("SQLXDESC.CLASS_REPRESENTATION_CACHE"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("client_timeout", ParameterItem.tInteger,
				"120", "", "", Messages.getString("SQLXDESC.CLIENT_TIMEOUT"),
				ParameterItem.uClient));
		items.add(new ParameterItem("commit_on_shutdown",
				ParameterItem.tBoolean, "0", "", "", Messages
						.getString("SQLXDESC.COMMIT_ON_SHUTDOWN"),
				ParameterItem.uClient));
		// items.add(new ParameterItem("compactdb_page_reclaim_only",
		// ParameterItem.tInteger, "0", "", "",
		// Messages.getString("SQLXDESC.COMPACTDB_PAGE_RECLAIM_ONLY"),
		// ParameterItem.uNone));
		items.add(new ParameterItem("connection_timeout",
				ParameterItem.tInteger, "2", "1", "", Messages
						.getString("SQLXDESC.CONNECTION_TIMEOUT"),
				ParameterItem.uClient));
		// items.add(new ParameterItem("cpu_weight", ParameterItem.tFloat,
		// "0.0025", "0", "1", Messages.getString("SQLXDESC.CPU_WEIGHT"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("cubrid_port_id", ParameterItem.tInteger,
				"0", "", "", Messages.getString("SQLXDESC.CUBRID_PORT_ID"),
				ParameterItem.uBoth));
		items.add(new ParameterItem("db_hosts", ParameterItem.tString, "NULL",
				"", "", Messages.getString("SQLXDESC.DB_HOSTS"),
				ParameterItem.uClient));
		items.add(new ParameterItem("deadlock_detection_interval",
				ParameterItem.tInteger, "1", "1", "", Messages
						.getString("SQLXDESC.DEADLOCK_DETECTION_INTERVAL"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("default_driver_dir",
		// ParameterItem.tString, "NULL", "", "",
		// Messages.getString("SQLXDESC.DEFAULT_DRIVER_DIR"),
		// ParameterItem.uClient));
		items
				.add(new ParameterItem(
						"disable_default_numeric_division_scale",
						ParameterItem.tBoolean,
						"0",
						"",
						"",
						Messages
								.getString("SQLXDESC.DISABLE_DEFAULT_NUMERIC_DIVISION_SCALE"),
						ParameterItem.uBoth));
		// items.add(new ParameterItem("do_ldb_class", ParameterItem.tBoolean,
		// "0", "", "", Messages.getString("SQLXDESC.DO_LDB_CLASS"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("dont_reuse_heap_file",
		// ParameterItem.tBoolean, "0", "", "",
		// Messages.getString("SQLXDESC.DONT_REUSE_HEAP_FILE"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("driver_page_fetch",
		// ParameterItem.tBoolean, "1", "", "",
		// Messages.getString("SQLXDESC.DRIVER_PAGE_FETCH"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("driver_timeout", ParameterItem.tInteger,
		// "-1", "", "", Messages.getString("SQLXDESC.DRIVER_TIMEOUT"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("enable_histo", ParameterItem.tBoolean,
		// "0", "", "", Messages.getString("SQLXDESC.ENABLE_HISTO"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("error_log", ParameterItem.tString, "", "",
				"", Messages.getString("SQLXDESC.ERROR_LOG"),
				ParameterItem.uBoth));
		// items.add(new ParameterItem("event_handler", ParameterItem.tString,
		// "NULL", "", "", Messages.getString("SQLXDESC.EVENT_HANDLER"),
		// ParameterItem.uBoth));
		// items.add(new ParameterItem("gc_block_increment",
		// ParameterItem.tInteger, "4", "", "",
		// Messages.getString("SQLXDESC.GC_BLOCK_INCREMENT"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("gc_enable", ParameterItem.tInteger, "0",
				"", "", Messages.getString("SQLXDESC.GC_ENABLE"),
				ParameterItem.uClient));
		items.add(new ParameterItem("index_scan_order", ParameterItem.tInteger,
				"0", "", "", Messages.getString("SQLXDESC.INDEX_SCAN_ORDER"),
				ParameterItem.uClient));
		items.add(new ParameterItem("initial_object_lock_table_size",
				ParameterItem.tInteger, "10000", "1000", "", Messages
						.getString("SQLXDESC.INITIAL_OBJECT_LOCK_TABLE_SIZE"),
				ParameterItem.uServer));
		items.add(new ParameterItem("initial_workspace_table_size",
				ParameterItem.tInteger, "4092", "1024", "", Messages
						.getString("SQLXDESC.INITIAL_WORKSPACE_TABLE_SIZE"),
				ParameterItem.uClient));
		// items.add(new ParameterItem("inquire_on_exit",
		// ParameterItem.tUnknown, "", "", "",
		// Messages.getString("SQLXDESC.INQUIRE_ON_EXIT"),
		// ParameterItem.uNotUse));
		// items.add(new ParameterItem("io_volinfo_increment",
		// ParameterItem.tInteger, "32", "8", "1024",
		// Messages.getString("SQLXDESC.IO_VOLINFO_INCREMENT"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("is_pthread_scope_process",
				ParameterItem.tBoolean, "1", "", "", Messages
						.getString("SQLXDESC.IS_PTHREAD_SCOPE_PROCESS"),
				ParameterItem.uServer));
		items.add(new ParameterItem("isolation_level",
				ParameterItem.tIsolation, "TRAN_REP_CLASS_COMMIT_INSTANCE",
				"TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE", "TRAN_SERIALIZABLE",
				Messages.getString("SQLXDESC.ISOLATION_LEVEL"),
				ParameterItem.uClient));
		// items.add(new ParameterItem("ldb_driver_decay_constant",
		// ParameterItem.tInteger, "300", "60", "",
		// Messages.getString("SQLXDESC.LDB_DRIVER_DECAY_CONSTANT"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("ldb_max_active_drivers",
		// ParameterItem.tInteger, "10", "1", "",
		// Messages.getString("SQLXDESC.LDB_MAX_ACTIVE_DRIVERS"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("ldb_min_active_drivers",
		// ParameterItem.tInteger, "1", "0", "",
		// Messages.getString("SQLXDESC.LDB_MIN_ACTIVE_DRIVERS"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("ldb_preconnect_list",
		// ParameterItem.tString, "NULL", "", "",
		// Messages.getString("SQLXDESC.LDB_PRECONNECT_LIST"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("like_term_selectivity",
		// ParameterItem.tFloat, "0.1", "0", "1",
		// Messages.getString("SQLXDESC.LIKE_TERM_SELECTIVITY"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("lock_escalation", ParameterItem.tInteger,
				"100000", "5", "", Messages
						.getString("SQLXDESC.LOCK_ESCALATION"),
				ParameterItem.uServer));
		items.add(new ParameterItem("lock_timeout_in_secs",
				ParameterItem.tInteger, "-1", "-1", "", Messages
						.getString("SQLXDESC.LOCK_TIMEOUT_IN_SECS"),
				ParameterItem.uServer));
		items.add(new ParameterItem("lock_timeout_message_dump_level",
				ParameterItem.tInteger, "0", "0", "2", Messages
						.getString("SQLXDESC.LOCK_TIMEOUT_MESSAGE_DUMP_LEVEL"),
				ParameterItem.uServer));
		items.add(new ParameterItem("lockf_enable", ParameterItem.tBoolean,
				"1", "", "", Messages.getString("SQLXDESC.LOCKF_ENABLE"),
				ParameterItem.uServer));
		items.add(new ParameterItem("log_path", ParameterItem.tString, "NULL",
				"", "", Messages.getString("SQLXDESC.LOG_PATH"),
				ParameterItem.uServer));
		items.add(new ParameterItem("log_prefix_name", ParameterItem.tString,
				"NULL", "", "", Messages.getString("SQLXDESC.LOG_PREFIX_NAME"),
				ParameterItem.uServer));
		items.add(new ParameterItem("log_reserve_space",
				ParameterItem.tBoolean, "0", "", "", Messages
						.getString("SQLXDESC.LOG_RESERVE_SPACE"),
				ParameterItem.uServer));
		items.add(new ParameterItem("log_size", ParameterItem.tInteger, "-1",
				"100", "", Messages.getString("SQLXDESC.LOG_SIZE"),
				ParameterItem.uServer));
		items.add(new ParameterItem("max_block_count", ParameterItem.tInteger,
				"128", "1", "", Messages.getString("SQLXDESC.MAX_BLOCK_COUNT"),
				ParameterItem.uClient));
		items.add(new ParameterItem("max_block_size", ParameterItem.tInteger,
				"128", "16", "8192", Messages
						.getString("SQLXDESC.MAX_BLOCK_SIZE"),
				ParameterItem.uClient));
		// items.add(new ParameterItem("max_classname_cache_entries",
		// ParameterItem.tInteger, "1024", "50", "",
		// Messages.getString("SQLXDESC.MAX_CLASSNAME_CACHE_ENTRIES"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("max_clients", ParameterItem.tInteger,
				"50", "1", "", Messages.getString("SQLXDESC.MAX_CLIENTS"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("max_entries_in_temp_file_cache",
		// ParameterItem.tInteger, "1024", "1024", "",
		// Messages.getString("SQLXDESC.MAX_ENTRIES_IN_TEMP_FILE_CACHE"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("max_index_scan_count",
		// ParameterItem.tInteger, "32", "32", "128",
		// Messages.getString("SQLXDESC.MAX_INDEX_SCAN_COUNT"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("max_outer_card_of_idxjoin",
		// ParameterItem.tInteger, "0", "0", "",
		// Messages.getString("SQLXDESC.MAX_OUTER_CARD_OF_IDXJOIN"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("max_pages_in_temp_file_cache",
		// ParameterItem.tInteger, "1000", "100", "",
		// Messages.getString("SQLXDESC.MAX_PAGES_IN_TEMP_FILE_CACHE"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("max_ping_wait", ParameterItem.tInteger,
				"5", "", "", Messages.getString("SQLXDESC.MAX_PING_WAIT"),
				ParameterItem.uServer));
		items.add(new ParameterItem("max_quick_size", ParameterItem.tInteger,
				"128", "32", "1024", Messages
						.getString("SQLXDESC.MAX_QUICK_SIZE"),
				ParameterItem.uClient));
		items.add(new ParameterItem("max_threads", ParameterItem.tInteger,
				"100", "2", "", Messages.getString("SQLXDESC.MAX_THREADS"),
				ParameterItem.uServer));
		items.add(new ParameterItem("maxlength_error_log",
				ParameterItem.tInteger, "240000", "", "", Messages
						.getString("SQLXDESC.MAXLENGTH_ERROR_LOG"),
				ParameterItem.uBoth));
		items.add(new ParameterItem("maxtmp_pages", ParameterItem.tInteger,
				"-1", "8000", "", Messages.getString("SQLXDESC.MAXTMP_PAGES"),
				ParameterItem.uServer));
		items.add(new ParameterItem("media_failures_are_supported",
				ParameterItem.tBoolean, "1", "", "", Messages
						.getString("SQLXDESC.MEDIA_FAILURES_ARE_SUPPORTED"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("memory_report_level",
		// ParameterItem.tInteger, "0", "0", "",
		// Messages.getString("SQLXDESC.MEMORY_REPORT_LEVEL"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("memory_verification_level",
		// ParameterItem.tInteger, "1", "0", "",
		// Messages.getString("SQLXDESC.MEMORY_VERIFICATION_LEVEL"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("min_num_rows_for_multi_delete",
		// ParameterItem.tInteger, "20", "1", "",
		// Messages.getString("SQLXDESC.MIN_NUM_ROWS_FOR_MULTI_DELETE"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("mutex_busy_waiting_cnt",
		// ParameterItem.tInteger, "0", "", "",
		// Messages.getString("SQLXDESC.MUTEX_BUSY_WAITING_CNT"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("network_pagesize", ParameterItem.tInteger,
				"32768", "1024", "1048576", Messages
						.getString("SQLXDESC.NETWORK_PAGESIZE"),
				ParameterItem.uBoth));
		items.add(new ParameterItem("network_service_name",
				ParameterItem.tString, "NULL", "", "", Messages
						.getString("SQLXDESC.NETWORK_SERVICE_NAME"),
				ParameterItem.uBoth));
		// items.add(new ParameterItem("new_classname_cache_size",
		// ParameterItem.tInteger, "1025", "50", "",
		// Messages.getString("SQLXDESC.NEW_CLASSNAME_CACHE_SIZE"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("no_range_optimization",
		// ParameterItem.tBoolean, "0", "", "",
		// Messages.getString("SQLXDESC.NO_RANGE_OPTIMIZATION"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("num_data_buffers", ParameterItem.tInteger,
				"10000", "1", "", Messages
						.getString("SQLXDESC.NUM_DATA_BUFFERS"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("num_index_oid_buffers",
		// ParameterItem.tInteger, "8", "1", "16",
		// Messages.getString("SQLXDESC.NUM_INDEX_OID_BUFFERS"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("num_log_buffers", ParameterItem.tInteger,
				"50", "3", "", Messages.getString("SQLXDESC.NUM_LOG_BUFFERS"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("num_LRU_chains", ParameterItem.tInteger,
		// "0", "0", "1000", Messages.getString("SQLXDESC.NUM_LRU_CHAINS"),
		// ParameterItem.uServer));
		// items.add(new ParameterItem("oid_batch_size", ParameterItem.tInteger,
		// "2000", "1", "", Messages.getString("SQLXDESC.OID_BATCH_SIZE"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("optimization_level",
				ParameterItem.tInteger, "1", "", "", Messages
						.getString("SQLXDESC.OPTIMIZATION_LEVEL"),
				ParameterItem.uClient));
		// items.add(new ParameterItem("optimization_limit",
		// ParameterItem.tInteger, "32", "0", "32",
		// Messages.getString("SQLXDESC.OPTIMIZATION_LIMIT"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("optimize_hostvars_at_runtime",
		// ParameterItem.tBoolean, "0", "", "",
		// Messages.getString("SQLXDESC.OPTIMIZE_HOSTVARS_AT_RUNTIME"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("oracle_style_empty_string",
				ParameterItem.tBoolean, "0", "", "", Messages
						.getString("SQLXDESC.ORACLE_STYLE_EMPTY_STRING"),
				ParameterItem.uClient));
		// items.add(new ParameterItem("oracle_style_outerjoin",
		// ParameterItem.tBoolean, "0", "", "",
		// Messages.getString("SQLXDESC.ORACLE_STYLE_OUTERJOIN"),
		// ParameterItem.uNone));
		// items.add(new ParameterItem("qo_dump_level", ParameterItem.tInteger,
		// "0", "0", "", Messages.getString("SQLXDESC.QO_DUMP_LEVEL"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("reset_tr_parser_interval",
		// ParameterItem.tInteger, "10", "", "",
		// Messages.getString("SQLXDESC.RESET_TR_PARSER_INTERVAL"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("server_inserts", ParameterItem.tInteger,
		// "1", "1", "7", Messages.getString("SQLXDESC.SERVER_INSERTS"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("session_mode_sync",
		// ParameterItem.tBoolean, "0", "", "",
		// Messages.getString("SQLXDESC.SESSION_MODE_SYNC"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("sql_trace_critical_time",
		// ParameterItem.tInteger, "300", "2", "",
		// Messages.getString("SQLXDESC.SQL_TRACE_CRITICAL_TIME"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("sql_trace_file_dir",
		// ParameterItem.tString, "NULL", "", "",
		// Messages.getString("SQLXDESC.SQL_TRACE_FILE_DIR"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("sql_trace_file_prefix",
		// ParameterItem.tString, "trace.", "", "",
		// Messages.getString("SQLXDESC.SQL_TRACE_FILE_PREFIX"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("sql_trace_file_suffix",
		// ParameterItem.tString, ".log", "", "",
		// Messages.getString("SQLXDESC.SQL_TRACE_FILE_SUFFIX"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("sql_trace_level",
		// ParameterItem.tInteger, "0", "0", "255",
		// Messages.getString("SQLXDESC.SQL_TRACE_LEVEL"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("sql_trace_max_lines",
		// ParameterItem.tInteger, "10000", "1", "",
		// Messages.getString("SQLXDESC.SQL_TRACE_MAX_LINES"),
		// ParameterItem.uClient));
		// items.add(new ParameterItem("sql_trace_warning_time",
		// ParameterItem.tInteger, "10", "1", "",
		// Messages.getString("SQLXDESC.SQL_TRACE_WARNING_TIME"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("sr_buffers", ParameterItem.tInteger, "16",
				"1", "", Messages.getString("SQLXDESC.SR_BUFFERS"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("suppress_fsync", ParameterItem.tBoolean,
		// "0", "", "", Messages.getString("SQLXDESC.SUPPRESS_FSYNC"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("temp_mem_buffer_pages",
				ParameterItem.tInteger, "4", "0", "20", Messages
						.getString("SQLXDESC.TEMP_MEM_BUFFER_PAGES"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("thread_private_mem_size",
		// ParameterItem.tInteger, "65536", "1024", "524288",
		// Messages.getString("SQLXDESC.THREAD_PRIVATE_MEM_SIZE"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("thread_stacksize", ParameterItem.tInteger,
				"102400", "", "65536", Messages
						.getString("SQLXDESC.THREAD_STACKSIZE"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("ttvol_shrink_enable",
		// ParameterItem.tBoolean, "0", "", "",
		// Messages.getString("SQLXDESC.TTVOL_SHRINK_ENABLE"),
		// ParameterItem.uServer));
		items.add(new ParameterItem("unfill_factor", ParameterItem.tFloat,
				"0.1", "0", "0.3",
				Messages.getString("SQLXDESC.UNFILL_FACTOR"),
				ParameterItem.uServer));
		items.add(new ParameterItem("unfill_index_factor",
				ParameterItem.tFloat, "0.2", "0", "0.35", Messages
						.getString("SQLXDESC.UNFILL_INDEX_FACTOR"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("use_oid_preflush",
		// ParameterItem.tBoolean, "1", "", "",
		// Messages.getString("SQLXDESC.USE_OID_PREFLUSH"),
		// ParameterItem.uClient));
		items.add(new ParameterItem("volext_path", ParameterItem.tString,
				"NULL", "", "", Messages.getString("SQLXDESC.VOLEXT_PATH"),
				ParameterItem.uServer));
		items.add(new ParameterItem("voltmp_path", ParameterItem.tString,
				"NULL", "", "", Messages.getString("SQLXDESC.VOLTMP_PATH"),
				ParameterItem.uServer));
		items.add(new ParameterItem("warn_outofspace_factor",
				ParameterItem.tFloat, "0.15", "0", "1", Messages
						.getString("SQLXDESC.WARN_OUTOFSPACE_FACTOR"),
				ParameterItem.uServer));
		items.add(new ParameterItem("bwcomp_primary_key",
				ParameterItem.tInteger, "0", "", "", Messages
						.getString("SQLXDESC.BWCOMP_PRIMARY_KEY"),
				ParameterItem.uServer));
		items.add(new ParameterItem("disable_java_stored_procedure",
				ParameterItem.tInteger, "0", "", "", Messages
						.getString("SQLXDESC.DISABLE_JAVA_STORED_PROCEDURE"),
				ParameterItem.uServer));
		items.add(new ParameterItem("is_replicated", ParameterItem.tInteger,
				"0", "", "", Messages.getString("SQLXDESC.IS_REPLICATED"),
				ParameterItem.uServer));
		// items.add(new ParameterItem("bytes_per_char", ParameterItem.tInteger,
		// "0", "", "", Messages.getString("SQLXDESC.BYTES_PER_CHAR"),
		// ParameterItem.uServer));
		return items;
	}
}
