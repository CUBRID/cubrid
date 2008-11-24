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

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.net.UnknownHostException;
import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Collections;
import java.util.Properties;

import org.eclipse.swt.widgets.Shell;

import cubridmanager.action.ManagerLogAction;
import cubridmanager.cas.BrokerAS;
import cubridmanager.cas.BrokerJobStatus;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.action.SetParameterAction;
import cubridmanager.cas.view.BrokerJob;
import cubridmanager.cubrid.AddVols;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.Authorizations;
import cubridmanager.cubrid.AutoQuery;
import cubridmanager.cubrid.BackupInfo;
import cubridmanager.cubrid.Constraint;
import cubridmanager.cubrid.DBAttribute;
import cubridmanager.cubrid.DBError;
import cubridmanager.cubrid.DBMethod;
import cubridmanager.cubrid.DBResolution;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.cubrid.Jobs;
import cubridmanager.cubrid.LocalDatabase;
import cubridmanager.cubrid.LockEntry;
import cubridmanager.cubrid.LockHolders;
import cubridmanager.cubrid.LockInfo;
import cubridmanager.cubrid.LockObject;
import cubridmanager.cubrid.LockTran;
import cubridmanager.cubrid.LockWaiters;
import cubridmanager.cubrid.Lock_B_Holders;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.cubrid.Parameters;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.SuperClass;
import cubridmanager.cubrid.Trigger;
import cubridmanager.cubrid.UnloadInfo;
import cubridmanager.cubrid.UserInfo;
import cubridmanager.cubrid.VolumeInfo;
import cubridmanager.cubrid.action.BackupAction;
import cubridmanager.cubrid.action.LoadAction;
import cubridmanager.cubrid.action.LockinfoAction;
import cubridmanager.cubrid.dialog.AUTOADDVOLDialog;
import cubridmanager.cubrid.dialog.FILEDOWN_PROGRESSDialog;
import cubridmanager.cubrid.dialog.LogViewDialog;
import cubridmanager.diag.DiagActivityMonitorTemplate;
import cubridmanager.diag.DiagActivityResult;
import cubridmanager.diag.DiagAnalyzeCasLogResult;
import cubridmanager.diag.DiagSiteDiagData;
import cubridmanager.diag.DiagStatusMonitorTemplate;
import cubridmanager.diag.DiagStatusResult;
import cubridmanager.diag.dialog.DiagActivityCASLogPathDialog;
import cubridmanager.diag.dialog.DiagActivityMonitorDialog;
import cubridmanager.diag.dialog.DiagCasRunnerResultViewDialog;
import cubridmanager.diag.dialog.DiagStatusMonitorDialog;

public class ClientSocket {
	public Object socketOwner = null;
	public int DiagMessageType = MainRegistry.DIAGMESSAGE_TYPE_STATUS;
	public String tempcmd = "";
	Socket sd = null;
	OutputStream out = null;
	InputStream in = null;
	DataInputStream input;
	DataOutputStream output;
	Shell callingsh = null;
	String currentRequest = null;
	ClientSocket runsoc = null;
	public String ErrorMsg = null;
	String[] toks = null;
	Thread prc = null;
	boolean iscon = false;
	boolean bUsingSpecialDelimiter = false;

	public ClientSocket() {
		runsoc = this;
		iscon = false;
		ErrorMsg = null;
	}

	public boolean Connect() {
		return Connect(MainRegistry.HostJSPort);
	}

	public boolean Connect(int port) {
		ErrorMsg = null;
		try {
			sd = new Socket(MainRegistry.HostAddr, port);
			sd.setTcpNoDelay(true);
			out = sd.getOutputStream();
			output = new DataOutputStream(out);
			in = sd.getInputStream();
			input = new DataInputStream(in);

			iscon = true;
			ErrorMsg = null;
			prc = new Thread() {
				public void run() {
					socrun();
					if (sd != null) {
						try {
							sd.close();
							sd = null;
						} catch (IOException e) {

						}
					}
					MainRegistry.WaitDlg = false;
				}
			};
			prc.start();
		} catch (UnknownHostException e) {
			ErrorMsg = CubridException
					.getErrorMessage(CubridException.ER_UNKNOWNHOST);
			return false;
		} catch (IOException e) {
			ErrorMsg = CubridException
					.getErrorMessage(CubridException.ER_CONNECT);
			return false;
		}
		return true;
	}

	/**
	 * Send(Shell, String, String, boolean)
	 * 
	 * @param sh
	 *            current shell
	 * @param msg
	 *            message
	 * @param taskType
	 *            task type
	 * @return 
	 *            return true at no error
	 */
	public boolean Send(Shell sh, String msg, String taskType) {
		try {
			callingsh = sh;
			String message = "task:" + taskType + "\n" + "token:"
					+ MainRegistry.HostToken + "\n" + msg + "\n\n";
			CommonTool.debugPrint(iscon + " " + message + "<<<"); // @@@
			currentRequest = taskType;
			if (message.endsWith("\n\n\n")) {
				message = message.substring(0, message.length() - 1);
			}
			output.write(message.getBytes());
			out.flush();
			output.flush();
			// sd.shutdownOutput();
		} catch (IOException e) {
			ErrorMsg = CubridException
					.getErrorMessage(CubridException.ER_CONNECT);
			iscon = false;
			return false;
		}
		return true;
	}

	public void socrun() {
		String buf = null;
		byte tmp[] = new byte[1024];
		StringBuffer strbuf = null;
		int srchIdx = 0;

		int len;
		while (iscon) {
			try {
				strbuf = new StringBuffer();
				while((len = in.read(tmp,0,1024))!= -1) {
					strbuf.append(new String(tmp, 0, len));
				}
			} catch (IOException e) {
				ErrorMsg = Messages.getString("ERROR.NETWORKFAIL");
				CommonTool.debugPrint(e.getMessage());
				len = -1;
			}
			if (strbuf.length() > 0) {
				if ((bUsingSpecialDelimiter && strbuf.indexOf(
						"\nEND__DIAGDATA\n", srchIdx) >= 0)
						|| (!bUsingSpecialDelimiter && strbuf.indexOf("\n\n",
								srchIdx) >= 0)) {
					buf = strbuf.toString();
					if (buf.length() <= 16) {
						ErrorMsg = Messages.getString("ERROR.NETWORKFAIL");
						break;
					}
					int idx;
					if ((idx = buf.indexOf("open:special")) >= 0) {
						String spmsg = buf.substring(idx + 13);
						spmsg = spmsg.substring(0, spmsg.length() - 15);
						CommonTool.WarnBox(callingsh, spmsg);
						buf = buf.substring(0, idx) + "\n";
					}

					String[] lines = buf.split("\n");
					toks = new String[lines.length * 2];
					for (int l1 = 0, la = 0; l1 < lines.length; l1++) {
						int l2;
						if (bUsingSpecialDelimiter)
							l2 = lines[l1].indexOf(":DIAG_DEL:");
						else
							l2 = lines[l1].indexOf(":");
						if (l2 >= 0) {
							toks[la * 2] = lines[l1].substring(0, l2);
							if (bUsingSpecialDelimiter)
								toks[la * 2 + 1] = lines[l1].substring(l2 + 10);
							else
								toks[la * 2 + 1] = lines[l1].substring(l2 + 1);
							la++;
						} else { // failure's note message and others
							toks[(la - 1) * 2 + 1] = toks[(la - 1) * 2 + 1]
									.concat("\n" + lines[l1]);
						}
					}

					if (!toks[0].equals("task") || // Check Message Format
							!toks[2].equals("status")
							|| !toks[4].equals("note")) {
						ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
						break;
					}

					if (!toks[3].equals("success")) { // fail
						if (!toks[1].equals("getadminloginfo")) { // fail=>0 item
							if (toks[1].equals("checkaccessright")) {
								// MainRegistry.IsConnected=false; // All network fail 
							}
							ErrorMsg = new String(toks[5]);
							break;
						}
					}

					if (toks[1].equals("access_list_addip")
							|| toks[1].equals("access_list_deleteip")
							|| toks[1].equals("addbackupinfo")
							|| toks[1].equals("addbroker")
							|| toks[1].equals("addtrigger")
							|| toks[1].equals("addvoldb")
							|| toks[1].equals("altertrigger")
							|| toks[1].equals("backupdb")
							|| toks[1].equals("broker_add")
							|| toks[1].equals("broker_drop")
							|| toks[1].equals("broker_job_first")
							|| toks[1].equals("broker_restart")
							|| toks[1].equals("broker_resume")
							|| toks[1].equals("broker_start")
							|| toks[1].equals("broker_stop")
							|| toks[1].equals("broker_suspend")
							|| toks[1].equals("checkdb")
							|| toks[1].equals("compactdb")
							|| toks[1].equals("copydb")
							|| toks[1].equals("createdb")
							|| toks[1].equals("createuser")
							|| toks[1].equals("deleteaccesslog")
							|| toks[1].equals("deletebackupinfo")
							|| toks[1].equals("deletebroker")
							|| toks[1].equals("deletedb")
							|| toks[1].equals("deletedbmtuser")
							|| toks[1].equals("deleteerrorlog")
							|| toks[1].equals("deleteuser")
							|| toks[1].equals("droptrigger")
							|| toks[1].equals("optimizedb")
							|| toks[1].equals("registerlocaldb")
							|| toks[1].equals("removelocaldb")
							|| toks[1].equals("renamedb")
							|| toks[1].equals("resetlog")
							|| toks[1].equals("removelog")
							|| toks[1].equals("restoredb")
							|| toks[1].equals("setautoaddvol")
							|| toks[1].equals("setautoexecquery")
							|| toks[1].equals("setbackupinfo")
							|| toks[1].equals("setbrokerenvinfo")
							|| toks[1].equals("setbrokeronconf")
							|| toks[1].equals("startdb")
							|| toks[1].equals("startbroker")
							|| toks[1].equals("stopdb")
							|| toks[1].equals("stopbroker")
							|| toks[1].equals("updateuser")
							|| toks[1].equals("validatequeryspec")
							|| toks[1].equals("validatevclass")
							|| toks[1].equals("addactivitylog")
							|| toks[1].equals("addstatustemplate")
							|| toks[1].equals("addactivitytemplate")
							|| toks[1].equals("removestatustemplate")
							|| toks[1].equals("removeactivitytemplate")
							|| toks[1].equals("updatestatustemplate")
							|| toks[1].equals("updateactivitytemplate")
							|| toks[1].equals("removecasrunnertmpfile")
							|| toks[1].equals("broker_setparam"))
						TaskGeneralJob();

					else if (toks[1].equals("access_list_info"))
						TaskAccess_List_Info();

					else if (toks[1].equals("addattribute")
							|| toks[1].equals("addconstraint")
							|| toks[1].equals("addmethod")
							|| toks[1].equals("addmethodfile")
							|| toks[1].equals("addqueryspec")
							|| toks[1].equals("addresolution")
							|| toks[1].equals("addsuper")
							|| toks[1].equals("changequeryspec")
							|| toks[1].equals("dropattribute")
							|| toks[1].equals("dropconstraint")
							|| toks[1].equals("dropmethod")
							|| toks[1].equals("dropmethodfile")
							|| toks[1].equals("dropqueryspec")
							|| toks[1].equals("dropresolution")
							|| toks[1].equals("dropsuper")
							|| toks[1].equals("updateattribute")
							|| toks[1].equals("updatemethod")
							|| toks[1].equals("changeowner"))
						TaskClassUpdate();

					else if (toks[1].equals("getdbmtuserinfo"))
						TaskGetDBMTUserInfo();
					else if (toks[1].equals("dbmtuserlogin"))
						TaskUserLoginJob();

					else if (toks[1].equals("adddbmtuser")
							|| toks[1].equals("setdbmtpasswd")
							|| toks[1].equals("updatedbmtuser"))
						TaskGeneralJob();

					else if (toks[1].equals("backupdbinfo"))
						TaskBackupDBInfo();
					else if (toks[1].equals("backupvolinfo"))
						TaskBackupVolInfo();
					else if (toks[1].equals("broker_getmonitorconf"))
						;
					else if (toks[1].equals("broker_getstatuslog"))
						;
					else if (toks[1].equals("broker_job_info"))
						TaskBrokerJobInfo();
					else if (toks[1].equals("broker_setmonitorconf"))
						;
					else if (toks[1].equals("checkaccessright"))
						TaskCheckAccessRight();
					else if (toks[1].equals("checkauthority"))
						TaskCheckAuthority();
					else if (toks[1].equals("checkdir"))
						TaskCheckDir();
					else if (toks[1].equals("checkfile"))
						TaskCheckFile();
					else if (toks[1].equals("class"))
						TaskClass();
					else if (toks[1].equals("classinfo"))
						TaskClassInfo();

					else if (toks[1].equals("createclass")
							|| toks[1].equals("createvclass")
							|| toks[1].equals("dropclass")
							|| toks[1].equals("renameclass"))
						TaskClassListUpdate();

					else if (toks[1].equals("dbspaceinfo"))
						TaskDbspaceInfo();
					else if (toks[1].equals("generaldbinfo"))
						TaskGeneralDBInfo();
					else if (toks[1].equals("getaddbrokerinfo"))
						TaskGetAddBrokerInfo();
					else if (toks[1].equals("getaddvolstatus"))
						TaskGetAddvolStatus();
					else if (toks[1].equals("getadminloginfo"))
						TaskGetAdminLogInfo();
					else if (toks[1].equals("getallsysparam"))
						TaskGetAllSysParam();
					else if (toks[1].equals("getaslimit"))
						TaskGetAsLimit();
					else if (toks[1].equals("getautoaddvol"))
						TaskGetAutoAddVol();
					else if (toks[1].equals("getautoaddvollog"))
						TaskGetAutoAddVolLog();
					else if (toks[1].equals("getautobackupdberrlog"))
						TaskGetAutoBackupDBErrLog();
					else if (toks[1].equals("getautoexecquery"))
						TaskGetAutoexecQuery();
					else if (toks[1].equals("getbackupinfo"))
						TaskGetBackupInfo();
					else if (toks[1].equals("getbackuplist"))
						TaskGetBackupList();
					else if (toks[1].equals("getbrokerenvinfo"))
						TaskGetBrokerEnvInfo();
					else if (toks[1].equals("getbrokerinfo"))
						TaskGetBrokerInfo();
					else if (toks[1].equals("getbrokeronconf"))
						TaskGetBrokerOnConf();
					else if (toks[1].equals("getbrokerstatus"))
						TaskGetBrokerStatus();
					else if (toks[1].equals("getdberror"))
						TaskGetDBError();
					else if (toks[1].equals("getdbsize"))
						TaskGetDBSize();
					else if (toks[1].equals("getenv"))
						TaskGetEnv();
					else if (toks[1].equals("getfile"))
						TaskGetFile();
					else if (toks[1].equals("gethistory"))
						;
					else if (toks[1].equals("gethistorylist"))
						;
					else if (toks[1].equals("getinitbrokersinfo"))
						TaskGetBrokersInfo();
					else if (toks[1].equals("getldbclass"))
						TaskGetLdbClass();
					else if (toks[1].equals("getldbclassatt"))
						TaskGetLdbClassAtt();
					else if (toks[1].equals("getlocaldbinfo"))
						TaskGetLocaldbInfo();
					else if (toks[1].equals("getlogfileinfo"))
						TaskGetLogFileInfo();
					else if (toks[1].equals("getloginfo"))
						TaskGetLogInfo();
					else if (toks[1].equals("getsuperclassesinfo"))
						TaskGetSuperClassesInfo();
					else if (toks[1].equals("gettransactioninfo"))
						TaskGetTransactionInfo();
					else if (toks[1].equals("gettriggerinfo"))
						TaskGetTriggerInfo();
					else if (toks[1].equals("getbrokersinfo"))
						TaskGetBrokersInfo();
					else if (toks[1].equals("killtransaction"))
						TaskKillTransaction();
					else if (toks[1].equals("kill_process"))
						;
					else if (toks[1].equals("loadaccesslog"))
						TaskLoadAccessLog();
					else if (toks[1].equals("loaddb"))
						TaskLoadDB();
					else if (toks[1].equals("lockdb"))
						TaskLockDB();
					else if (toks[1].equals("renamebroker"))
						;
					else if (toks[1].equals("sethistory"))
						;
					else if (toks[1].equals("setsysparam"))
						TaskGetAllSysParam();
					else if (toks[1].equals("startinfo"))
						TaskStartInfo();
					else if (toks[1].equals("unloaddb"))
						TaskUnloadDB();
					else if (toks[1].equals("unloadinfo"))
						TaskUnloadInfo();
					else if (toks[1].equals("userinfo"))
						TaskUserInfo();
					else if (toks[1].equals("viewhistorylog"))
						;
					else if (toks[1].equals("viewlog"))
						TaskViewLog();
					else if (toks[1].equals("viewlog2"))
						TaskViewLog2();
					else if (toks[1].equals("getdiagdata"))
						TaskGetDiagData();
					else if (toks[1].equals("getstatustemplate"))
						TaskGetStatusTemplate();
					else if (toks[1].equals("getactivitytemplate"))
						TaskGetActivityTemplate();
					else if (toks[1].equals("analyzecaslog"))
						TaskAnalyzeCasLog();
					else if (toks[1].equals("executecasrunner"))
						TaskExecuteCasRunner();
					else if (toks[1].equals("getcaslogtopresult"))
						TaskGetCasLogTopResult();
					else {
						ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					}
					break;
				}
			} else {
				try {
					Thread.sleep(100);
				} catch (Exception e) {
				}
			}
			srchIdx = strbuf.length();
			if (len > 15) {
				// 15 : length of end marker
				srchIdx -= 15;
			}
		} // end while
		iscon = false;
		if (sd != null) {
			try {
				sd.shutdownInput();
				sd.close();
				sd = null;
			} catch (IOException e) {
			}
		}
	}

