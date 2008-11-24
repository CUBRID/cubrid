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

import java.util.ArrayList;
import java.util.Properties;

import org.eclipse.ui.IWorkbenchPart;

import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.diag.DiagActivityMonitorTemplate;
import cubridmanager.diag.DiagExecuteCasRunnerResult;
import cubridmanager.diag.DiagSiteDiagData;
import cubridmanager.diag.DiagStatusMonitorTemplate;
import cubridmanager.query.StructQueryEditorOptions;
import cubridmanager.query.view.QueryEditor;

public class MainRegistry {
	/* diag message type */
	public final static int DIAGMESSAGE_TYPE_STATUS = 1;
	public final static int DIAGMESSAGE_TYPE_ACTIVITY = 2;
	public static boolean FirstLogin = true;
	public static ArrayList Authinfo = new ArrayList();
	public static ArrayList CASinfo = new ArrayList();
	public static ArrayList CASadminlog = new ArrayList();
	public static ArrayList AddedBrokers = new ArrayList();
	public static ArrayList DeletedBrokers = new ArrayList();
	public static ArrayList listDBUserInfo = new ArrayList();
	public static ArrayList listOtherCMUserInfo = new ArrayList();
	public static byte DiagAuth = MainConstants.AUTH_NONE;
	public static byte CASAuth = MainConstants.AUTH_NONE;
	public static boolean IsDBAAuth = false;
	public static boolean IsConnected = false;
	public static boolean IsSecurityManager = false;
	public static boolean IsCASStart = false;
	public static boolean IsCASinfoReady = false;
	public static boolean NaviDraw_CUBRID = false;
	public static boolean NaviDraw_CAS = false;
	public static boolean NaviDraw_DIAG = false;
	public static byte Current_Navigator = MainConstants.NAVI_CUBRID;
	public static String Current_Topview = new String("");
	public static String UserID = new String("");
	public static String UserPassword = new String("");
	public static String UserSignedData = new String("");
	public static CUBRIDConnectionKey connKey = null;
	public static String HostDesc = new String("");
	public static String HostAddr = new String("");
	public static String HostToken = new String("");
	public static int HostPort = 0;
	public static int HostJSPort = 0;
	public static int upaPort = 0;
	public static boolean WaitDlg = false;
	public static String SQLX_DATABUFS = new String("");
	public static String SQLX_MEDIAFAIL = new String("");
	public static String SQLX_MAXCLI = new String("");
	public static String DBPARA_GENERICNUM = new String("");
	public static String DBPARA_LOGNUM = new String("");
	public static String DBPARA_PAGESIZE = new String("");
	public static String DBPARA_DATANUM = new String("");
	public static String DBPARA_INDEXNUM = new String("");
	public static String DBPARA_TEMPNUM = new String("");
	public static String MONPARA_STATUS = new String("");
	public static String MONPARA_INTERVAL = new String("");
	public static String envCUBRID = new String("");
	public static String envCUBRID_DATABASES = new String("");
	public static String envCUBRID_DBMT = new String("");
	public static String CUBRIDVer = new String("");
	public static String BROKERVer = new String("");
	public static String hostMonTab0 = new String("");
	public static String hostMonTab1 = new String("");
	public static String hostMonTab2 = new String("");
	public static String hostMonTab3 = new String("");
	public static String hostOsInfo = new String("");
	public static String TmpVolsize = null;
	public static String TmpVolpath = null;
	public static ArrayList Tmpchkrst = new ArrayList();
	public static ArrayList Tmpary = new ArrayList();
	public static ArrayList tmpCasLogList = new ArrayList();
	public static ArrayList tmpAnalyzeCasLogResult = new ArrayList();
	public static HostmonSocket soc = null;
	public static boolean isCertificateLogin = true;

	/* server config */
	public static String CubridConf = new String("");
	public static String BrokerConf = new String("");
	
	/* diag data */
	public static ArrayList diagSiteDiagDataList = new ArrayList();
	public static String currentSite = new String();
	public static DiagExecuteCasRunnerResult tmpDiagExecuteCasRunnerResult = new DiagExecuteCasRunnerResult();
	public static boolean CASLogView_RequestedInDiag = false;
	public static String CASLogView_RequestBrokername = new String("");
	public static String diagErrorString = new String("");

	/* Query editor option */
	public static StructQueryEditorOptions queryEditorOption = new StructQueryEditorOptions();
	public static boolean isFindDlgOpen = false;

	public static QueryEditor getCurrentQueryEditor() {
		IWorkbenchPart currentView = WorkView.workwindow.getActivePage()
				.getActivePart();
		if (currentView instanceof QueryEditor)
			return (QueryEditor) currentView;
		else
			return null;
	}

	public static void SetCurrentSiteName(String name) {
		currentSite = name;
	}

