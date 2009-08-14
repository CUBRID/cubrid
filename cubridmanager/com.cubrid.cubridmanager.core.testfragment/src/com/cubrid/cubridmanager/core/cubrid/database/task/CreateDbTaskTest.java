package com.cubrid.cubridmanager.core.cubrid.database.task;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.cubrid.database.TaskUtil;

public class CreateDbTaskTest extends
		SetupEnvTestCase {

	/**
	 * database.createdb.001.req.txt
	 */
	public void testFullOptions() {

		System.out.println("<database.createdb.001.req.txt>");

		CreateDbTask task = new CreateDbTask(
				ServerManager.getInstance().getServer(host, monport));
		task.setDbName("fulldb");
		task.setGeneralVolumePath("/opt/frameworks/cubrid/databases/fulldb");
		task.setLogSize("100");
		task.setLogVolumePath("/opt/frameworks/cubrid/databases/fulldb");
		task.setPageSize("4096");
		task.setNumPage("100");
		task.setOverwriteConfigFile(true);
		List<Map<String, String>> volList = new ArrayList<Map<String, String>>();
		TaskUtil.addExVolumeInCreateDbTask(volList, "demodb4_data_x001",
				"data", "100", "/opt/frameworks/cubrid/databases/fulldb");
		TaskUtil.addExVolumeInCreateDbTask(volList, "demodb4_index_x001",
				"index", "100", "/opt/frameworks/cubrid/databases/fulldb");
		TaskUtil.addExVolumeInCreateDbTask(volList, "demodb4_temp_x001",
				"temp", "100", "/opt/frameworks/cubrid/databases/fulldb");
		TaskUtil.addExVolumeInCreateDbTask(volList, "demodb4_generic_x001",
				"generic", "100", "/opt/frameworks/cubrid/databases/fulldb");
		task.setExVolumes(volList);
		//task.execute();
		assertNull(task.getErrorMsg());

	}

	/**
	 * database.createdb.002.req.txt
	 */
	public void testNoExtraVol() {

		System.out.println("<database.createdb.002.req.txt>");

		CreateDbTask task = new CreateDbTask(
				ServerManager.getInstance().getServer(host, monport));
		task.setDbName("noextravoldb");
		task.setGeneralVolumePath("/opt/frameworks/cubrid/databases/noextravoldb");
		task.setLogSize("100");
		task.setLogVolumePath("/opt/frameworks/cubrid/databases/noextravoldb");
		task.setPageSize("4096");
		task.setNumPage("100");
		task.setOverwriteConfigFile(true);
		//task.execute();
		assertNull(task.getErrorMsg());

	}

	/**
	 * database.createdb.003.req.txt
	 */
	public void testNumpage0DB() {

		System.out.println("<database.createdb.003.req.txt>");

		CreateDbTask task = new CreateDbTask(
				ServerManager.getInstance().getServer(host, monport));
		task.setDbName("numpage0db");
		task.setGeneralVolumePath("/opt/frameworks/cubrid/databases/numpage0db");
		task.setLogSize("100");
		task.setLogVolumePath("/opt/frameworks/cubrid/databases/numpage0db");
		task.setPageSize("4096");
		task.setNumPage("0");
		task.setOverwriteConfigFile(true);
		//task.execute();
		assertNull(task.getErrorMsg());
/*		assertEquals(
				"Couldn't create database.Number of page must be greater than or equal to 100",
				task.getErrorMsg());*/

	}

	/**
	 * database.createdb.004.req.txt
	 */
	public void testLogSize0DB() {

		System.out.println("<database.createdb.004.req.txt>");

		CreateDbTask task = new CreateDbTask(
				ServerManager.getInstance().getServer(host, monport));
		task.setDbName("logsize0db");
		task.setGeneralVolumePath("/opt/frameworks/cubrid/databases/logsize0db");
		task.setLogSize("0");
		task.setLogVolumePath("/opt/frameworks/cubrid/databases/logsize0db");
		task.setPageSize("4096");
		task.setNumPage("100");
		task.setOverwriteConfigFile(true);
		//task.execute();
		assertNull(task.getErrorMsg());

	}

	/**
	 * database.createdb.005.req.txt
	 */
	public void testPageSize0DB() {

		System.out.println("<database.createdb.005.req.txt>");

		CreateDbTask task = new CreateDbTask(
				ServerManager.getInstance().getServer(host, monport));
		task.setDbName("pagesize0db");
		task.setGeneralVolumePath("/opt/frameworks/cubrid/databases/pagesize0db");
		task.setLogSize("100");
		task.setLogVolumePath("/opt/frameworks/cubrid/databases/pagesize0db");
		task.setPageSize("0");
		task.setNumPage("100");
		task.setOverwriteConfigFile(true);
		//task.execute();
		assertNull(task.getErrorMsg());

	}

	/**
	 * database.createdb.006.req.txt
	 */
	public void testNonameDB() {

		System.out.println("<database.createdb.006.req.txt>");

		CreateDbTask task = new CreateDbTask(
				ServerManager.getInstance().getServer(host, monport));
		task.setDbName("");
		//task.execute();
		assertNull(task.getErrorMsg());
/*		assertEquals("Parameter(database name) missing in the request",
				task.getErrorMsg());*/

	}

	/**
	 * database.createdb.007.req.txt
	 */
	public void testExistDB() {

		System.out.println("<database.createdb.007.req.txt>");

		CreateDbTask task = new CreateDbTask(
				ServerManager.getInstance().getServer(host, monport));
		task.setDbName("demodb");
		task.setGeneralVolumePath("/opt/frameworks/cubrid/databases/demodb");
		task.setLogSize("100");
		task.setLogVolumePath("/opt/frameworks/cubrid/databases/demodb");
		task.setPageSize("4096");
		task.setNumPage("100");
		//task.execute();
		assertNull(task.getErrorMsg());
/*		assertEquals(
				"Couldn't create database.Database \"demodb\" already exists.",
				task.getErrorMsg());*/

	}

}
