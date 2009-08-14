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
package com.cubrid.cubridmanager.ui.monitoring;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * 
 * This is message bundle classes and provide convenience methods for
 * manipulating messages.
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-2 created by lizhiqiang
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".monitoring.Messages", Messages.class);
	}
	//DiagStatusMonitorTemplate
	public static String addTitle;
	public static String editTitle;
	public static String addMessage;
	public static String editMessage;
	public static String statusMonitorList;
	public static String statusMonitorListDb;
	public static String statusMonitorListBroker;
	public static String diagCategory;
	public static String diagName;
	public static String emptyNameTxt;
	public static String errsamplingTermsTxt;
	public static String overLmtSamplingTermsTx;
	public static String emptyDescTxt;
	public static String emptyDbTxt;
	public static String hasSameName;
	public static String noSuchDb;
	public static String noTargetObj;
	public static String noPermitMonitorDb;

	public static String targetObjGroup;
	public static String addBtnTxt;
	public static String removeBtnTxt;
	public static String templateGroup;
	public static String templateName;
	public static String sammpleTerm;
	public static String templateDesc;
	public static String targetDb;

	//DeleteStatusMonitorTemplate
	public static String delStatusMonitorConfirmContent;

}