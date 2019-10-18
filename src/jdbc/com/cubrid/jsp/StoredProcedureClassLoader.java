/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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

package com.cubrid.jsp;

import java.io.File;
import java.io.FileFilter;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.HashMap;

public class StoredProcedureClassLoader extends URLClassLoader {
	private static volatile StoredProcedureClassLoader instance = null;

	private HashMap<String, Long> files = new HashMap<String, Long>();

	private File root;

	private StoredProcedureClassLoader() {
		super(new URL[0]);
		init();
	}

	private void init() {
		root = new File(Server.getSpPath() + "/java");
		initJars();
		initClasses();
	}

	private void initJars() {
		File[] jars = root.listFiles(new FileFilter() {
			public boolean accept(File f) {
				return isJarFile(f);

			}
		});

		if (jars == null) {
			return;
		}

		for (int i = 0; i < jars.length; i++) {
			files.put(jars[i].getName(), jars[i].lastModified());
		}

		try {
			addURL(root.toURI().toURL());
			for (int i = 0; i < jars.length; i++) {
				addURL(jars[i].toURI().toURL());
			}
		} catch (MalformedURLException e) {
			Server.log(e);
		}
	}

	private void initClasses() {
		File[] classes = root.listFiles(new FileFilter() {
			public boolean accept(File f) {
				return isClassFile(f);
			}
		});

		if (classes == null) {
			return;
		}

		for (int i = 0; i < classes.length; i++) {
			files.put(classes[i].getName(), classes[i].lastModified());
		}
	}

	public Class<?> loadClass(String name) throws ClassNotFoundException {
		if (!modified()) {
			return super.loadClass(name);
		}

		instance = new StoredProcedureClassLoader();
		return instance.loadClass(name);
	}

	private boolean modified() {
		File[] files = root.listFiles(new FileFilter() {
			public boolean accept(File f) {
				return isJarFile(f) || isClassFile(f);
			}
		});

		if (files == null) {
			return !this.files.isEmpty();
		}

		if (this.files.size() != files.length) {
			return true;
		}

		for (int i = 0; i < files.length; i++) {
			if (!this.files.containsKey(files[i].getName())) {
				return true;
			}

			long l = this.files.get(files[i].getName());
			if (files[i].lastModified() != l) {
				return true;
			}
		}

		return false;
	}

	public static StoredProcedureClassLoader getInstance() {
		if (instance == null) {
			synchronized (StoredProcedureClassLoader.class) {
				if (instance == null) {
					instance = new StoredProcedureClassLoader();
				}
			}
		}

		return instance;
	}

	private boolean isJarFile(File f) {
		return f.getName().lastIndexOf(".jar") > 0;
	}

	private boolean isClassFile(File f) {
		return f.getName().lastIndexOf(".class") > 0;
	}
}
