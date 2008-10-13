package cubridmanager.diag.view;

import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.TreeItem;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.viewers.IStructuredSelection;

import cubridmanager.*;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.view.CASLogs;
import cubridmanager.cubrid.LogFileInfo;
//import cubridmanager.diag.DiagActivityMonitorTemplate;
//import cubridmanager.diag.DiagDataActivityLog;
import cubridmanager.diag.DiagSiteDiagData;
import cubridmanager.diag.DiagStatusMonitorTemplate;

import java.util.ArrayList;

public class DiagView extends ViewPart {
	public static final String ID = "navigator.diagnose";
	public static String Current_job = Messages.getString("TREE.STATUSMONITOR");
	public static String Current_select = StatusTemplate.ID;
	public static DiagView myNavi = null;
	public static TreeExpandedState treeExpandedState = new TreeExpandedState();
	public static TreeViewer viewer = null;
	private static TreeParent root = null;
	public TreeObject oldobj = null;

	private static class TreeExpandedState {
		boolean bRoot = true;
		boolean bObjectMonitor = true;
		boolean bStatusMonitorTemplate = false;
		boolean bActivityProfile = true;
		boolean bActivityTemplate = false;
		boolean bActivityLog = false;
		boolean bCasLog = false;
		boolean bDiag = true;
		boolean bDiagTemplate = false;
		boolean bDiagReport = false;
		boolean bTroubleTrace = true;

		public TreeExpandedState() {
		}
	}

	public DiagView() {
		myNavi = this;
	}

