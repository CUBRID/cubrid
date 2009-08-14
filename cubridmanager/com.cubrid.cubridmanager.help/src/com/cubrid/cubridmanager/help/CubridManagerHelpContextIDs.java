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
package com.cubrid.cubridmanager.help;

/**
 * 
 * The dialog is used to show CasRunnerResult.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-7-13 created by wuyingshi
 */
public interface CubridManagerHelpContextIDs {
	public static final String PREFIX = CubridManagerHelpPlugin.PLUGIN_ID;
	/*host */
	//add
	public static final String hostAdd = PREFIX + ".hostAdd";
	//connect
	public static final String hostConnect = PREFIX + ".hostConnect";
	//change password
	public static final String changePasswd = PREFIX + ".changePasswd";

	//property
	public static final String hostInfoProperty = PREFIX + ".hostInfoProperty";
	public static final String serviceProperty = PREFIX + ".serviceProperty";
	public static final String serverProperty = PREFIX + ".serverProperty";
	public static final String brokerProperty = PREFIX + ".brokerProperty";
	public static final String managerProperty = PREFIX + ".managerProperty";
	public static final String queryOption = PREFIX + ".queryOption";

	/*database */
	//database group popup menu
	public static final String databaseCreate = PREFIX + ".databaseCreate";
	//property = serverProperty	
	//database popup menu
	public static final String databaseLoad = PREFIX + ".databaseLoad";
	public static final String databaseUnload = PREFIX + ".databaseUnload";
	public static final String databaseBackup = PREFIX + ".databaseBackup";
	public static final String databaseRestore = PREFIX + ".databaseRestore";
	public static final String databaseRename = PREFIX + ".databaseRename";
	public static final String databaseCopy = PREFIX + ".databaseCopy";
	public static final String databaseOptimize = PREFIX + ".databaseOptimize";
	public static final String databaseCompact = PREFIX + ".databaseCompact";
	public static final String databaseCheck = PREFIX + ".databaseCheck";
	public static final String databaseLock = PREFIX + ".databaseLock";
	public static final String databaseTransaction = PREFIX
			+ ".databaseTransaction";
	public static final String databaseOid = PREFIX + ".databaseOid";
	public static final String connectProperty = PREFIX + ".connectProperty";
	//property2 = serverProperty	
	// user
	public static final String databaseUser = PREFIX + ".databaseUser";
	//table
	public static final String databaseTable = PREFIX + ".databaseTable";
	//view
	public static final String databaseView = PREFIX + ".databaseView";
	//trigger
	public static final String databaseTrigger = PREFIX + ".databaseTrigger";
	//serial
	public static final String databaseSerial = PREFIX + ".databaseSerial";
	//procedure
	public static final String databaseProcedure = PREFIX
			+ ".databaseProcedure";
	//jobauto
	public static final String databaseJobauto = PREFIX + ".databaseJobauto";
	//space
	public static final String databaseSpace = PREFIX + ".databaseSpace";

	/*broker */
	//property = brokerProperty	
	//sql log
	public static final String brokerSqlLog = PREFIX + ".brokerSqlLog";
	//log property = logProperty

	/*monitor */
	public static final String monitorAdd = PREFIX + ".monitorAdd";
	public static final String monitorEdit = PREFIX + ".monitorEdit";

	/*log */
	public static final String logProperty = PREFIX + ".logProperty";

	/*MENU */
	//File->Preferences
	public static final String baseProperty = PREFIX + ".baseProperty";
	public static final String helpProperty = PREFIX + ".helpProperty";
	//Preference3 = queryOption
	//Tools -> User Management
	public static final String userSet = PREFIX + ".userSet";
	public static final String userAdd = PREFIX + ".userAdd";
	public static final String userEdit = PREFIX + ".userEdit";

	/*EDITOR PART AND VIEW PART */
	public static final String logEditor = PREFIX + ".logEditor";
	public static final String queryEditor = PREFIX + ".queryEditor";
	public static final String databaseStatusEditor = PREFIX
			+ ".databaseStatusEditor";
	public static final String volumeInformationEditor = PREFIX
			+ ".volumeInformationEditor";
	public static final String volumeFolderInfoEditor = PREFIX
			+ ".volumeFolderInfoEditor";
	public static final String schemaEditor = PREFIX + ".schemaEditor";
	public static final String statusMonitorView = PREFIX
			+ ".statusMonitorView";
	public static final String brokerEnvStatusView = PREFIX
			+ ".brokerEnvStatusView";
	public static final String brokerStatusView = PREFIX + ".brokerStatusView";
	public static final String schemaView = PREFIX + ".schemaView";
}
