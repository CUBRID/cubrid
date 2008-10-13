package cubridmanager.query.action;

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.Application;
import cubridmanager.query.dialog.QuerySampleDialog;

public class QuerySampleAction extends Action {
	// private final IWorkbenchWindow window;

	public QuerySampleAction(String text, IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("QuerySampleAction");
		setActionDefinitionId("QuerySampleAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/sample.png"));
		setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/disable_icons/sample.png"));
	}

	public void run() {
		QuerySampleDialog querySample = new QuerySampleDialog(
				Application.mainwindow.getShell());
		querySample.doModal();
	}

}
