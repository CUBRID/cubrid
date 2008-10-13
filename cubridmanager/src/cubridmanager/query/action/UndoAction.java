package cubridmanager.query.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;

import cubridmanager.MainRegistry;
import cubridmanager.query.view.QueryEditor;

public class UndoAction extends Action {
	public UndoAction(String text, String img) {
		super(text + "\tCtrl+Z");
		// The id is used to refer to the action in a menu or toolbar
		setId("UndoAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText("TOOLTIP." + text);
		setAccelerator(SWT.CTRL + 'Z');
	}

	public void run() {
		QueryEditor qe = MainRegistry.getCurrentQueryEditor();
		if (qe == null)
			return;
		qe.undo();
	}
}
