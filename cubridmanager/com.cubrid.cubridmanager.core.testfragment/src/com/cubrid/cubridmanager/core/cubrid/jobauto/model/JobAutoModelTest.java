package com.cubrid.cubridmanager.core.cubrid.jobauto.model;

import java.util.ArrayList;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.cubrid.jobauto.model.errlog.BackUpErrorLog;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.errlog.BackUpErrorLogList;

public class JobAutoModelTest extends TestCase {
	public void testModelBackupPlanInfo() {
		BackupPlanInfo bean = new BackupPlanInfo();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setBackupid("backupid");
		assertEquals(bean.getBackupid(), "backupid");
		bean.setPath("path");
		assertEquals(bean.getPath(), "path");
		bean.setPeriod_type("period_type");
		assertEquals(bean.getPeriod_type(), "period_type");
		bean.setPeriod_date("period_date");
		assertEquals(bean.getPeriod_date(), "period_date");
		bean.setTime("time");
		assertEquals(bean.getTime(), "time");
		bean.setLevel("level");
		assertEquals(bean.getLevel(), "level");
		bean.setArchivedel("archivedel");
		assertEquals(bean.getArchivedel(), "archivedel");
		bean.setUpdatestatus("updatestatus");
		assertEquals(bean.getUpdatestatus(), "updatestatus");
		bean.setStoreold("storeold");
		assertEquals(bean.getStoreold(), "storeold");
		bean.setOnoff("onoff");
		assertEquals(bean.getOnoff(), "onoff");
		bean.setZip("zip");
		assertEquals(bean.getZip(), "zip");
		bean.setCheck("check");
		assertEquals(bean.getCheck(), "check");
		bean.setMt("mt");
		assertEquals(bean.getMt(), "mt");
	}

	public void testModelQueryLogInfo() {
		QueryLogInfo bean = new QueryLogInfo();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setUsername("username");
		assertEquals(bean.getUsername(), "username");
		bean.setQuery_id("query_id");
		assertEquals(bean.getQuery_id(), "query_id");
		bean.setError_time("error_time");
		assertEquals(bean.getError_time(), "error_time");
		bean.setError_code("error_code");
		assertEquals(bean.getError_code(), "error_code");
		bean.setError_desc("error_desc");
		assertEquals(bean.getError_desc(), "error_desc");
	}

	public void testModelQueryLogList() {
		QueryLogList bean = new QueryLogList();
		bean.setQueryLogList(new ArrayList());
		assertEquals(bean.getQueryLogList().getClass(), ArrayList.class);

		assertEquals(bean.getTaskName(), "getautoexecqueryerrlog");
		bean.addError(new QueryLogInfo());
		// bean.getTaskName();
	}

	public void testModelQueryPlanInfo() {
		QueryPlanInfo bean = new QueryPlanInfo();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setQuery_id("query_id");
		assertEquals(bean.getQuery_id(), "query_id");
		bean.setPeriod("period");
		assertEquals(bean.getPeriod(), "period");
		bean.setDetail("detail");
		assertEquals(bean.getDetail(), "detail");
		bean.setQuery_string("query_string");
		assertEquals(bean.getQuery_string(), "query_string");
	}

	public void testModelQueryPlanInfoHelp() {
		QueryPlanInfoHelp bean = new QueryPlanInfoHelp();
		QueryPlanInfo queryPlanInfo = new QueryPlanInfo();
		queryPlanInfo.setDetail("10:20");
		bean.setQueryPlanInfo(queryPlanInfo);
		assertEquals(bean.getQueryPlanInfo().getClass(), QueryPlanInfo.class);
		bean.setHour("10");
		bean.setMinute("20");
		bean.setTime();
		bean.getTime();

		
		assertEquals(bean.getHour(), 10);
		
		assertEquals(bean.getMinute(), 20);

		bean.setDbname("dbName");
		assertEquals(bean.getDbname(), "dbName");
		bean.setQuery_id("setQuery_id");
		assertEquals(bean.getQuery_id(), "setQuery_id");
		bean.setPeriod("setPeriod");
		assertEquals(bean.getPeriod(), "A specific day");
		bean.setDetail("setDetail");
		assertEquals(bean.getDetail(), "setDetail");
		bean.setQuery_string("getQuery_string");
		assertEquals(bean.getQuery_string(), "getQuery_string");
		bean.buildMsg();
	}

	public void testModelBackUpErrorLogList() {
		BackUpErrorLogList bean = new BackUpErrorLogList();
		bean.setErrorLogList(new ArrayList());
		bean.addError(new BackUpErrorLog());
		assertEquals(bean.getErrorLogList().size(), 1);
		assertEquals(bean.getTaskName(), "getautobackupdberrlog");

		// bean.getTaskName();
	}

	public void testModelBackUpErrorLog() {
		BackUpErrorLog bean = new BackUpErrorLog();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setBackupid("backupid");
		assertEquals(bean.getBackupid(), "backupid");
		bean.setError_time("error_time");
		assertEquals(bean.getError_time(), "error_time");
		bean.setError_desc("error_desc");
		assertEquals(bean.getError_desc(), "error_desc");
	}
}
