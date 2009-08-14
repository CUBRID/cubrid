/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.core.common.log;

import java.io.File;
import java.net.URL;

import org.apache.log4j.Logger;
import org.apache.log4j.PropertyConfigurator;
import org.eclipse.core.runtime.Platform;

import com.cubrid.cubridmanager.core.CubridManagerCorePlugin;

/**
 * 
 * This class is common log4j interface and be convinent to Get Logger
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class LogUtil {

	static {
		try {
			URL url = CubridManagerCorePlugin.getDefault().getBundle().getEntry(
					"log4j.properties");
			PropertyConfigurator.configure(url);
			String path = Platform.getInstallLocation().getURL().getPath();
			if (path != null) {
				path = path.trim();
			}
			if (path != null && path.length() > 0
					&& path.lastIndexOf(File.separator) == path.length() - 1) {
				path = path.substring(0, path.length() - 1);
			}
			path = path + File.separator + "logs";
			File logFile = new File(path);
			if (!logFile.exists()) {
				boolean success = logFile.mkdirs();
				assert (success);
			}
		} catch (Exception ignored) {
		}
	}

	/**
	 * 
	 * Get logger
	 * 
	 * @param clazz
	 * @return
	 */
	public static Logger getLogger(Class<?> clazz) {
		return Logger.getLogger(clazz);
	}

	/**
	 * 
	 * Get logger
	 * 
	 * @param name
	 * @return
	 */
	public static Logger getLogger(String name) {
		return Logger.getLogger(name);
	}
}