	/**
	 * We will set up a dummy model to initialize tree heararchy. In real code,
	 * you will connect to a real model and expose its hierarchy.
	 */
	public TreeObject createModel() {
		if (OnlyQueryEditor.connectOldServer)
			return null;

		TreeParent root = new TreeParent(null, "root", null, null);
		if (MainRegistry.IsConnected) {
			/*
			 * if
			 * (MainRegistry.getSiteDiagDataByName(MainRegistry.GetCurrentSiteName()).ExecuteDiag ==
			 * false) return root;
			 */
			GetTemplateList();
			GetCASLogList();
			TreeParent host1 = new TreeParent(Diagnose.ID,
					MainRegistry.HostDesc, "icons/diag.png", Diagnose.ID);

			if (MainRegistry.DiagAuth != MainConstants.AUTH_NONE) {
				// TreeParent events = new TreeParent(Events.ID,
				// Messages.getString("TREE.OBJECT_STATUS_ACTIVITY"),
				// "icons/event.png", Events.ID);
				TreeParent status_template = new TreeParent(StatusTemplate.ID,
						Messages.getString("TREE.STATUSMONITOR"),
						"icons/event_template.png", StatusTemplate.ID);
				host1.addChild(status_template);
				// events.addChild(status_template);

				ArrayList statusTemplateList = null;
				for (int i = 0; i < MainRegistry.diagSiteDiagDataList.size(); i++) {
					if (((DiagSiteDiagData) (MainRegistry.diagSiteDiagDataList
							.get(i))).site_name == MainRegistry
							.GetCurrentSiteName())
						statusTemplateList = ((DiagSiteDiagData) (MainRegistry.diagSiteDiagDataList
								.get(i))).statusTemplateList;
				}

				for (int i = 0; statusTemplateList != null
						&& i < statusTemplateList.size(); i++) {
					TreeObject template = new TreeObject(StatusTemplate.ID,
							((DiagStatusMonitorTemplate) statusTemplateList
									.get(i)).templateName,
							"icons/event_template_obj.png", StatusTemplate.ID);

					status_template.addChild(template);
				}
				/*
				 * TreeObject status_warning = new TreeObject(StatusWarning.ID,
				 * Messages.getString("TREE.STATUSWARNING"),
				 * "icons/event_warning.png", StatusWarning.ID);
				 * events.addChild(status_warning);
				 * 
				 * ///////////////////////////// // Remove code for ACTIVITY PROFILE
				 * 
				 * TreeParent activity_profile = new
				 * TreeParent(ActivityTemplate.ID,
				 * Messages.getString("TREE.ACTIVITYMONITOR"),
				 * "icons/activity_template.png", ActivityTemplate.ID);
				 * events.addChild(activity_profile); TreeParent
				 * activity_template = new TreeParent(ActivityTemplate.ID,
				 * Messages.getString("TREE.ACTIVITYTEMPLATE"),"icons/activity_template.png",
				 * ActivityTemplate.ID); TreeParent activity_logs = new
				 * TreeParent(ActivityLogs.ID,
				 * Messages.getString("TREE.ACTIVITYLOGS"),
				 * "icons/activity_logs.png", ActivityLogs.ID); TreeParent
				 * cas_logs = new TreeParent(ActivityLogs.ID,
				 * Messages.getString("TREE.CASLOGS"),
				 * "icons/cas_script_log.png", ActivityLogs.ID);
				 * activity_profile.addChild(activity_template);
				 * activity_profile.addChild(activity_logs);
				 * activity_profile.addChild(cas_logs);
				 * 
				 * ArrayList activityTemplateList =
				 * ((DiagSiteDiagData)MainRegistry.diagSiteDiagDataList.get(0)).activityTemplateList;
				 * 
				 * for (int i=0 ; i<activityTemplateList.size(); i++) {
				 * TreeObject activitytemplate = new TreeObject(
				 * ActivityTemplate.ID ,
				 * ((DiagActivityMonitorTemplate)activityTemplateList.get(i)).templateName ,
				 * "icons/event_template.png" , ActivityTemplate.ID);
				 * 
				 * activity_template.addChild(activitytemplate); }
				 * 
				 * ArrayList activityLogList =
				 * ((DiagSiteDiagData)MainRegistry.diagSiteDiagDataList.get(0)).diagDataActivityLogList;
				 * 
				 * for (int i=0 ; i<activityLogList.size(); i++) { TreeObject
				 * activitylog = new TreeObject( ActivityLogs.ID ,
				 * ((DiagDataActivityLog)activityLogList.get(i)).name ,
				 * "icons/activity_logs.png" , ActivityTemplate.ID);
				 * 
				 * activity_logs.addChild(activitylog); }
				 */
				TreeParent cas_logs = new TreeParent(ActivityLogs.ID, Messages
						.getString("TREE.CASLOGS"), "icons/cas_script_log.png",
						ActivityLogs.ID);
				host1.addChild(cas_logs);
				/* add cas log list */
				CASItem casrec;
				for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
					casrec = (CASItem) MainRegistry.CASinfo.get(i);
					ArrayList loginfo = LogFileInfo
							.BrokerLog_get(casrec.broker_name);
					LogFileInfo log;
					for (int ai = 0, an = loginfo.size(); ai < an; ai++) {
						log = (LogFileInfo) loginfo.get(ai);
						if (log.type.equals("script")) {
							cas_logs
									.addChild(new TreeObject(CASLogs.SCRIPTOBJ,
											log.filename,
											"icons/cas_script_log_obj.png",
											CASLogs.ID));
						}
					}
				}

				// host1.addChild(events);
				/*
				 * TreeParent diag = new TreeParent(DiagSub.ID,
				 * Messages.getString("TREE.DIAGDIAG"), "icons/diagnose.png",
				 * DiagSub.ID); TreeParent diag_template = new
				 * TreeParent(DiagTemplate.ID,
				 * Messages.getString("TREE.DIAGDIAGTEMPLATE"),
				 * "icons/diagnose_template.png", DiagTemplate.ID);
				 * diag.addChild(diag_template); TreeObject diag_template_CUBRID =
				 * new TreeObject(DiagTemplate.ID, "CUBRID_diag_template",
				 * "icons/diagnose_template.png", DiagTemplate.ID); TreeObject
				 * diag_template_CAS = new TreeObject(DiagTemplate.ID,
				 * "CAS_diag_template", "icons/diagnose_template.png",
				 * DiagTemplate.ID);
				 * diag_template.addChild(diag_template_CUBRID);
				 * diag_template.addChild(diag_template_CAS);
				 * 
				 * TreeParent diag_report = new TreeParent(DiagReport.ID,
				 * Messages.getString("TREE.DIAGDIAGREPORT"),
				 * "icons/diagnose_report.png", DiagReport.ID); TreeObject
				 * diag_report_2006_04 = new TreeObject(DiagReport.ID,
				 * "200604_diag_report.drp", "icons/diagnose_report.png",
				 * DiagReport.ID); TreeObject diag_report_2006_05 = new
				 * TreeObject(DiagReport.ID, "200605_diag_report.drp",
				 * "icons/diagnose_report.png", DiagReport.ID);
				 * diag.addChild(diag_report);
				 * diag_report.addChild(diag_report_2006_04);
				 * diag_report.addChild(diag_report_2006_05);
				 * host1.addChild(diag);
				 * 
				 * TreeParent troubleTrace = new TreeParent(ServiceReport.ID,
				 * Messages.getString("TREE.TROUBLETRACE"), "icons/report.png",
				 * ServiceReport.ID); TreeObject troubleTrace_error_1 = new
				 * TreeObject(ServiceReport.ID, "subway.err(-100)",
				 * "icons/report.png", ServiceReport.ID); TreeObject
				 * troubleTrace_error_2 = new TreeObject(ServiceReport.ID,
				 * "subway.err(-110)", "icons/report.png", ServiceReport.ID);
				 * troubleTrace.addChild(troubleTrace_error_1);
				 * troubleTrace.addChild(troubleTrace_error_2);
				 * host1.addChild(troubleTrace);
				 */
			}
			root.addChild(host1);
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
		viewer.setAutoExpandLevel(3);

		viewer.addSelectionChangedListener(new ISelectionChangedListener() {
			public void selectionChanged(SelectionChangedEvent event) {
				if (event.getSelection().isEmpty()) {
					return;
				}
				if (event.getSelection() instanceof IStructuredSelection) {
					IStructuredSelection selection = (IStructuredSelection) event
							.getSelection();
					TreeObject selobj = (TreeObject) selection
							.getFirstElement();
					Current_select = selobj.getID();
					Current_job = selobj.toString();

					if (Current_job.equals(Messages
							.getString("TREE.STATUSMONITOR"))
							|| Current_job.equals(Messages
									.getString("TREE.OBJECT_STATUS_ACTIVITY"))
							|| Current_job.equals(Messages
									.getString("TREE.ACTIVITYMONITOR"))
							|| Current_job.equals(Messages
									.getString("TREE.ACTIVITYTEMPLATE"))
							|| Current_job.equals(Messages
									.getString("TREE.ACTIVITYLOGS"))
							|| Current_job.equals(Messages
									.getString("TREE.DIAGDIAGREPORT"))
							|| Current_job.equals(Messages
									.getString("TREE.DIAGDIAG"))
							|| Current_job.equals(MainRegistry.HostDesc)
							|| Current_job.equals(Messages
									.getString("TREE.TROUBLETRACE"))
							|| Current_job.equals(Messages
									.getString("TREE.CASLOGS")))
						; // empty statement!
					else {
						if (selobj.getParent().getName().equals(
								Messages.getString("TREE.CASLOGS"))) {
							/* broker name set */
							ArrayList info = MainRegistry.CASinfo;
							boolean find = false;
							for (int i = 0; i < info.size(); i++) {
								ArrayList loginfo = ((CASItem) info.get(i)).loginfo;
								for (int j = 0; j < loginfo.size(); j++) {
									if (((LogFileInfo) loginfo.get(j)).filename
											.equals(Current_job)) {
										find = true;
										MainRegistry.CASLogView_RequestBrokername = ((CASItem) info
												.get(i)).broker_name;
										MainRegistry.CASLogView_RequestedInDiag = true;
										break;
									}
								}
								if (find)
									break;
							}
						}
						WorkView.SetView(selobj.getViewID(), selobj.getName(),
								selobj.getID());
						oldobj = selobj;
						MainRegistry.CASLogView_RequestedInDiag = false;
					}
					ApplicationActionBarAdvisor
							.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
				}
			}
		});
	}

