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

import java.io.FileDescriptor;
import java.net.InetAddress;
import java.security.Permission;

public class SpSecurityManager extends SecurityManager {
	public void checkExit(int status) {
		throw new SecurityException();
	}

	public void checkAccept(String host, int port) {
	}

	public void checkAccess(Thread t) {
	}

	public void checkPermission(Permission perm, Object context) {
	}

	public void checkPermission(Permission perm) {
	}

	public void checkAccess(ThreadGroup g) {
	}

	public void checkAwtEventQueueAccess() {
	}

	public void checkConnect(String host, int port, Object context) {
	}

	public void checkConnect(String host, int port) {
	}

	public void checkCreateClassLoader() {
	}

	public void checkDelete(String file) {
	}

	public void checkExec(String cmd) {
	}

	public void checkLink(String lib) {
	}

	public void checkListen(int port) {
	}

	public void checkMemberAccess(Class<?> clazz, int which) {
	}

	public void checkMulticast(InetAddress maddr, byte ttl) {
	}

	public void checkMulticast(InetAddress maddr) {
	}

	public void checkPackageAccess(String pkg) {
	}

	public void checkPackageDefinition(String pkg) {
	}

	public void checkPrintJobAccess() {
	}

	public void checkPropertiesAccess() {
	}

	public void checkPropertyAccess(String key) {
	}

	public void checkRead(FileDescriptor fd) {
	}

	public void checkRead(String file, Object context) {
	}

	public void checkRead(String file) {
	}

	public void checkSecurityAccess(String target) {
	}

	public void checkSetFactory() {
	}

	public void checkSystemClipboardAccess() {
	}

	public void checkWrite(FileDescriptor fd) {
	}

	public void checkWrite(String file) {
	}
}
