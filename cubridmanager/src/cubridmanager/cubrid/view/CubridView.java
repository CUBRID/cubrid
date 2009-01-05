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

package cubridmanager.cubrid.view;

import java.util.ArrayList;
import java.util.Timer;

import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.ITreeViewerListener;
import org.eclipse.jface.viewers.StructuredSelection;
import org.eclipse.jface.viewers.TreeExpansionEvent;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.dnd.DragSource;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Cursor;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TreeItem;
import org.eclipse.ui.part.ViewPart;

import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.BackgroundSocket;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.OnlyQueryEditor;
import cubridmanager.TreeObject;
import cubridmanager.TreeParent;
import cubridmanager.ViewContentProvider;
import cubridmanager.ViewLabelProvider;
import cubridmanager.WorkView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.AutoQuery;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.cubrid.Jobs;
import cubridmanager.cubrid.LocalDatabase;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.Trigger;
import cubridmanager.cubrid.UserInfo;
import cubridmanager.cubrid.VolumeInfo;
import cubridmanager.cubrid.action.LogViewAction;
import cubridmanager.cubrid.dialog.LoginDialog;

public class CubridView extends ViewPart {
	public static final String ID = "navigator.server";
	public static String Current_db = new String("");
	public static String Current_select = DatabaseListInHost.ID;
	public static CubridView myNavi = null;
	public static TreeViewer viewer = null;
	public static TreeParent root = null;
	public static Timer viewtimer = new Timer();
	public static CubridViewTimer cvt = new CubridViewTimer();
	private static TreeObject oldobj = null;
	private static TreeObject selobj = null;
	public static boolean isHaveAuth = false;
	public static boolean actMouseUp = false;
	private TreeItem selitem = null;
	public static DragSource source;
	public static Transfer[] types;
	public static boolean logoutJob = false;

	public CubridView() {
		myNavi = this;
		if (MainRegistry.FirstLogin) {
			if (MainRegistry.IsConnected
					&& MainRegistry.MONPARA_STATUS.equals("ON"))
				viewtimer
						.scheduleAtFixedRate(
								cvt,
								CommonTool.atol(MainRegistry.MONPARA_INTERVAL) * 60 * 1000,
								CommonTool.atol(MainRegistry.MONPARA_INTERVAL) * 60 * 1000);
		}
	}

