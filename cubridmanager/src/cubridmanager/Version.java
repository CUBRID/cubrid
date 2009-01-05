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

package cubridmanager;

import java.util.MissingResourceException;
import java.util.ResourceBundle;

public class Version {
	public static final boolean cmDebugMode = false;

	private static final String BUNDLE_NAME = "version";
	private static final ResourceBundle RESOURCE_BUNDLE = ResourceBundle.getBundle(BUNDLE_NAME);

	private Version() {
	}

	public static String getString(String key) {
		try {
			return RESOURCE_BUNDLE.getString(key);
		} catch (MissingResourceException e) {
			return '!' + key + '!';
		}
	}
	/**
	 * 지원되는 JRE 버전 체크. 1.4버전 이하는 지원하지 않는 것으로 false를 반환함.
	 * @return boolean
	 */
	public static boolean isSupportsUsedJRE() {
		String vmVersion = System.getProperty("java.vm.version");

		if (vmVersion.startsWith("0") || vmVersion.startsWith("1.0")
				|| vmVersion.startsWith("1.1") || vmVersion.startsWith("1.2")
				|| vmVersion.startsWith("1.3") || vmVersion.startsWith("1.4")) {
			return false;
		} else {
			return true;
		}
	}

	/**
	 * 지원되지 않는 버전에 대한 ERROR 메지시 반환
	 * @return String
	 */
	public static String getUnsupportedJREMessage() {
		return "JRE " + System.getProperty("java.vm.version") + "\r\n"
				+ Messages.getString("ERROR.UNSUPPORTEDJRE");
	}

	/**
	 * 현재 JRE 버전이 1.5이상 버전인지를 확인함
	 * @return boolean
	 */
	public static boolean isJRE15OrAbove() {
		String vmVersion = System.getProperty("java.vm.version")
				.substring(0, 3);

		if (vmVersion.compareTo("1.5") >= 0) {
			return true;
		} else {
			return false;
		}
	}
	
}