	public boolean SendClientMessage(Shell sh, String msg, String taskType) {
		return SendClientMessage(sh, msg, taskType, true);
	}

	public boolean SendClientMessage(Shell sh, String msg, String taskType,
			boolean outclose) {
		return SendClientMessage(sh, msg, taskType, outclose,
				MainRegistry.HostJSPort);
	}

	public boolean SendMessageUsingSpecialDelimiter(Shell sh, String msg,
			String taskType) {
		bUsingSpecialDelimiter = true;
		return SendClientMessage(sh, msg, taskType, true);
	}

	public boolean SendMessageBackGroundUsingSpecialDelimiter(Shell sh,
			String msg, String taskType, String waitmsg) {
		bUsingSpecialDelimiter = true;
		return SendBackGround(sh, msg, taskType, waitmsg);
	}

	public boolean SendClientMessage(Shell sh, String msg, String taskType,
			boolean outclose, int port) {
		if (!MainRegistry.IsConnected) {
			ErrorMsg = Messages.getString("ERROR.DISCONNECTED");
			return false;
		}
		try {
			sd = new Socket(MainRegistry.HostAddr, port);
			sd.setTcpNoDelay(true);
			callingsh = sh;
			out = sd.getOutputStream();
			output = new DataOutputStream(out);
			// out.flush();
			// output.flush();

			in = sd.getInputStream();
			input = new DataInputStream(in);

			try {
				String message = new String();

				if (msg.endsWith("\n")) {
					message = "task:" + taskType + "\n" + "token:"
							+ MainRegistry.HostToken + "\n" + msg + "\n";
				} else {
					message = "task:" + taskType + "\n" + "token:"
							+ MainRegistry.HostToken + "\n" + msg + "\n\n";
				}
				CommonTool.debugPrint(MainRegistry.IsConnected + " " + message
						+ "<<<"); // @@@

				currentRequest = taskType;
				if (message.endsWith("\n\n\n")) {
					message = message.substring(0, message.length() - 1);
				}
				output.write(message.getBytes());
				output.flush();
				// if (outclose) sd.shutdownOutput();
			} catch (IOException e) {
				ErrorMsg = CubridException
						.getErrorMessage(CubridException.ER_CONNECT)
						+ " " + e.getMessage();
				return false;
			}
			iscon = true;
			ErrorMsg = null;
			socrun();
			if (ErrorMsg != null)
				return false;
		} catch (UnknownHostException e) {
			ErrorMsg = CubridException
					.getErrorMessage(CubridException.ER_UNKNOWNHOST);
			return false;
		} catch (IOException e) {
			ErrorMsg = CubridException
					.getErrorMessage(CubridException.ER_CONNECT);
			return false;
		}
		return true;
	}

	public boolean SendBackGround(Shell sh, String requestMsg, String cmds,
			String waitmsg) {
		if (runsoc.Connect()) {
			if (runsoc.Send(sh, requestMsg, cmds)) {
				WaitingMsgBox dlg = new WaitingMsgBox(sh);
				dlg.run(waitmsg);
				if (runsoc.ErrorMsg != null) {
					return false;
				}
			} else {
				return false;
			}
		} else {
			return false;
		}
		return true;
	}

	String GetValueFor(String[] valary, String var) {
		int i1, n;
		for (i1 = 0, n = valary.length; i1 < n; i1 += 2) {
			if (valary[i1].equals(var))
				break;
		}
		if (i1 < n) {
			return valary[i1 + 1];
		}
		return "";
	}

	void TaskStartInfo() {
		int dblistStart = 0, dblistEnd = 0;
		int activelistStart = 0, activelistEnd = 0;

		for (int index = 6; index < toks.length; index += 2) {
			if (toks[index].equals("open") && toks[index + 1].equals("dblist")) {
				dblistStart = index;
				continue;
			}

			if (toks[index].equals("close") && toks[index + 1].equals("dblist")) {
				dblistEnd = index;
				continue;
			}

			if (toks[index].equals("open")
					&& toks[index + 1].equals("activelist")) {
				activelistStart = index;
				continue;
			}

			if (toks[index].equals("close")
					&& toks[index + 1].equals("activelist")) {
				activelistEnd = index;
				break;
			}
		}

		for (int i = 0; i < MainRegistry.Authinfo.size(); i++) {
			AuthItem ai = (AuthItem) MainRegistry.Authinfo.get(i);
			ai.status = MainConstants.STATUS_STOP;
			ai.dbdir = null;
		}

		for (int i = dblistStart + 2; i < dblistEnd; i += 4) {
			if (toks[i].equals("dbname")) {
				AuthItem ai = MainRegistry.Authinfo_find(toks[i + 1]);
				if (ai == null)
					MainRegistry.Authinfo_add(toks[i + 1], "", toks[i + 3],
							MainConstants.STATUS_STOP);
				else {
					ai.dbdir = new String(toks[i + 3]);
					MainRegistry.Authinfo_update(ai);
				}
			}
		}

		for (int i = activelistStart + 1; i < activelistEnd; i++) {
			if (toks[i].equals("dbname")) {
				AuthItem ai = MainRegistry.Authinfo_find(toks[i + 1]);
				if (ai != null) {
					ai.status = MainConstants.STATUS_START;
					MainRegistry.Authinfo_update(ai);
				}
			}
		}
		for (int i = 0; i < MainRegistry.Authinfo.size();) {
			AuthItem ai = (AuthItem) MainRegistry.Authinfo.get(i);
			if (ai.dbdir == null || ai.dbdir.length() <= 0) {
				MainRegistry.Authinfo.remove(i); // no dbdir ==> deleted DB
			} else
				i++;
		}
	}

	void TaskGetAddvolStatus() {
		MainRegistry.TmpVolsize = toks[3 * 2 + 1];
		MainRegistry.TmpVolpath = toks[4 * 2 + 1];
	}

