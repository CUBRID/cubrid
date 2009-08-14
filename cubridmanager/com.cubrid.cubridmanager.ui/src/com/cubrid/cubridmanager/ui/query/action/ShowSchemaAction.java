package com.cubrid.cubridmanager.ui.query.action;

import org.apache.log4j.Logger;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IViewPart;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.query.editor.SchemaView;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.FocusAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * 
 * Schema Info View Action for a query editor and a query explain tool in a query
 * editor
 * 
 * ShowSchemaAction Description
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 06 created by pcraft
 */
public class ShowSchemaAction extends
		FocusAction {
	private static final Logger logger = LogUtil.getLogger(ShowSchemaAction.class);
	public static final String ID = ShowSchemaAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 */
	public ShowSchemaAction(Shell shell, String text) {
		this(shell, text, null);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public ShowSchemaAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 */
	protected ShowSchemaAction(Shell shell, Control provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
	}

	/**
	 * Override the run method in order to complete showing brokers status
	 * server to a broker
	 * 
	 */
	public void run() {
		
		// Wether the selected table is a exist object in the current tree.
		SchemaInfo schemaInfo = getSchemaInfoWithSelection();
		if (schemaInfo == null) {
			CommonTool.openErrorBox(Messages.QEDIT_SELECT_TABLE_NOT_EXIST_IN_DB);
			return;
		}
		
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (null == window) {
			return;
		}
		
		IWorkbenchPage activePage = window.getActivePage();
		IViewPart viewPart = activePage.findView(SchemaView.ID);

		if (null != viewPart) {
			activePage.hideView(viewPart);
		}
		try {
			activePage.showView(SchemaView.ID);
		} catch (PartInitException e) {
			logger.error(e.getMessage());
		}
	}

	/**
	 * Makes this action not support to select multi object
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#allowMultiSelections ()
	 */
	public boolean allowMultiSelections() {
		return false;
	}

	/**
	 * Return whether this action support this object,if not support,this action
	 * will be disabled
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		return true;
	}
	
	private SchemaInfo getSchemaInfoWithSelection() {
		
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (null == window) {
			return null;
		}

		IEditorPart editor = window.getActivePage().getActiveEditor();
		if (editor == null) {
			return null;
		}

		if (!(editor instanceof QueryEditorPart)) {
			return null;
		}

		QueryEditorPart queryEditorPart = (QueryEditorPart) editor;

		String tableName = queryEditorPart.getCurrentSchemaName();
		if (tableName == null) {
			return null;
		}

		CubridDatabase db = queryEditorPart.getSelectedDatabase();
		if (db == null || !db.isLogined()) {
			return null;
		}

		DatabaseInfo database = db.getDatabaseInfo();
		if (database == null) {
			return null;
		}

		SchemaInfo schemaInfo = null;
		try {
			schemaInfo = database.getSchemaInfo(tableName);			
		} catch (Exception ignored) {
			schemaInfo = null;
		}
		
		return schemaInfo;
		
	}
}
