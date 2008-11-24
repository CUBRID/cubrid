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

import java.sql.SQLException;

import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.ui.IFolderLayout;
import org.eclipse.ui.IPageLayout;
import org.eclipse.ui.IViewLayout;
import org.eclipse.ui.IViewPart;
import org.eclipse.ui.IViewReference;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;

import cubridmanager.cas.view.CASLogs;
import cubridmanager.cas.view.CASView;
import cubridmanager.cas.view.BrokerJob;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.cubrid.view.JobAutomation;
import cubridmanager.cubrid.view.DBSpace;
import cubridmanager.diag.view.ActivityLogs;
import cubridmanager.diag.view.ActivityTemplate;
import cubridmanager.diag.view.DiagReport;
import cubridmanager.diag.view.DiagTemplate;
import cubridmanager.diag.view.DiagView;
import cubridmanager.diag.view.ServiceReport;
import cubridmanager.diag.view.StatusTemplate;
import cubridmanager.query.action.QueryEditorConnection;
import cubridmanager.query.view.QueryEditor;

public class WorkView extends ViewPart {
	public static final String ID = "cubridmanager.workview";
	public static IWorkbenchWindow workwindow = null;
	public static String EditorSequence = null;
	public static String EditorCommand = null;
	public static String EditorDBName = null;
	public static QueryEditorConnection connector;

	WorkView() {
		super();
		// TODO Auto-generated constructor stub
	}

	public void createPartControl(Composite parent) {

	}

	public void setFocus() {
	}

	public static void SetNavigator(IPageLayout layout) {
		layout.setEditorAreaVisible(false);

		IFolderLayout folder = layout.createFolder("basefolder",
				IPageLayout.LEFT, 0.25f, IPageLayout.ID_EDITOR_AREA);
		folder.addPlaceholder("navigator.*");
		folder.addView(CubridView.ID);
		folder.addView(CASView.ID);
		folder.addView(DiagView.ID);
		IViewLayout viewLayout = layout.getViewLayout(CubridView.ID);
		viewLayout.setCloseable(false);
		viewLayout.setMoveable(false);
		viewLayout = layout.getViewLayout(CASView.ID);
		viewLayout.setCloseable(false);
		viewLayout.setMoveable(false);
		viewLayout = layout.getViewLayout(DiagView.ID);
		viewLayout.setCloseable(false);
		viewLayout.setMoveable(false);

		IFolderLayout wfolder = layout.createFolder(MainConstants.WORK_FOLDER,
				IPageLayout.BOTTOM, 0.75f, IPageLayout.ID_EDITOR_AREA);
		wfolder.addPlaceholder("workview.*:query*");

		workwindow = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
	}

	public static void TopView(String viewid) {
		IViewPart orgview = workwindow.getActivePage().findView(viewid);
		try {
			if (orgview != null)
				workwindow.getActivePage().bringToTop(orgview);
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}
	}

	public static void DeleteView(String viewid) {
		IViewPart orgview = workwindow.getActivePage().findView(viewid);
		try {
			if (orgview != null)
				workwindow.getActivePage().hideView(orgview);
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}
	}

	public static void ShowView(String viewid) {
		IViewPart orgview = workwindow.getActivePage().findView(viewid);
		try {
			if (orgview != null)
				workwindow.getActivePage().showView(viewid);
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}
	}

	public static void RefreshView(String viewid) {
		IViewPart orgview = workwindow.getActivePage().findView(viewid);
		if (orgview != null) {
			workwindow.getActivePage().hideView(orgview);
		}
		try {
			workwindow.getActivePage().showView(viewid, null,
					IWorkbenchPage.VIEW_ACTIVATE);
		} catch (PartInitException e) {
			CommonTool.debugPrint(e);
		}
	}

