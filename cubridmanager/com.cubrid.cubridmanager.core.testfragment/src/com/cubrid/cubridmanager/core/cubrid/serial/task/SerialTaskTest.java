package com.cubrid.cubridmanager.core.cubrid.serial.task;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;

public class SerialTaskTest extends
		SetupJDBCTestCase {

	public void testSerialTask() {
		//test create serial
		CreateOrEditSerialTask createOrEditSerialTask = new CreateOrEditSerialTask(
				databaseInfo, site, null, brokerPort);
		createOrEditSerialTask.createSerial("serial1", "1", "1", "100", "1",
				true);
		assertTrue(createOrEditSerialTask.getErrorMsg() == null
				|| createOrEditSerialTask.getErrorMsg().trim().length() <= 0);
		GetSerialInfoTask getSerialInfoTask = new GetSerialInfoTask(
				databaseInfo, site, null, brokerPort);
		SerialInfo serialInfo = getSerialInfoTask.getSerialInfo("serial1");
		boolean isOk = serialInfo != null
				&& serialInfo.getName().equals("serial1")
				&& serialInfo.getCurrentValue().equals("1")
				&& serialInfo.getIncrementValue().equals("1")
				&& serialInfo.getMaxValue().equals("100")
				&& serialInfo.getMinValue().equals("1") && serialInfo.isCycle();
		assertTrue(isOk);

		//test edit serial
		createOrEditSerialTask = new CreateOrEditSerialTask(databaseInfo, site,
				null, brokerPort);
		createOrEditSerialTask.editSerial("serial1", "2", "2", "102", "2",
				false);
		assertTrue(createOrEditSerialTask.getErrorMsg() == null
				|| createOrEditSerialTask.getErrorMsg().trim().length() <= 0);
		getSerialInfoTask = new GetSerialInfoTask(databaseInfo, site, null,
				brokerPort);
		serialInfo = getSerialInfoTask.getSerialInfo("serial1");
		isOk = serialInfo != null && serialInfo.getName().equals("serial1")
				&& serialInfo.getCurrentValue().equals("2")
				&& serialInfo.getIncrementValue().equals("2")
				&& serialInfo.getMaxValue().equals("102")
				&& serialInfo.getMinValue().equals("2")
				&& !serialInfo.isCycle();
		assertTrue(isOk);

		//test get serial information list
		GetSerialInfoListTask getSerialInfoListTask = new GetSerialInfoListTask(
				databaseInfo, site, null, brokerPort);
		getSerialInfoListTask.execute();
		assertTrue(getSerialInfoListTask.getSerialInfoList().size() > 0);

		//test get serial information
		getSerialInfoTask = new GetSerialInfoTask(databaseInfo, site, null,
				brokerPort);
		serialInfo = getSerialInfoTask.getSerialInfo("serial1");
		assertTrue(serialInfo != null && serialInfo.getName().equals("serial1"));

		//test delete serial
		DeleteSerialTask deleteSerialTask = new DeleteSerialTask(databaseInfo,
				site, null, brokerPort);
		deleteSerialTask.deleteSerial(new String[] { "serial1" });
		assertTrue(createOrEditSerialTask.getErrorMsg() == null
				|| createOrEditSerialTask.getErrorMsg().trim().length() <= 0);
		getSerialInfoTask = new GetSerialInfoTask(databaseInfo, site, null,
				brokerPort);
		serialInfo = getSerialInfoTask.getSerialInfo("serial1");
		assertTrue(serialInfo == null);
	}
}
