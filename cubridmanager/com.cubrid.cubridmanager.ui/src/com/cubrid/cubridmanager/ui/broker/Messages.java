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
package com.cubrid.cubridmanager.ui.broker;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * 
 * This is message bundle classes and provide convenient methods for
 * manipulating messages.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-3-2 created by pangqiren
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".broker.Messages", Messages.class);
	}

	public static String msgSelectBroker;
	public static String errTitle;
	//BrokerParameterDialog
	public static String editTitle;
	public static String addTitle;
	public static String editMsg;
	public static String addMsg;

	//brokerEnvStatusView
	public static String tblBrokerName;
	public static String tblBrokerStatus;
	public static String tblBrokerProcess;
	public static String tblPort;
	public static String tblServer;
	public static String tblQueue;
	public static String tblLongTran;
	public static String tblLongQuery;
	public static String tblErrQuery;
	public static String tblRequest;
	public static String tblAutoAdd;
	public static String tblTps;
	public static String tblQps;
	public static String tblConn;
	public static String tblSession;
	public static String tblSqllog;
	public static String tblLog;
	public static String envHeadTitel;

	//brokerStausView
	public static String tblAsId;
	public static String tblAsProcess;
	public static String tblAsRequest;
	public static String tblAsSize;
	public static String tblAsStatus;
	public static String tblAsLastAccess;
	public static String tblAsCur;
	public static String tblAsQps;
	public static String tblAsLqs;
	public static String tblAsDb;
	public static String tblAsHost;
	public static String tblAsLct;
	public static String jobTblTitle;
	public static String tblJobId;
	public static String tblJobPriority;
	public static String tblJobAddress;
	public static String tblJobTime;
	public static String tblJobRequest;
	public static String headTitel;
	public static String restartBrokerServerTip;
	public static String restartBrokerServerMsg;

	//BrokerEditorProperty
	public static String refreshNameOfTap;
	public static String parameterNameOfTap;
	public static String refreshTitle;
	public static String tblParameter;
	public static String tblValueType;
	public static String tblParamValue;
	public static String refreshOnLbl;
	public static String refreshUnitLbl;
	public static String restartBrokerMsg;

	//BrokerParameterDialog
	public static String paraTblParameter;
	public static String paraTblValueType;
	public static String paraTblParamValue;
	public static String brokerNameLbl;
	public static String paraRefreshNameOfTap;
	public static String paraParameterNameOfTap;
	public static String shellEditTitle;
	public static String shellAddTitle;
	public static String errReduplicatePort;
	public static String errNoPort;
	public static String errReduplicateName;
	public static String errParameterValue;
	public static String errBrokerName;
	public static String errMaxStringLengthValue;
	public static String errPositiveValue;
	public static String errMinNumApplServerValue;
	public static String errMaxNumApplServeValue;
	public static String errUseMasterShmId;
	public static String errReduplicateShmId;
	public static String errBrokerPortAndShmId;
	//StopBrokerAction
	public static String stopBrokerConfirmTitle;
	public static String stopBrokerConfirmContent;
	//StopBrokerEnvAction
	public static String stopBrokerEnvConfirmTitle;
	public static String stopBrokerEnvConfirmContent;

}