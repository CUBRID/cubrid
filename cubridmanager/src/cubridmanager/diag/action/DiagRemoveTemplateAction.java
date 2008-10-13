package cubridmanager.diag.action;

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.diag.view.DiagView;

public class DiagRemoveTemplateAction extends Action {
	public String templateName = "";
	public String templateType = "";
	// private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagRemoveTemplate";

	public DiagRemoveTemplateAction(String text, String tType,
			IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
		templateType = tType;
	}

	public DiagRemoveTemplateAction(String text, String tType, String tName,
			IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		setId(ID);
		setActionDefinitionId(ID);

		templateType = tType;
		templateName = tName;
	}

	public void run() {
		String msg;
		ClientSocket cs = new ClientSocket();
		msg = "name:";
		msg += templateName;
		msg += "\n";
		if (templateType == "status") {
			if (!cs.SendBackGround(Application.mainwindow.getShell(), msg,
					"removestatustemplate", "Removing status template")) {
				CommonTool.ErrorBox(cs.ErrorMsg);
			} else
				DiagView.refresh();
		} else if (templateType == "activity") {
			if (!cs.SendBackGround(Application.mainwindow.getShell(), msg,
					"removeactivitytemplate", "Removing activity template")) {
				CommonTool.ErrorBox(cs.ErrorMsg);
			} else
				DiagView.refresh();
		}
	}
}