	public void DrawTree() {
		if (root != null) {
			viewer.remove(root);
		}
		viewer.setInput(createModel());

		MainRegistry.NaviDraw_DIAG = true;
		hookContextMenu(viewer);
	}

	private void hookContextMenu(TreeViewer treeViewer) {
		MenuManager menuMgr = new MenuManager("PopupMenu", "contextMenu");
		menuMgr.setRemoveAllWhenShown(true);
		menuMgr.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				ApplicationActionBarAdvisor.setActionsMenu(manager);
			}
		});
		Menu menu = menuMgr.createContextMenu(treeViewer.getControl());
		MenuItem newContextMenuItem = new MenuItem(menu, SWT.NONE);
		newContextMenuItem.setText("context.item");

		treeViewer.getControl().setMenu(menu);
		getSite().registerContextMenu(menuMgr, treeViewer);
	}

	/**
	 * Passing the focus request to the viewer's control.
	 */
	public void setFocus() {
		if (!MainRegistry.NaviDraw_DIAG)
			DrawTree();
		viewer.getControl().setFocus();
		if (MainRegistry.Current_Navigator != MainConstants.NAVI_DIAG) {
			MainRegistry.Current_Navigator = MainConstants.NAVI_DIAG;
			updateWorkView();
		}
		ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_DIAG);
		ApplicationActionBarAdvisor
				.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
	}

	public static void refresh() {
		if (myNavi == null)
			return;
		myNavi.saveExpandedState();
		viewer.setInput(myNavi.createModel());
		viewer.refresh();
		myNavi.restoreExpandedState();
		MainRegistry.Current_Navigator = MainConstants.NAVI_DIAG;
		ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_DIAG);
		ApplicationActionBarAdvisor
				.setActionsMenu(ApplicationActionBarAdvisor.actionMenu);
	}

	public void GetTemplateList() {
		MainRegistry.SetCurrentSiteName(MainRegistry.HostDesc);

		// Set template through message to data structures
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(Application.mainwindow.getShell(), "",
				"getstatustemplate")) {
			CommonTool.ErrorBox(cs.ErrorMsg);
		}

		/*
		 * //ACTIVITY PROFILE if
		 * (!cs.SendClientMessage(Application.mainwindow.getShell(), "",
		 * "getactivitytemplate" )) { CommonTool.ErrorBox(cs.ErrorMsg); }
		 */
	}

	public void GetCASLogList() {
		if (MainRegistry.CASinfo.size() == 0) {
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(Application.mainwindow.getShell(), "",
					"getbrokersinfo")) {
				// CommonTool.ErrorBox(cs.ErrorMsg);
			}
		}

		for (int i = 0; i < MainRegistry.CASinfo.size(); i++) {
			CASItem item = (CASItem) MainRegistry.CASinfo.get(i);
			String bname = item.broker_name;

			ClientSocket cs3 = new ClientSocket();
			if (!cs3.SendClientMessage(Application.mainwindow.getShell(),
					"broker:" + bname + "\n", "getlogfileinfo")) {
				// CommonTool.ErrorBox(cs.ErrorMsg);
			}
		}
	}

	public void saveExpandedState() {
		Tree tree = viewer.getTree();
		TreeItem root = tree.getItem(0);
		treeExpandedState.bRoot = root.getExpanded();
		for (int i = 0; i < root.getItemCount(); i++) {
			TreeItem currentItem = root.getItem(i);
			if (currentItem.getText().equals(
					Messages.getString("TREE.STATUSMONITOR")))
				treeExpandedState.bStatusMonitorTemplate = currentItem
						.getExpanded();
			else if (currentItem.getText().equals(
					Messages.getString("TREE.CASLOGS")))
				treeExpandedState.bCasLog = currentItem.getExpanded();
		}

		viewer.setAutoExpandLevel(0);
	}

	public void restoreExpandedState() {
		Tree tree = viewer.getTree();
		TreeItem root = tree.getItem(0);
		viewer.expandToLevel(root, 1);

		TreeObject[] level1 = ((TreeParent) root.getData()).getChildren();

		for (int i = 0; i < 2; i++) {
			if (level1[i].getName().equals(
					Messages.getString("TREE.STATUSMONITOR"))) {
				if ((treeExpandedState.bStatusMonitorTemplate)
						&& ((TreeParent) level1[i]).hasChildren())
					viewer.expandToLevel(level1[i], 1);
			} else if (level1[i].getName().equals(
					Messages.getString("TREE.CASLOGS"))) {
				if ((treeExpandedState.bCasLog)
						&& ((TreeParent) level1[i]).hasChildren())
					viewer.expandToLevel(level1[i], 1);
			}
		}
	}

	public static void removeAll() {
		if (root != null)
			viewer.remove(root.getChildren());
	}

	public void updateWorkView() {
		if ((MainRegistry.IsConnected) && (oldobj != null)) {
			if ((oldobj.getParent() != null)
					&& oldobj.getParent().getName().equals(
							Messages.getString("TREE.CASLOGS"))) {
				ArrayList info = MainRegistry.CASinfo;
				boolean find = false;
				for (int i = 0; i < info.size(); i++) {
					ArrayList loginfo = ((CASItem) info.get(i)).loginfo;
					for (int j = 0; j < loginfo.size(); j++) {
						if (((LogFileInfo) loginfo.get(j)).filename
								.equals(Current_job)) {
							find = true;
							MainRegistry.CASLogView_RequestBrokername = ((CASItem) info
									.get(i)).broker_name;
							MainRegistry.CASLogView_RequestedInDiag = true;
							break;
						}
					}
					if (find)
						break;
				}

				if (!find)
					return;

				WorkView.SetView(oldobj.getViewID(), oldobj.getName(), oldobj
						.getID());
				MainRegistry.CASLogView_RequestedInDiag = false;
				return;
			}
			WorkView.SetView(oldobj.getViewID(), oldobj.getName(), oldobj
					.getID());
		}
	}
}
