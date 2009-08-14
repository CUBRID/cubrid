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
package com.cubrid.cubridmanager.app;

import org.eclipse.jface.preference.IPreferenceStore;

public class GeneralPreference {

	public static String MAXIMIZE_WINDOW_ON_START_UP = ".maximize_window_on_start_up";
	public static String CHECK_NEW_INFO_ON_START_UP = ".check_new_information_on_start_up";
	public static String USE_CLICK_SINGLE = ".use_click_single";

	private static IPreferenceStore pref = null;

	static {
		pref = CubridManagerAppPlugin.getDefault().getPreferenceStore();
		pref.setDefault(CHECK_NEW_INFO_ON_START_UP, true);
		pref.setDefault(USE_CLICK_SINGLE, false);
	}

	/**
	 * Return whether window is maximized when start up
	 * 
	 * @return
	 */
	public static boolean isMaximizeWindowOnStartUp() {
		try {
			return pref.getBoolean(MAXIMIZE_WINDOW_ON_START_UP);
		} catch (Exception ignored) {
			return false;
		}
	}

	/**
	 * 
	 * Return whether check new information of CUBRID
	 * 
	 * @return
	 */
	public static boolean isCheckNewInfoOnStartUp() {
		try {
			return pref.getBoolean(CHECK_NEW_INFO_ON_START_UP);
		} catch (Exception ignored) {
			return false;
		}
	}

	/**
	 * 
	 * Use click once
	 * 
	 * @return
	 */
	public static boolean isUseClickOnce() {
		try {
			return pref.getBoolean(USE_CLICK_SINGLE);
		} catch (Exception ignored) {
			return false;
		}
	}
}
