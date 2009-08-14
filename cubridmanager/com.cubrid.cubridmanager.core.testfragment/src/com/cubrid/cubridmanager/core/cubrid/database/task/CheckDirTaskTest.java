package com.cubrid.cubridmanager.core.cubrid.database.task;

import java.util.List;

import com.cubrid.cubridmanager.core.JsonObjectUtil;
import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;

public class CheckDirTaskTest extends
		SetupEnvTestCase {

	public void testCheckDirNotExists() {

		CheckDirTask task = new CheckDirTask(site);

		task.setUsingSpecialDelimiter(false);
		// task:checkdir
		// token:8ec1ab8a91333c78c9e6cdb7dd8bf452e103ddfeface7072c47a07a0b1f66f6f7926f07dd201b6aa
		// dir:C:\CUBRID\databases\notExistDir1
		// dir:C:\CUBRID\databases\notExistDir2
		//
		// task:checkdir
		// status:success
		// note:none
		// noexist:C:\MyDev\tools\CUBRID\databases\notExistDir1
		// noexist:C:\MyDev\tools\CUBRID\databases\notExistDir2		
		task.setDirectory(new String[] {
				serverPath + getPathSeparator()+"databases"+getPathSeparator()+"notExistDir1",
				serverPath + getPathSeparator()+"databases"+getPathSeparator()+"notExistDir2" });
		task.execute();

		assertEquals(null, task.getErrorMsg());
		System.out.println("task.getErrorMsg()=" + task.getErrorMsg());

		String[] noExistArray = task.getNoExistDirectory();
		assertEquals(noExistArray.length, 2);
		System.out.println("task.getNoExistDirectory()="
				+ JsonObjectUtil.object2json(noExistArray));

		assertTrue(task.isSuccess());
		System.out.println("task.isSuccess()=" + task.isSuccess());

	}

	public void testCheckDirExists() {

		CheckDirTask task = new CheckDirTask(site);

		task.setUsingSpecialDelimiter(false);
		// task:checkdir
		// token:8ec1ab8a91333c78c9e6cdb7dd8bf452e103ddfeface7072c47a07a0b1f66f6f7926f07dd201b6aa
		// dir:C:\CUBRID\databases
		// dir:C:\CUBRID\databases\demodb
		//
		// task:checkdir
		// status:success
		// note:none
		task.setDirectory(new String[] {
				serverPath + getPathSeparator() + "databases",
				serverPath + getPathSeparator() + "databases"
						+ getPathSeparator() + "demodb11111" });
		task.execute();

		System.out.println("task.getErrorMsg()=" + task.getErrorMsg());
		assertEquals(null, task.getErrorMsg());

		String[] noExistArray = task.getNoExistDirectory();
		System.out.println("task.getNoExistDirectory()="
				+ JsonObjectUtil.object2json(noExistArray));
		assertEquals(noExistArray.length>0,true);

		System.out.println("task.isSuccess()=" + task.isSuccess());
		assertTrue(task.isSuccess());

	}

	public void testCheckDirHalf() {

		CheckDirTask task = new CheckDirTask(site);

		task.setUsingSpecialDelimiter(false);
		// task:checkdir
		// token:8ec1ab8a91333c78c9e6cdb7dd8bf452e103ddfeface7072c47a07a0b1f66f6f7926f07dd201b6aa
		// dir:C:\CUBRID\databases\demodb
		// dir:C:\CUBRID\databases\notExistDir2
		//
		// task:checkdir
		// status:success
		// note:none
		final GetDatabaseListTask getDatabaseListTask = new GetDatabaseListTask(
				site);
		getDatabaseListTask.execute();
		 List<DatabaseInfo> databaseInfoList=getDatabaseListTask.loadDatabaseInfo();
		
		String[] strs=new String[databaseInfoList.size()];
		if(databaseInfoList!=null)
			for(int i=0;i<databaseInfoList.size();i++){
				DatabaseInfo bean=databaseInfoList.get(i);
				strs[i]=bean.getDbDir();
			}
			
		task.setDirectory(strs);
		task.execute();

		System.out.println("task.getErrorMsg()=" + task.getErrorMsg());
		assertEquals(null, task.getErrorMsg());

		String[] noExistArray = task.getNoExistDirectory();
		System.out.println("task.getNoExistDirectory()="
				+ JsonObjectUtil.object2json(noExistArray));
		assertEquals(noExistArray, null);

		System.out.println("task.isSuccess()=" + task.isSuccess());
		assertTrue(task.isSuccess());

	}

	public void testCheckDirUnacceptableDirectory() {

		CheckDirTask task = new CheckDirTask(site);

		task.setUsingSpecialDelimiter(false);

		task.setDirectory(new String[] { serverPath + getPathSeparator() +"databases\\?",
				serverPath + getPathSeparator() +"databases"+ getPathSeparator() +"*",
				serverPath + getPathSeparator() +"databases"+ getPathSeparator() +" both_space " });
		task.execute();

		System.out.println("task.getErrorMsg()=" + task.getErrorMsg());
		assertEquals(null, task.getErrorMsg());

		String[] noExistArray = task.getNoExistDirectory();
		System.out.println("task.getNoExistDirectory()="
				+ JsonObjectUtil.object2json(noExistArray));
		assertEquals(noExistArray.length, 3);

		System.out.println("task.isSuccess()=" + task.isSuccess());
		assertTrue(task.isSuccess());

	}

	public void testCheckDirBulkDirs() {

		CheckDirTask task = new CheckDirTask(site);

		task.setUsingSpecialDelimiter(false);

		String[] dirs = new String[1000];
		for (int i = 0; i < dirs.length; i++) {
			dirs[i] = serverPath +  getPathSeparator() +"databases"+ getPathSeparator() +"notExistDir" + i;
		}

		task.setDirectory(dirs);
		task.execute();

		System.out.println("task.getErrorMsg()=" + task.getErrorMsg());
		assertEquals(null, task.getErrorMsg());

		String[] noExistArray = task.getNoExistDirectory();
		System.out.println("task.getNoExistDirectory()="
				+ JsonObjectUtil.object2json(noExistArray));
		assertEquals(noExistArray.length, dirs.length);

		System.out.println("task.isSuccess()=" + task.isSuccess());
		assertTrue(task.isSuccess());

	}

}
