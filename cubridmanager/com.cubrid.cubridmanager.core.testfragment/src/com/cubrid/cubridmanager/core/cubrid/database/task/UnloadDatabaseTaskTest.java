package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.common.socket.MessageUtil;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class UnloadDatabaseTaskTest extends
		SetupEnvTestCase {

	public void testSend() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/unloaddb_send");
		String msg = Tool.getFileContent(filepath);

		//replace "token" field with the latest value
		msg = msg.replaceFirst("token:.*\n", "token:" + this.token + "\n");
		//composite message
		UnloadDatabaseTask task = new UnloadDatabaseTask(site);
		task.setDbName("demodb");
		task.setUnloadDir("C:\\CUBRID\\DATABA~1\\demodb");
		task.setUsedHash(false, "none");
		task.setUnloadType("both");
		task.setClasses(new String[] { "athlete", "code", "event" });
		task.setIncludeRef(false);
		task.setClassOnly(true);
		task.setUsedDelimit(false);
		task.setUsedEstimate(false, "none");
		task.setUsedCache(false, "none");
		task.setUsedPrefix(false, "none");
		task.setUsedLoFile(false, "none");
		task.execute();
		//compare 
		assertEquals(msg, task.getRequest());
		task.getUnloadDbResult();
	}

	public void testReceive() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/database/task/test.message/unloaddb_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		//compare 
		assertEquals("success", node.getValue("status"));

	}

}
