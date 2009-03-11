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

import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.widgets.Display;

public interface MainConstants {
	public static final byte BUILD_NORMAL = 0;
	public static final byte BUILD_PROTEGO = 1;
	public static final String MANAGER_VERSION = Version.getString("RELEASE.VERSION");
	public static final byte buildType = build_type.buildType;

	// User level
	public static final byte AUTH_NONE = 0;
	public static final byte AUTH_NONDBA = 1;
	public static final byte AUTH_DBA = 2;
	public static final byte AUTH_DBASTART = 3;
	public static final byte AUTH_DBASTOP = 4;
	public static final byte STATUS_START = 0;
	public static final byte STATUS_STOP = 1;
	public static final byte STATUS_NONE = 2; // not exist DB
	public static final byte STATUS_WAIT = 3; // suspend
	public static final byte NO = 0;
	public static final byte YES = 1;
	public static final byte NAVI_CUBRID = 0;
	public static final byte NAVI_CAS = 1;
	public static final byte NAVI_DIAG = 2;
	public static final String SYSPARA_HOSTCNT = "syspara_hostcnt";
	public static final String SYSPARA_HOSTBASE = "syspara_host";
	public static final String SYSPARA_HOSTADDR = "_addr";
	public static final String SYSPARA_HOSTPORT = "_port";
	public static final String SYSPARA_HOSTID = "_id";
	public static final String SQLX_DATABUFS = "num_data_buffers";
	public static final String SQLX_MEDIAFAIL = "media_failures_are_supported";
	public static final String SQLX_MAXCLI = "max_clients";
	public static final String DBPARA_GENERICNUM = "generic_num_page";
	public static final String DBPARA_LOGNUM = "log_num_page";
	public static final String DBPARA_PAGESIZE = "page_size";
	public static final String DBPARA_DATANUM = "datavol_num_page";
	public static final String DBPARA_INDEXNUM = "indexvol_num_page";
	public static final String DBPARA_TEMPNUM = "tempvol_num_page";
	public static final String MONPARA_STATUS = "monitoring_status";
	public static final String MONPARA_INTERVAL = "monitoring_interval";
	public static final String FILE_CONFIGURATION = "configuration.ini";
	public static final String PROP_HOSTCONFIGURATION = "Cubridmanager Configuration";
	public static final String WORK_FOLDER = "workfolder";
	public static final String DBVERSION_NEED_MDBC_NODE = "6.0";
	public static final String DBVERSION_NEED_REVERSE_INDEX = "6.1";
	public static final int MEGABYTES = 1024000;
	public static final String propLastSelectionItem = "lastselectionitem";

	// Query editor option
	public static final String queryEditorOptionAucoCommit = "autocommit";
	public static final String queryEditorOptionGetQueryPlan = "getqueryplan";
	public static final String queryEditorOptionRecordLimit = "recordlimit";
	public static final String queryEditorOptionPageLimit = "pagelimit";
	public static final String queryEditorOptionGetOidInfo = "oidinfo";
	public static final String queryEditorOptionCasPort = "casport";
	public static final String queryEditorOptionCharSet = "charset";
	public static final String queryEditorOptionFontString = "fontstring";
	public static final String queryEditorOptionFontColorRed = "fontcolorred";
	public static final String queryEditorOptionFontColorGreen = "fontcolorgreen";
	public static final String queryEditorOptionFontColorBlue = "fontcolorblue";

	// Main window size
	public static final String mainWindowX = "mainwindowx";
	public static final String mainWindowY = "mainwindowy";
	public static final String mainWindowMaximize = "mainwindowmaximize";
	public static final byte viewBrokerStatus = 0;
	public static final byte viewBrokerJob = 1;
	public static final byte viewNone = -1;
	public static final Color colorOddLine = new Color(Display.getCurrent(),
			255, 255, 255);
	public static final Color colorEvenLine = new Color(Display.getCurrent(),
			246, 246, 244);
	public static final String NEW_LINE = System.getProperty("line.separator");

	// protego login option
	public static final String protegoLoginType = new String("protegoLoginType");
	public static final String protegoLoginTypeCert = new String("cert");
	public static final String protegoLoginTypeMtId = new String("mtid");
}
