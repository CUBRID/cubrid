package com.cubrid.cubridmanager.core.cubrid.database.model;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.BlockedHolders;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.DatabaseLockInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.DatabaseTransaction;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.DbLotEntry;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.DbLotInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.LockHolders;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.LockInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.LockWaiters;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.BackupPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.trigger.model.Trigger;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;

public class DatabaseModelTest extends
		SetupEnvTestCase {

	public void testModelDatabaseInfo() {
		DatabaseInfo bean = new DatabaseInfo(dbname, site);
		bean.setDbName("dbName");
		assertEquals(bean.getDbName(), "dbName");
		bean.setLogined(true);
		assertTrue(bean.isLogined());
		bean.setDbDir("dbDir");
		assertEquals(bean.getDbDir(), "dbDir");
		bean.setRunningType(DbRunningType.CS);
		assertEquals(bean.getRunningType(), DbRunningType.CS);
		DbUserInfoList dbUserInfoList = new DbUserInfoList();
		bean.setDbUserInfoList(dbUserInfoList);
		assertTrue(bean.getDbUserInfoList() != null);

		DbUserInfo dbUserInfo = new DbUserInfo();
		bean.addDbUserInfo(dbUserInfo);
		bean.removeDbUserInfo(dbUserInfo);

		bean.setAuthLoginedDbUserInfo(dbUserInfo);
		assertEquals(bean.getAuthLoginedDbUserInfo(), dbUserInfo);

		bean.setBrokerPort("brokerPort");
		assertEquals(bean.getBrokerPort(), "brokerPort");
		bean.setTriggerList(new ArrayList());
		assertEquals(bean.getTriggerList() != null, true);
		bean.setUserTableInfoList(new ArrayList());
		assertEquals(bean.getUserTableInfoList() != null, true);
		bean.setUserViewInfoList(new ArrayList());
		assertEquals(bean.getUserViewInfoList() != null, true);
		bean.setSysTableInfoList(new ArrayList());
		assertEquals(bean.getSysTableInfoList() != null, true);
		bean.setSysViewInfoList(new ArrayList());
		assertEquals(bean.getSysViewInfoList() != null, true);
		bean.setPartitionedTableMap(new HashMap());
		assertEquals(bean.getPartitionedTableMap() != null, true);
		bean.setBackupPlanInfoList(new ArrayList());
		assertEquals(bean.getBackupPlanInfoList() != null, true);
		bean.setQueryPlanInfoList(new ArrayList());
		assertEquals(bean.getQueryPlanInfoList() != null, true);
		bean.setDbSpaceInfoList(new DbSpaceInfoList());
		assertEquals(bean.getDbSpaceInfoList() != null, true);
		bean.setDbUserInfoList(new DbUserInfoList());
		assertEquals(bean.getDbUserInfoList() != null, true);
		bean.setSpProcedureInfoList(new ArrayList());
		assertEquals(bean.getSpProcedureInfoList() != null, true);
		bean.setSpFunctionInfoList(new ArrayList());
		assertEquals(bean.getSpFunctionInfoList() != null, true);
		bean.setSerialInfoList(new ArrayList());
		assertEquals(bean.getSerialInfoList() != null, true);
		bean.setServerInfo(site);
		assertEquals(bean.getServerInfo() != null, true);
		bean.setDriverPath("driverPath");
		assertEquals(bean.getDriverPath(), "driverPath");
		bean.setPort("port");
		assertEquals(bean.getPort(), "port");
		bean.clear();
		bean.setLogined(true);
		assertEquals(bean.isLogined(), true);
		bean.addDbUserInfo(new DbUserInfo());
		bean.removeDbUserInfo(new DbUserInfo());
		Trigger trigger = new Trigger();
		trigger.setName("trigger1");
		bean.addTrigger(trigger);
		bean.getTrigger("trigger1");
		bean.getClassInfoList();
		bean.addPartitionedTableList("dbname", null);
		bean.addBackupPlanInfo(new BackupPlanInfo());
		bean.removeBackupPlanInfo(new BackupPlanInfo());
		bean.removeAllBackupPlanInfo();
		bean.addQueryPlanInfo(new QueryPlanInfo());
		bean.removeQueryPlanInfo(new QueryPlanInfo());
		bean.removeAllQueryPlanInfo();
		bean.addSpaceInfo(new DbSpaceInfo());
		bean.removeSpaceInfo(new DbSpaceInfo());
		bean.getSpInfoList();
		bean.getSchemaInfo("tableName");
		bean.putSchemaInfo(new SchemaInfo());
		bean.clearSchemas();
		bean.getErrorMessage();
		bean.setDbUserInfoList(null);
		assertEquals(bean.getDbUserInfoList(), null);
		bean.addDbUserInfo(new DbUserInfo());
		bean.removeDbUserInfo(new DbUserInfo());
	}

	public void testModelBlockedHolders() {
		BlockedHolders bean = new BlockedHolders();
		bean.setTran_index(10);
		assertEquals(bean.getTran_index(), 10);
		bean.setGranted_mode("granted_mode");
		assertEquals(bean.getGranted_mode(), "granted_mode");
		bean.setCount(5);
		assertEquals(bean.getCount(), 5);
		bean.setNsubgranules(12);
		assertEquals(bean.getNsubgranules(), 12);
		bean.setBlocked_mode("blocked_mode");
		assertEquals(bean.getBlocked_mode(), "blocked_mode");
		bean.setStart_at("Start_at");
		assertEquals(bean.getStart_at(), "Start_at");
		bean.setWait_for_sec("wait_for_sec");
		assertEquals(bean.getWait_for_sec(), "wait_for_sec");
	}

	public void testModelDatabaseLockInfo() {
		DatabaseLockInfo bean = new DatabaseLockInfo();
		bean.addLockInfo(new LockInfo());
		assertEquals(bean.getLockInfo() instanceof LockInfo, true);
		assertEquals(bean.getTaskName(), "lockdb");
	}

	public void testModelDatabaseTransaction() {
		DatabaseTransaction bean = new DatabaseTransaction();
		bean.setIndex(5);
		assertEquals(bean.getIndex(), 5);
		bean.setPname("pname");
		assertEquals(bean.getPname(), "pname");
		bean.setUid("uid");
		assertEquals(bean.getUid(), "uid");
		bean.setHost("host");
		assertEquals(bean.getHost(), "host");
		bean.setPid("pid");
		assertEquals(bean.getPid(), "pid");
		bean.setIsolevel("isolevel");
		assertEquals(bean.getIsolevel(), "isolevel");
		bean.setTimeout(7);
		assertEquals(bean.getTimeout(), 7);
	}

	public void testModelDbLotEntry() {
		DbLotEntry bean = new DbLotEntry();
		bean.setOpen("open");
		assertEquals(bean.getOpen(), "open");
		bean.setOid("oid");
		assertEquals(bean.getOid(), "oid");
		bean.setOb_type("ob_type");
		assertEquals(bean.getOb_type(), "ob_type");
		bean.setNum_holders(11);
		assertEquals(bean.getNum_holders(), 11);
		bean.setNum_b_holders(13);
		assertEquals(bean.getNum_b_holders(), 13);
		bean.setNum_waiters(11);
		assertEquals(bean.getNum_waiters(), 11);

		bean.getLockHoldersList();
		bean.addLock_holders(new LockHolders());
		bean.getLockWaitersList();
		bean.addWaiters(new LockWaiters());
		bean.getBlockHoldersList();
		bean.addB_holders(new BlockedHolders());
	}

	public void testModelDbLotInfo() {
		DbLotInfo bean = new DbLotInfo();
		bean.setNumlocked(9);
		assertEquals(bean.getNumlocked(), 9);
		bean.setMaxnumlock(10);
		assertEquals(bean.getMaxnumlock(), 10);
		bean.addEntry(new DbLotEntry());
		bean.getDbLotEntryList();
	}

	public void testModelLockHolders() {
		LockHolders bean = new LockHolders();
		bean.setTran_index(10);
		assertEquals(bean.getTran_index(), 10);
		bean.setGranted_mode("granted_mode");
		assertEquals(bean.getGranted_mode(), "granted_mode");
		bean.setCount(5);
		assertEquals(bean.getCount(), 5);
		bean.setNsubgranules(12);
		assertEquals(bean.getNsubgranules(), 12);
	}

	public void testModelLockInfo() {
		LockInfo bean = new LockInfo();
		bean.setEsc(3);
		assertEquals(bean.getEsc(), 3);
		bean.setDinterval(9);
		assertEquals(bean.getDinterval(), 9);
		bean.addTransaction(new DatabaseTransaction());
		assertEquals(bean.getTransaction() instanceof List, true);
		bean.getDbLotInfo();
		bean.addLot(new DbLotInfo());
	}

	public void testModelLockWaiters() {
		LockWaiters bean = new LockWaiters();
		bean.setTran_index(10);
		assertEquals(bean.getTran_index(), 10);
		bean.setB_mode("b_mode");
		assertEquals(bean.getB_mode(), "b_mode");
		bean.setStart_at("start_at");
		assertEquals(bean.getStart_at(), "start_at");
		bean.setWaitfornsec("waitfornsec");
		assertEquals(bean.getWaitfornsec(), "waitfornsec");
	}
}
