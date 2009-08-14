package com.cubrid.cubridmanager.core;

import java.io.InputStream;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;

public class SystemParameter {

	static Map<String, String> properties = new HashMap<String, String>();
	static {
		readProperties("sysParam.property");
	}

	public static String getParameterValue(String key) {
		return properties.get(key);
	}
	public static int getParameterIntValue(String key) {
		String val=properties.get(key);
		
		return Integer.valueOf(val);
	}
	/**
	 * 
	 * @return
	 */
	public Properties getSysHomeDirFromProperties(String path) {
		Properties initProps = new Properties();
		InputStream in = null;
		try {
			in = this.getClass().getResourceAsStream(path);
			initProps.load(in);
		} catch (Exception e) {

			e.printStackTrace();
		} finally {
			try {
				if (in != null) {
					in.close();
				}
			} catch (Exception e) {
			}
		}
		return initProps;
	}

	public static void readProperties(String filePath) {
		Properties props = new Properties();
		SystemParameter initProperty = new SystemParameter();
		try {

			props = initProperty.getSysHomeDirFromProperties(filePath);
			Enumeration en = props.propertyNames();
			while (en.hasMoreElements()) {
				String key = (String) en.nextElement();
				String property = props.getProperty(key);
				if (!properties.containsKey(key)) {
					properties.put(key, property);
				}
			}
		} catch (Exception e) {
			e.printStackTrace();
		}
	}

}
