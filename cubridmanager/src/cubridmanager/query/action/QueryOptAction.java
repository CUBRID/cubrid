package cubridmanager.query.action;

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.Application;
import cubridmanager.query.dialog.QueryEditorOptionDialog;

public class QueryOptAction extends Action {
	// private final IWorkbenchWindow window;

	public QueryOptAction(String text, IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("QueryOptAction");
		setActionDefinitionId("QueryOptAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/queryopt.png"));
		setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/disable_icons/queryopt.png"));
	}

	public void run() {
		QueryEditorOptionDialog dlg = new QueryEditorOptionDialog(
				Application.mainwindow.getShell());
		dlg.doModal();
	}
}
