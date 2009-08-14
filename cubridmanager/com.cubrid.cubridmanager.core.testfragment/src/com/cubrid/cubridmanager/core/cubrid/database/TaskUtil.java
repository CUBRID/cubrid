package com.cubrid.cubridmanager.core.cubrid.database;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class TaskUtil {

	private TaskUtil() {
		throw new UnsupportedOperationException();
	}

	/**
	 * 
	 * a exvol parameter append utility method in a createdb process
	 * 
	 * @param volList
	 * @param volumeName
	 * @param volumeType
	 * @param pageNumber
	 * @param volumePath
	 */
	public static void addExVolumeInCreateDbTask(
			List<Map<String, String>> volList, String volumeName,
			String volumeType, String pageNumber, String volumePath) {
		Map<String, String> map = new HashMap<String, String>();
		map.put("0", volumeName);
		map.put("1", volumeType);
		map.put("2", pageNumber);
		map.put("3", volumePath);
		volList.add(map);
	}

}