	public static String GetCurrentSiteName() {
		return currentSite;
	}

	public static boolean Authinfo_add(String name, String user, byte stat) {
		AuthItem au = new AuthItem(name, user, stat);

		return Authinfo.add(au);
	}

	public static boolean Authinfo_add(String name, String user, String dir,
			byte stat) {
		AuthItem au = new AuthItem(name, user, dir, stat);

		return Authinfo.add(au);
	}

	public static boolean Authinfo_add(AuthItem ai) {
		return Authinfo.add(ai);
	}

	public static AuthItem Authinfo_find(String name) {
		AuthItem authrec;
		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			authrec = (AuthItem) MainRegistry.Authinfo.get(i);
			if (authrec.dbname.equals(name))
				return authrec;
		}
		return null;
	}

	public static boolean Authinfo_remove(String name) {
		AuthItem authrec;
		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			authrec = (AuthItem) MainRegistry.Authinfo.get(i);
			if (authrec.dbname.equals(name)) {
				MainRegistry.Authinfo.remove(i);
				return true;
			}
		}
		return false;
	}

	public static boolean Authinfo_update(AuthItem newrec) {
		AuthItem authrec;
		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			authrec = (AuthItem) MainRegistry.Authinfo.get(i);
			if (authrec.dbname.equals(newrec.dbname)) {
				MainRegistry.Authinfo.set(i, newrec);
				return true;
			}
		}
		return false;
	}

	public static AuthItem Authinfo_ready(String name) {
		AuthItem authrec;
		long fromtime, totime;

		fromtime = System.currentTimeMillis();
		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			authrec = (AuthItem) MainRegistry.Authinfo.get(i);
			if (authrec.dbname.equals(name)) {
				if (authrec.lock) { // others working
					totime = System.currentTimeMillis();
					if ((totime - fromtime) > (10 * 1000))
						return authrec; // for deadlock
					try {
						Thread.sleep(50);
					} catch (Exception e) {
					}
					i--; // retry;
					continue;
				} else {
					authrec.lock = true;
					MainRegistry.Authinfo.set(i, authrec);
					return authrec;
				}
			}
		}
		return null;
	}

	public static boolean Authinfo_commit(AuthItem newrec) {
		AuthItem authrec;
		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			authrec = (AuthItem) MainRegistry.Authinfo.get(i);
			if (authrec.dbname.equals(newrec.dbname)) {
				newrec.lock = false;
				MainRegistry.Authinfo.set(i, newrec);
				return true;
			}
		}
		return false;
	}

	public static boolean addDBUserInfo(String name, String user,
			String password) {
		DBUserInfo ui = new DBUserInfo(name, user, password);

		return listDBUserInfo.add(ui);
	}

	public static boolean addDBUserInfo(DBUserInfo ui) {
		return listDBUserInfo.add(ui);
	}

	public static DBUserInfo getDBUserInfo(String name) {
		DBUserInfo dbuser;
		for (int i = 0, n = MainRegistry.listDBUserInfo.size(); i < n; i++) {
			dbuser = (DBUserInfo) MainRegistry.listDBUserInfo.get(i);
			if (dbuser.dbname.equals(name))
				return dbuser;
		}
		return null;
	}

	public static boolean DBUserInfo_remove(String name) {
		DBUserInfo dbuser;
		for (int i = 0, n = MainRegistry.listDBUserInfo.size(); i < n; i++) {
			dbuser = (DBUserInfo) MainRegistry.listDBUserInfo.get(i);
			if (dbuser.dbname.equals(name)) {
				MainRegistry.listDBUserInfo.remove(i);
				return true;
			}
		}
		return false;
	}

	public static boolean DBUserInfo_update(DBUserInfo newdbuser) {
		DBUserInfo dbuser;
		for (int i = 0, n = MainRegistry.listDBUserInfo.size(); i < n; i++) {
			dbuser = (DBUserInfo) MainRegistry.listDBUserInfo.get(i);
			if (dbuser.dbname.equals(newdbuser.dbname)) {
				MainRegistry.listDBUserInfo.set(i, newdbuser);
				return true;
			}
		}
		return false;
	}

	public static boolean DBUserInfo_update(String dbname, String username,
			String password) {
		DBUserInfo dbuser;
		for (int i = 0, n = MainRegistry.listDBUserInfo.size(); i < n; i++) {
			dbuser = (DBUserInfo) MainRegistry.listDBUserInfo.get(i);
			if (dbuser.dbname.equals(dbname) && dbuser.dbuser.equals(username)) {
				dbuser.dbpassword = password;
				return true;
			}
		}
		return false;
	}

	public static CASItem CASinfo_find(String name) {
		CASItem casrec;
		for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
			casrec = (CASItem) MainRegistry.CASinfo.get(i);
			if (casrec.broker_name.equals(name))
				return casrec;
		}
		return null;
	}

	public static CASItem CASinfo_find(int port) {
		CASItem casrec;
		for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
			casrec = (CASItem) MainRegistry.CASinfo.get(i);
			if (casrec.broker_port == port)
				return casrec;
		}
		return null;
	}

	public static boolean CASinfo_update(CASItem newrec) {
		CASItem casrec;
		for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
			casrec = (CASItem) MainRegistry.CASinfo.get(i);
			if (casrec.broker_name.equals(newrec.broker_name)) {
				MainRegistry.CASinfo.set(i, newrec);
				return true;
			}
		}
		return false;
	}

	public static DiagSiteDiagData getSiteDiagDataByName(String siteName) {
		int siteNum = diagSiteDiagDataList.size();
		DiagSiteDiagData tempSiteInfo;

		for (int i = 0; i < siteNum; i++) {
			tempSiteInfo = (DiagSiteDiagData) (diagSiteDiagDataList.get(i));
			// temporarily add
			if (tempSiteInfo.site_name.equals(siteName)) {
				return tempSiteInfo;
			}
		}

		return null;
	}

	public static DiagStatusMonitorTemplate getStatusTemplateByName(
			String siteName, String templateName) {
		int siteNum = diagSiteDiagDataList.size();
		DiagSiteDiagData tempSiteInfo;
		DiagStatusMonitorTemplate tempTemplate;

		for (int i = 0; i < siteNum; i++) {
			tempSiteInfo = (DiagSiteDiagData) (diagSiteDiagDataList.get(i));
			// temporarily add
			if (tempSiteInfo.site_name.equals(siteName)) {
				int templateCount = tempSiteInfo.statusTemplateList.size();
				for (int j = 0; j < templateCount; j++) {
					tempTemplate = (DiagStatusMonitorTemplate) tempSiteInfo.statusTemplateList
							.get(j);
					if (tempTemplate.templateName.equals(templateName)) {
						return tempTemplate;
					}
				}
			}
		}

		return null;
	}

	public static boolean removeStatusTemplateByName(String siteName,
			String templateName) {
		int siteNum = diagSiteDiagDataList.size();
		DiagSiteDiagData tempSiteInfo;
		DiagStatusMonitorTemplate tempTemplate;

		for (int i = 0; i < siteNum; i++) {
			tempSiteInfo = (DiagSiteDiagData) (diagSiteDiagDataList.get(i));
			// temporarily add
			if (tempSiteInfo.site_name.equals(siteName)) {
				int templateCount = tempSiteInfo.statusTemplateList.size();
				for (int j = 0; j < templateCount; j++) {
					tempTemplate = (DiagStatusMonitorTemplate) tempSiteInfo.statusTemplateList
							.get(j);
					if (tempTemplate.templateName.equals(templateName)) {
						tempSiteInfo.statusTemplateList.remove(j);
						return true;
					}
				}
			}
		}

		return false;
	}

	public static boolean removeActivityTemplateByName(String siteName,
			String templateName) {
		int siteNum = diagSiteDiagDataList.size();
		DiagSiteDiagData tempSiteInfo;
		DiagActivityMonitorTemplate tempTemplate;

		for (int i = 0; i < siteNum; i++) {
			tempSiteInfo = (DiagSiteDiagData) (diagSiteDiagDataList.get(i));
			// temporarily add
			if (tempSiteInfo.site_name.equals(siteName)) {
				int templateCount = tempSiteInfo.activityTemplateList.size();
				for (int j = 0; j < templateCount; j++) {
					tempTemplate = (DiagActivityMonitorTemplate) tempSiteInfo.activityTemplateList
							.get(j);
					if (tempTemplate.templateName.equals(templateName)) {
						tempSiteInfo.activityTemplateList.remove(j);
						return true;
					}
				}
			}
		}
		return false;
	}

	public static DiagActivityMonitorTemplate getActivityTemplateByName(
			String siteName, String templateName) {
		int siteNum = diagSiteDiagDataList.size();
		DiagSiteDiagData tempSiteInfo;
		DiagActivityMonitorTemplate tempTemplate;

		for (int i = 0; i < siteNum; i++) {
			tempSiteInfo = (DiagSiteDiagData) (diagSiteDiagDataList.get(i));
			// temporarily add
			if (tempSiteInfo.site_name.equals(siteName)) {
				int templateCount = tempSiteInfo.activityTemplateList.size();
				for (int j = 0; j < templateCount; j++) {
					tempTemplate = (DiagActivityMonitorTemplate) tempSiteInfo.activityTemplateList
							.get(j);
					if (tempTemplate.templateName.equals(templateName)) {
						return tempTemplate;
					}
				}
			}
		}

		return null;
	}

	/**
	 * Query editor option setting - All parameter type are String and save with
	 * automatically converting 
	 * 
	 * @param isAutoCommit
	 *            autoCommit : yes/no
	 * @param doesGetQueryPlan
	 *            Get Query Plan after query execute : yes/no
	 * @param recordLimit
	 *            Query result limit : integer value equal or bigger then 0
	 * @param getOidInfo
	 *            Include OID with query result : yes/no 
	 * @param casPort
	 *            Cas port that using by Query Editor : 0~65535
	 * @param charSet
	 *            Cas's Character set : ex) euc_kr 
	 */
	public static void setQueryEditorOption(String isAutoCommit,
			String doesGetQueryPlan, String recordLimit, String getOidInfo,
			String casPort, String charSet, String fontString,
			String fontColorRed, String fontColorGreen, String fontColorBlue) {
		if (isAutoCommit == null)
			isAutoCommit = "Yes";
		if (doesGetQueryPlan == null)
			doesGetQueryPlan = "Yes";
		if (getOidInfo == null)
			getOidInfo = "No";

		queryEditorOption.autocommit = CommonTool.yesNoToBoolean(isAutoCommit);
		queryEditorOption.getqueryplan = CommonTool
				.yesNoToBoolean(doesGetQueryPlan);
		queryEditorOption.oidinfo = CommonTool.yesNoToBoolean(getOidInfo);
		try {
			queryEditorOption.recordlimit = Integer.parseInt(recordLimit);
			if (queryEditorOption.recordlimit < 1)
				queryEditorOption.recordlimit = 0;
		} catch (Exception e) {
			queryEditorOption.recordlimit = 10000;
		}
		try {
			queryEditorOption.casport = Integer.parseInt(casPort);
			if (queryEditorOption.casport > 65535
					|| queryEditorOption.casport < 0)
				throw new Exception();
		} catch (Exception e) {
			if (CASinfo != null)
				queryEditorOption.casport = ((CASItem) CASinfo.get(0)).broker_port;
			else
				queryEditorOption.casport = 30000;
		}
		if (charSet == null)
			queryEditorOption.charset = "";
		else
			queryEditorOption.charset = charSet;

		if (fontString == null)
			queryEditorOption.fontString = "";
		else
			queryEditorOption.fontString = fontString;

		try {
			queryEditorOption.fontColorRed = Integer.parseInt(fontColorRed);
			queryEditorOption.fontColorGreen = Integer.parseInt(fontColorGreen);
			queryEditorOption.fontColorBlue = Integer.parseInt(fontColorBlue);
		} catch (Exception e) {
			/* default black */
			queryEditorOption.fontColorRed = 0;
			queryEditorOption.fontColorGreen = 0;
			queryEditorOption.fontColorBlue = 0;
		}

	}

	public static void saveMainRegistryToProperty() {
		Properties prop = new Properties();
		if (!CommonTool.LoadProperties(prop)) {
			return;
		}
		prop.setProperty(MainConstants.queryEditorOptionAucoCommit, CommonTool
				.BooleanYesNo(queryEditorOption.autocommit));
		prop.setProperty(MainConstants.queryEditorOptionCasPort, Integer
				.toString(queryEditorOption.casport));
		prop.setProperty(MainConstants.queryEditorOptionCharSet,
				queryEditorOption.charset);
		prop.setProperty(MainConstants.queryEditorOptionFontString,
				queryEditorOption.fontString);
		prop.setProperty(MainConstants.queryEditorOptionFontColorRed, Integer
				.toString(queryEditorOption.fontColorRed));
		prop.setProperty(MainConstants.queryEditorOptionFontColorGreen, Integer
				.toString(queryEditorOption.fontColorGreen));
		prop.setProperty(MainConstants.queryEditorOptionFontColorBlue, Integer
				.toString(queryEditorOption.fontColorBlue));
		prop.setProperty(MainConstants.queryEditorOptionGetOidInfo, CommonTool
				.BooleanYesNo(queryEditorOption.oidinfo));
		prop.setProperty(MainConstants.queryEditorOptionGetQueryPlan,
				CommonTool.BooleanYesNo(queryEditorOption.getqueryplan));
		prop.setProperty(MainConstants.queryEditorOptionRecordLimit, Integer
				.toString(queryEditorOption.recordlimit));

		CommonTool.SaveProperties(prop);
	}

	public static boolean isProtegoBuild() {
		if (MainConstants.buildType == MainConstants.BUILD_PROTEGO)
			return true;
		else
			return false;
	}

	public static boolean isCertLogin() {
		if (isProtegoBuild() && isCertificateLogin)
			return true;
		else
			return false;
	}

	public static boolean isMTLogin() {
		if (isProtegoBuild() && !isCertificateLogin)
			return true;
		else
			return false;
	}
}
