package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;

public class CheckFileTaskTest extends SetupEnvTestCase {

	public void testCheckDirBulkDirs() {

		CheckFileTask task = new CheckFileTask(site);

		task.setUsingSpecialDelimiter(false);

		String[] dirs = new String[1000];
		for (int i = 0; i < dirs.length; i++) {
			dirs[i] = serverPath + getPathSeparator() + "databases" + getPathSeparator() + "notExistDir" + i;
		}

		task.setFile(dirs);
		task.execute();
		task.getExistFiles();
		System.out.println("task.getErrorMsg()=" + task.getErrorMsg());
		assertTrue(task.isSuccess());

	}

}
