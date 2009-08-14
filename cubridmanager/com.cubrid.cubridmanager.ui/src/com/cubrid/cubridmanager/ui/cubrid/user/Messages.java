/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search
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
package com.cubrid.cubridmanager.ui.cubrid.user;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * Message bundle classes. Provides convenience methods for manipulating
 * messages.
 * 
 * @author pangqiren 2009-3-2
 * 
 */
public class Messages extends NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID + ".cubrid.user.Messages", Messages.class);
	}

	public static String errInput;

	public static String addClassButtonName;
	public static String deleteClassButtonName;

	public static String tabItemGeneral;
	public static String tabItemAuthoration;
	public static String lblOldPassword;
	public static String lblNewPassword;
	public static String lblNewPasswordConf;
	public static String lblPassword;
	public static String lblPasswordConf;
	public static String lblAllUser;
	public static String grpUserGroupList;
	public static String grpUserMemberList;
	public static String errRomoveUserGroup;
	public static String tblColMemberName;
	public static String tblColClassName;
	public static String tblColClassSchematype;
	public static String tblColClassOwner;
	public static String tblColClassSuperclass;
	public static String tblColClassType;
	public static String errRemoveSysClass;
	public static String msgUserSchema;
	public static String msgVirtualClass;
	public static String msgClass;
	public static String tblColAuthTable;
	public static String tblColAuthSelect;
	public static String tblColAuthInsert;
	public static String tblColAuthUpdate;
	public static String tblColAuthDelete;
	public static String tblColAuthAlter;
	public static String tblColAuthIndex;
	public static String tblColAuthExecute;
	public static String tblColAuthGrantselect;
	public static String tblColAuthGrantinsert;
	public static String tblColAuthGrantupdate;
	public static String tblColAuthGrantdelete;
	public static String tblColAuthGrantalter;
	public static String tblColAuthGrantindex;
	public static String tblColAuthGrantexecute;
	public static String msgEditUserDialog;
	public static String msgAddUserDialog;
	public static String errPasswordDiff;
	public static String errInputPassLength;
	public static String errInputName;
	public static String errInputNameLength;
	public static String errInputNameExist;
	public static String errInputNameContain;
	public static String errInputNameAccept;
	public static String errInputNameValidate;
	public static String msgTitleWarning;
	public static String msgQuestionSureDelete;
	public static String grpUserMemberInfo;
	public static String lblUserName;
	public static String msgSelectDB;
	public static String errOldPassword;
	public static String grpPasswordSetting;
	public static String btnPasswordChange;
	public static String btnAddGroup;
	public static String btnRemoveGroup;
	public static String msgLogoutInfomation;
	public static String titleLogout;
	public static String errInputPassword;
	public static String errInvalidPassword;
	public static String lblAuthorizedTable;
	public static String lblUnAuthorizedTable;
	public static String lblOwnerClassList;
	public static String tblColOwnerClassName;
	public static String tblColOwnerClassSchema;
	public static String tblColOwnerClassType;
	public static String lblAuthorizationList;
	public static String lblDbaAllAuth;
	public static String msgSystemSchema;
	public static String lblGroupList;
	public static String lblGroupNotExist;
	public static String lblMemberList;
	public static String lblMemberNotExist;
	public static String msgTaskJobName;
}