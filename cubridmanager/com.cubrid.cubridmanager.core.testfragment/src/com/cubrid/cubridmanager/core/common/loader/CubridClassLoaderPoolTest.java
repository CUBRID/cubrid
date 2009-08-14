package com.cubrid.cubridmanager.core.common.loader;

import java.io.FileNotFoundException;
import java.net.MalformedURLException;

import junit.framework.TestCase;

public class CubridClassLoaderPoolTest extends
		TestCase {

	public void testClassLoader() {
		try {
			CubridClassLoaderPool.getClassLoader("d:\\classloader");
		} catch (FileNotFoundException e) {
		} catch (MalformedURLException e) {
		}
		CubridClassLoaderPool.getCubridDriver("");
		CubridClassLoaderPool.getCubridDriver("d:\\driver");
	}
}