	void TaskGetAutoAddVol() {
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("data"))
				AUTOADDVOLDialog.data_chk = (toks[i + 1].equals("ON")) ? true
						: false;
			else if (toks[i].equals("index"))
				AUTOADDVOLDialog.idx_chk = (toks[i + 1].equals("ON")) ? true
						: false;
			else if (toks[i].equals("data_ext_page"))
				AUTOADDVOLDialog.data_ext = toks[i + 1];
			else if (toks[i].equals("index_ext_page"))
				AUTOADDVOLDialog.idx_ext = toks[i + 1];
			else if (toks[i].equals("data_warn_outofspace"))
				AUTOADDVOLDialog.data_warn = toks[i + 1];
			else if (toks[i].equals("index_warn_outofspace"))
				AUTOADDVOLDialog.idx_warn = toks[i + 1];
		}
	}

	void TaskDbspaceInfo() {
		if (!toks[toks.length - 2].equals("freespace")) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		AuthItem ai = MainRegistry.Authinfo_ready(toks[7]);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}

		ai.pagesize = CommonTool.atoi(toks[9]);
		ai.freespace = CommonTool.atof(toks[toks.length - 1]);
		ai.Volinfo.clear();
		VolumeInfo vi;
		for (int i = 10, j = 0, n = toks.length; i < n;) {
			if (toks[i].equals("open") && toks[i + 1].equals("spaceinfo")) {
				if (CommonTool.atoi(toks[i + (4 * 2) + 1]) > 0) {
					vi = new VolumeInfo(toks[i + (1 * 2) + 1], toks[i + (2 * 2)
							+ 1], toks[i + (3 * 2) + 1], toks[i + (4 * 2) + 1],
							toks[i + (5 * 2) + 1], toks[i + (6 * 2) + 1], j);
					ai.Volinfo.add(vi);
					j++;
				}
				i += (7 * 2);
			} else
				i += 2;
		}

		// Sorting
		Collections.sort(ai.Volinfo);
		MainRegistry.Authinfo_commit(ai);
	}

	void TaskUserInfo() {
		AuthItem ai = MainRegistry.Authinfo_ready(toks[7]);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}

		ai.UserInfo.clear();
		UserInfo ui;
		for (int i = 8, n = toks.length; i < n;) {
			if (toks[i].equals("open") && toks[i + 1].equals("user")) {
				if (toks[i + (3 * 2)].equals("password")) {
					ui = new UserInfo(toks[i + (1 * 2) + 1].toLowerCase(),
							toks[i + (2 * 2) + 1], toks[i + (3 * 2) + 1], false);
					i += (4 * 2);
				} else {
					ui = new UserInfo(toks[i + (1 * 2) + 1].toLowerCase(),
							toks[i + (2 * 2) + 1], toks[i + (3 * 2) + 1], false);
					i += (3 * 2);
				}
				boolean flag_auth = false, flag_grp = false;
				for (; i < n;) {
					if (toks[i].equals("close") && toks[i + 1].equals("user")) {
						i += 2;
						break;
					} else if (toks[i].equals("open")
							&& toks[i + 1].equals("authorization"))
						flag_auth = true;
					else if (toks[i].equals("close")
							&& toks[i + 1].equals("authorization"))
						flag_auth = false;
					else if (toks[i].equals("open")
							&& toks[i + 1].equals("groups"))
						flag_grp = true;
					else if (toks[i].equals("close")
							&& toks[i + 1].equals("groups"))
						flag_grp = false;
					else if (flag_auth) {
						Authorizations auth = new Authorizations(toks[i]
								.toLowerCase(), false);
						int authmap = CommonTool.atoi(toks[i + 1]);
						for (int m1 = 0; m1 < 16; m1++) {
							if ((authmap & (1 << m1)) != 0) {
								switch (m1) {
								case 0:
									auth.selectPriv = true;
									break;
								case 1:
									auth.insertPriv = true;
									break;
								case 2:
									auth.updatePriv = true;
									break;
								case 3:
									auth.deletePriv = true;
									break;
								case 4:
									auth.alterPriv = true;
									break;
								case 5:
									auth.indexPriv = true;
									break;
								case 6:
									auth.executePriv = true;
									break;
								case 8:
									auth.grantSelectPriv = true;
									break;
								case 9:
									auth.grantInsertPriv = true;
									break;
								case 10:
									auth.grantUpdatePriv = true;
									break;
								case 11:
									auth.grantDeletePriv = true;
									break;
								case 12:
									auth.grantAlterPriv = true;
									break;
								case 13:
									auth.grantIndexPriv = true;
									break;
								case 14:
									auth.grantExecutePriv = true;
									break;
								}
							}
						}
						ui.authorizations.add(auth);
					} else if (flag_grp) {
						ui.groupNames.add(toks[i + 1].toLowerCase());
					}
					i += 2;
				}
				ai.UserInfo.add(ui);
			} else
				i += 2;
		}

		Collections.sort(ai.UserInfo);

		// groups, members adjust
		UserInfo ui2, uitmp;
		for (int i = 0, n = ai.UserInfo.size(); i < n; i++) { // groups adjust
			ui = (UserInfo) ai.UserInfo.get(i);
			if (ui.groupNames.size() <= 0)
				continue;
			for (int i2 = 0, n2 = ui.groupNames.size(); i2 < n2; i2++) {
				if ((uitmp = UserInfo.UserInfo_find(ai.UserInfo,
						(String) ui.groupNames.get(i2))) != null) {
					ui.groups.add(uitmp);
				}
			}
			ai.UserInfo.set(i, ui);
		}
		for (int i = 0, n = ai.UserInfo.size(); i < n; i++) { // members adjust
			ui = (UserInfo) ai.UserInfo.get(i);

			for (int i2 = 0, n2 = ai.UserInfo.size(); i2 < n2; i2++) {
				if (i == i2)
					continue;
				ui2 = (UserInfo) ai.UserInfo.get(i2);
				for (int i3 = 0, n3 = ui2.groups.size(); i3 < n3; i3++) {
					uitmp = (UserInfo) ui2.groups.get(i3);
					if (uitmp.userName.equals(ui.userName)) {
						ui.members.add(ui2);
					}
				}
			}
			ai.UserInfo.set(i, ui);
		}
		MainRegistry.Authinfo_commit(ai);
	}

	void TaskClass() {
		if (!toks[6].equals("open") || !toks[7].equals("classinfo")) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}

		String dbname = null;
		String classname = null;
		String classtype = null;
		String schemaowner = null;
		String virtual = null;

		ArrayList superClasses = new ArrayList();
		ArrayList subClasses = new ArrayList();
		ArrayList methodFiles = new ArrayList();
		ArrayList querySpecs = new ArrayList();
		ArrayList classAttributes = new ArrayList();
		ArrayList attributes = new ArrayList();
		ArrayList classMethods = new ArrayList();
		ArrayList methods = new ArrayList();
		ArrayList classResolutions = new ArrayList();
		ArrayList resolutions = new ArrayList();
		ArrayList constraints = new ArrayList();

		int i, j, n = toks.length;
		for (i = 8; i < n
				&& (!toks[i].equals("close") || !toks[i + 1]
						.equals("classinfo"));) {
			if (toks[i].equals("dbname"))
				dbname = toks[i + 1];
			else if (toks[i].equals("classname"))
				classname = toks[i + 1];
			else if (toks[i].equals("type"))
				classtype = toks[i + 1];
			else if (toks[i].equals("owner"))
				schemaowner = toks[i + 1];
			else if (toks[i].equals("superclass"))
				superClasses.add(toks[i + 1]);
			else if (toks[i].equals("subclass"))
				subClasses.add(toks[i + 1]);
			else if (toks[i].equals("virtual"))
				virtual = toks[i + 1];
			else if (toks[i].equals("methodfile"))
				methodFiles.add(toks[i + 1]);
			else if (toks[i].equals("queryspec"))
				querySpecs.add(toks[i + 1]);
			else if (toks[i].equals("open")) {
				if (toks[i + 1].equals("classattribute")
						|| toks[i + 1].equals("attribute")) {
					String name = "";
					String type = "";
					String inherit = "";
					boolean isIndexed = false;
					boolean isNotNull = false;
					boolean isShared = false;
					boolean isUnique = false;
					String defaultval = "";
					for (j = i + 2; j < n
							&& (!toks[j].equals("close") || (!toks[j + 1]
									.equals("classattribute") && !toks[j + 1]
									.equals("attribute"))); j += 2) {
						if (toks[j].equals("name"))
							name = toks[j + 1];
						else if (toks[j].equals("type"))
							type = toks[j + 1];
						else if (toks[j].equals("inherit"))
							inherit = toks[j + 1];
						else if (toks[j].equals("indexed"))
							isIndexed = toks[j + 1].equals("y") ? true : false;
						else if (toks[j].equals("unique"))
							isUnique = toks[j + 1].equals("y") ? true : false;
						else if (toks[j].equals("notnull"))
							isNotNull = toks[j + 1].equals("y") ? true : false;
						else if (toks[j].equals("shared"))
							isShared = toks[j + 1].equals("y") ? true : false;
						else if (toks[j].equals("default"))
							defaultval = toks[j + 1];
					}
					DBAttribute da = new DBAttribute(name, type, inherit,
							isIndexed, isNotNull, isShared, isUnique,
							defaultval);
					if (toks[i + 1].equals("classattribute"))
						classAttributes.add(da);
					else
						attributes.add(da);
					i = j;
				} else if (toks[i + 1].equals("classmethod")
						|| toks[i + 1].equals("method")) {
					String name = "";
					String inherit = "";
					ArrayList arguments = new ArrayList();
					String function = "";
					for (j = i + 2; j < n
							&& (!toks[j].equals("close") || (!toks[j + 1]
									.equals("classmethod") && !toks[j + 1]
									.equals("method"))); j += 2) {
						if (toks[j].equals("name"))
							name = toks[j + 1];
						else if (toks[j].equals("inherit"))
							inherit = toks[j + 1];
						else if (toks[j].equals("argument"))
							arguments.add(toks[j + 1]);
						else if (toks[j].equals("function"))
							function = toks[j + 1];
					}
					DBMethod dm = new DBMethod(name, inherit, function);
					dm.arguments = new ArrayList(arguments);
					if (toks[i + 1].equals("classmethod"))
						classMethods.add(dm);
					else
						methods.add(dm);
					i = j;
				} else if (toks[i + 1].equals("classresolution")
						|| toks[i + 1].equals("resolution")) {
					String name = "";
					String className = "";
					String alias = "";
					for (j = i + 2; j < n
							&& (!toks[j].equals("close") || (!toks[j + 1]
									.equals("classresolution") && !toks[j + 1]
									.equals("resolution"))); j += 2) {
						if (toks[j].equals("name"))
							name = toks[j + 1];
						else if (toks[j].equals("classname"))
							className = toks[j + 1];
						else if (toks[j].equals("alias"))
							alias = toks[j + 1];
					}
					DBResolution dr = new DBResolution(name, className, alias);
					if (toks[i + 1].equals("classresolution"))
						classResolutions.add(dr);
					else
						resolutions.add(dr);
					i = j;
				}
				if (toks[i + 1].equals("constraint")) {
					String name = "";
					String type = "";
					ArrayList classAtt = new ArrayList();
					ArrayList att = new ArrayList();
					ArrayList rule = new ArrayList();
					for (j = i + 2; j < n
							&& (!toks[j].equals("close") || !toks[j + 1]
									.equals("constraint")); j += 2) {
						if (toks[j].equals("name"))
							name = toks[j + 1];
						else if (toks[j].equals("type"))
							type = toks[j + 1];
						else if (toks[j].equals("classattribute"))
							classAtt.add(toks[j + 1]);
						else if (toks[j].equals("attribute"))
							att.add(toks[j + 1]);
						else if (toks[j].equals("rule"))
							rule.add(toks[j + 1]);
					}
					Constraint cr = new Constraint(name, type);
					cr.classAttributes = new ArrayList(classAtt);
					cr.attributes = new ArrayList(att);
					cr.rule = new ArrayList(rule);
					constraints.add(cr);
					i = j;
				}
			}
			i += 2;
		}

		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		SchemaInfo si = null;
		for (i = 0, n = ai.Schema.size(); i < n; i++) {
			if (((SchemaInfo) ai.Schema.get(i)).name.equals(classname)) {
				si = (SchemaInfo) ai.Schema.get(i);
				break;
			}
		}
		if (si == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}

		si.type = new String(classtype);
		si.schemaowner = new String(schemaowner);
		si.virtual = new String(virtual);
		si.superClasses = new ArrayList(superClasses);
		si.subClasses = new ArrayList(subClasses);
		si.methodFiles = new ArrayList(methodFiles);
		si.querySpecs = new ArrayList(querySpecs);
		si.classAttributes = new ArrayList(classAttributes);
		si.attributes = new ArrayList(attributes);
		si.classMethods = new ArrayList(classMethods);
		si.methods = new ArrayList(methods);
		si.classResolutions = new ArrayList(classResolutions);
		si.resolutions = new ArrayList(resolutions);
		si.constraints = new ArrayList(constraints);
		ai.Schema.set(i, si);

		MainRegistry.Authinfo_update(ai);
	}

	void TaskGetAllSysParam() {
		MainRegistry.CubridConf = "";
		for (int i = 10, n = toks.length; i < n && 
					(!toks[i].equals("close") || 
					!toks[i + 1].equals("conflist")); i += 2) {
			if (toks[i].equals("confdata")) {
				MainRegistry.CubridConf = MainRegistry.CubridConf + toks[i+1];
			}
		}
	}

	void TaskClassListUpdate() {
	}

	void TaskClassUpdate() {
		TaskClass();
	}

	void TaskGetSuperClassesInfo() {
		if (!toks[6].equals("open") || !toks[7].equals("superclassesinfo")) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}

		String classname = null;
		ArrayList classAttributes = new ArrayList();
		ArrayList attributes = new ArrayList();
		ArrayList classMethods = new ArrayList();
		ArrayList methods = new ArrayList();
		boolean classdata = false;
		MainRegistry.Tmpchkrst.clear();

		for (int i = 8, n = toks.length; i < n
				&& (!toks[i].equals("close") || !toks[i + 1]
						.equals("superclassesinfo")); i += 2) {
			if (toks[i].equals("open") && toks[i + 1].equals("class")) {
				classdata = true;
				classAttributes.clear();
				attributes.clear();
				classMethods.clear();
				methods.clear();
			} else if (toks[i].equals("close") && toks[i + 1].equals("class")) {
				classdata = false;
				SuperClass sc = new SuperClass(classname, classAttributes,
						attributes, classMethods, methods);
				MainRegistry.Tmpchkrst.add(sc);
			} else if (classdata) {
				if (toks[i].equals("name"))
					classname = toks[i + 1];
				else if (toks[i].equals("classmethod"))
					classMethods.add(toks[i + 1]);
				else if (toks[i].equals("method"))
					methods.add(toks[i + 1]);
				else if (toks[i].equals("classattribute"))
					classAttributes.add(toks[i + 1]);
				else if (toks[i].equals("attribute"))
					attributes.add(toks[i + 1]);
			}
		}
	}

	void TaskGetLogInfo() {
		AuthItem ai = MainRegistry.Authinfo_ready(toks[7]);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		ai.LogInfo.clear();
		boolean flag_data = false;
		String filename = "";
		String fileowner = "";
		String size = "";
		String date = "";
		String path = "";
		LogFileInfo li = null;
		for (int i = 10, n = toks.length; i < n;) {
			if (toks[i].equals("open") && toks[i + 1].equals("log"))
				flag_data = true;
			else if (toks[i].equals("close") && toks[i + 1].equals("log")) {
				flag_data = false;
				int lastidx;
				if ((lastidx = path.lastIndexOf("/")) >= 0) {
					filename = path.substring(lastidx + 1);
				}
				li = new LogFileInfo(filename, fileowner, size, date, path);
				ai.LogInfo.add(li);
			} else if (flag_data) {
				if (toks[i].equals("filename"))
					filename = toks[i + 1];
				else if (toks[i].equals("owner"))
					fileowner = toks[i + 1];
				else if (toks[i].equals("size"))
					size = toks[i + 1];
				else if (toks[i].equals("lastupdate"))
					date = toks[i + 1];
				else if (toks[i].equals("path"))
					path = toks[i + 1];
			}
			i += 2;
		}

		// Sorting
		Collections.sort(ai.LogInfo);
		MainRegistry.Authinfo_commit(ai);
	}

	void TaskViewLog() {
		MainRegistry.Tmpchkrst.clear();

		int i = 10;
		for (int n = toks.length; i < n; i += 2) {
			if (toks[i].equals("close") && toks[i + 1].equals("log"))
				break;
			if (toks[i].equals("line")) {
				MainRegistry.Tmpchkrst.add(toks[i + 1]);
			}
		}
		i += 2;
		LogViewDialog.line_start = CommonTool.atol(toks[i + 1]);
		DiagCasRunnerResultViewDialog.line_start = CommonTool.atol(toks[i + 1]);
		i += 2;
		LogViewDialog.line_end = CommonTool.atol(toks[i + 1]);
		DiagCasRunnerResultViewDialog.line_end = CommonTool.atol(toks[i + 1]);
		i += 2;
		LogViewDialog.line_tot = CommonTool.atol(toks[i + 1]);
		DiagCasRunnerResultViewDialog.line_tot = CommonTool.atol(toks[i + 1]);
	}

	void TaskGetDBMTUserInfo() {
		if (!toks[6].equals("open") || !toks[7].equals("dblist")) { // format error
			MainRegistry.IsConnected = false;
			return;
		}

		ArrayList tmpAuthInfo = new ArrayList();

		for (int i = 0; i < MainRegistry.Authinfo.size(); i++) {
			AuthItem ai = (AuthItem) MainRegistry.Authinfo.get(i);
			if (ai.setinfo || ai.dbuser.length() > 0)
				tmpAuthInfo.add(new AuthItem(ai.dbname, ai.dbuser, "",
						MainConstants.STATUS_STOP, ai.isDBAGroup));
		}

		MainRegistry.Authinfo.clear();
		MainRegistry.listDBUserInfo.clear();
		MainRegistry.listOtherCMUserInfo.clear();

		int startUserlistLine = 8;
		for (int i = startUserlistLine, n = toks.length; (!toks[i]
				.equals("open") || !toks[i + 1].equals("userlist"))
				&& i < n;) {
			i += 2;
			startUserlistLine += 2;
		}

		boolean isCurrentCMUser = false;
		for (int i = startUserlistLine, n = toks.length; (!toks[i]
				.equals("close") || !toks[i + 1].equals("userlist"))
				&& i < n;) {
			if (toks[i].equals("open") && toks[i + 1].equals("user")) {
				CubridManagerUserInfo cmUserInfo = null;
				while ((!toks[i].equals("close") || !toks[i + 1].equals("user"))
						&& i < toks.length) {
					String cmuser = new String();
					String cmpasswd = new String();
					if (toks[i].equals("id")) {
						cmuser = toks[i + 1];
						if (!MainRegistry.UserID.equals(cmuser))
							isCurrentCMUser = false;
						else
							isCurrentCMUser = true;

						if (!isCurrentCMUser)
							cmUserInfo = new CubridManagerUserInfo(cmuser);
					} else if (toks[i].equals("passwd")) {
						cmpasswd = toks[i + 1];
						if (isCurrentCMUser)
							MainRegistry.UserPassword = cmpasswd;
						else
							cmUserInfo.cmPassword = cmpasswd;
					} else if (toks[i].equals("open")
							&& toks[i + 1].equals("dbauth")) {
						i += 2;
						while ((!toks[i].equals("close") || !toks[i + 1]
								.equals("dbauth"))
								&& i < toks.length) {
							if (isCurrentCMUser) {
								MainRegistry.addDBUserInfo(toks[i + 1],
										toks[i + 3], toks[i + 5]);
								AuthItem ai = (AuthItem) MainRegistry
										.Authinfo_find(toks[i + 1]);
								if (ai == null)
									MainRegistry.Authinfo_add(toks[i + 1], "",
											MainConstants.STATUS_STOP);
								for (int j = 0; j < tmpAuthInfo.size(); j++) {
									if (toks[i + 1]
											.equals(((AuthItem) tmpAuthInfo
													.get(j)).dbname))
										MainRegistry
												.Authinfo_update((AuthItem) tmpAuthInfo
														.get(j));
								}
							} else {
								cmUserInfo.addDBUserInfo(toks[i + 1],
										toks[i + 3], toks[i + 5]);
							}
							i += 6;
						}
					} else if (toks[i].equals("casauth")) {
						if (isCurrentCMUser) {
							if (toks[i + 1].equals("admin"))
								MainRegistry.CASAuth = MainConstants.AUTH_DBA;
							else if (toks[i + 1].equals("monitor"))
								MainRegistry.CASAuth = MainConstants.AUTH_NONDBA;
							else
								MainRegistry.CASAuth = MainConstants.AUTH_NONE;
						} else {
							if (toks[i + 1].equals("admin"))
								cmUserInfo.CASAuth = MainConstants.AUTH_DBA;
							else if (toks[i + 1].equals("monitor"))
								cmUserInfo.CASAuth = MainConstants.AUTH_NONDBA;
							else
								cmUserInfo.CASAuth = MainConstants.AUTH_NONE;
						}

					} else if (toks[i].equals("dbcreate")) {
						if (isCurrentCMUser) {
							if (toks[i + 1].equals("admin"))
								MainRegistry.IsDBAAuth = true;
							else
								MainRegistry.IsDBAAuth = false;
						} else {
							if (toks[i + 1].equals("admin"))
								cmUserInfo.IsDBAAuth = true;
							else
								cmUserInfo.IsDBAAuth = false;
						}
					}
					i += 2;
				} // end of while()
				if (cmUserInfo != null)
					MainRegistry.listOtherCMUserInfo.add(cmUserInfo);
			} // end of if()
			i += 2;
		} // end of for()

		Collections.sort(MainRegistry.Authinfo);

		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(callingsh, "", "getenv"))
			ErrorMsg = cs.ErrorMsg;
	}

	void TaskUserLoginJob() {
		/*
		 * toks[0] = targetid toks[2] = dbname toks[4] = authority
		 */

		for (int i = 0; i < toks.length; i += 2) {
			if (toks[i].equals("targetid")
					&& toks[i + 1].equals(MainRegistry.UserID)) {
				i += 2;
				DBUserInfo ui = MainRegistry.getDBUserInfo(toks[i + 1]);
				if (toks[i].equals("dbname") && ui != null) {
					i += 2;
					if (MainRegistry.isProtegoBuild()) {
						if (toks[i].equals("dbuser"))
							ui.dbuser = toks[i + 1];
						i += 2;
					}
					if (toks[i].equals("authority")) {
						if (toks[i + 1].equals("isdba"))
							ui.isDBAGroup = true;
						else if (toks[i + 1].equals("isnotdba"))
							ui.isDBAGroup = false;
						else
							ErrorMsg = Messages
									.getString("ERROR.MESSAGEFORMAT");
					}
				}
			}
		}
	}

	void TaskGetBrokerInfo() {
		SetParameterAction.bpi.paraname.clear();
		SetParameterAction.bpi.paraval.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("bname"))
				SetParameterAction.bpi.broker_name = toks[i + 1];
			else if (toks[i].equals("MASTER_SHM_ID"))
				SetParameterAction.masterShmId = toks[i + 1];
			else if (toks[i].equals("APPL_SERVER"))
				SetParameterAction.bpi.server_type = toks[i + 1];
			else if (toks[i].equals("MIN_NUM_APPL_SERVER"))
				SetParameterAction.bpi.min_as_num = toks[i + 1];
			else if (toks[i].equals("MAX_NUM_APPL_SERVER"))
				SetParameterAction.bpi.max_as_num = toks[i + 1];
			else if (toks[i].equals("BROKER_PORT"))
				SetParameterAction.bpi.broker_port = toks[i + 1];
			else if (toks[i].equals("APPL_ROOT"))
				SetParameterAction.bpi.appl_root = toks[i + 1];
			else if (!toks[i].equals("open") && !toks[i].equals("close")
					&& !toks[i].equals("%")) {
				SetParameterAction.bpi.paraname.add(toks[i]);
				SetParameterAction.bpi.paraval.add(toks[i + 1]);
			}
		}

		if (!SetParameterAction.bpi.server_type.equals("CAS")) {
			if (SetParameterAction.bpi.appl_root.length() <= 0) {
				if (SetParameterAction.bpi.server_type.equals("VAS"))
					SetParameterAction.bpi.appl_root = "$BROKER/script/vas*";
				else if (SetParameterAction.bpi.server_type.equals("WAS"))
					SetParameterAction.bpi.appl_root = "$BROKER/script/was*";
				else if (SetParameterAction.bpi.server_type.equals("ULS"))
					SetParameterAction.bpi.appl_root = "$BROKER/script/uls*";
				else
					SetParameterAction.bpi.appl_root = "$BROKER/script/ams*";
			}
		}
	}

	void TaskBrokerGetMonitorConf() {
		/*
		 * CUSQLAdminApp* app = (CUSQLAdminApp *)AfxGetApp();
		 * if(app->m_waitDialog){ app->m_waitDialog->EndDialog(IDOK);
		 * app->m_waitDialog=NULL; } CBrokerMonitorSettingDlg* dlg =
		 * (CBrokerMonitorSettingDlg*)app->m_currentDialog;
		 * 
		 * if (m_numPair>5) { if (!strcmp(m_NVList[4].value,"ON")) dlg->m_monCPU =
		 * true; if (!strcmp(m_NVList[5].value,"ON")) dlg->m_monBT = true; if
		 * (!strcmp(m_NVList[6].value,"ON")) dlg->m_logCPU = true; if
		 * (!strcmp(m_NVList[7].value,"ON")) dlg->m_logBT = true; if
		 * (!strcmp(m_NVList[8].value,"ON")) dlg->m_asCPU = true; if
		 * (!strcmp(m_NVList[9].value,"ON")) dlg->m_asBT = true; //if
		 * (!strcmp(m_NVList[10].value,"ON")) dlg->m_alarmCPU = true; //if
		 * (!strcmp(m_NVList[11].value,"ON")) dlg->m_alarmBT = true;
		 * dlg->m_cpulimit = m_NVList[10].value; dlg->m_btlimit =
		 * m_NVList[11].value; }
		 * 
		 * dlg->UpdateData(false);
		 */}

	void TaskBrokerGetStatusLog() {
		/*
		 * CUSQLAdminApp* app = (CUSQLAdminApp*)AfxGetApp();
		 * if(app->m_waitDialog){ app->m_waitDialog->EndDialog(IDOK);
		 * app->m_waitDialog=NULL; }
		 *  // Success or failure if (strcmp(m_NVList[1].name, "status")) {
		 * AfxMessageBox("Messsage format error"); return; } else if
		 * (!strcmp(m_NVList[1].value, "failure")) {
		 * AfxMessageBox(m_NVList[2].value); return; }
		 * 
		 * CBrokerMonLog* dlg = (CBrokerMonLog*)app->m_currentDialog;
		 * 
		 * LV_ITEM lvitem; dlg->m_listCtrl.DeleteAllItems();
		 * 
		 * int bodari = (int)(m_numPair/6) ;
		 * 
		 * if (bodari == 0) return;
		 * 
		 * for (int i=3 ; i<m_numPair ; i++) { if (toks[i].equals( "open")) {
		 * CString tmp[7]; tmp[0] = m_NVList[i+1].value; tmp[1] =
		 * m_NVList[i+2].value; tmp[2] = m_NVList[i+3].value; tmp[3] =
		 * m_NVList[i+4].value; tmp[4] = m_NVList[i+5].value; tmp[5] =
		 * m_NVList[i+6].value; tmp[6] = m_NVList[i+7].value;
		 *  // LPTSTR lpname = new TCHAR[tmp[0].GetLength()+1]; //
		 * _tcscpy(lpname, tmp[0]);
		 * 
		 * lvitem.mask = LVIF_TEXT ; lvitem.iSubItem = 0; lvitem.pszText =
		 * (char*)(LPCTSTR)tmp[0];//lpname; lvitem.iItem =
		 * dlg->m_listCtrl.GetItemCount();
		 * 
		 * dlg->m_listCtrl.InsertItem(&lvitem);
		 * 
		 * for (int j =1; j<6;j++) { // LPTSTR lpval = new
		 * TCHAR[tmp[j].GetLength()+1]; // _tcscpy(lpval, tmp[j]); lvitem.mask =
		 * LVIF_TEXT; lvitem.iSubItem = j; lvitem.pszText =
		 * (char*)(LPCTSTR)tmp[j];//lpval; dlg->m_listCtrl.SetItem(&lvitem); } } }
		 * 
		 * dlg->UpdateData();
		 */}

	void TaskBrokerJobInfo() {
		MainRegistry.Tmpchkrst.clear();
		BrokerJobStatus bjs = null;
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open")) {
				bjs = new BrokerJobStatus(toks[i + (1 * 2) + 1], toks[i
						+ (2 * 2) + 1], toks[i + (3 * 2) + 1], toks[i + (4 * 2)
						+ 1], toks[i + (5 * 2) + 1]);
				MainRegistry.Tmpchkrst.add(bjs);
				i += (4 * 2);
			}
		}
	}

	void TaskGeneralDBInfo() {
		// dbspaceinfo
	}

	void TaskGetBackupInfo() {
		AuthItem ai = MainRegistry.Authinfo_ready(toks[7]);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		ai.JobInfo.clear();
		Jobs jobs;
		for (int i = 8, n = toks.length; i < n;) {
			if (toks[i].equals("backupid")) {
				jobs = new Jobs(toks[i + (0 * 2) + 1], toks[i + (1 * 2) + 1],
						toks[i + (2 * 2) + 1], toks[i + (3 * 2) + 1], toks[i
								+ (4 * 2) + 1], toks[i + (5 * 2) + 1], toks[i
								+ (6 * 2) + 1], toks[i + (7 * 2) + 1], toks[i
								+ (8 * 2) + 1], toks[i + (9 * 2) + 1], toks[i
								+ (10 * 2) + 1], toks[i + (11 * 2) + 1], toks[i
								+ (12 * 2) + 1]);
				ai.JobInfo.add(jobs);
				i += (13 * 2);
			} else
				i += 2;
		}

		Collections.sort(ai.JobInfo);
		MainRegistry.Authinfo_commit(ai);
	}

	void TaskGetDBError() {
		DBError dber = null;
		String dbname = null;
		MainRegistry.Tmpary.clear();

		Calendar calendar = Calendar.getInstance();
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMinimumIntegerDigits(2);
		calendar.add(Calendar.MINUTE, CommonTool
				.atoi(MainRegistry.MONPARA_INTERVAL)
				* -1);
		String fromtime = calendar.get(Calendar.YEAR)
				+ nf.format(calendar.get(Calendar.MONTH) + 1)
				+ nf.format(calendar.get(Calendar.DATE))
				+ nf.format(calendar.get(Calendar.HOUR_OF_DAY))
				+ nf.format(calendar.get(Calendar.MINUTE))
				+ nf.format(calendar.get(Calendar.SECOND));

		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("dbname")) {
				dbname = toks[i + 1];
			} else if (toks[i].equals("time")) {
				int imon = 1;
				String smon = null;
				// sscanf(str, "%*s %s %d %d:%d:%d %d", mon, &iday, &ihour,
				// &imin, &isec, &iyear);
				int idx = toks[i + 1].indexOf(" ");
				String[] timesp = (toks[i + 1].substring(idx + 1) + " 0 0 0 0 0 0")
						.split(" "); // first token skip
				smon = timesp[0].toUpperCase();
				if (smon.equals("JAN"))
					imon = 1;
				else if (smon.equals("FEB"))
					imon = 2;
				else if (smon.equals("MAR"))
					imon = 3;
				else if (smon.equals("APR"))
					imon = 4;
				else if (smon.equals("MAY"))
					imon = 5;
				else if (smon.equals("JUN"))
					imon = 6;
				else if (smon.equals("JUL"))
					imon = 7;
				else if (smon.equals("AUG"))
					imon = 8;
				else if (smon.equals("SEP"))
					imon = 9;
				else if (smon.equals("OCT"))
					imon = 10;
				else if (smon.equals("NOV"))
					imon = 11;
				else if (smon.equals("DEC"))
					imon = 12;
				String[] timesp2 = (timesp[2] + ":0:0:0").split(":");
				String curtime = timesp[3] + nf.format(imon)
						+ nf.format(CommonTool.atoi(timesp[1]))
						+ nf.format(CommonTool.atoi(timesp2[0]))
						+ nf.format(CommonTool.atoi(timesp2[1]))
						+ nf.format(CommonTool.atoi(timesp2[2]));
				if (fromtime.compareTo(curtime) <= 0) {
					dber = new DBError(dbname, toks[i + 1], toks[i + (1 * 2)
							+ 1], toks[i + (2 * 2) + 1]);
					MainRegistry.Tmpary.add(dber);
				}
				i += (2 * 2);
			}
		}
	}

	void TaskGetBrokersInfo() {
		if (!toks[6].equals("open") || !toks[7].equals("brokersinfo")) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		MainRegistry.CASinfo.clear();
		String broker_name = "";
		String shmid = "";
		String type = "";
		String state = "";
		String pid = "";
		String port = "";
		String as = "";
		String jq = "";
		String thr = "";
		String cpu = "";
		String time = "";
		String req = "";
		String auto = "";
		String ses = "";
		String sqll = "";
		String log = "";
		boolean bSource_env = false;
		boolean bAccess_list = false;
		boolean flag_data = false;
		CASItem casitem;
		MainRegistry.IsCASStart = false;
		for (int i = 8, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open") && toks[i + 1].equals("broker")) {
				flag_data = true;
				broker_name = "";
				type = "";
				state = "";
				pid = "";
				port = "";
				as = "";
				jq = "";
				thr = "";
				cpu = "";
				time = "";
				req = "";
				auto = "";
				ses = "";
				sqll = "";
				log = "";
				shmid = "";
				bSource_env = false;
				bAccess_list = false;
			} else if (toks[i].equals("close") && toks[i + 1].equals("broker")) {
				flag_data = false;
				boolean isDeleted = false;
				for (int di = 0, dn = MainRegistry.DeletedBrokers.size(); di < dn; di++) {
					if (broker_name.equals((String) MainRegistry.DeletedBrokers
							.get(di))) {
						isDeleted = true;
						break;
					}
				}
				if (!isDeleted) {
					casitem = new CASItem(shmid, broker_name, type, state, pid,
							port, as, jq, thr, cpu, time, req, auto, ses, sqll,
							log, bSource_env, bAccess_list);
					MainRegistry.CASinfo.add(casitem);
				}
			} else if (toks[i].equals("brokerstatus")) {
				if (toks[i + 1].equals("ON"))
					MainRegistry.IsCASStart = true;
			} else if (flag_data) {
				if (toks[i].equals("name"))
					broker_name = toks[i + 1];
				else if (toks[i].equals("type"))
					type = toks[i + 1];
				else if (toks[i].equals("pid"))
					pid = toks[i + 1];
				else if (toks[i].equals("port"))
					port = toks[i + 1];
				else if (toks[i].equals("as"))
					as = toks[i + 1];
				else if (toks[i].equals("jq"))
					jq = toks[i + 1];
				else if (toks[i].equals("thr"))
					thr = toks[i + 1];
				else if (toks[i].equals("cpu"))
					cpu = toks[i + 1];
				else if (toks[i].equals("time"))
					time = toks[i + 1];
				else if (toks[i].equals("req"))
					req = toks[i + 1];
				else if (toks[i].equals("auto"))
					auto = toks[i + 1];
				else if (toks[i].equals("ses"))
					ses = toks[i + 1];
				else if (toks[i].equals("sqll"))
					sqll = toks[i + 1];
				else if (toks[i].equals("log"))
					log = toks[i + 1];
				else if (toks[i].equals("state"))
					state = toks[i + 1];
				else if (toks[i].equals("source_env"))
					bSource_env = (CommonTool.atoi(toks[i + 1]) > 0) ? true
							: false;
				else if (toks[i].equals("access_list"))
					bAccess_list = (CommonTool.atoi(toks[i + 1]) > 0) ? true
							: false;
				else if (toks[i].equals("appl_server_shm_id"))
					shmid = toks[i + 1];
			}
		}// end for

		Collections.sort(MainRegistry.CASinfo);

		if (!MainRegistry.IsCASStart) {
			CASItem casrec;
			for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
				casrec = (CASItem) MainRegistry.CASinfo.get(i);
				casrec.state = "OFF";
				casrec.status = MainConstants.STATUS_STOP;
				MainRegistry.CASinfo.set(i, casrec);
			}
		}
	}

	void TaskGetBrokerStatus() {
		BrokerJob.asinfo.clear();
		BrokerJob.jobqinfo.clear();

		if (toks.length < 10)
			return;

		BrokerAS as;
		BrokerJobStatus job;
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open") && toks[i + 1].equals("asinfo")) {
				as = new BrokerAS(toks[i + (1 * 2) + 1], toks[i + (2 * 2) + 1],
						toks[i + (3 * 2) + 1], toks[i + (4 * 2) + 1], toks[i
								+ (5 * 2) + 1], toks[i + (6 * 2) + 1], toks[i
								+ (7 * 2) + 1], toks[i + (8 * 2) + 1], toks[i
								+ (9 * 2) + 1]);
				BrokerJob.asinfo.add(as);
				i += (2 * 9);
			} else if (toks[i].equals("open") && toks[i + 1].equals("jobinfo")) {
				job = new BrokerJobStatus(toks[i + (1 * 2) + 1], toks[i
						+ (2 * 2) + 1], toks[i + (3 * 2) + 1], toks[i + (4 * 2)
						+ 1], toks[i + (5 * 2) + 1]);
				BrokerJob.jobqinfo.add(job);
			}
		}
	}

	void TaskGetAdminLogInfo() {
		boolean flag_data = false;
		String filename = "";
		String fileowner = "";
		String size = "";
		String date = "";
		String path = "";
		LogFileInfo li = null;
		MainRegistry.CASadminlog.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open") && toks[i + 1].equals("adminloginfo"))
				flag_data = true;
			else if (toks[i].equals("close")
					&& toks[i + 1].equals("adminloginfo")) {
				flag_data = false;
				int lastidx;
				if ((lastidx = path.lastIndexOf("/")) >= 0) {
					filename = path.substring(lastidx + 1);
				}
				li = new LogFileInfo(filename, fileowner, size, date, path);
				MainRegistry.CASadminlog.add(li);
			} else if (flag_data) {
				if (toks[i].equals("filename"))
					filename = toks[i + 1];
				else if (toks[i].equals("owner"))
					fileowner = toks[i + 1];
				else if (toks[i].equals("size"))
					size = toks[i + 1];
				else if (toks[i].equals("lastupdate"))
					date = toks[i + 1];
				else if (toks[i].equals("path"))
					path = toks[i + 1];
			}
		}
	}

	void TaskGetLogFileInfo() {
		CASItem ci = MainRegistry.CASinfo_find(toks[7]);
		if (ci == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		ci.loginfo.clear();
		boolean flag_data = false;
		String filename = "";
		String fileowner = "";
		String size = "";
		String type = "";
		String date = "";
		String path = "";
		LogFileInfo li = null;
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open") && toks[i + 1].equals("logfile")) {
				flag_data = true;
				filename = "";
				fileowner = "";
				size = "";
				type = "";
				date = "";
				path = "";
			} else if (toks[i].equals("close") && toks[i + 1].equals("logfile")) {
				flag_data = false;
				int lastidx;
				if ((lastidx = path.lastIndexOf("/")) >= 0) {
					filename = path.substring(lastidx + 1);
				}
				li = new LogFileInfo(filename, fileowner, size, date, path);
				li.type = new String(type);
				ci.loginfo.add(li);
			} else if (flag_data) {
				if (toks[i].equals("filename"))
					filename = toks[i + 1];
				else if (toks[i].equals("owner"))
					fileowner = toks[i + 1];
				else if (toks[i].equals("size"))
					size = toks[i + 1];
				else if (toks[i].equals("lastupdate"))
					date = toks[i + 1];
				else if (toks[i].equals("path"))
					path = toks[i + 1];
				else if (toks[i].equals("type"))
					type = toks[i + 1];
			}
		}
		MainRegistry.CASinfo_update(ci);
	}

	void TaskBackupDBInfo() {
		BackupAction.backinfo.clear();
		BackupAction.free_space = toks[toks.length - 1];
		BackupAction.dbdir = toks[6 + 1];

		for (int i = 8, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open")) {
				BackupInfo bi = new BackupInfo(toks[i + (0 * 2) + 1], toks[i
						+ (1 * 2) + 1], toks[i + (2 * 2) + 1], toks[i + (3 * 2)
						+ 1]);
				BackupAction.backinfo.add(bi);
				i += (4 * 2);
			}
		}
	}

	void TaskCheckAccessRight() {
		/*
		 * String tmpdb=null;
		 * 
		 * ArrayList tmpAuthInfo = new ArrayList();
		 * 
		 * for (int i=0; i < MainRegistry.Authinfo.size(); i++) { AuthItem ai =
		 * (AuthItem) MainRegistry.Authinfo.get(i); if (!ai.setinfo)
		 * MainRegistry.Authinfo.remove(ai); else ai.setinfo = false; }
		 * MainRegistry.Authinfo.trimToSize(); tmpAuthInfo = (ArrayList)
		 * MainRegistry.Authinfo.clone();
		 * 
		 * MainRegistry.Authinfo.clear();
		 * 
		 * if (!toks[6].equals("open") || !toks[7].equals("userauth")) { //
		 * format error MainRegistry.IsConnected=false; return; } for (int
		 * i=8,n=toks.length; (!toks[i].equals("close") ||
		 * !toks[i+1].equals("userauth")) && i<n; ) { if (toks[i].equals("id")) {
		 * if (!MainRegistry.UserID.equals(toks[i+1])) { // is not login ID
		 * MainRegistry.IsConnected=false; return; } } else if
		 * (toks[i].equals("casauth")) { if (toks[i+1].equals("none"))
		 * MainRegistry.CASAuth=MainConstants.AUTH_NONE; else if
		 * (toks[i+1].equals("monitor"))
		 * MainRegistry.CASAuth=MainConstants.AUTH_NONDBA; else
		 * MainRegistry.CASAuth=MainConstants.AUTH_DBA; } else if
		 * (toks[i].equals("dbcreate")) { if (toks[i+1].equals("admin"))
		 * MainRegistry.IsDBAAuth=true; else MainRegistry.IsDBAAuth=false; }
		 * else if (toks[i].equals("open") && toks[i+1].equals("dbauth")) {
		 * i+=2; for (; !toks[i].equals("close") || !toks[i+1].equals("dbauth"); ) {
		 * if (toks[i].equals("dbname")) { tmpdb=toks[i+1]; } else if
		 * (toks[i].equals("authority")) { AuthItem ai = (AuthItem)
		 * MainRegistry.Authinfo_find(toks[i+1]); if (ai == null)
		 * MainRegistry.Authinfo_add(tmpdb, "", MainConstants.STATUS_STOP);
		 * for(int j = 0; j < tmpAuthInfo.size(); j ++) { if
		 * (tmpdb.equals(((AuthItem)tmpAuthInfo.get(j)).dbname))
		 * MainRegistry.Authinfo_update((AuthItem)tmpAuthInfo.get(j)); } } else { //
		 * format error MainRegistry.IsConnected=false; return; } i+=2; } } else {
		 * MainRegistry.IsConnected=false; return; } i+=2; }
		 * 
		 * ClientSocket cs=new ClientSocket(); if
		 * (!cs.SendClientMessage(callingsh, "", "getenv"))
		 * ErrorMsg=cs.ErrorMsg;
		 */
	}

	void TaskGetAddBrokerInfo() {
		MainRegistry.BrokerConf = "";
		for (int i = 10, n = toks.length; i < n && 
					(!toks[i].equals("close") || 
					!toks[i + 1].equals("conflist")); i += 2) {
			if (toks[i].equals("confdata")) {
				MainRegistry.BrokerConf = MainRegistry.BrokerConf + toks[i+1];
			}
		}
	}

	void TaskGetAsLimit() {
		CASItem casrec;
		for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
			casrec = (CASItem) MainRegistry.CASinfo.get(i);
			if (tempcmd.equals(casrec.broker_name)) {
				for (int ci = 6, cn = toks.length; ci < cn; ci += 2) {
					if (toks[ci].equals("maxas"))
						casrec.ASmax = CommonTool.atoi(toks[ci + 1]);
					else if (toks[ci].equals("minas"))
						casrec.ASmin = CommonTool.atoi(toks[ci + 1]);
					else if (toks[ci].equals("asnum"))
						casrec.ASnum = CommonTool.atoi(toks[ci + 1]);
				}
				MainRegistry.CASinfo.set(i, casrec);
				break;
			}
		}
	}

	void TaskGetBrokerEnvInfo() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 6, n = toks.length; i < n; i++) {
			MainRegistry.Tmpchkrst.add(toks[i]);
		}
	}

	void TaskUnloadInfo() {
		LoadAction.unloaddb.clear();
		UnloadInfo ui = null;
		boolean dbopen = false;
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open") && toks[i + 1].equals("database")) {
				ui = new UnloadInfo(toks[i + (1 * 2) + 1]); // dbname
				dbopen = true;
				i += 2;
			} else if (toks[i].equals("close")
					&& toks[i + 1].equals("database")) {
				LoadAction.unloaddb.add(ui);
				dbopen = false;
			} else if (dbopen) {
				if (toks[i].equals("schema")) {
					int sepa = toks[i + 1].indexOf(";");
					if (sepa >= 0) {
						ui.schemaDir.add(toks[i + 1].substring(0, sepa));
						ui.schemaDate.add(toks[i + 1].substring(sepa + 1));
					}
				} else if (toks[i].equals("object")) {
					int sepa = toks[i + 1].indexOf(";");
					if (sepa >= 0) {
						ui.objectDir.add(toks[i + 1].substring(0, sepa));
						ui.objectDate.add(toks[i + 1].substring(sepa + 1));
					}
				} else if (toks[i].equals("index")) {
					int sepa = toks[i + 1].indexOf(";");
					if (sepa >= 0) {
						ui.indexDir.add(toks[i + 1].substring(0, sepa));
						ui.indexDate.add(toks[i + 1].substring(sepa + 1));
					}
				} else if (toks[i].equals("trigger")) {
					int sepa = toks[i + 1].indexOf(";");
					if (sepa >= 0) {
						ui.triggerDir.add(toks[i + 1].substring(0, sepa));
						ui.triggerDate.add(toks[i + 1].substring(sepa + 1));
					}
				}
			}
		}
	}

	void TaskLoadDB() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			MainRegistry.Tmpchkrst.add(toks[i + 1]);
		}
	}

	void TaskUnloadDB() {
		MainRegistry.Tmpchkrst.clear();

		for (int i = 8; !toks[i].equals("close")
				|| !toks[i + 1].equals("result"); i += 2) {
			MainRegistry.Tmpchkrst.add(toks[i]); // classname
			MainRegistry.Tmpchkrst.add(toks[i + 1]); // status
		}

	}

	void TaskGetTransactionInfo() {
		MainRegistry.Tmpchkrst.clear();
		LockTran lt = null;
		boolean flagtran = false;
		for (int i = 8, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open") && toks[i + 1].equals("transaction")) {
				lt = new LockTran("", "", "", "", "", "", "");
				flagtran = true;
			} else if (toks[i].equals("close")
					&& toks[i + 1].equals("transaction")) {
				MainRegistry.Tmpchkrst.add(lt);
				flagtran = false;
			} else if (flagtran) {
				if (toks[i].equals("tranindex"))
					lt.index = toks[i + 1];
				if (toks[i].equals("user"))
					lt.uid = toks[i + 1];
				if (toks[i].equals("host"))
					lt.host = toks[i + 1];
				if (toks[i].equals("pid"))
					lt.pid = toks[i + 1];
				if (toks[i].equals("program"))
					lt.pname = toks[i + 1];
			}
		}
	}

	void TaskKillTransaction() {
		TaskGetTransactionInfo();
	}

	void TaskGetBackupList() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("level0") || toks[i].equals("level1")
					|| toks[i].equals("level2")) {
				MainRegistry.Tmpchkrst.add(toks[i]);
				MainRegistry.Tmpchkrst.add(toks[i + 1]);
			}
		}
	}

	void TaskLockDB() {
		LockEntry le = null;
		if (LockinfoAction.linfo != null) {
			LockinfoAction.linfo.locktran.clear();
		}
		if (LockinfoAction.lockobj != null) {
			LockinfoAction.lockobj.entry.clear();
		}
		for (int index = 6, n = toks.length; index < n; index += 2) {
			if (toks[index].equals("open")
					&& toks[index + 1].equals("lockinfo")) {
				LockinfoAction.linfo = new LockInfo(toks[index + (1 * 2) + 1],
						toks[index + (2 * 2) + 1]);
				index += (2 * 2);
			} else if (toks[index].equals("open")
					&& toks[index + 1].equals("transaction")) {
				LockTran lt = new LockTran(toks[index + (1 * 2) + 1],
						toks[index + (2 * 2) + 1], toks[index + (3 * 2) + 1],
						toks[index + (4 * 2) + 1], toks[index + (5 * 2) + 1],
						toks[index + (6 * 2) + 1], toks[index + (7 * 2) + 1]);
				LockinfoAction.linfo.locktran.add(lt);
				index += (7 * 2);
			} else if (toks[index].equals("open")
					&& toks[index + 1].equals("lot")) {
				LockinfoAction.lockobj = new LockObject(toks[index + (1 * 2)
						+ 1], toks[index + (2 * 2) + 1]);
				index += (2 * 2);
			} else if (toks[index].equals("open")
					&& toks[index + 1].equals("entry")) {
				le = new LockEntry(toks[index + (1 * 2) + 1], toks[index
						+ (2 * 2) + 1], toks[index + (3 * 2) + 1], toks[index
						+ (4 * 2) + 1], toks[index + (5 * 2) + 1]);
				index += (5 * 2);
			} else if (toks[index].equals("close")
					&& toks[index + 1].equals("entry")) {
				LockinfoAction.lockobj.entry.add(le);
			} else if (toks[index].equals("open")
					&& toks[index + 1].equals("lock_holders")) {
				LockHolders lh;
				if (toks[index + (4 * 2)].equals("nsubgranules")) {
					lh = new LockHolders(toks[index + (1 * 2) + 1], toks[index
							+ (2 * 2) + 1], toks[index + (3 * 2) + 1],
							toks[index + (4 * 2) + 1]);
					index += (4 * 2);
				} else {
					lh = new LockHolders(toks[index + (1 * 2) + 1], toks[index
							+ (2 * 2) + 1], toks[index + (3 * 2) + 1], "");
					index += (3 * 2);
				}
				le.LockHolders.add(lh);
			} else if (toks[index].equals("open")
					&& toks[index + 1].equals("b_holders")) {
				Lock_B_Holders lbh;
				if (toks[index + (4 * 2)].equals("nsubgranules")) {
					lbh = new Lock_B_Holders(toks[index + (1 * 2) + 1],
							toks[index + (2 * 2) + 1],
							toks[index + (3 * 2) + 1],
							toks[index + (4 * 2) + 1],
							toks[index + (5 * 2) + 1],
							toks[index + (6 * 2) + 1],
							toks[index + (7 * 2) + 1]);
					index += (7 * 2);
				} else {
					lbh = new Lock_B_Holders(toks[index + (1 * 2) + 1],
							toks[index + (2 * 2) + 1],
							toks[index + (3 * 2) + 1], "", toks[index + (4 * 2)
									+ 1], toks[index + (5 * 2) + 1], toks[index
									+ (6 * 2) + 1]);
					index += (6 * 2);
				}
				le.Lock_B_Holders.add(lbh);
			} else if (toks[index].equals("open")
					&& toks[index + 1].equals("waiters")) {
				LockWaiters lw;
				lw = new LockWaiters(toks[index + (1 * 2) + 1], toks[index
						+ (2 * 2) + 1], toks[index + (3 * 2) + 1], toks[index
						+ (4 * 2) + 1]);
				index += (4 * 2);
				le.LockWaiters.add(lw);
			}
		}
	}

	void TaskLoadAccessLog() {
		boolean flag_access = false, flag_error = false;

		ManagerLogAction.Accesslog.clear();
		ManagerLogAction.Errorlog.clear();
		LogFileInfo lfi = null;
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open") && toks[i + 1].equals("accesslog")) {
				flag_access = true;
			} else if (toks[i].equals("close")
					&& toks[i + 1].equals("accesslog")) {
				flag_access = false;
			} else if (toks[i].equals("open") && toks[i + 1].equals("errorlog")) {
				flag_error = true;
			} else if (toks[i].equals("close")
					&& toks[i + 1].equals("errorlog")) {
				flag_error = false;
			} else if (flag_error) {
				lfi = new LogFileInfo(toks[i + (0 * 2) + 1], toks[i + (1 * 2)
						+ 1], toks[i + (2 * 2) + 1], toks[i + (3 * 2) + 1], "");
				ManagerLogAction.Errorlog.add(lfi);
				i += (3 * 2);
			} else if (flag_access) {
				lfi = new LogFileInfo(toks[i + (0 * 2) + 1], toks[i + (1 * 2)
						+ 1], toks[i + (2 * 2) + 1], "", "");
				ManagerLogAction.Accesslog.add(lfi);
				i += (2 * 2);
			}
		}
	}

	void TaskClassInfo() {
		AuthItem ai = MainRegistry.Authinfo_ready(toks[7]);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}

		ai.Schema.clear();
		SchemaInfo si;
		boolean flag_sys = true, flag_class = false, flag_oid = false;
		String name = null;
		String schemaowner = null;
		String virtual = null;
		String ldbname = null;
		String is_partitionGroup = null;
		String partitionGroupName = null;

		ArrayList superClasses = new ArrayList();
		ArrayList subClasses = new ArrayList();
		ArrayList OidList = new ArrayList();
		for (int i = 8, n = toks.length; i < n;) {
			if (toks[i].equals("open") && toks[i + 1].equals("systemclass"))
				flag_sys = true;
			else if (toks[i].equals("open") && toks[i + 1].equals("userclass"))
				flag_sys = false;
			else if (toks[i].equals("open") && toks[i + 1].equals("class")) {
				flag_class = true;
				superClasses.clear();
				subClasses.clear();
				OidList.clear();
				name = "";
				schemaowner = "";
				virtual = "";
				ldbname = "";
				is_partitionGroup = "";
				partitionGroupName = "";
			} else if (toks[i].equals("close") && toks[i + 1].equals("class")) {
				flag_class = false;
				si = new SchemaInfo(name, (flag_sys) ? "system" : "user",
						schemaowner, virtual);
				si.ldbname = new String(ldbname);
				si.superClasses = new ArrayList(superClasses);
				si.subClasses = new ArrayList(subClasses);
				si.OidList = new ArrayList(OidList);
				si.is_partitionGroup = new String(is_partitionGroup);
				si.partitionGroupName = new String(partitionGroupName);
				ai.Schema.add(si);
			} else if (toks[i].equals("open") && toks[i + 1].equals("oid_list"))
				flag_oid = true;
			else if (toks[i].equals("close") && toks[i + 1].equals("oid_list"))
				flag_oid = false;
			else if (flag_class) {
				if (flag_oid) {
					if (toks[i].equals("oid"))
						OidList.add(toks[i + 1]);
				} else {
					if (toks[i].equals("classname"))
						name = toks[i + 1];
					else if (toks[i].equals("owner"))
						schemaowner = toks[i + 1];
					else if (toks[i].equals("superclass"))
						superClasses.add(toks[i + 1]);
					else if (toks[i].equals("subclass"))
						subClasses.add(toks[i + 1]);
					else if (toks[i].equals("virtual"))
						virtual = toks[i + 1];
					else if (toks[i].equals("ldb"))
						ldbname = toks[i + 1];
					// Dongsoo begin
					else if (toks[i].equals("is_partitiongroup"))
						is_partitionGroup = toks[i + 1];
					else if (toks[i].equals("partitiongroupname"))
						partitionGroupName = toks[i + 1];
					// end Dongsoo
				}
			}
			i += 2;
		}

		Collections.sort(ai.Schema);
		MainRegistry.Authinfo_commit(ai);
	}

	void TaskSetHistory() {
		/*
		 * if (strcmp(m_NVList[1].name, "status")) { AfxMessageBox("Messsage
		 * format error"); return; } else if (!strcmp(m_NVList[1].value,
		 * "failure")) { AfxMessageBox(m_NVList[2].value); return; }
		 * 
		 * AfxMessageBox("History setting is finished") ;
		 */}

	void TaskGetHistory() {
		/*
		 * if (strcmp(m_NVList[1].name, "status")) { AfxMessageBox("Messsage
		 * format error"); return; } else if (!strcmp(m_NVList[1].value,
		 * "failure")) { AfxMessageBox(m_NVList[2].value); return; }
		 * 
		 * CUSQLAdminApp* app = (CUSQLAdminApp *)AfxGetApp(); CHostMonDlg *h_Dlg =
		 * app->m_hostMonThread->m_pDlg ;
		 * 
		 * h_Dlg->m_hosmonTab.m_tabPages2->m_dataStr.RemoveAll() ;
		 * 
		 * 
		 * int i = 3 ; while ((strcmp(toks[i], "close") !=0) ||
		 * (strcmp(toks[i+1], "dblist") !=0)) {
		 * h_Dlg->m_hosmonTab.m_tabPages2->m_dataStr.AddTail(toks[i+1]); ++i ; }
		 */}

	void TaskCheckAuthority() {
		/*
		 * ASSERT(strcmp(m_NVList[1].value,"success") == 0);
		 *  // Data structure set... CUSQLAdminApp *app = (CUSQLAdminApp*)AfxGetApp();
		 * 
		 * CString dbName = m_NVList[3].value; CString dbAuth =
		 * m_NVList[4].value; CUserAuth *userAuth = app->m_UserAuth; for (int i =
		 * 0; i < userAuth->m_dbname->GetSize(); i++) { if
		 * (userAuth->m_dbname->GetAt(i) == dbName) {
		 * userAuth->m_dbauth->SetAt(i, dbAuth); break;; } }
		 *  // send endjob message... POST_ENDJOB_MSG();
		 */}

	void TaskBackupVolInfo() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			MainRegistry.Tmpchkrst.add(toks[i + 1]);
		}
	}

	void TaskGetDBSize() {
		MainRegistry.TmpVolsize = new String(toks[6 + 1]);
	}

	void TaskGetAutoBackupDBErrLog() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open")) {
				MainRegistry.Tmpchkrst.add(toks[i + (1 * 2) + 1]);
				MainRegistry.Tmpchkrst.add(toks[i + (2 * 2) + 1]);
				MainRegistry.Tmpchkrst.add(toks[i + (3 * 2) + 1]);
				MainRegistry.Tmpchkrst.add(toks[i + (4 * 2) + 1]);
				i += (5 * 2);
			}
		}
	}

	void TaskGetHistoryList() {
		/*
		 * if (strcmp(m_NVList[1].name, "status")) { AfxMessageBox("Messsage
		 * format error"); return; } else if (!strcmp(m_NVList[1].value,
		 * "failure")) { AfxMessageBox(m_NVList[2].value); return; }
		 * 
		 * CHostMonHistoryLists dlg ;
		 * 
		 * dlg.m_fileList = new CStringList ; dlg.m_fileList->RemoveAll();
		 * 
		 * 
		 * int i = 0 ; while ((strcmp(toks[i], "open") !=0) &&
		 * (strcmp(toks[i+1], "filelist") !=0)) ++i ; ++i; while
		 * ((strcmp(toks[i], "close") !=0) && (strcmp(toks[i+1], "filelist")
		 * !=0)) { dlg.m_fileList->AddTail(m_NVList[i++].value);
		 * dlg.m_fileList->AddTail(m_NVList[i++].value);
		 * dlg.m_fileList->AddTail(m_NVList[i++].value);
		 * dlg.m_fileList->AddTail(m_NVList[i++].value); }
		 * 
		 * dlg.DoModal();
		 */}

	void TaskViewHistoryLog() {
		/*
		 * CHostMonFileView
		 * dlg((CCUBRIDTreeView*)this->GetTreeCtrl()->GetParent());
		 * 
		 * if (strcmp(m_NVList[3].name, "open") || strcmp(m_NVList[3].value,
		 * "view")) { AfxMessageBox("Message format error"); return; } for (int
		 * i = 4; strcmp(toks[i], "close") && strcmp(toks[i+1], "view"); i++) {
		 * if (toks[i].equals( "msg")) { dlg.m_lines += toks[i+1]; dlg.m_lines +=
		 * "\r\n"; } else { AfxMessageBox("Message format error"); return; } }
		 * 
		 * dlg.DoModal();
		 */}

	void TaskGetBrokerOnConf() {
		MainRegistry.Tmpchkrst.clear();
		MainRegistry.Tmpchkrst.add(toks[3 * 2 + 1]);
		MainRegistry.Tmpchkrst.add(toks[4 * 2 + 1]);
		MainRegistry.Tmpchkrst.add(toks[5 * 2 + 1]);
		MainRegistry.Tmpchkrst.add(toks[6 * 2 + 1]);
		MainRegistry.Tmpchkrst.add(toks[7 * 2 + 1]);
		MainRegistry.Tmpchkrst.add(toks[8 * 2 + 1]);
		MainRegistry.Tmpchkrst.add(toks[9 * 2 + 1]);
	}

	void TaskGetAutoAddVolLog() {
		MainRegistry.Tmpchkrst.clear();

		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("open")) {
				AddVols av = new AddVols(toks[i + (1 * 2) + 1], toks[i
						+ (2 * 2) + 1], toks[i + (3 * 2) + 1], toks[i + (4 * 2)
						+ 1], toks[i + (5 * 2) + 1], toks[i + (6 * 2) + 1]);
				MainRegistry.Tmpchkrst.add(av);
				i += (7 * 2);
			}
		}
	}

	void TaskCheckDir() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			MainRegistry.Tmpchkrst.add(toks[i + 1]);
		}
	}

	void TaskViewLog2() {
		/*
		 * ASSERT(strcmp(m_NVList[1].value,"success") == 0);
		 *  // Data structure set... CUSQLAdminApp *app = (CUSQLAdminApp*)AfxGetApp();
		 * 
		 * if (!app) { POST_INTERNALERR_MSG(); ASSERT(0); return; }
		 * 
		 * if (strcmp(m_NVList[4].name, "open") || strcmp(m_NVList[4].value,
		 * "log")) { POST_INTERNALERR_MSG(); ASSERT(0); return; }
		 * 
		 * CLogStringInfo *logstringinfo = new CLogStringInfo();
		 * 
		 * for (int i = 5; strcmp(toks[i], "close") && strcmp(toks[i+1], "log");
		 * i++) { if (toks[i].equals( "line")) {
		 * logstringinfo->m_saLogString.Add(toks[i+1]); } else {
		 * POST_INTERNALERR_MSG(); ASSERT(0); return; } }
		 * 
		 * i++; logstringinfo->m_Path = m_NVList[3].value;
		 * logstringinfo->m_StartLine = atol(m_NVList[i++].value);
		 * logstringinfo->m_EndLine = atol(m_NVList[i++].value);
		 * logstringinfo->m_TotalLine = atol(m_NVList[i++].value);
		 * 
		 * app->m_TempData.pLogStringInfo = logstringinfo;
		 *  // send endjob message POST_ENDJOB_MSG();
		 */
	}

	void TaskGetDiagData() {
		DiagStatusResult diagStatusResult = null;
		ArrayList activityResultList = null;

		if (DiagMessageType == MainRegistry.DIAGMESSAGE_TYPE_STATUS) {
			diagStatusResult = ((DiagStatusMonitorDialog) socketOwner).diagStatusResult;

			for (int i = 6, n = toks.length; i < n && toks[i] != null; i += 2) {
				if (toks[i].equals("start_time_sec")) {
					// MainRegistry.monitor_start_time_sec = toks[i+1];
				} else if (toks[i].equals("start_time_usec")) {
					// MainRegistry.monitor_start_time_usec = toks[i+1];
				} else if (toks[i].equals("mon_cub_query_open_page")) {
					diagStatusResult.server_query_open_page = toks[i + 1];
				} else if (toks[i].equals("mon_cub_query_opened_page")) {
					diagStatusResult.server_query_opened_page = toks[i + 1];
				} else if (toks[i].equals("mon_cub_query_slow_query")) {
					diagStatusResult.server_query_slow_query = toks[i + 1];
				} else if (toks[i].equals("mon_cub_query_full_scan")) {
					diagStatusResult.server_query_full_scan = toks[i + 1];
				} else if (toks[i].equals("mon_cub_conn_cli_request")) {
					diagStatusResult.server_conn_cli_request = toks[i + 1];
				} else if (toks[i].equals("mon_cub_conn_aborted_clients")) {
					diagStatusResult.server_conn_aborted_clients = toks[i + 1];
				} else if (toks[i].equals("mon_cub_conn_conn_req")) {
					diagStatusResult.server_conn_conn_req = toks[i + 1];
				} else if (toks[i].equals("mon_cub_conn_conn_reject")) {
					diagStatusResult.server_conn_conn_reject = toks[i + 1];
				} else if (toks[i].equals("mon_cub_buffer_page_write")) {
					diagStatusResult.server_buffer_page_write = toks[i + 1];
				} else if (toks[i].equals("mon_cub_buffer_page_read")) {
					diagStatusResult.server_buffer_page_read = toks[i + 1];
				} else if (toks[i].equals("mon_cub_lock_deadlock")) {
					diagStatusResult.server_lock_deadlock = toks[i + 1];
				} else if (toks[i].equals("mon_cub_lock_request")) {
					diagStatusResult.server_lock_request = toks[i + 1];
				} else if (toks[i].equals("cas_mon_req")) {
					diagStatusResult.SetCAS_Request_Sec(toks[i + 1]);
				} else if (toks[i].equals("cas_mon_tran")) {
					diagStatusResult.SetCAS_Transaction_Sec(toks[i + 1]);
				} else if (toks[i].equals("cas_mon_act_session")) {
					diagStatusResult.SetCAS_Active_Session(toks[i + 1]);
				}
			}
		} else if (DiagMessageType == MainRegistry.DIAGMESSAGE_TYPE_ACTIVITY) {
			activityResultList = ((DiagActivityMonitorDialog) socketOwner).diagDataList;
			activityResultList.clear();
			for (int i = 6, n = toks.length; i < n && toks[i] != null; i += 2) {
				if (toks[i].equals("start_time_sec")) {
					((DiagActivityMonitorDialog) socketOwner).monitor_start_time_sec = toks[i + 1];
				} else if (toks[i].equals("start_time_usec")) {
					((DiagActivityMonitorDialog) socketOwner).monitor_start_time_usec = toks[i + 1];
				} else if (toks[i].equals("EventClass")) {
					DiagActivityResult tempActivityResult = new DiagActivityResult();
					tempActivityResult.SetEventClass(toks[i + 1]);
					tempActivityResult.SetTextData(toks[i + 3]);
					tempActivityResult.SetBinData(toks[i + 5]);
					tempActivityResult.SetIntegerData(toks[i + 7]);
					tempActivityResult.SetTimeData(toks[i + 9]);
					activityResultList.add(tempActivityResult);
					i += 8;
				}
			}
		}
	}

	void TaskKillProcess() {
		/*
		 * if (strcmp(m_NVList[1].name, "status")) { AfxMessageBox("Messsage
		 * format error"); return; } else if (!strcmp(m_NVList[1].value,
		 * "failure")) { AfxMessageBox(m_NVList[2].value); return; } CString msg =
		 * ""; msg = msg + m_NVList[3].value ; msg = msg + " is killed by user" ;
		 * AfxMessageBox(msg);
		 */}

	void TaskCheckFile() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			MainRegistry.Tmpchkrst.add(toks[i + 1]);
		}
	}

	void TaskGetFile() {
		if (CommonTool.atoi(toks[7]) != 1) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		if (!toks[6 * 2 + 1].equals("none")) { // file status
			ErrorMsg = toks[6 * 2 + 1];
			return;
		}
		long file_size = CommonTool.atol(toks[7 * 2 + 1]), totalReceived = 0;
		FileWriter out = null;
		try {
			out = new FileWriter(
					FILEDOWN_PROGRESSDialog.actdlg.EDIT_FILEDOWN_DESTNAME
							.getText());
		} catch (Exception e) {
			ErrorMsg = Messages.getString("ERROR.FILEOPEN")
					+ " "
					+ FILEDOWN_PROGRESSDialog.actdlg.EDIT_FILEDOWN_DESTNAME
							.getText();
			return;
		}

		try {
			output.writeBytes("ACK"); // start file transfer
			output.flush();
		} catch (IOException e) {
			ErrorMsg = Messages.getString("ERROR.NETWORKFAIL")
					+ " "
					+ FILEDOWN_PROGRESSDialog.actdlg.EDIT_FILEDOWN_DESTNAME
							.getText();
			try {
				out.close();
			} catch (Exception ex) {
			}
			return;
		}

		FILEDOWN_PROGRESSDialog.actdlg.label
				.setText("/ " + file_size + "Bytes");

		int len;
		byte tmp[] = new byte[8192];
		while (FILEDOWN_PROGRESSDialog.isfiledowncontinue) {
			try {
				len = in.read(tmp);
			} catch (IOException e) {
				len = -1;
			}
			if (len < 0) { // EOF
				ErrorMsg = Messages.getString("ERROR.FILEDOWNFAIL")
						+ " "
						+ FILEDOWN_PROGRESSDialog.actdlg.EDIT_FILEDOWN_DESTNAME
								.getText();
				break;
			}
			if (len > 0) {
				try {
					out.write(new String(tmp, 0, len));
				} catch (Exception e) {
					ErrorMsg = Messages.getString("ERROR.FILEWRITE")
							+ " "
							+ FILEDOWN_PROGRESSDialog.actdlg.EDIT_FILEDOWN_DESTNAME
									.getText();
					break;
				}
				totalReceived += len;
				FILEDOWN_PROGRESSDialog.actdlg.PROGRESS_BACKUPFILE_DOWN
						.setSelection((int) ((totalReceived * 100) / file_size));
				FILEDOWN_PROGRESSDialog.actdlg.label3.setText(" "
						+ totalReceived);
				if (totalReceived >= file_size)
					break;
			} else {
				try {
					Thread.sleep(50);
				} catch (Exception e) {
				}
			}
		}
		if (!FILEDOWN_PROGRESSDialog.isfiledowncontinue)
			ErrorMsg = Messages.getString("ERROR.CANCELDOWNLOAD");
		try {
			out.close();
		} catch (Exception e) {
			ErrorMsg = Messages.getString("ERROR.FILECLOSE")
					+ " "
					+ FILEDOWN_PROGRESSDialog.actdlg.EDIT_FILEDOWN_DESTNAME
							.getText();
			return;
		}
	}

	void TaskGetAutoexecQuery() {
		AuthItem ai = MainRegistry.Authinfo_ready(toks[7]);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		ai.AutoQueryInfo.clear();
		AutoQuery aq;
		for (int i = 10, n = toks.length; i < n;) {
			if (toks[i].equals("open") && toks[i + 1].equals("queryplan")) {
				aq = new AutoQuery(toks[i + (1 * 2) + 1],
						toks[i + (2 * 2) + 1], toks[i + (3 * 2) + 1], toks[i
								+ (4 * 2) + 1]);
				ai.AutoQueryInfo.add(aq);
				i += (5 * 2);
			}
			i += 2;
		}
		MainRegistry.Authinfo_commit(ai);
	}

	void TaskGetTriggerInfo() {
		AuthItem ai = MainRegistry.Authinfo_ready(toks[7]);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		ai.TriggerInfo.clear();
		boolean flag_data = false;
		String Name = "";
		String ConditionTime = "";
		String EventType = "";
		String EventTarget = "";
		String ConditionString = "";
		String ActionTime = "";
		String ActionType = "";
		String ActionString = "";
		String Status = "";
		String Priority = "";
		Trigger tr = null;
		for (int i = 10, n = toks.length; i < n;) {
			if (toks[i].equals("open") && toks[i + 1].equals("triggerinfo"))
				flag_data = true;
			else if (toks[i].equals("close")
					&& toks[i + 1].equals("triggerinfo")) {
				flag_data = false;
				tr = new Trigger(Name, ConditionTime, EventType, EventTarget,
						ConditionString, ActionTime, ActionType, ActionString,
						Status, Priority);
				ai.TriggerInfo.add(tr);
			} else if (flag_data) {
				if (toks[i].equals("name"))
					Name = toks[i + 1];
				else if (toks[i].equals("conditiontime"))
					ConditionTime = toks[i + 1];
				else if (toks[i].equals("eventtype"))
					EventType = toks[i + 1];
				else if (toks[i].equals("action"))
					ActionString = toks[i + 1];
				else if (toks[i].equals("target_class"))
					EventTarget = toks[i + 1];
				else if (toks[i].equals("target_att"))
					EventTarget = EventTarget.concat("(" + toks[i + 1] + ")");
				else if (toks[i].equals("condition"))
					ConditionString = toks[i + 1];
				else if (toks[i].equals("actiontime"))
					ActionTime = toks[i + 1];
				else if (toks[i].equals("status"))
					Status = toks[i + 1];
				else if (toks[i].equals("priority"))
					Priority = toks[i + 1];
			}
			i += 2;
		}

		Collections.sort(ai.TriggerInfo);
		MainRegistry.Authinfo_commit(ai);
	}

	void TaskGetLocaldbInfo() {
		AuthItem ai = MainRegistry.Authinfo_ready(toks[7]);
		if (ai == null) {
			ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
			return;
		}
		ai.LDBInfo.clear();
		boolean flag_data = false;
		String Name = "";
		String NameIn_Host = "";
		String Type = "";
		String Host = "";
		String U_ID = "";
		String MaxActive = "";
		String MinActive = "";
		String DecayConstant = "";
		String Directory = "";
		String ObjectID = "";
		LocalDatabase ldb = null;
		for (int i = 10, n = toks.length; i < n;) {
			if (toks[i].equals("open") && toks[i + 1].equals("localdbinfo"))
				flag_data = true;
			else if (toks[i].equals("close")
					&& toks[i + 1].equals("localdbinfo")) {
				flag_data = false;
				ldb = new LocalDatabase(Name, NameIn_Host, Type, Host, U_ID,
						MaxActive, MinActive, DecayConstant, Directory,
						ObjectID);
				ai.LDBInfo.add(ldb);
			} else if (flag_data) {
				if (toks[i].equals("name"))
					Name = toks[i + 1];
				else if (toks[i].equals("nameinhost"))
					NameIn_Host = toks[i + 1];
				else if (toks[i].equals("type"))
					Type = toks[i + 1];
				else if (toks[i].equals("hostname"))
					Host = toks[i + 1];
				else if (toks[i].equals("username"))
					U_ID = toks[i + 1];
				else if (toks[i].equals("maxactive"))
					MaxActive = toks[i + 1];
				else if (toks[i].equals("minactive"))
					MinActive = toks[i + 1];
				else if (toks[i].equals("decayconstant"))
					DecayConstant = toks[i + 1];
				else if (toks[i].equals("directory"))
					Directory = toks[i + 1];
				else if (toks[i].equals("object_id"))
					ObjectID = toks[i + 1];
			}
			i += 2;
		}

		Collections.sort(ai.LDBInfo);
		MainRegistry.Authinfo_commit(ai);
	}

	void TaskGetLdbClass() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 8, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("name")) {
				int idx = toks[i + 1].indexOf(".");
				if (idx < 0)
					MainRegistry.Tmpchkrst.add(toks[i + 1]);
				else
					MainRegistry.Tmpchkrst.add(toks[i + 1].substring(idx + 1));
			}
		}
	}

	void TaskGetLdbClassAtt() {
		MainRegistry.Tmpchkrst.clear();
		for (int i = 6, n = toks.length; i < n; i += 2) {
			if (toks[i].equals("name") && toks[i + 2].equals("type")) {
				MainRegistry.Tmpchkrst.add(toks[i + 1]);
				if (toks[i + 3] == null || toks[i + 3].length() <= 0)
					MainRegistry.Tmpchkrst.add("object");
				else
					MainRegistry.Tmpchkrst.add(toks[i + 3]);
				i += 2;
			}
		}
	}

	void TaskGetEnv() {
		MainRegistry.envCUBRID = GetValueFor(toks, "CUBRID");
		MainRegistry.envCUBRID_DATABASES = GetValueFor(toks, "CUBRID_DATABASES");
		MainRegistry.envCUBRID_DBMT = GetValueFor(toks, "CUBRID_DBMT");
		MainRegistry.CUBRIDVer = GetValueFor(toks, "CUBRIDVER");
		MainRegistry.BROKERVer = GetValueFor(toks, "BROKERVER");

		MainRegistry.hostMonTab0 = GetValueFor(toks, "HOSTMONTAB0");
		MainRegistry.hostMonTab1 = GetValueFor(toks, "HOSTMONTAB1");
		MainRegistry.hostMonTab2 = GetValueFor(toks, "HOSTMONTAB2");
		MainRegistry.hostMonTab3 = GetValueFor(toks, "HOSTMONTAB3");
		MainRegistry.hostOsInfo = GetValueFor(toks, "osinfo");

		if (MainRegistry.isProtegoBuild()) {
			String port = GetValueFor(toks, "upaport");
			if (!port.equals("unknown")) {
				try {
					MainRegistry.upaPort = Integer.parseInt(port);
				} catch (Exception ee) {
				}
			}
		}

		Properties prop = new Properties();

		int recnt = 0;
		if (!CommonTool.LoadProperties(prop))
			CommonTool.SetDefaultParameter();
		while (recnt < 2) {
			MainRegistry.SQLX_DATABUFS = prop
					.getProperty(MainConstants.SQLX_DATABUFS);
			MainRegistry.SQLX_MEDIAFAIL = prop
					.getProperty(MainConstants.SQLX_MEDIAFAIL);
			MainRegistry.SQLX_MAXCLI = prop
					.getProperty(MainConstants.SQLX_MAXCLI);

			MainRegistry.DBPARA_GENERICNUM = prop
					.getProperty(MainConstants.DBPARA_GENERICNUM);
			MainRegistry.DBPARA_LOGNUM = prop
					.getProperty(MainConstants.DBPARA_LOGNUM);
			MainRegistry.DBPARA_PAGESIZE = prop
					.getProperty(MainConstants.DBPARA_PAGESIZE);
			MainRegistry.DBPARA_DATANUM = prop
					.getProperty(MainConstants.DBPARA_DATANUM);
			MainRegistry.DBPARA_INDEXNUM = prop
					.getProperty(MainConstants.DBPARA_INDEXNUM);
			MainRegistry.DBPARA_TEMPNUM = prop
					.getProperty(MainConstants.DBPARA_TEMPNUM);
			MainRegistry.MONPARA_STATUS = prop
					.getProperty(MainConstants.MONPARA_STATUS);
			MainRegistry.MONPARA_INTERVAL = prop
					.getProperty(MainConstants.MONPARA_INTERVAL);
			if (MainRegistry.SQLX_DATABUFS == null
					|| MainRegistry.SQLX_MEDIAFAIL == null
					|| MainRegistry.SQLX_MAXCLI == null
					|| MainRegistry.DBPARA_GENERICNUM == null
					|| MainRegistry.DBPARA_LOGNUM == null
					|| MainRegistry.DBPARA_PAGESIZE == null
					|| MainRegistry.DBPARA_DATANUM == null
					|| MainRegistry.DBPARA_INDEXNUM == null
					|| MainRegistry.DBPARA_TEMPNUM == null
					|| MainRegistry.MONPARA_STATUS == null
					|| MainRegistry.MONPARA_INTERVAL == null) {
				CommonTool.SetDefaultParameter();
			} else
				break;
			recnt++;
		}
		if (recnt >= 2) {
			ErrorMsg = Messages.getString("ERROR.GETPARAMETERS");
		}
	}

	void TaskAccess_List_Info() {
		/*
		 * ASSERT(strcmp(m_NVList[1].value,"success") == 0);
		 *  // data struct set... CBROKERDoc *pDoc =
		 * (CBROKERDoc*)GetNeededDocument(DOC_TYPE_CAS);
		 * 
		 * CStringArray *iplist_info = new CStringArray;
		 * 
		 * for(int i=5 ; i<m_numPair-1 ; i++){ iplist_info->Add(toks[i+1]); }
		 * 
		 * pDoc->m_TempData.psaAccessIPList = iplist_info;
		 *  // send endjob message... POST_ENDJOB_MSG();
		 */}

	void TaskGeneralJob() {
		// NO SETTING
	}

	void TaskGetStatusTemplate() {
		DiagSiteDiagData diagSiteDiagData = MainRegistry
				.getSiteDiagDataByName(MainRegistry.GetCurrentSiteName());

		diagSiteDiagData.statusTemplateList.clear();

		int i = 6;
		if (i < toks.length && toks[i].equals("start")
				&& toks[i + 1].equals("templatelist")) {
			i += 2;
			while (i < toks.length && toks[i].equals("start")
					&& toks[i + 1].equals("template")) {
				i += 2;
				DiagStatusMonitorTemplate newtemplate = new DiagStatusMonitorTemplate();
				if (toks[i].equals("name")) {
					newtemplate.templateName = toks[i + 1];
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				if (toks[i].equals("desc")) {
					newtemplate.desc = toks[i + 1];
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				if (toks[i].equals("db_name")) {
					newtemplate.targetdb = toks[i + 1];
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				if (toks[i].equals("sampling_term")) {
					newtemplate.sampling_term = toks[i + 1];
					i += 2;
					if (toks[i].equals("start")
							&& toks[i + 1].equals("target_config")) {
						i += 2;
						while (!(toks[i].equals("end") && toks[i + 1]
								.equals("target_config"))) {
							int color;
							float mag;
							try {
								color = Integer.parseInt(toks[i + 1]);
								mag = Float.parseFloat(toks[i + 3]);
							} catch (Exception ee) {
								ErrorMsg = Messages
										.getString("ERROR.MESSAGEFORMAT");
								return;
							}

							if (toks[i].equals("cas_st_request")) {
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS();
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_REQ();
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_REQ_COLOR(color);
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_REQ_MAG(mag);
							} else if (toks[i].equals("cas_st_active_session")) {
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS();
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION();
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_COLOR(color);
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_MAG(mag);
							} else if (toks[i].equals("cas_st_transaction")) {
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS();
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_TRAN();
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_TRAN_COLOR(color);
								newtemplate.monitor_config
										.SET_CLIENT_MONITOR_INFO_CAS_TRAN_MAG(mag);
							} else if (toks[i].equals("server_query_open_page")) {
								newtemplate.monitor_config.dbData.need_mon_cub_query = true;
								newtemplate.monitor_config.dbData.query_open_page = true;
								newtemplate.monitor_config.dbData.query_open_page_color = color;
								newtemplate.monitor_config.dbData.query_open_page_magnification = mag;
							} else if (toks[i]
									.equals("server_query_opened_page")) {
								newtemplate.monitor_config.dbData.need_mon_cub_query = true;
								newtemplate.monitor_config.dbData.query_opened_page = true;
								newtemplate.monitor_config.dbData.query_opened_page_color = color;
								newtemplate.monitor_config.dbData.query_opened_page_magnification = mag;
							} else if (toks[i]
									.equals("server_query_slow_query")) {
								newtemplate.monitor_config.dbData.need_mon_cub_query = true;
								newtemplate.monitor_config.dbData.query_slow_query = true;
								newtemplate.monitor_config.dbData.query_slow_query_color = color;
								newtemplate.monitor_config.dbData.query_slow_query_magnification = mag;
							} else if (toks[i].equals("server_query_full_scan")) {
								newtemplate.monitor_config.dbData.need_mon_cub_query = true;
								newtemplate.monitor_config.dbData.query_full_scan = true;
								newtemplate.monitor_config.dbData.query_full_scan_color = color;
								newtemplate.monitor_config.dbData.query_full_scan_magnification = mag;
							} else if (toks[i]
									.equals("server_conn_cli_request")) {
								newtemplate.monitor_config.dbData.need_mon_cub_conn = true;
								newtemplate.monitor_config.dbData.conn_cli_request = true;
								newtemplate.monitor_config.dbData.conn_cli_request_color = color;
								newtemplate.monitor_config.dbData.conn_cli_request_magnification = mag;
							} else if (toks[i]
									.equals("server_conn_aborted_clients")) {
								newtemplate.monitor_config.dbData.need_mon_cub_conn = true;
								newtemplate.monitor_config.dbData.conn_aborted_clients = true;
								newtemplate.monitor_config.dbData.conn_aborted_clients_color = color;
								newtemplate.monitor_config.dbData.conn_aborted_clients_magnification = mag;
							} else if (toks[i].equals("server_conn_conn_req")) {
								newtemplate.monitor_config.dbData.need_mon_cub_conn = true;
								newtemplate.monitor_config.dbData.conn_conn_req = true;
								newtemplate.monitor_config.dbData.conn_conn_req_color = color;
								newtemplate.monitor_config.dbData.conn_conn_req_magnification = mag;
							} else if (toks[i]
									.equals("server_conn_conn_reject")) {
								newtemplate.monitor_config.dbData.need_mon_cub_conn = true;
								newtemplate.monitor_config.dbData.conn_conn_reject = true;
								newtemplate.monitor_config.dbData.conn_conn_reject_color = color;
								newtemplate.monitor_config.dbData.conn_conn_reject_magnification = mag;
							} else if (toks[i]
									.equals("server_buffer_page_write")) {
								newtemplate.monitor_config.dbData.need_mon_cub_buffer = true;
								newtemplate.monitor_config.dbData.buffer_page_write = true;
								newtemplate.monitor_config.dbData.buffer_page_write_color = color;
								newtemplate.monitor_config.dbData.buffer_page_write_magnification = mag;
							} else if (toks[i]
									.equals("server_buffer_page_read")) {
								newtemplate.monitor_config.dbData.need_mon_cub_buffer = true;
								newtemplate.monitor_config.dbData.buffer_page_read = true;
								newtemplate.monitor_config.dbData.buffer_page_read_color = color;
								newtemplate.monitor_config.dbData.buffer_page_read_magnification = mag;
							} else if (toks[i].equals("server_lock_deadlock")) {
								newtemplate.monitor_config.dbData.need_mon_cub_lock = true;
								newtemplate.monitor_config.dbData.lock_deadlock = true;
								newtemplate.monitor_config.dbData.lock_deadlock_color = color;
								newtemplate.monitor_config.dbData.lock_deadlock_magnification = mag;
							} else if (toks[i].equals("server_lock_request")) {
								newtemplate.monitor_config.dbData.need_mon_cub_lock = true;
								newtemplate.monitor_config.dbData.lock_request = true;
								newtemplate.monitor_config.dbData.lock_request_color = color;
								newtemplate.monitor_config.dbData.lock_request_magnification = mag;
							} else {
								ErrorMsg = Messages
										.getString("ERROR.MESSAGEFORMAT");
								return;
							}
							i += 4;
						}
						i += 2;
					} else {
						ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
						return;
					}
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				diagSiteDiagData.statusTemplateList.add(newtemplate);
			}
		}
	}

	void TaskGetActivityTemplate() {
		DiagSiteDiagData diagSiteDiagData = MainRegistry
				.getSiteDiagDataByName(MainRegistry.GetCurrentSiteName());

		diagSiteDiagData.activityTemplateList.clear();

		int i = 6;
		if (i < toks.length && toks[i].equals("start")
				&& toks[i + 1].equals("templatelist")) {
			i += 2;
			while (i < toks.length && toks[i].equals("start")
					&& toks[i + 1].equals("template")) {
				i += 2;
				DiagActivityMonitorTemplate newtemplate = new DiagActivityMonitorTemplate();
				if (toks[i].equals("name")) {
					newtemplate.templateName = toks[i + 1];
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				if (toks[i].equals("desc")) {
					newtemplate.desc = toks[i + 1];
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				if (toks[i].equals("db_name")) {
					newtemplate.targetdb = toks[i + 1];
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				if (toks[i].equals("start")
						&& toks[i + 1].equals("target_config")) {
					i += 2;
					while (!(toks[i].equals("end") && toks[i + 1]
							.equals("target_config"))) {
						if (toks[i].equals("cas_act_request")
								&& toks[i + 1].equals("yes")) {
							newtemplate.activity_config
									.SET_CLIENT_ACTINFO_CAS();
							newtemplate.activity_config
									.SET_CLIENT_ACTINFO_CAS_REQ();
						} else if (toks[i].equals("cas_act_transaction")
								&& toks[i + 1].equals("yes")) {
							newtemplate.activity_config
									.SET_CLIENT_ACTINFO_CAS();
							newtemplate.activity_config
									.SET_CLIENT_ACTINFO_CAS_TRAN();
						} else if (toks[i].equals("act_cub_query_fullscan")
								&& toks[i + 1].equals("yes")) {
							newtemplate.activity_config.dbData.needCubActivity = true;
							newtemplate.activity_config.dbData.act_query_fullscan = true;
						} else if (toks[i].equals("act_cub_lock_deadlock")
								&& toks[i + 1].equals("yes")) {
							newtemplate.activity_config.dbData.needCubActivity = true;
							newtemplate.activity_config.dbData.act_lock_deadlock = true;
						} else if (toks[i].equals("act_cub_buffer_page_read")
								&& toks[i + 1].equals("yes")) {
							newtemplate.activity_config.dbData.needCubActivity = true;
							newtemplate.activity_config.dbData.act_buffer_page_read = true;
						} else if (toks[i].equals("act_cub_buffer_page_write")
								&& toks[i + 1].equals("yes")) {
							newtemplate.activity_config.dbData.needCubActivity = true;
							newtemplate.activity_config.dbData.act_buffer_page_write = true;
						} else {
							ErrorMsg = Messages
									.getString("ERROR.MESSAGEFORMAT");
							return;
						}
						i += 2;
					}
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				diagSiteDiagData.activityTemplateList.add(newtemplate);
				i += 2;
			}
		}
	}

	void TaskAnalyzeCasLog() {
		int i;
		if (toks[6].equals("resultlist") && toks[7].equals("start")) {
			i = 8;
			MainRegistry.tmpAnalyzeCasLogResult.clear();
			while (toks[i] != null && toks[i].equals("result")
					&& toks[i + 1].equals("start")) {
				i += 2;
				DiagAnalyzeCasLogResult diagAnalyzeCasLogResult = new DiagAnalyzeCasLogResult();
				if (toks[i].equals("qindex")) {
					diagAnalyzeCasLogResult.qindex = toks[i + 1];
					i += 2;
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}

				if (toks[i].equals("max")) {
					diagAnalyzeCasLogResult.max = toks[i + 1];
					i += 2;
				}

				if (toks[i].equals("min")) {
					diagAnalyzeCasLogResult.min = toks[i + 1];
					i += 2;
				}

				if (toks[i].equals("avg")) {
					diagAnalyzeCasLogResult.avg = toks[i + 1];
					i += 2;
				}

				if (toks[i].equals("cnt")) {
					diagAnalyzeCasLogResult.cnt = toks[i + 1];
					i += 2;
				}

				if (toks[i].equals("err")) {
					diagAnalyzeCasLogResult.err_cnt = toks[i + 1];
					i += 2;
				}

				if (toks[i].equals("exec_time")) {
					diagAnalyzeCasLogResult.exec_time = toks[i + 1];
					i += 2;
				}

				i += 2; // result:end
				MainRegistry.tmpAnalyzeCasLogResult
						.add(diagAnalyzeCasLogResult);
			}
			i += 2; // resultlist:end
			if (toks[i].equals("resultfile"))
				((DiagActivityCASLogPathDialog) socketOwner).resultFile = new String(
						toks[i + 1]);
		}
	}

	void TaskExecuteCasRunner() {
		int i;
		StringBuffer result_string = new StringBuffer("");
		if (toks[6].equals("result_list") && (toks[7].equals("start"))) {
			MainRegistry.tmpDiagExecuteCasRunnerResult.resultString = "";
			for (i = 8; i < toks.length && toks[i] != null; i += 2) {
				if (toks[i].equals("result_list") && toks[i + 1].equals("end")) {
					continue;
				} else if (toks[i].equals("result")) {
					result_string.append(toks[i + 1]);
					result_string.append("\r\n");
				} else if (toks[i].equals("query_result_file")) {
					MainRegistry.tmpDiagExecuteCasRunnerResult.queryResultFile = toks[i + 1];
				} else if (toks[i].equals("query_result_file_num")) {
					try {
						MainRegistry.tmpDiagExecuteCasRunnerResult.resultFileNum = Integer
								.parseInt(toks[i + 1]);
					} catch (Exception ee) {
						MainRegistry.tmpDiagExecuteCasRunnerResult.resultFileNum = 0;
					}
				} else {
					ErrorMsg = Messages.getString("ERROR.MESSAGEFORMAT");
					return;
				}
			}
		}
		MainRegistry.tmpDiagExecuteCasRunnerResult.resultString = result_string
				.toString();
	}

	void TaskGetCasLogTopResult() {
		StringBuffer logstring = new StringBuffer();
		if (toks[6].equals("logstringlist") && (toks[7].equals("start"))) {
			int i = 8;
			while (i < toks.length && toks[i].equals("logstring")) {
				logstring.append(toks[i + 1]);
				logstring.append("\r\n");
				i += 2;
			}
		}

		int result_index = ((DiagActivityCASLogPathDialog) socketOwner).currentResultIndex;
		DiagAnalyzeCasLogResult logResult = (DiagAnalyzeCasLogResult) MainRegistry.tmpAnalyzeCasLogResult
				.get(result_index);
		logResult.queryString = logstring.toString();
	}
}