	public static void SetView(String viewid, String Selected, String Subcmd) {
		if (workwindow == null || viewid == null)
			return;
		try {
			if (viewid.equals(DBSpace.ID)) {
				DBSpace.CurrentSelect = Selected;
			} else if (viewid.equals(JobAutomation.ID)) {
				JobAutomation.Current_select = Selected;
			} else if (viewid.equals(DBSchema.ID)) {
				DBSchema.CurrentSelect = Selected;
			} else if (viewid.equals(CASLogs.ID)) {
				CASLogs.CurrentSelect = Subcmd;
				CASLogs.CurrentText = Selected;
			} else if (viewid.equals(StatusTemplate.ID)) {
				StatusTemplate.CurrentSelect = Subcmd;
				StatusTemplate.CurrentText = Selected;
			} else if (viewid.equals(ActivityTemplate.ID)) {
				ActivityTemplate.CurrentSelect = Subcmd;
				ActivityTemplate.CurrentText = Selected;
			} else if (viewid.equals(ActivityLogs.ID)) {
				ActivityLogs.CurrentSelect = Subcmd;
				ActivityLogs.CurrentText = Selected;
			} else if (viewid.equals(DiagTemplate.ID)) {
				DiagTemplate.CurrentSelect = Subcmd;
				DiagTemplate.CurrentText = Selected;
			} else if (viewid.equals(DiagReport.ID)) {
				DiagReport.CurrentSelect = Subcmd;
				DiagReport.CurrentText = Selected;
			} else if (viewid.equals(ServiceReport.ID)) {
				ServiceReport.CurrentSelect = Subcmd;
				ServiceReport.CurrentText = Selected;
			} else if (viewid.equals(BrokerJob.ID)) {
				BrokerJob.Current_broker = Selected;
			}

			IViewReference[] actviews = workwindow.getActivePage()
					.getViewReferences();
			for (int i = 0, n = actviews.length; i < n; i++) {
				if (!actviews[i].getId().equals(CASView.ID)
						&& !actviews[i].getId().equals(CubridView.ID)
						&& !actviews[i].getId().equals(QueryEditor.ID)
						&& !actviews[i].getId().equals(DiagView.ID)) {
					try {
						workwindow.getActivePage().hideView(actviews[i]);
					} catch (Exception e) {
						;
					}
				}
			}
			workwindow.getActivePage().showView(viewid, "query0",
					IWorkbenchPage.VIEW_ACTIVATE);
		} catch (PartInitException e) {
			CommonTool.debugPrint(e);
		}
	}

	public static void SetView(String viewid, DBUserInfo ui, String Subcmd) {
		if (workwindow == null || viewid == null)
			return;
		try {
			if (viewid.equals(QueryEditor.ID)) {
				EditorCommand = Subcmd;
				IViewReference[] actviews = workwindow.getActivePage()
						.getViewReferences();

				EditorSequence = "0";
				int maxwnds = -1;

				try {
					connector = new QueryEditorConnection(ui);
				} catch (SQLException e) {
					CommonTool.debugPrint(e);
					return;
				} catch (ClassNotFoundException e) {
					CommonTool.debugPrint(e);
					return;
				}

				for (int i = 0, n = actviews.length; i < n; i++) {
					if (actviews[i].getId().equals(QueryEditor.ID)) {
						String secondid = actviews[i].getSecondaryId();
						if (secondid != null) {
							int wndnum = CommonTool.atoi(secondid.substring(5));
							if (wndnum > maxwnds)
								maxwnds = wndnum;
						}
					}
				}
				workwindow.getActivePage().showView(viewid,
						"query" + (maxwnds + 1), IWorkbenchPage.VIEW_ACTIVATE);
				EditorSequence = Integer.toString(maxwnds + 1);
			}
		} catch (PartInitException e) {
			CommonTool.debugPrint(e);
		}
	}

	public static void DeleteViewAll() {
		if (workwindow == null)
			return;

		IViewReference[] actviews = workwindow.getActivePage()
				.getViewReferences();
		for (int i = 0, n = actviews.length; i < n; i++) {
			if (!actviews[i].getId().equals(CASView.ID)
					&& !actviews[i].getId().equals(CubridView.ID)
					&& !actviews[i].getId().equals(DiagView.ID))
				workwindow.getActivePage().hideView(actviews[i]);
		}
	}
}
