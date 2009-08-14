/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */
package com.cubrid.cubridmanager.core.common.loader;

import java.io.File;
import java.io.FileNotFoundException;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.sql.Driver;
import java.util.HashMap;
import java.util.Map;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.log.LogUtil;

/**
 * 
 * A class loader pool to manage Cubrid JDBC class loader
 * 
 * @author wangsl
 * @version 1.0 - 2009-6-4 created by wangsl
 */
public class CubridClassLoaderPool {

	private static final Logger logger = LogUtil.getLogger(CubridClassLoaderPool.class);
	public static final String CUBRIDDRIVER = "cubrid.jdbc.driver.CUBRIDDriver";

	/**
	 * classloader cache
	 */
	private static Map<String, ClassLoader> loaders = new HashMap<String, ClassLoader>(
			10);

	/**
	 * get class loader with local file path
	 * 
	 * @param path
	 * @return
	 * @throws FileNotFoundException
	 * @throws MalformedURLException
	 */
	public static ClassLoader getClassLoader(String path) throws FileNotFoundException,
			MalformedURLException {
		if (loaders.containsKey(path)) {
			return loaders.get(path);
		}
		ClassLoader loader = getLocalLoader(path);
		loaders.put(path, loader);
		return loader;
	}

	public static Driver getCubridDriver(String path) {
		Class<?> clazz = null;
		try {
			if (path == null || path.equals("")) {
				clazz = Class.forName(CUBRIDDRIVER);
			} else {
				ClassLoader loader = getClassLoader(path);
				clazz = loader.loadClass(CUBRIDDRIVER);
			}
			Driver driver = (Driver) clazz.newInstance();
			return driver;

		} catch (Exception e) {
			logger.error(e.getMessage(), e);
		}
		return null;
	}

	/**
	 * 
	 * Get class loader by CUBRID JDBC jar path
	 * 
	 * @param path
	 * @return
	 * @throws FileNotFoundException
	 * @throws MalformedURLException
	 */
	private static ClassLoader getLocalLoader(String path) throws FileNotFoundException,
			MalformedURLException {
		File file = new File(path);
		if (!file.exists() || file.isDirectory()) {
			throw new FileNotFoundException();
		}
		return getLocalLoader(file);
	}

	/**
	 * 
	 * Get class loader by CUBRID JDBC jar path
	 * 
	 * @param file
	 * @return
	 * @throws MalformedURLException
	 */
	private static ClassLoader getLocalLoader(File file) throws MalformedURLException {
		URL url = file.toURI().toURL();
		//	Thread current = Thread.currentThread();
		ClassLoader parent = Object.class.getClassLoader();
		ClassLoader loader = new URLClassLoader(new URL[] { url }, parent);
		//	current.setContextClassLoader(loader);
		return loader;
	}

}
