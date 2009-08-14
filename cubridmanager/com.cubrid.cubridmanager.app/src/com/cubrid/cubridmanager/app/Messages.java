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

import org.eclipse.osgi.util.NLS;

/**
 * 
 * This is message bundle classes and provide convenience methods for
 * manipulating messages.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerAppPlugin.PLUGIN_ID + ".Messages",
				Messages.class);
	}
	public static String unsupportedJRETitle;
	public static String unsupportedJRE;
	//menu name
	public static String mnu_fileMenuName;
	public static String mnu_editMenuName;
	public static String mnu_toolsMenuName;
	public static String mnu_actionMenuName;
	public static String mnu_helpMneuName;
	//action
	public static String openPreferenceActionName;
	public static String exitActionName;

	public static String closeActionName;
	public static String closeAllActionName;
	public static String saveActionName;
	public static String saveAsActionName;
	public static String saveAllActionName;

	public static String helpActionName;
	public static String dynamicHelpActionName;
	public static String searchActionName;
	public static String checkNewVersionActionName;
	public static String cubridOnlineForumActionName;
	public static String cubridProjectSiteActionName;
	public static String aboutActionName;

	public static String msgExistConfirm;

	//manager general preferences
	public static String prefTitleGeneral;
	public static String maximizeWindowOnStartUp;
	public static String btnCheckNewInfo;
	public static String btnUseSingleClick;
	public static String btnUseDoubleClick;
	public static String grpOpenMode;

	public static String cubridNewInfoEditorName;
	public static String errCannotOpenInternalBrowser;
	public static String errCannotOpenExternalBrowser;

}
