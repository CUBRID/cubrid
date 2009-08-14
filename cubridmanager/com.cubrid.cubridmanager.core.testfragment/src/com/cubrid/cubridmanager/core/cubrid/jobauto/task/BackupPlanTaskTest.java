package com.cubrid.cubridmanager.core.cubrid.jobauto.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.YesNoType;
/**
 * Test add and edit backupinfo 
 * 
 * @author lizhiqiang
 * Apr 3, 2009
 */
public class BackupPlanTaskTest  extends SetupEnvTestCase {
	/*
	 * Test add backupplan
	 *  @throws Exception
	 */
	public void testAddBackupPlanSend() throws Exception {
		BackupPlanTask task = new BackupPlanTask("addbackupinfo",site);
		task.setDbname("demodb");
		task.setBackupid("ccc");
        task.setPath("C:\\CUBRID\\DATABA~1\\demodb\\backup");
        task.setPeriodType("Weekly");
        task.setPeriodDate("Tuesday");
        task.setTime("0606");
        task.setLevel("2");
        task.setArchivedel(OnOffType.ON);
        task.setUpdatestatus(OnOffType.ON);
        task.setStoreold(OnOffType.ON);
        task.setOnoff(OnOffType.ON);
        task.setZip(YesNoType.Y);
        task.setCheck(YesNoType.Y);
        task.setMt("5");
        task.execute();
		task.setUsingSpecialDelimiter(false);
		//compare 
       assertTrue(task.isSuccess());
	}
	/*
	 *Tests edit backupplan  
	 * 
	 * @throws Exception
	 */
	public void testEditBackupPlanSend() throws Exception {
		BackupPlanTask task = new BackupPlanTask("setbackupinfo",site);
		task.setDbname("demodb");
		task.setBackupid("ccc");
        task.setPath("C:\\CUBRID\\DATABA~1\\demodb\\backup");
        task.setPeriodType("Weekly");
        task.setPeriodDate("Tuesday");
        task.setTime("0706");
        task.setLevel("2");
        task.setArchivedel(OnOffType.ON);
        task.setUpdatestatus(OnOffType.ON);
        task.setStoreold(OnOffType.ON);
        task.setOnoff(OnOffType.ON);
        task.setZip(YesNoType.Y);
        task.setCheck(YesNoType.Y);
        task.setMt("5");
        task.execute();
		task.setUsingSpecialDelimiter(false);
		//compare 
     //  assertTrue(task.isSuccess());
	}
}