	/**
	 * We will set up a dummy model to initialize tree heararchy. In real code,
	 * you will connect to a real model and expose its hierarchy.
	 */
	public TreeObject createModel() {
		if (root == null)
			root = new TreeParent(null, "root", null, null);
		TreeObject[] rootchild = root.getChildren();
		if (MainRegistry.IsConnected) {
			TreeParent host1;
			if (rootchild.length <= 0) {
				host1 = new TreeParent(DatabaseListInHost.ID,
						MainRegistry.HostDesc, "icons/host.png",
						DatabaseListInHost.ID);
				Shell psh = viewer.getControl().getShell();
				if (MainRegistry.IsConnected) {
					ClientSocket cs = new ClientSocket();
					if (!cs.SendClientMessage(psh, "", "startinfo")) {
						CommonTool.ErrorBox(psh, cs.ErrorMsg);
						MainRegistry.IsConnected = false;
						return root;
					}
				}
			} else
				host1 = (TreeParent) rootchild[0];
			TreeObject[] hostchild = host1.getChildren();
			boolean[] chk_hostchild = new boolean[hostchild.length];
			for (int ci = 0, cn = chk_hostchild.length; ci < cn; ci++) {
				chk_hostchild[ci] = false;
			}

			AuthItem authrec;
			for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
				authrec = (AuthItem) MainRegistry.Authinfo.get(i);
				if (authrec.status == MainConstants.STATUS_NONE)
					continue; // not exist DB
				TreeParent db1 = null;
				for (int ci = 0, cn = hostchild.length; ci < cn; ci++) {
					if (hostchild[ci].getName().equals(authrec.dbname)) {
						db1 = (TreeParent) hostchild[ci];
						db1.setImage((authrec.status == MainConstants.STATUS_START) ? "icons/server_act.png"
										: "icons/server_stop.png");
						chk_hostchild[ci] = true;
					}
				}
				if (db1 == null)
					db1 = new TreeParent(
							DatabaseStatus.ID,
							authrec.dbname,
							(authrec.status == MainConstants.STATUS_START) ? "icons/server_act.png"
									: "icons/server_stop.png",
							DatabaseStatus.ID);
				TreeObject[] dbchild = db1.getChildren();
				if (authrec.dbuser.length() > 0) { // authority exist
					TreeParent o_user = (TreeParent) TreeObject.FindID(dbchild,
							null, DBUsers.ID);
					if (authrec.status == MainConstants.STATUS_START) {
						boolean addflag = false;
						if (o_user == null) {
							o_user = new TreeParent(DBUsers.ID, Messages
									.getString("TREE.USERS"),
									"icons/dbuser.png", null);
							addflag = true;
						}
						ArrayList userinfo = UserInfo
								.UserInfo_get(authrec.dbname);
						UserInfo ui;
						TreeObject[] childtree = o_user.getChildren();
						boolean[] chktree = new boolean[childtree.length];
						TreeObject.FindReset(chktree);
						for (int ai = 0, an = userinfo.size(); ai < an; ai++) {
							ui = (UserInfo) userinfo.get(ai);
							TreeObject obj = TreeObject.FindName(childtree,
									chktree, ui.userName);
							if (obj == null) {
								if (!ui.userName.equals("dba")
										&& !ui.userName.equals("public")) 
									obj = new TreeObject(DBUsers.USERS,
											ui.userName,
											"icons/dbuser_obj.png", DBUsers.ID);
								else
									obj = new TreeObject(DBUsers.RESERVED,
											ui.userName,
											"icons/dbuser_obj.png", DBUsers.ID);
								o_user.addChild(obj);
							}
						}
						TreeObject.FindRemove(o_user, childtree, chktree);
						if (addflag)
							db1.addChild(o_user);
					} else {
						if (o_user == null) {
							o_user = new TreeParent(DBUsers.ID, Messages
									.getString("TREE.USERS"),
									"icons/dbuser.png", null);
							db1.addChild(o_user);
						}
					}

					boolean o_jobauto_add = false, o_backjob_add = false, o_queryjob_add = false;
					TreeParent o_jobauto = (TreeParent) TreeObject.FindID(
							dbchild, null, JobAutomation.ID);
					if (o_jobauto == null) {
						o_jobauto_add = true;
						o_jobauto = new TreeParent(JobAutomation.ID, Messages
								.getString("TREE.JOBAUTO"),
								"icons/jobauto.png", null);
					}
					TreeObject[] jobautotree = o_jobauto.getChildren();
					TreeParent o_backjob = (TreeParent) TreeObject.FindID(
							jobautotree, null, JobAutomation.BACKJOBS);
					if (o_backjob == null) {
						o_backjob_add = true;
						o_backjob = new TreeParent(JobAutomation.BACKJOBS,
								Messages.getString("TREE.BACKJOBS"),
								"icons/jobauto_backupplan.png", null);
					}
					TreeObject[] jobtree = o_backjob.getChildren();
					boolean[] jobchk = new boolean[jobtree.length];
					TreeObject.FindReset(jobchk);
					ArrayList jobinfo = Jobs.JobsInfo_get(authrec.dbname);
					Jobs ji;
					for (int ai = 0, an = jobinfo.size(); ai < an; ai++) {
						ji = (Jobs) jobinfo.get(ai);
						TreeObject obj = TreeObject.FindName(jobtree, jobchk,
								ji.backupid);
						if (obj == null)
							o_backjob
									.addChild(new TreeObject(
											JobAutomation.BACKJOB, ji.backupid,
											"icons/jobauto_item.png",
											JobAutomation.ID));
					}
					TreeObject.FindRemove(o_backjob, jobtree, jobchk);

					TreeParent o_queryjob = (TreeParent) TreeObject.FindID(
							jobautotree, null, JobAutomation.QUERYJOBS);
					if (o_queryjob == null) {
						o_queryjob_add = true;
						o_queryjob = new TreeParent(JobAutomation.QUERYJOBS,
								Messages.getString("TREE.QUERYJOBS"),
								"icons/jobauto_queryplan.png", null);
					}
					TreeObject[] qrytree = o_queryjob.getChildren();
					boolean[] qrychk = new boolean[qrytree.length];
					TreeObject.FindReset(qrychk);
					ArrayList aqinfo = AutoQuery
							.AutoQueryInfo_get(authrec.dbname);
					AutoQuery aqi;
					for (int ai = 0, an = aqinfo.size(); ai < an; ai++) {
						aqi = (AutoQuery) aqinfo.get(ai);
						TreeObject obj = TreeObject.FindName(qrytree, qrychk,
								aqi.QueryID);
						if (obj == null)
							o_queryjob
									.addChild(new TreeObject(
											JobAutomation.QUERYJOB,
											aqi.QueryID,
											"icons/jobauto_item.png",
											JobAutomation.ID));
					}
					TreeObject.FindRemove(o_queryjob, qrytree, qrychk);

					if (o_backjob_add)
						o_jobauto.addChild(o_backjob);
					if (o_queryjob_add)
						o_jobauto.addChild(o_queryjob);
					if (o_jobauto_add)
						db1.addChild(o_jobauto);

					boolean spaceadd = false, vol1add = false, vol2add = false, vol3add = false;
					TreeParent p_dbspace = (TreeParent) TreeObject.FindID(
							dbchild, null, DBSpace.ID);
					if (p_dbspace == null) {
						spaceadd = true;
						p_dbspace = new TreeParent(DBSpace.ID, Messages
								.getString("TREE.DBSPACE"),
								"icons/dbspace.png", null);
					}
					TreeObject[] spacetree = p_dbspace.getChildren();
					TreeParent o_vol1 = (TreeParent) TreeObject.FindID(
							spacetree, null, DBSpace.VOL_GENERAL);
					if (o_vol1 == null) {
						vol1add = true;
						o_vol1 = new TreeParent(DBSpace.VOL_GENERAL, Messages
								.getString("TREE.GENERIC"),
								"icons/space_generic.png", null);
					}
					TreeParent o_vol2 = (TreeParent) TreeObject.FindID(
							spacetree, null, DBSpace.VOL_ACTIVE);
					if (o_vol2 == null) {
						vol2add = true;
						o_vol2 = new TreeParent(DBSpace.VOL_ACTIVE, Messages
								.getString("TREE.ACTIVE"),
								"icons/space_active.png", null);
					}
					TreeParent o_vol3 = (TreeParent) TreeObject.FindID(
							spacetree, null, DBSpace.VOL_ARCHIVE);
					if (o_vol3 == null) {
						vol3add = true;
						o_vol3 = new TreeParent(DBSpace.VOL_ARCHIVE, Messages
								.getString("TREE.ARCHIVE"),
								"icons/space_archive.png", null);
					}
					TreeObject[] vol1tree = o_vol1.getChildren();
					boolean[] vol1chk = new boolean[vol1tree.length];
					TreeObject.FindReset(vol1chk);
					TreeObject[] vol2tree = o_vol2.getChildren();
					boolean[] vol2chk = new boolean[vol2tree.length];
					TreeObject.FindReset(vol2chk);
					TreeObject[] vol3tree = o_vol3.getChildren();
					boolean[] vol3chk = new boolean[vol3tree.length];
					TreeObject.FindReset(vol3chk);

					ArrayList spinfo = authrec.Volinfo;
					VolumeInfo vi;
					for (int ai = 0, an = spinfo.size(); ai < an; ai++) {
						vi = (VolumeInfo) spinfo.get(ai);
						if (!vi.type.equals("Active_log")
								&& !vi.type.equals("Archive_log")) {
							TreeObject obj = TreeObject.FindName(vol1tree,
									vol1chk, vi.spacename);
							if (obj == null)
								o_vol1.addChild(new TreeObject(
										DBSpace.VOL_OBJECT, vi.spacename,
										"icons/space_generic_obj.png",
										DBSpace.ID));
						} else if (vi.type.equals("Active_log")) {
							TreeObject obj = TreeObject.FindName(vol2tree,
									vol2chk, vi.spacename);
							if (obj == null)
								o_vol2.addChild(new TreeObject(
										DBSpace.VOL_OBJECT, vi.spacename,
										"icons/space_active_obj.png",
										DBSpace.ID));
						} else if (vi.type.equals("Archive_log")) {
							TreeObject obj = TreeObject.FindName(vol3tree,
									vol3chk, vi.spacename);
							if (obj == null)
								o_vol3.addChild(new TreeObject(
										DBSpace.VOL_OBJECT, vi.spacename,
										"icons/space_archive_obj.png",
										DBSpace.ID));
						}
					}
					TreeObject.FindRemove(o_vol1, vol1tree, vol1chk);
					TreeObject.FindRemove(o_vol2, vol2tree, vol2chk);
					TreeObject.FindRemove(o_vol3, vol3tree, vol3chk);

					if (vol1add)
						p_dbspace.addChild(o_vol1);
					if (vol2add)
						p_dbspace.addChild(o_vol2);
					if (vol3add)
						p_dbspace.addChild(o_vol3);
					if (spaceadd)
						db1.addChild(p_dbspace);

					TreeParent p_schema = (TreeParent) TreeObject.FindID(
							dbchild, null, DBSchema.ID);
					boolean addflag = false;
					TreeParent p_sysschema = null;
					TreeParent o_stable = null;
					TreeParent o_sview = null;
					TreeParent p_userschema = null;
					TreeParent o_table = null;
					TreeParent o_view = null;
					if (p_schema == null) {
						addflag = true;
						p_schema = new TreeParent(DBSchema.ID, Messages
								.getString("TREE.SCHEMA"), "icons/schema.png",
								null);
						p_sysschema = new TreeParent(DBSchema.SYS_SCHEMA,
								Messages.getString("TREE.SYSSCHEMA"),
								"icons/sysschema.png", null);
						o_stable = new TreeParent(DBSchema.SYS_TABLE, Messages
								.getString("TREE.TABLE"),
								"icons/system_table.png", null);
						o_sview = new TreeParent(DBSchema.SYS_VIEW, Messages
								.getString("TREE.VIEW"),
								"icons/system_view.png", null);
						p_userschema = new TreeParent(DBSchema.USER_SCHEMA,
								Messages.getString("TREE.USERSCHEMA"),
								"icons/userschema.png", null);
						o_table = new TreeParent(DBSchema.USER_TABLE, Messages
								.getString("TREE.TABLE"),
								"icons/user_table.png", null);
						o_view = new TreeParent(DBSchema.USER_VIEW, Messages
								.getString("TREE.VIEW"), "icons/user_view.png",
								null);
					} else {
						TreeObject[] schematree = p_schema.getChildren();
						p_sysschema = (TreeParent) TreeObject.FindID(
								schematree, null, DBSchema.SYS_SCHEMA);
						p_userschema = (TreeParent) TreeObject.FindID(
								schematree, null, DBSchema.USER_SCHEMA);
						TreeObject[] sysschematree = p_sysschema.getChildren();
						TreeObject[] userschematree = p_userschema
								.getChildren();
						o_stable = (TreeParent) TreeObject.FindID(
								sysschematree, null, DBSchema.SYS_TABLE);
						o_sview = (TreeParent) TreeObject.FindID(sysschematree,
								null, DBSchema.SYS_VIEW);
						o_table = (TreeParent) TreeObject.FindID(
								userschematree, null, DBSchema.USER_TABLE);
						o_view = (TreeParent) TreeObject.FindID(userschematree,
								null, DBSchema.USER_VIEW);
					}

					TreeObject[] stabletree = o_stable.getChildren();
					boolean[] stablechk = new boolean[stabletree.length];
					TreeObject.FindReset(stablechk);
					TreeObject[] sviewtree = o_sview.getChildren();
					boolean[] sviewchk = new boolean[sviewtree.length];
					TreeObject.FindReset(sviewchk);
					TreeObject[] tabletree = o_table.getChildren();
					boolean[] tablechk = new boolean[tabletree.length];
					TreeObject.FindReset(tablechk);
					TreeObject[] viewtree = o_view.getChildren();
					boolean[] viewchk = new boolean[viewtree.length];
					TreeObject.FindReset(viewchk);

					ArrayList sinfo = SchemaInfo.SchemaInfo_get(authrec.dbname);
					SchemaInfo si;

					TreeParent o_temp = null;
					SchemaInfo child_si;
					boolean childAddkey = true;

					for (int ai = 0, an = sinfo.size(); ai < an; ai++) {
						si = (SchemaInfo) sinfo.get(ai);
						if (si.type.equals("system")) {
							if (si.virtual.equals("normal")) {
								TreeObject obj = TreeObject.FindName(
										stabletree, stablechk, si.name);
								if (obj == null)
									o_stable.addChild(new TreeObject(
											DBSchema.SYS_OBJECT, si.name,
											"icons/system_table_obj.png",
											DBSchema.ID));
							} else if (si.virtual.equals("view")) {
								TreeObject obj = TreeObject.FindName(sviewtree,
										sviewchk, si.name);
								if (obj == null)
									o_sview.addChild(new TreeObject(
											DBSchema.SYS_OBJECT, si.name,
											"icons/system_view_obj.png",
											DBSchema.ID));
							}
						} else {
							if (si.virtual.equals("normal")) {
								TreeObject obj = TreeObject.FindName(tabletree,
										tablechk, si.name);
								if (obj == null) {
									if (si.is_partitionGroup.equals("y")) {
										o_table.addChild(new TreeParent(
												DBSchema.USER_OBJECT, si.name,
												"icons/user_table_obj.png",
												DBSchema.ID));
									} else {
										if (si.partitionGroupName.length() == 0) {
											o_table.addChild(new TreeObject(
													DBSchema.USER_OBJECT,
													si.name,
													"icons/user_table_obj.png",
													DBSchema.ID));
										} else {
											TreeObject[] xtabletree = o_table
													.getChildren();
											for (int zi = 0; zi < xtabletree.length; zi++) {
												if (xtabletree[zi]
														.getName()
														.equals(
																si.partitionGroupName)) {
													TreeObject[] tmptree = ((TreeParent) xtabletree[zi])
															.getChildren();
													childAddkey = true;
													for (int pi = 0; pi < tmptree.length; pi++) {
														if (tmptree[pi]
																.getName()
																.equals(si.name)) {
															childAddkey = false;
															break;
														}
													}
													if (childAddkey) {
														((TreeParent) xtabletree[zi])
																.addChild(new TreeObject(
																		DBSchema.USER_OBJECT,
																		si.name,
																		"icons/user_table_obj.png",
																		DBSchema.ID));
													}
													break;
												}
											}
										}
									}
								}
							} else if (si.virtual.equals("view")) {
								TreeObject obj = TreeObject.FindName(viewtree,
										viewchk, si.name);
								if (obj == null)
									o_view.addChild(new TreeObject(
											DBSchema.USER_OBJECT, si.name,
											"icons/user_view_obj.png",
											DBSchema.ID));
							}
						}
					}

					TreeObject.FindRemove(o_stable, stabletree, stablechk);
					TreeObject.FindRemove(o_sview, sviewtree, sviewchk);
					TreeObject.FindRemove(o_table, tabletree, tablechk);
					TreeObject.FindRemove(o_view, viewtree, viewchk);

					for (int ai = 0, an = sinfo.size(); ai < an; ai++) {
						si = (SchemaInfo) sinfo.get(ai);
						if (!si.type.equals("system")
								&& si.virtual.equals("normal")) {
							if (si.is_partitionGroup.equals("y")) {
								TreeObject[] xtree = o_table.getChildren();
								for (int zi = 0; zi < xtree.length; zi++) {
									if (xtree[zi].getName().equals(si.name)) {
										o_temp = (TreeParent) xtree[zi];
										break;
									}
								}
								TreeObject[] deltree = o_temp.getChildren();
								boolean[] deltreechk = new boolean[deltree.length];
								TreeObject.FindReset(deltreechk);
								for (int vm = 0, vl = sinfo.size(); vm < vl; vm++) {
									child_si = (SchemaInfo) sinfo.get(vm);
									if (child_si.partitionGroupName
											.equals(si.name)) {
										TreeObject.FindName(deltree,
												deltreechk, child_si.name);
									}
								}
								TreeObject.FindRemove(o_temp, deltree,
										deltreechk);
							}
						}
					}

					if (addflag) {
						p_sysschema.addChild(o_stable);
						p_sysschema.addChild(o_sview);
						p_userschema.addChild(o_table);
						p_userschema.addChild(o_view);
						p_schema.addChild(p_sysschema);
						p_schema.addChild(p_userschema);
						db1.addChild(p_schema);
					}

					boolean trigadd = false;
					TreeParent o_trigger = (TreeParent) TreeObject.FindID(
							dbchild, null, DBTriggers.ID);
					if (o_trigger == null) {
						trigadd = true;
						o_trigger = new TreeParent(DBTriggers.ID, Messages
								.getString("TREE.TRIGGER"),
								"icons/trigger.png", null);
					}
					TreeObject[] trigtree = o_trigger.getChildren();
					boolean[] trigchk = new boolean[trigtree.length];
					TreeObject.FindReset(trigchk);
					ArrayList tginfo = Trigger.TriggerInfo_get(authrec.dbname);
					Trigger tg;
					for (int ai = 0, an = tginfo.size(); ai < an; ai++) {
						tg = (Trigger) tginfo.get(ai);
						TreeObject obj = TreeObject.FindName(trigtree, trigchk,
								tg.Name);
						if (obj == null)
							o_trigger.addChild(new TreeObject(DBTriggers.OBJ,
									tg.Name, "icons/trigger_obj.png",
									DBTriggers.ID));
					}
					TreeObject.FindRemove(o_trigger, trigtree, trigchk);
					if (trigadd)
						db1.addChild(o_trigger);

					boolean logsadd = false;
					TreeParent o_logs = (TreeParent) TreeObject.FindID(dbchild,
							null, DBLogs.ID);
					if (o_logs == null) {
						logsadd = true;
						o_logs = new TreeParent(DBLogs.ID, Messages
								.getString("TREE.LOGS"), "icons/dblogs.png",
								null);
					}
					TreeObject[] logstree = o_logs.getChildren();
					boolean[] logschk = new boolean[logstree.length];
					TreeObject.FindReset(logschk);
					ArrayList loginfo = LogFileInfo
							.DBLogInfo_get(authrec.dbname);
					LogFileInfo log;
					for (int ai = 0, an = loginfo.size(); ai < an; ai++) {
						log = (LogFileInfo) loginfo.get(ai);
						TreeObject obj = TreeObject.FindName(logstree, logschk,
								log.filename);
						if (obj == null)
							o_logs.addChild(new TreeObject(DBLogs.OBJ,
									log.filename, "icons/dblogs_obj.png",
									DBLogs.ID));
					}
					TreeObject.FindRemove(o_logs, logstree, logschk);
					if (logsadd)
						db1.addChild(o_logs);
				} // end authority exist
				if (TreeObject.FindName(hostchild, null, authrec.dbname) == null)
					host1.addChild(db1);
			} // end for
			TreeObject.FindRemove(host1, hostchild, chk_hostchild);
			if (rootchild.length <= 0)
				root.addChild(host1);
		} // end isconnected
		else {
			if (rootchild.length > 0)
				root.removeChild(rootchild[0]);
		}
		return root;
	}

	/**
	 * This is a callback that will allow us to create the viewer and initialize
	 * it.
	 */
	public void createPartControl(Composite parent) {
		viewer = new TreeViewer(parent, SWT.SINGLE | SWT.H_SCROLL
				| SWT.V_SCROLL | SWT.BORDER);
		viewer.setContentProvider(new ViewContentProvider());
		viewer.setLabelProvider(new ViewLabelProvider());
		viewer.setAutoExpandLevel(2);

		viewer.getTree().addMouseListener(
				new org.eclipse.swt.events.MouseAdapter() {
					public void mouseUp(org.eclipse.swt.events.MouseEvent e) {
						if (actMouseUp == true && selobj != null
								&& selobj.getViewID() != null
								&& isHaveAuth == true && e.button == 1)
							WorkView.SetView(selobj.getViewID(),
									selobj.getID(), null);
					}

					public void mouseDown(org.eclipse.swt.events.MouseEvent e) {
						String seldb = "";
						Point pt = new Point(e.x, e.y);

						actMouseUp = false;
						selitem = viewer.getTree().getItem(pt);
						if (selitem == null)
							return;
						IStructuredSelection selection = (IStructuredSelection) viewer
								.getSelection();
						selobj = (TreeObject) selection.getFirstElement();
						if (selobj == null)
							return;

						Current_select = selobj.getID();
						if (Current_select.equals(DatabaseListInHost.ID))
							seldb = "";
						else if (Current_select.equals(DatabaseStatus.ID)) {
							seldb = selobj.toString();
						} else if (selobj.getParent().getID().equals(
								DatabaseStatus.ID)) {
							seldb = selobj.getParent().toString();
						} else if (selobj.getParent().getParent().getID()
								.equals(DatabaseStatus.ID)) {
							seldb = selobj.getParent().getParent().toString();
						} else if (selobj.getParent().getParent().getParent()
								.getID().equals(DatabaseStatus.ID)) {
							seldb = selobj.getParent().getParent().getParent()
									.toString();
						} else if (selobj.getParent().getParent().getParent()
								.getParent().getID().equals(DatabaseStatus.ID)) {
							seldb = selobj.getParent().getParent().getParent()
									.getParent().toString();
						}
						else if (selobj.getParent().getParent().getParent()
								.getParent().getParent().getID().equals(
										DatabaseStatus.ID)) {
							seldb = selobj.getParent().getParent().getParent()
									.getParent().getParent().toString();
						}
						else
							seldb = "";

						if (oldobj != null
								&& selobj.equals(oldobj)
								&& (!selobj.getID().equals(DatabaseStatus.ID) || (selobj
										.getID().equals(DatabaseStatus.ID) && MainRegistry
										.Authinfo_find(seldb).setinfo == true)))
							return;

						oldobj = selobj;
						Current_db = seldb;
						GetDBInfo(Current_db);

						ApplicationActionBarAdvisor
								.AdjustToolbar(MainConstants.NAVI_CUBRID);
						ApplicationActionBarAdvisor
								.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);

						if (MainRegistry.Authinfo_find(Current_db) != null
								&& MainRegistry.Authinfo_find(Current_db).status == MainConstants.STATUS_STOP) {
							if (selobj.getID().equals(DBUsers.ID)
									|| selobj.getID().equals(DBUsers.RESERVED)
									|| selobj.getID().equals(DBUsers.USERS)
									|| selobj.getID().equals(DBSchema.ID)
									|| selobj.getID().equals(
											DBSchema.SYS_SCHEMA)
									|| selobj.getID()
											.equals(DBSchema.SYS_TABLE)
									|| selobj.getID().equals(DBSchema.SYS_VIEW)
									|| selobj.getID().equals(
											DBSchema.SYS_OBJECT)
									|| selobj.getID().equals(
											DBSchema.USER_SCHEMA)
									|| selobj.getID().equals(
											DBSchema.USER_TABLE)
									|| selobj.getID()
											.equals(DBSchema.USER_VIEW)
									|| selobj.getID().equals(
											DBSchema.USER_OBJECT)) {
								CommonTool.ErrorBox(Messages
										.getString("ERROR.SERVERNOTSTARTED"));
								return;
							}
						}

						if (Current_select.equals(DBUsers.ID)
								|| Current_select.equals(DBUsers.RESERVED)
								|| Current_select.equals(DBUsers.USERS)) {
							DBUsers.Current_select = selobj.getName();
						} else if (Current_select.equals(JobAutomation.BACKJOB)
								|| Current_select
										.equals(JobAutomation.QUERYJOB)) {
							JobAutomation.CurrentObj = selobj.getName();
						} else if (Current_select.equals(DBSpace.VOL_OBJECT)) {
							DBSpace.CurrentObj = selobj.getName();
							DBSpace.CurrentVolumeType = selobj.getParent()
									.getID();
						} else if (Current_select.equals(DBSchema.SYS_OBJECT)
								|| Current_select.equals(DBSchema.USER_OBJECT)) {
							DBSchema.CurrentObj = selobj.getName();
						} else if (Current_select.equals(DBTriggers.OBJ)) {
							DBTriggers.Current_select = selobj.getName();
						} else if (Current_select.equals(DBLogs.OBJ)) {
							DBLogs.Current_select = selobj.getName();
							LogViewAction.viewlist = LogFileInfo
									.DBLogInfo_get(Current_db);
							LogViewAction.viewitem = selobj.getName();
						}
						actMouseUp = true;
					}

					public void mouseDoubleClick(MouseEvent e) {
						Point pt = new Point(e.x, e.y);
						selitem = viewer.getTree().getItem(pt);
						if (selitem == null)
							return;
						IStructuredSelection selection = (IStructuredSelection) viewer
								.getSelection();
						selobj = (TreeObject) selection.getFirstElement();
						if (selobj == null)
							return;

						if (selobj instanceof TreeParent) {
							// viewer.setExpandedState(selobj,(viewer.getExpandedState(selobj)) ? false : true);
						} else {
							if (Current_select.equals(DBUsers.RESERVED)
									|| Current_select.equals(DBUsers.USERS)) {
								ApplicationActionBarAdvisor.userPropertyAction
										.run();
							} else if (Current_select
									.equals(JobAutomation.BACKJOB)) {
								ApplicationActionBarAdvisor.updateBackupPlanAction
										.run();
							} else if (Current_select
									.equals(JobAutomation.QUERYJOB)) {
								ApplicationActionBarAdvisor.updatequeryPlanAction
										.run();
							} else if (Current_select.equals(DBLogs.OBJ)) {
								ApplicationActionBarAdvisor.logViewAction.run();
							}
							// else if
							// (Current_select.equals(DBSchema.SYS_OBJECT) ||
							// Current_select.equals(DBSchema.USER_OBJECT) ||
							// Current_select.equals(DBSchema.PROXY_OBJECT)) {
							// WorkView.SetView(QueryEditor.ID,
							// MainRegistry.getDBUserInfo(Current_db),
							// selobj.getName() + ":SELECTALL");
							// }
						}
					}
				});

		viewer.getTree().addSelectionListener(
				new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(SelectionEvent e) {
						selobj = (TreeObject) ((IStructuredSelection) viewer
								.getSelection()).getFirstElement();
						selitem = viewer.getTree().getSelection()[0];
					}
				});

		viewer.addTreeListener(new ITreeViewerListener() {
			public void treeExpanded(TreeExpansionEvent e) {
				TreeObject tobj = (TreeObject) e.getElement();
				if (tobj.getID().equals(DatabaseStatus.ID)) {
					GetDBInfo(tobj.toString());
					viewer.setExpandedState(tobj, true);
				}
				viewer.setSelection(new StructuredSelection(tobj));
				selobj = tobj;
				Current_select = selobj.getID();

				if (!(oldobj != null && selobj.equals(oldobj)))
					oldobj = selobj;
			}

			public void treeCollapsed(TreeExpansionEvent e) {
				TreeObject tobj = (TreeObject) e.getElement();
				viewer.setSelection(new StructuredSelection(tobj));

				selobj = tobj;
				Current_select = selobj.getID();

				if (!(oldobj != null && selobj.equals(oldobj)))
					oldobj = selobj;
			}
		});
	}

	public void DrawTree() {
		viewer.setInput(createModel());

		hookContextMenu(viewer.getControl());

		MainRegistry.NaviDraw_CUBRID = true;
		setViewDefault();
	}

	public static void setViewDefault() {
		WorkView.SetView(DatabaseListInHost.ID, (String) null,
				DatabaseListInHost.ID);
	}

	public static void hookContextMenu(Control popctrl) {
		MenuManager menuMgr = new MenuManager("PopupMenu", "contextMenu");
		menuMgr.setRemoveAllWhenShown(true);
		menuMgr.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				ApplicationActionBarAdvisor.setActionsMenu(manager);
			}
		});
		Menu menu = menuMgr.createContextMenu(popctrl);
		MenuItem newContextMenuItem = new MenuItem(menu, SWT.NONE);
		newContextMenuItem.setText("context.item");

		popctrl.setMenu(menu);
		// getSite().registerContextMenu(menuMgr, treeViewer);
	}

	/**
	 * Passing the focus request to the viewer's control.
	 */
	public void setFocus() {
		if (!MainRegistry.NaviDraw_CUBRID)
			DrawTree();
		viewer.getControl().setFocus();
		if (MainRegistry.Current_Navigator != MainConstants.NAVI_CUBRID) {
			MainRegistry.Current_Navigator = MainConstants.NAVI_CUBRID;
			updateWorkView();
		}
		ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_CUBRID);
		ApplicationActionBarAdvisor
				.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
	}

	public static void refresh() {
		if (myNavi == null)
			return;
		viewer.setInput(myNavi.createModel());
		viewer.refresh();
		MainRegistry.Current_Navigator = MainConstants.NAVI_CUBRID;

		ArrayList newset = new ArrayList();
		newset.add(viewer.getTree().getItem(0));
		viewer.getTree().setSelection(
				(TreeItem[]) newset.toArray(new TreeItem[1]));
		Current_db = "";
		TreeObject treeobj = (TreeObject) viewer.getTree().getItem(0).getData();
		if (isHaveAuth == true)
			WorkView.SetView(treeobj.getViewID(), treeobj.getID(), null);
		ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_CUBRID);
		ApplicationActionBarAdvisor
				.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
		myNavi.updateWorkView();
	}

	public static TreeObject SelectDB(String dbname) {
		if (dbname == null || root == null)
			return null;
		TreeItem rootitem = viewer.getTree().getItem(0);
		TreeItem[] items = rootitem.getItems();
		for (int i = 0, n = items.length; i < n; i++) {
			TreeObject obj = (TreeObject) items[i].getData();
			if (obj.getID().equals(DatabaseStatus.ID)
					&& obj.getName().equals(dbname)) {
				ArrayList newset = new ArrayList();
				newset.add(items[i]);
				viewer.getTree().setSelection(
						(TreeItem[]) newset.toArray(new TreeItem[1]));
				Current_db = dbname;
				Current_select = DatabaseStatus.ID;
				myNavi.GetDBInfo(Current_db);
				ApplicationActionBarAdvisor
						.AdjustToolbar(MainConstants.NAVI_CUBRID);
				ApplicationActionBarAdvisor
						.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
				return obj;
			}
		}
		return null;
	}

	public void GetDBInfo(String dbname) {
		if (OnlyQueryEditor.connectOldServer)
			return;

		Shell psh = viewer.getControl().getShell();

		if (dbname == null || dbname.length() <= 0)
			return;
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return;
		if (ai.setinfo) { // already info set
			isHaveAuth = true;
			return;
		}
		isHaveAuth = false;

		if (ai.dbuser.length() <= 0) {
			if (logoutJob) {
				logoutJob = false;
				return;
			}
			if (MainRegistry.isProtegoBuild()) {
				DBUserInfo ui = MainRegistry.getDBUserInfo(dbname);
				if (ui == null) {
					ui = new DBUserInfo(dbname, "", "");
					MainRegistry.addDBUserInfo(ui);
				}

				ClientSocket cs = new ClientSocket();
				String msg = "targetid:" + MainRegistry.UserID + "\n";
				msg += "dbname:" + dbname + "\n";

				if (!cs.SendClientMessage(Application.mainwindow.getShell(),
						msg, "dbmtuserlogin")) {
					CommonTool.ErrorBox(Application.mainwindow.getShell(),
							cs.ErrorMsg);
					return;
				}

				ai.dbuser = ui.dbuser;
				ai.isDBAGroup = ui.isDBAGroup;
			} else {
				LoginDialog dlg = new LoginDialog(dbname,
						Application.mainwindow.getShell());
				if (!dlg.doModal()) {
					return;
				}
			}
		}
		isHaveAuth = true;
		ai.setinfo = true;
		MainRegistry.Authinfo_update(ai);

		// DB info retrieve
		String msg = "dbname:" + dbname;

		psh.setEnabled(false);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_WAIT));

		// generaldbinfo can modified using by dbspaceinfo.
		// BackgroundSocket bs_gdi=new BackgroundSocket(psh, msg, "generaldbinfo");
		// bs_gdi.start();
		// gettriggerinfo, dbspaceinfo, getlocaldbinfo is need "SA MODE"
		BackgroundSocket bs_ti = new BackgroundSocket(psh, msg,
				"gettriggerinfo");
		bs_ti.start();
		BackgroundSocket bs_ui = new BackgroundSocket(psh, msg, "userinfo");
		BackgroundSocket bs_ci = new BackgroundSocket(psh, msg
				+ "\ndbstatus:on", "classinfo");
		if (ai.status == MainConstants.STATUS_START) {
			bs_ui.start();
			bs_ci.start();
		} else {
			bs_ui.isrunning = false;
			bs_ci.isrunning = false;
			bs_ui.result = true;
			bs_ci.result = true;
		}
		BackgroundSocket bs_bi = new BackgroundSocket(psh, msg, "getbackupinfo");
		bs_bi.start();
		BackgroundSocket bs_aeq = new BackgroundSocket(psh, msg,
				"getautoexecquery");
		bs_aeq.start();
		BackgroundSocket bs_li = new BackgroundSocket(psh, msg, "getloginfo");
		bs_li.start();

		long fromtime, totime;
		fromtime = System.currentTimeMillis();
		String ErrMsg = null;
		while (true) {
			if (!bs_ui.isrunning && !bs_ci.isrunning && !bs_bi.isrunning
					&& !bs_aeq.isrunning && !bs_ti.isrunning
					&& !bs_li.isrunning)
				break;
			totime = System.currentTimeMillis();
			if ((totime - fromtime) > (20 * 1000)) {
				ErrMsg = Messages.getString("ERROR.DISCONNECTED");
				break;
			}
			try {
				Thread.sleep(100);
			} catch (Exception e) {
			}
		}
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(psh, msg, "dbspaceinfo")) {
			CommonTool.ErrorBox(psh, cs.ErrorMsg);
		}

		psh.setEnabled(true);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_ARROW));

		if (!bs_ui.result)
			ErrMsg = bs_ui.sc.ErrorMsg;
		else if (!bs_ci.result)
			ErrMsg = bs_ci.sc.ErrorMsg;
		else if (!bs_ci.result)
			ErrMsg = bs_ci.sc.ErrorMsg;
		else if (!bs_aeq.result)
			ErrMsg = bs_aeq.sc.ErrorMsg;
		else if (!bs_ti.result)
			ErrMsg = bs_ti.sc.ErrorMsg;
		else if (!bs_li.result)
			ErrMsg = bs_li.sc.ErrorMsg;
		if (ErrMsg != null) {
			CommonTool.ErrorBox(psh, ErrMsg);
		}

		createModel();
		viewer.refresh();
		updateWorkView();
	}

	public static void removeAll() {
		if (root != null)
			viewer.remove(root.getChildren());
	}

	public void updateWorkView() {
		if (MainRegistry.IsConnected) {
			if (oldobj != null && oldobj.getViewID() != null
					&& isHaveAuth == true && Current_db != null
					&& Current_db.length() > 0)
				WorkView.SetView(oldobj.getViewID(), oldobj.getID(), null);
			else
				setViewDefault();
		}
	}

	public void SelectDB_UpdateView(String dbname) {
		if (MainRegistry.IsConnected) {
			TreeObject dbObj = SelectDB(dbname);
			oldobj = dbObj;
			updateWorkView();
		}
	}

	public static TreeObject getSelobj() {
		return selobj;
	}
}
