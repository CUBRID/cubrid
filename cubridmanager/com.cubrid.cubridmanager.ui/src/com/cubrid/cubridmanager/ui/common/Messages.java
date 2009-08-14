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

package com.cubrid.cubridmanager.ui.common;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

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
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".common.Messages", Messages.class);
	}
	//common button message
	public static String btnOK;
	public static String btnCancel;
	public static String btnDetail;
	public static String btnRefresh;
	public static String btnDelete;
	public static String btnFind;
	public static String btnAdd;
	public static String btnEdit;
	public static String btnClose;
	//common title message
	public static String titleConfirm;
	public static String titleError;
	public static String titleWarning;
	public static String titleSuccess;
	//navigator tree related message
	public static String msgAddingChildren;
	public static String msgLoading;
	public static String msgLoadingChildren;
	//add host and edit host dialog related message
	public static String titleAddHostDialog;
	public static String titleConnectHostDialog;
	public static String msgAddHostDialog;
	public static String msgConnectHostDialog;
	public static String btnConnectHost;
	public static String btnAddHost;

	public static String tipAddHostButton;
	public static String tipConnectHostButton1;
	public static String tipConnectHostButton2;
	public static String lblHostName;
	public static String lblAddress;
	public static String lblPort;
	public static String lblUserName;
	public static String lblPassword;
	public static String errHostName;
	public static String errAddress;
	public static String errUserName;
	public static String errHostExist;
	public static String errAddressExist;
	public static String errPort;

	public static String msgConfirmDeleteHost;
	public static String msgConfirmDisconnectHost;
	public static String msgConfirmStopService;
	public static String titleServerVersion;

	//change password dialog related message
	public static String lblOldPassword;
	public static String lblNewPassword;
	public static String lblPasswordConfirm;
	public static String titleChangePasswordDialog;
	public static String msgChangePasswordDialog;
	public static String errNewPassword;
	public static String errPasswordConfirm;
	public static String errNotEqualPassword;
	public static String msgChangePassSuccess;
	//OID Navigator dialog related message
	public static String lblOIDValue;
	public static String titleOIDNavigatorDialog;
	public static String msgOIDNavigatorDialog;
	public static String errOIDValue1;
	public static String errOIDValue2;
	//user management dialog related message
	public static String tblColumnUserId;
	public static String tblColumnDbAuth;
	public static String tblColumnBrokerAuth;
	public static String tblColumnMonitorAuth;
	public static String titleUserManagementDialog;
	public static String msgUserManagementDialog;
	public static String msgUserManagementList;
	public static String msgDeleteUserConfirm;
	public static String lblUserId;
	public static String titleAddUser;
	public static String titleEditUser;
	public static String msgAddUser;
	public static String msgEidtUser;
	public static String lblDbAuth;
	public static String lblBrokerAuth;
	public static String lblMonitorAuth;
	public static String errPassword;
	public static String errUserExist;
	public static String errUserId;
	public static String tblColumnDbName;
	public static String tblColumnConnected;
	public static String tblColumnDbUser;
	public static String tblColumnBrokerIP;
	public static String tblColumnBrokerPort;
	public static String msgDbAuthList;
	public static String titleDbAuth;
	public static String msgDbAuth;
	public static String errDbAuth;
	//properties dialog related message
	public static String grpConnectInformation;
	public static String grpServerType;
	public static String grpService;
	public static String grpAutoDatabase;
	public static String tabItemGeneral;
	public static String tabItemAdvanceOptions;
	public static String tblColumnParameterName;
	public static String tblColumnValueType;
	public static String tblColumnParameterValue;
	public static String tblColumnParameterType;
	public static String grpGeneral;
	public static String grpDiagnositics;
	public static String msgChangeCMParaSuccess;
	public static String msgChangeServiceParaSuccess;
	public static String msgChangeServerParaSuccess;
	public static String lblServerType;
	//properties dialog server parameter error
	public static String errDataBufferPages;
	public static String errSortBufferPages;
	public static String errLogBufferPages;
	public static String errLockEscalation;
	public static String errLockTimeout;
	public static String errDeadLock;
	public static String errCheckpoint;
	public static String errCubridPortId;
	public static String errMaxClients;
	public static String errYesNoParameter;
	public static String errBackupVolumeMaxSize;
	public static String errCsqlHistoryNum;
	public static String errGroupCommitInterval;
	public static String errIndexScanInOidBuffPage;
	public static String errInsertExeMode;
	public static String errLockTimeOutMessageType;
	public static String errQueryCachMode;
	public static String errTempFileMemorySize;
	public static String errThreadStackSize;
	public static String errOnlyInteger;
	public static String errUnfillFactor;
	public static String errOnlyFloat;
	public static String errParameterValue;
	//property dialog cm pamameter error
	public static String errCmPort;
	public static String errMonitorInterval;
	public static String errServerLongQueryTime;
	// common messages
	public static String msgConfirmExistTitle;
	public static String msgExistConfirm;

	//BrokerParameterProperty
	public static String deleteBtnName;
	public static String editBtnName;
	public static String addBtnName;
	public static String refreshUnit;
	public static String refreshEnvOnLbl;
	public static String refreshEnvTitle;
	public static String portOfBrokerLst;
	public static String nameOfBrokerLst;
	public static String brokerLstGroupName;
	public static String generalInfoGroupName;
	public static String refreshEnvOfTap;
	public static String brokerLstOfTap;
	public static String editActionTxt;
	public static String addActionTxt;
	public static String delActionTxt;
	public static String restartBrokerMsg;
	public static String errMasterShmId;
	public static String errMasterShmIdSamePort;

	public static String productName;
	public static String aboutMessage;
	public static String titleAboutDialog;

}
