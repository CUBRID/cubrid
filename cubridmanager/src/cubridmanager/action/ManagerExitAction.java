package cubridmanager.action;

import java.util.Properties;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;
import org.eclipse.ui.actions.ActionFactory.IWorkbenchAction;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;

public class ManagerExitAction extends Action {
	private final IWorkbenchWindow window;

	public ManagerExitAction(String text, IWorkbenchWindow window) {
		super(text);
		this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("ManagerExitAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/exit.png"));
	}

	public void run() {
		if (CommonTool.MsgBox(window.getShell(), SWT.ICON_WARNING | SWT.YES
				| SWT.NO, Messages.getString("MSG.WARNING"), //$NON-NLS-1$
				Messages.getString("MSG.QUIT_PROGRAM")) == SWT.YES) { //$NON-NLS-1$
			Properties prop = new Properties();
			if (CommonTool.LoadProperties(prop)) {
				if (Application.mainwindow.getShell().getMaximized())
					prop.setProperty(MainConstants.mainWindowMaximize, "yes");
				else
					prop.setProperty(MainConstants.mainWindowMaximize, "no");

				Application.mainwindow.getShell().setMaximized(false);

				prop.setProperty(MainConstants.mainWindowX,
						Integer.toString(Application.mainwindow.getShell()
								.getSize().x));
				prop.setProperty(MainConstants.mainWindowY,
						Integer.toString(Application.mainwindow.getShell()
								.getSize().y));

				if (MainRegistry.isProtegoBuild()) {
					if (MainRegistry.isMTLogin())
						prop.setProperty(MainConstants.protegoLoginType,
								MainConstants.protegoLoginTypeMtId);
					else
						prop.setProperty(MainConstants.protegoLoginType,
								MainConstants.protegoLoginTypeCert);
				}
				CommonTool.SaveProperties(prop);
			}

			IWorkbenchAction exitapp = ActionFactory.QUIT.create(window);
			exitapp.run();
		}
	}

}
