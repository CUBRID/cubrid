package com.cubrid.cubridmanager.core.cubrid.jobauto.task;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfoHelp;

/**
 * Tests Type GetBackupPlanListTask
 * 
 * @author lizhiqiang Apr 3, 2009
 */
public class SetAndGetQueryPlanListTaskTest extends
		SetupEnvTestCase {
	/**
	 * Tests getBackupPlanListTask method
	 * 
	 */
	public void test() {
		QueryPlanInfo queryPlan = new QueryPlanInfo();
		queryPlan.setDbname("demodb");
		queryPlan.setQuery_id("test_set");
		queryPlan.setPeriod("MONTH");
		queryPlan.setDetail("1 02:01");
		List<String> msgList = new ArrayList<String>();
		QueryPlanInfoHelp qHelp = new QueryPlanInfoHelp();
		qHelp.setQueryPlanInfo(queryPlan);
		msgList.add(qHelp.buildMsg());

		SetQueryPlanListTask taskSet = new SetQueryPlanListTask(site);
		taskSet.setDbname("demodb");
		taskSet.buildMsg(msgList);
		taskSet.execute();
		String errorSet = taskSet.getErrorMsg();
		assertNull(errorSet);

		GetQueryPlanListTask taskGet = new GetQueryPlanListTask(site);
		taskGet.setDbName("demodb");
		taskGet.execute();
		String errorGet = taskGet.getErrorMsg();
		assertNull(errorGet);
		List<QueryPlanInfo> queryPlanInfoList = taskGet.getQueryPlanInfoList();
		assertNotNull(queryPlanInfoList);

		if (queryPlanInfoList.size() > 0) {
			QueryPlanInfo info = queryPlanInfoList.get(0);
			assertNotNull(info);
		}

	}

}
