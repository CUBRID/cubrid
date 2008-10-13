package cubridmanager;

import java.util.ArrayList;

import org.eclipse.jface.action.GroupMarker;
import org.eclipse.jface.action.ICoolBarManager;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.action.ToolBarContributionItem;
import org.eclipse.jface.action.ToolBarManager;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.swt.SWT;
import org.eclipse.ui.IWorkbenchActionConstants;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.application.ActionBarAdvisor;
import org.eclipse.ui.application.IActionBarConfigurer;

import cubridmanager.action.AboutAction;
import cubridmanager.action.CertificateLoginAction;
import cubridmanager.action.ConnectAction;
import cubridmanager.action.DisconnectAction;
import cubridmanager.action.HelpAction;
import cubridmanager.action.IdPasswordLoginAction;
import cubridmanager.action.ManagerExitAction;
import cubridmanager.action.ManagerLogAction;
import cubridmanager.action.ProtegoAPIDAddAction;
import cubridmanager.action.ProtegoAPIDChangeAction;
import cubridmanager.action.ProtegoAPIDDeleteAction;
import cubridmanager.action.ProtegoMTUserAddAction;
import cubridmanager.action.ProtegoMTUserChangeAction;
import cubridmanager.action.ProtegoMTUserDeleteAction;
import cubridmanager.action.ProtegoUserAddAction;
import cubridmanager.action.ProtegoUserChangeAction;
import cubridmanager.action.ProtegoUserDeleteAction;
import cubridmanager.action.ProtegoUserManagementAction;
import cubridmanager.action.ProtegoUserManagementRefreshAction;
import cubridmanager.action.ProtegoUserSaveAsFileAction;
import cubridmanager.action.RefreshAction;
import cubridmanager.action.RefreshIntervalAction;
import cubridmanager.action.RemoveLogAction;
import cubridmanager.action.StartServerAction;
import cubridmanager.action.StopServerAction;
import cubridmanager.action.UserManagementAction;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.action.CASVersionAction;
import cubridmanager.cas.action.DeleteBrokerAction;
import cubridmanager.cas.action.ResetCASAdminLogAction;
import cubridmanager.cas.action.RestartAPServerAction;
import cubridmanager.cas.action.SetParameterAction;
import cubridmanager.cas.action.StartBrokerAction;
import cubridmanager.cas.action.StopBrokerAction;
import cubridmanager.cas.view.BrokerJob;
import cubridmanager.cas.view.BrokerList;
import cubridmanager.cas.view.BrokerStatus;
import cubridmanager.cas.view.CASLogs;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.cubrid.action.AddVolumeAction;
import cubridmanager.cubrid.action.AddedVolumeLogAction;
import cubridmanager.cubrid.action.AlterTriggerAction;
import cubridmanager.cubrid.action.AutoBackupErrorLogAction;
import cubridmanager.cubrid.action.BackupAction;
import cubridmanager.cubrid.action.BackupPlanAction;
import cubridmanager.cubrid.action.CheckAction;
import cubridmanager.cubrid.action.CompactAction;
import cubridmanager.cubrid.action.CopyAction;
import cubridmanager.cubrid.action.CreateAction;
import cubridmanager.cubrid.action.CreateNewUserAction;
import cubridmanager.cubrid.action.DBLogout;
import cubridmanager.cubrid.action.DeleteAction;
import cubridmanager.cubrid.action.DeleteBackupPlanAction;
import cubridmanager.cubrid.action.DeleteQueryPlanAction;
import cubridmanager.cubrid.action.DeleteUserAction;
import cubridmanager.cubrid.action.DropTriggerAction;
import cubridmanager.cubrid.action.LoadAction;
import cubridmanager.cubrid.action.LockinfoAction;
import cubridmanager.cubrid.action.LogViewAction;
import cubridmanager.cubrid.action.NewClassAction;
import cubridmanager.cubrid.action.NewTriggerAction;
import cubridmanager.cubrid.action.OptimizeAction;
import cubridmanager.cubrid.action.QueryPlanAction;
import cubridmanager.cubrid.action.RenameAction;
import cubridmanager.cubrid.action.RestoreAction;
import cubridmanager.cubrid.action.ServerVersionAction;
import cubridmanager.cubrid.action.SetAutoAddVolumeAction;
import cubridmanager.cubrid.action.SqlxinitAction;
import cubridmanager.cubrid.action.TableDeleteAction;
import cubridmanager.cubrid.action.TableDropAction;
import cubridmanager.cubrid.action.TableExportAction;
import cubridmanager.cubrid.action.TableImportAction;
import cubridmanager.cubrid.action.TableInsertAction;
import cubridmanager.cubrid.action.TablePropertyAction;
import cubridmanager.cubrid.action.TableRenameAction;
import cubridmanager.cubrid.action.TableSelectAllAction;
import cubridmanager.cubrid.action.TableSelectCountAction;
import cubridmanager.cubrid.action.TraninfoAction;
import cubridmanager.cubrid.action.UnloadAction;
import cubridmanager.cubrid.action.UpdateBackupPlanAction;
import cubridmanager.cubrid.action.UpdateQueryPlanAction;
import cubridmanager.cubrid.action.UserPropertyAction;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBLogs;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.cubrid.view.DBSpace;
import cubridmanager.cubrid.view.DBTriggers;
import cubridmanager.cubrid.view.DBUsers;
import cubridmanager.cubrid.view.DatabaseListInHost;
import cubridmanager.cubrid.view.DatabaseStatus;
import cubridmanager.cubrid.view.JobAutomation;
import cubridmanager.diag.action.DiagActivityAnalyzeCASLogAction;
import cubridmanager.diag.action.DiagActivityCASLogRunAction;
import cubridmanager.diag.action.DiagActivityImportCASLogAction;
import cubridmanager.diag.action.DiagActivityLogAnalyzeLogAction;
import cubridmanager.diag.action.DiagActivityLogDisplayLogAction;
import cubridmanager.diag.action.DiagActivityLogRemoveLogAction;
import cubridmanager.diag.action.DiagActivityLogSetLoggingTimeAction;
import cubridmanager.diag.action.DiagActivityMonitorAction;
import cubridmanager.diag.action.DiagActivityUpdateTemplateAction;
import cubridmanager.diag.action.DiagDiagReportAction;
import cubridmanager.diag.action.DiagDiagReportSetStartTimeAction;
import cubridmanager.diag.action.DiagDiagReportUpdateTemplateAction;
import cubridmanager.diag.action.DiagNewUserTroubleAction;
import cubridmanager.diag.action.DiagRemoveDiagReportAction;
import cubridmanager.diag.action.DiagRemoveStatusWarningAction;
import cubridmanager.diag.action.DiagRemoveTemplateAction;
import cubridmanager.diag.action.DiagRemoveTroubleAction;
import cubridmanager.diag.action.DiagStartStatusWarningAction;
import cubridmanager.diag.action.DiagStatusLogAnalyzeLogAction;
import cubridmanager.diag.action.DiagStatusLogDisplayLogAction;
import cubridmanager.diag.action.DiagStatusLogRemoveLogAction;
import cubridmanager.diag.action.DiagStatusMonitorAction;
import cubridmanager.diag.action.DiagStatusUpdateTemplateAction;
import cubridmanager.diag.action.DiagStatusWarningAction;
import cubridmanager.diag.action.DiagStatusWarningLogViewAction;
import cubridmanager.diag.action.DiagStopStatusWarningAction;
import cubridmanager.diag.action.DiagTroubleTraceAction;
import cubridmanager.diag.action.DiagViewDiagReportAction;
import cubridmanager.diag.view.ActivityLogs;
import cubridmanager.diag.view.ActivityTemplate;
import cubridmanager.diag.view.DiagReport;
import cubridmanager.diag.view.DiagTemplate;
import cubridmanager.diag.view.DiagView;
import cubridmanager.diag.view.LogAnalyze;
import cubridmanager.diag.view.ServiceReport;
import cubridmanager.diag.view.StatusTemplate;
import cubridmanager.diag.view.StatusWarning;
import cubridmanager.query.action.CommentAddAction;
import cubridmanager.query.action.CommentDeleteAction;
import cubridmanager.query.action.CopyClipboardAction;
import cubridmanager.query.action.CutAction;
import cubridmanager.query.action.FindAction;
import cubridmanager.query.action.FindNextAction;
import cubridmanager.query.action.OidNaviAction;
import cubridmanager.query.action.OpenQueryAction;
import cubridmanager.query.action.PasteAction;
import cubridmanager.query.action.QueryEditAction;
import cubridmanager.query.action.QueryOptAction;
import cubridmanager.query.action.QuerySampleAction;
import cubridmanager.query.action.RedoAction;
import cubridmanager.query.action.ReplaceAction;
import cubridmanager.query.action.SaveAsQueryAction;
import cubridmanager.query.action.SaveQueryAction;
import cubridmanager.query.action.ScriptRunAction;
import cubridmanager.query.action.TabAddAction;
import cubridmanager.query.action.TabDeleteAction;
import cubridmanager.query.action.UndoAction;

/**
 * An action bar advisor is responsible for creating, adding, and disposing of
 * the actions added to a workbench window. Each window will be populated with
 * new actions.
 */
public class ApplicationActionBarAdvisor extends ActionBarAdvisor {

	// Actions - important to allocate these only in makeActions, and then use
	// them
	// in the fill methods. This ensures that the actions aren't recreated
	// when fillActionBars is called with FILL_PROXY.
	public static AuthItem Current_auth = null;
	// private IWorkbenchAction aboutAction;
	private AboutAction aboutAction;
	private QuerySampleAction querySampleAction;
	private HelpAction helpAction;
	private static RefreshAction refreshActionOnToolbar;
	private static RefreshIntervalAction refreshIntervalActionOnToolbar;
	private static ServerVersionAction serverVersionActionOnToolbar;
	private static CASVersionAction cASVersionActionOnToolbar;
	public static StartServerAction startServerActionOnToolbar;
	public static StopServerAction stopServerActionOnToolbar;
	private static CreateAction createActionOnToolbar;
	private static DeleteAction deleteActionOnToolbar;
	private static LoadAction loadActionOnToolbar;
	private static UnloadAction unloadActionOnToolbar;
	private static BackupAction backupActionOnToolbar;
	private static RestoreAction restoreActionOnToolbar;
	private static RenameAction renameActionOnToolbar;
	private static CopyAction copyActionOnToolbar;
	private static OptimizeAction optimizeActionOnToolbar;
	private static CompactAction compactActionOnToolbar;
	private static CheckAction checkActionOnToolbar;
	private static LockinfoAction lockinfoActionOnToolbar;
	private static TraninfoAction traninfoActionOnToolbar;
	public static DBLogout dBLogoutAction;
	public static QueryEditAction queryEditActionOnToolbar;
	public static QueryEditAction queryEditActionOnPopup;
	public static RefreshAction refreshAction;
	public static CreateAction createAction;
	public static SqlxinitAction sqlxinitAction;
	public static ServerVersionAction serverVersionAction;
	public static DeleteAction deleteAction;
	public static StartServerAction startServerAction;
	public static StopServerAction stopServerAction;
	public static LoadAction loadAction;
	public static UnloadAction unloadAction;
	public static BackupAction backupAction;
	public static RestoreAction restoreAction;
	public static RenameAction renameAction;
	public static CopyAction copyAction;
	public static OptimizeAction optimizeAction;
	public static CompactAction compactAction;
	public static CheckAction checkAction;
	public static LockinfoAction lockinfoAction;
	public static TraninfoAction traninfoAction;
	public static RefreshIntervalAction refreshIntervalAction;
	public static CASVersionAction cASVersionAction;
	// public static DBServerPropertyAction dBServerPropertyAction;
	// public static DownloadFilesAction downloadFilesAction;
	public static CreateNewUserAction createNewUserAction;
	public static DeleteUserAction deleteUserAction;
	public static UserPropertyAction userPropertyAction;
	public static AddedVolumeLogAction addedVolumeLogAction;
	public static AddVolumeAction addVolumeAction;
	public static AutoBackupErrorLogAction autoBackupErrorLogAction;
	public static BackupPlanAction backupPlanAction;
	public static UpdateBackupPlanAction updateBackupPlanAction;
	public static DeleteBackupPlanAction deleteBackupPlanAction;
	// public static DBStatusMonitoringAction dBStatusMonitoringAction;
	// public static JobPriorityAction jobPriorityAction;
	public static NewClassAction newClassAction;
	public static NewClassAction newViewAction;
	public static NewTriggerAction newTriggerAction;
	public static AlterTriggerAction alterTriggerAction;
	public static DropTriggerAction dropTriggerAction;
	public static QueryPlanAction queryPlanAction;
	public static UpdateQueryPlanAction updatequeryPlanAction;
	public static DeleteQueryPlanAction deletequeryPlanAction;
	public static RemoveLogAction removeAllErrLogAction;
	public static RemoveLogAction removeErrLogAction;
	public static RemoveLogAction removeAllAccessLogAction;
	public static RemoveLogAction removeAllScriptLogAction;
	public static RemoveLogAction removeLogAction;
	public static RemoveLogAction removeScriptLogAction;
	public static ResetCASAdminLogAction resetCASAdminLogAction;
	public static RestartAPServerAction restartAPServerAction;
	public static SetAutoAddVolumeAction setAutoAddVolumeAction;
	public static SetParameterAction setParameterAction;
	// public static SetParameterUsingChangerAction
	// setParameterUsingChangerAction;
	public static StartBrokerAction startBrokerAction;
	public static StopBrokerAction stopBrokerAction;
	// public static SuspendBrokerAction suspendBrokerAction;
	// public static ResumeBrokerAction resumeBrokerAction;
	public static DeleteBrokerAction deleteBrokerAction;
	public static LogViewAction logViewAction;
	
	// diagnostic action
	public static DiagStatusMonitorAction diagStatusMonitorAction;
	public static DiagStatusMonitorAction diagDefaultStatusMonitorAction;
	public static DiagStatusUpdateTemplateAction diagNewStatusMonitorTemplateAction;
	public static DiagStatusUpdateTemplateAction diagUpdateStatusMonitorTemplateAction;
	public static DiagRemoveTemplateAction diagRemoveStatusTemplateAction;
	public static DiagStatusLogDisplayLogAction diagStatusLogDisplayLogAction;
	public static DiagStatusLogAnalyzeLogAction diagStatusLogAnalyzeLogAction;
	public static DiagStatusLogRemoveLogAction diagStatusLogRemoveLogAction;
	public static DiagActivityMonitorAction diagActivityMonitorAction;
	public static DiagActivityMonitorAction diagDefaultActivityMonitorAction;
	public static DiagActivityUpdateTemplateAction diagNewActivityMonitorTemplateAction;
	public static DiagActivityUpdateTemplateAction diagUpdateActivityTemplateAction;
	public static DiagRemoveTemplateAction diagRemoveActivityTemplateAction;
	public static DiagActivityLogDisplayLogAction diagActivityLogDisplayLogAction;
	public static DiagActivityLogAnalyzeLogAction diagActivityLogAnalyzeLogAction;
	public static DiagActivityLogRemoveLogAction diagActivityLogRemoveLogAction;
	public static DiagActivityLogSetLoggingTimeAction diagActivityLogSetLoggingTimeAction;
	public static DiagActivityImportCASLogAction diagActivityImportCASLogAction;
	public static DiagActivityAnalyzeCASLogAction diagActivityAnalyzeCASLogAction;
	public static DiagActivityCASLogRunAction diagActivityCASLogRunAction;
	// public static DiagStatusWarningAction diagNewStatusWarningAction;
	public static DiagStatusWarningAction diagUpdateStatusWarningAction;
	public static DiagRemoveStatusWarningAction diagRemoveStatusWarningAction;
	public static DiagStartStatusWarningAction diagStartStatusWarningAction;
	public static DiagStopStatusWarningAction diagStopStatusWarningAction;
	public static DiagStatusWarningLogViewAction diagStatusWarningLogViewAction;
	public static DiagDiagReportAction diagRunDiagReportAction;
	public static DiagDiagReportAction diagDefaultDiagReportAction;
	public static DiagDiagReportUpdateTemplateAction diagNewDiagReportTemplateAction;
	public static DiagDiagReportUpdateTemplateAction diagUpdateDiagReportTemplateAction;
	public static DiagRemoveTemplateAction diagRemoveDiagReportTemplateAction;
	public static DiagRemoveDiagReportAction diagRemoveDiagReportAction;
	public static DiagViewDiagReportAction diagViewDiagReportAction;
	public static DiagDiagReportSetStartTimeAction diagDiagReportSetStartTimeAction;
	// public static DiagRemoveLogAnalysisReportAction
	// diagRemoveLogAnalysisReportAction;
	public static DiagTroubleTraceAction diagTroubleTraceAction;
	public static DiagNewUserTroubleAction diagNewUserTroubleAction;
	public static DiagRemoveTroubleAction diagRemoveTroubleAction;
	public static TableDropAction tableDropAction;
	public static TableInsertAction tableInsertAction;
	public static TableRenameAction tableRenameAction;
	public static TableSelectCountAction tableSelectCountAction;
	public static TableDeleteAction tableDeleteAction;
	public static TableImportAction tableImportAction;
	public static TableExportAction tableExportAction;
	public static TablePropertyAction tablePropertyAction;
	public static TableSelectAllAction tableSelectAllAction;
	public static String actionString = null;

	// File
	public static ConnectAction connectAction;
	public static DisconnectAction disconnectAction;
	public static QueryEditAction newQueryAction;
	public static OpenQueryAction openQueryAction;
	public static SaveQueryAction saveQueryAction;
	public static SaveAsQueryAction saveAsQueryAction;
	private ManagerExitAction managerExitAction;

	// Edit
	public static UndoAction undoAction;
	public static RedoAction redoAction;
	public static CopyClipboardAction copyClipboardAction;
	public static CutAction cutAction;
	public static PasteAction pasteAction;
	private static FindAction findAction;
	private static FindNextAction findNextAction;
	private static ReplaceAction replaceAction;
	private static CommentAddAction commentAddAction;
	private static CommentDeleteAction commentDeleteAction;
	private static TabAddAction tabAddAction;
	private static TabDeleteAction tabDeleteAction;

	// Tools
	public static ScriptRunAction scriptRunAction;
	public static OidNaviAction oidNaviAction;
	private static ManagerLogAction managerLogAction;
	private static UserManagementAction userManagementAction;
	public static QueryOptAction queryOptAction;
	public static ProtegoUserManagementAction protegoUserManagementAction;

	// Action
	public static MenuManager actionMenu;

	// Option
	public static CertificateLoginAction certificateLoginAction;
	public static IdPasswordLoginAction idPasswordLoginAction;

	// protego User Management Action
	public static ProtegoUserAddAction protegoUserAddAction;
	public static ProtegoUserAddAction protegoUserBulkAddAction;
	public static ProtegoUserSaveAsFileAction protegoUserSaveAsFileAction;
	public static ProtegoUserDeleteAction protegoUserDeleteAction;
	public static ProtegoUserChangeAction protegoUserChangeAction;
	public static ProtegoMTUserAddAction protegoMTUserAddAction;
	public static ProtegoMTUserDeleteAction protegoMTUserDeleteAction;
	public static ProtegoMTUserChangeAction protegoMTUserChangeAction;
	public static ProtegoAPIDAddAction protegoAPIDAddAction;
	public static ProtegoAPIDDeleteAction protegoAPIDDeleteAction;
	public static ProtegoAPIDChangeAction protegoAPIDChangeAction;
	public static ProtegoUserManagementRefreshAction protegoUserManagementRefreshAction;

	public ApplicationActionBarAdvisor(IActionBarConfigurer configurer) {
		super(configurer);
	}

	protected void makeActions(final IWorkbenchWindow window) {
		// Creates the actions and registers them.
		// Registering is needed to ensure that key bindings work.
		// The corresponding commands keybindings are defined in the plugin.xml
		// file.
		// Registering also provides automatic disposal of the actions when
		// the window is closed.

		// exitAction = ActionFactory.QUIT.create(window);
		// register(exitAction);
		Application.mainwindow = window;
		// aboutAction = ActionFactory.ABOUT.create(window);
		aboutAction = new AboutAction(window);
		register(aboutAction);

		queryEditActionOnToolbar = new QueryEditAction(Messages
				.getString("MENU.QUERYEDIT"), "icons/queryedit.png", true);
		register(queryEditActionOnToolbar);
		queryEditActionOnPopup = new QueryEditAction(Messages
				.getString("MENU.QUERYEDIT"), "icons/queryedit_16.png", false);
		register(queryEditActionOnPopup);

		querySampleAction = new QuerySampleAction(Messages
				.getString("MENU.QUERYSAMPLE"), window);
		register(querySampleAction);
		helpAction = new HelpAction(Messages.getString("MENU.HELP"), window);
		register(helpAction);

		// diagnostic menu
		diagDefaultStatusMonitorAction = new DiagStatusMonitorAction(Messages
				.getString("MENU.DIAGDEFAULTSTATUSMONITOR"), "DefaultTemplate",
				window);
		register(diagDefaultStatusMonitorAction);
		diagDefaultActivityMonitorAction = new DiagActivityMonitorAction(
				Messages.getString("MENU.DIAGDEFAULTACTIVITYMONITOR"), window);
		register(diagDefaultActivityMonitorAction);
		diagDefaultDiagReportAction = new DiagDiagReportAction(Messages
				.getString("MENU.DIAGDEFAULTDIAGREPORT"), "DefaultTemplate", 0,
				window);
		register(diagDefaultDiagReportAction);

		// 24 pixel icon
		refreshActionOnToolbar = new RefreshAction(Messages
				.getString("TOOL.REFRESH"), "icons/refresh.png");
		refreshIntervalActionOnToolbar = new RefreshIntervalAction(Messages
				.getString("TOOL.REFRESHINTERVAL"),
				"icons/refresh_interval.png");
		serverVersionActionOnToolbar = new ServerVersionAction(Messages
				.getString("TOOL.SERVERVERSION"), "icons/version.png");
		cASVersionActionOnToolbar = new CASVersionAction(Messages
				.getString("TOOL.CASVERSION"), "icons/cas_version.png");
		startServerActionOnToolbar = new StartServerAction(Messages
				.getString("TOOL.STARTSERVER"), "icons/start.png");
		stopServerActionOnToolbar = new StopServerAction(Messages
				.getString("TOOL.STOPSERVER"), "icons/stop.png");
		createActionOnToolbar = new CreateAction(Messages
				.getString("TOOL.CREATE"), "icons/createnew.png");
		deleteActionOnToolbar = new DeleteAction(Messages
				.getString("TOOL.DELETE"), "icons/delete.png");
		loadActionOnToolbar = new LoadAction(Messages.getString("TOOL.LOAD"),
				"icons/load.png");
		unloadActionOnToolbar = new UnloadAction(Messages
				.getString("TOOL.UNLOAD"), "icons/unload.png");
		backupActionOnToolbar = new BackupAction(Messages
				.getString("TOOL.BACKUP"), "icons/backup.png");
		restoreActionOnToolbar = new RestoreAction(Messages
				.getString("TOOL.RESTORE"), "icons/restore.png");
		renameActionOnToolbar = new RenameAction(Messages
				.getString("TOOL.RENAME"), "icons/rename.png");
		copyActionOnToolbar = new CopyAction(Messages.getString("TOOL.COPY"),
				"icons/copy.png");
		optimizeActionOnToolbar = new OptimizeAction(Messages
				.getString("TOOL.OPTIMIZE"), "icons/optimize.png");
		compactActionOnToolbar = new CompactAction(Messages
				.getString("TOOL.COMPACT"), "icons/compact.png");
		checkActionOnToolbar = new CheckAction(
				Messages.getString("TOOL.CHECK"), "icons/checkdb.png");
		lockinfoActionOnToolbar = new LockinfoAction(Messages
				.getString("TOOL.LOCKINFO"), "icons/lockinfo.png");
		traninfoActionOnToolbar = new TraninfoAction(Messages
				.getString("TOOL.TRANINFO"), "icons/traninfo.png");

		sqlxinitAction = new SqlxinitAction(
				Messages.getString("TOOL.SQLXINIT"), "icons/sqlxinit.png");
		// dBServerPropertyAction = new
		// DBServerPropertyAction(Messages.getString("TOOL.DBSERVERPROPERTY"),
		// "icons/server_property.png");
		// downloadFilesAction = new
		// DownloadFilesAction(Messages.getString("TOOL.DOWNLOADFILES"),
		// "icons/download_files.png");
		createNewUserAction = new CreateNewUserAction(Messages
				.getString("TOOL.CREATENEWUSER"), "icons/user_add.png");
		deleteUserAction = new DeleteUserAction(Messages
				.getString("TOOL.DELETEUSERACTION"), null);
		userPropertyAction = new UserPropertyAction(Messages
				.getString("TOOL.USERPROPERTYACTION"), null);
		logViewAction = new LogViewAction(Messages
				.getString("TOOL.LOGVIEWACTION")
				+ "...", null);

		// 16 pixel icon
		refreshAction = new RefreshAction(Messages.getString("TOOL.REFRESH"),
				"icons/refresh_16.png");
		refreshIntervalAction = new RefreshIntervalAction(Messages
				.getString("TOOL.REFRESHINTERVAL")
				+ "...", "icons/refresh_interval_16.png");
		serverVersionAction = new ServerVersionAction(Messages
				.getString("TOOL.SERVERVERSION"), "icons/version_16.png");
		cASVersionAction = new CASVersionAction(Messages
				.getString("TOOL.CASVERSION"), "icons/cas_version_16.png");

		dBLogoutAction = new DBLogout(Messages.getString("TOOL.DBLOGOUT"), null);
		startServerAction = new StartServerAction(Messages
				.getString("TOOL.STARTSERVER"), "icons/start_16.png");
		stopServerAction = new StopServerAction(Messages
				.getString("TOOL.STOPSERVER"), "icons/stop_16.png");
		createAction = new CreateAction(Messages.getString("TOOL.CREATE"),
				"icons/createnew_16.png");
		deleteAction = new DeleteAction(Messages.getString("TOOL.DELETE"),
				"icons/delete_16.png");

		loadAction = new LoadAction(Messages.getString("TOOL.LOAD"),
				"icons/load_16.png");
		unloadAction = new UnloadAction(Messages.getString("TOOL.UNLOAD"),
				"icons/unload_16.png");
		backupAction = new BackupAction(Messages.getString("TOOL.BACKUP"),
				"icons/backup_16.png");
		restoreAction = new RestoreAction(Messages.getString("TOOL.RESTORE"),
				"icons/restore_16.png");
		renameAction = new RenameAction(Messages.getString("TOOL.RENAME"),
				"icons/rename_16.png");
		copyAction = new CopyAction(Messages.getString("TOOL.COPY"),
				"icons/copy_16.png");
		optimizeAction = new OptimizeAction(
				Messages.getString("TOOL.OPTIMIZE"), "icons/optimize_16.png");
		compactAction = new CompactAction(Messages.getString("TOOL.COMPACT"),
				"icons/compact_16.png");
		checkAction = new CheckAction(Messages.getString("TOOL.CHECK"),
				"icons/checkdb_16.png");

		lockinfoAction = new LockinfoAction(
				Messages.getString("TOOL.LOCKINFO"), "icons/lockinfo_16.png");
		traninfoAction = new TraninfoAction(
				Messages.getString("TOOL.TRANINFO"), "icons/traninfo_16.png");

		addedVolumeLogAction = new AddedVolumeLogAction(Messages
				.getString("TOOL.ADDEDVOLUMELOGACTION"), null);
		addVolumeAction = new AddVolumeAction(Messages
				.getString("TOOL.ADDVOLUMEACTION"), null);
		autoBackupErrorLogAction = new AutoBackupErrorLogAction(Messages
				.getString("TOOL.AUTOBACKUPERRORLOGACTION"), null);
		backupPlanAction = new BackupPlanAction(Messages
				.getString("TOOL.BACKUPPLANACTION"), null);
		updateBackupPlanAction = new UpdateBackupPlanAction(Messages
				.getString("TOOL.UPDATEBACKUPPLANACTION"), null);
		deleteBackupPlanAction = new DeleteBackupPlanAction(Messages
				.getString("TOOL.DELETEBACKUPPLANACTION"), null);
		// dBStatusMonitoringAction = new DBStatusMonitoringAction
		// (Messages.getString("TOOL.DBSTATUSMONITORINGACTION"), null);
		// jobPriorityAction = new JobPriorityAction
		// (Messages.getString("TOOL.JOBPRIORITYACTION")+"...", null);
		newClassAction = new NewClassAction(Messages
				.getString("TOOL.NEWCLASSACTION"), null);
		newViewAction = new NewClassAction(Messages
				.getString("TOOL.NEWVIEWACTION"), null);
		newTriggerAction = new NewTriggerAction(Messages
				.getString("TOOL.NEWTRIGGERACTION"), null);
		dropTriggerAction = new DropTriggerAction(Messages
				.getString("TOOL.DROPTRIGGERACTION"), null);
		alterTriggerAction = new AlterTriggerAction(Messages
				.getString("TOOL.ALTERTRIGGERACTION"), null);
		queryPlanAction = new QueryPlanAction(Messages
				.getString("TOOL.QUERYPLANACTION"), null);
		updatequeryPlanAction = new UpdateQueryPlanAction(Messages
				.getString("TOOL.UPDATEQUERYPLANACTION"), null);
		deletequeryPlanAction = new DeleteQueryPlanAction(Messages
				.getString("TOOL.DELETEQUERYPLANACTION"), null);
		resetCASAdminLogAction = new ResetCASAdminLogAction(Messages
				.getString("TOOL.RESETCASADMINLOGACTION"), null);
		removeAllErrLogAction = new RemoveLogAction(Messages
				.getString("TOOL.REMOVEALLERRLOG"), null);
		removeErrLogAction = new RemoveLogAction(Messages
				.getString("TOOL.REMOVEERRLOG"), null);
		removeAllAccessLogAction = new RemoveLogAction(Messages
				.getString("TOOL.REMOVEALLACCESSLOG"), null);
		removeAllScriptLogAction = new RemoveLogAction(Messages
				.getString("TOOL.REMOVEALLSCRIPTLOG"), null);
		removeLogAction = new RemoveLogAction(Messages
				.getString("TOOL.REMOVELOG"), null);
		removeScriptLogAction = new RemoveLogAction(Messages
				.getString("TOOL.REMOVESCRIPTLOG"), null);
		restartAPServerAction = new RestartAPServerAction(Messages
				.getString("TOOL.RESTARTAPSERVERACTION")
				+ "...", null);
		setAutoAddVolumeAction = new SetAutoAddVolumeAction(Messages
				.getString("TOOL.SETAUTOADDVOLUMEACTION"), null);
		setParameterAction = new SetParameterAction(Messages
				.getString("TOOL.SETPARAMETERACTION")
				+ "...", null);
		// setParameterUsingChangerAction = new SetParameterUsingChangerAction
		// (Messages.getString("TOOL.SETPARAMETERUSINGCHANGERACTION")+"...",
		// null);
		startBrokerAction = new StartBrokerAction(Messages
				.getString("TOOL.STARTBROKERACTION"), null);
		stopBrokerAction = new StopBrokerAction(Messages
				.getString("TOOL.STOPBROKERACTION"), null);
		// suspendBrokerAction = new SuspendBrokerAction
		// (Messages.getString("TOOL.SUSPENDBROKERACTION"), null);
		// resumeBrokerAction = new ResumeBrokerAction
		// (Messages.getString("TOOL.RESUMEBROKERACTION"), null);
		deleteBrokerAction = new DeleteBrokerAction(Messages
				.getString("TOOL.DELETEBROKERACTION"), null);

		// diagnostic action
		diagStatusMonitorAction = new DiagStatusMonitorAction(Messages
				.getString("TITLE.DIAGSTATUSMONITOR")
				+ "...", "", window);
		diagNewStatusMonitorTemplateAction = new DiagStatusUpdateTemplateAction(
				Messages.getString("TOOL.DIAGSTATUS_NEWTEMPLATE") + "...",
				window);
		diagUpdateStatusMonitorTemplateAction = new DiagStatusUpdateTemplateAction(
				Messages.getString("TOOL.DIAG_UPDATETEMPLATE") + "...", window);
		diagRemoveStatusTemplateAction = new DiagRemoveTemplateAction(Messages
				.getString("TOOL.DIAGREMOVETEMPLATE"), "status", window);

		diagStatusLogDisplayLogAction = new DiagStatusLogDisplayLogAction(
				Messages.getString("TREE.DIAGSTATUSLOG_DISPLAY"),
				"c:\\CUBRIDMANAGER\\logs\\logName", window);
		diagStatusLogAnalyzeLogAction = new DiagStatusLogAnalyzeLogAction(
				Messages.getString("TREE.DIAGSTATUSLOG_ANALYZE"),
				"c:\\CUBRIDMANAGER\\logs\\logName", window);
		diagStatusLogRemoveLogAction = new DiagStatusLogRemoveLogAction(
				Messages.getString("TREE.DIAGSTATUSLOG_REMOVE"),
				"c:\\CUBRIDMANAGER\\logs\\logName", window);

		diagActivityMonitorAction = new DiagActivityMonitorAction(Messages
				.getString("TOOL.DIAGACTIVITYMONITOR"), window);
		diagNewActivityMonitorTemplateAction = new DiagActivityUpdateTemplateAction(
				Messages.getString("TOOL.DIAGACTIVITY_NEWTEMPLATE"), window);
		diagUpdateActivityTemplateAction = new DiagActivityUpdateTemplateAction(
				Messages.getString("TOOL.DIAG_UPDATETEMPLATE"), window);
		diagRemoveActivityTemplateAction = new DiagRemoveTemplateAction(
				Messages.getString("TOOL.DIAGREMOVETEMPLATE"), "activity",
				window);

		diagActivityLogDisplayLogAction = new DiagActivityLogDisplayLogAction(
				Messages.getString("TREE.DIAGACTIVITYLOG_DISPLAY"), window);
		diagActivityLogAnalyzeLogAction = new DiagActivityLogAnalyzeLogAction(
				Messages.getString("TREE.DIAGACTIVITYLOG_ANALYZE"), "", window);
		diagActivityLogRemoveLogAction = new DiagActivityLogRemoveLogAction(
				Messages.getString("TREE.DIAGACTIVITYLOG_REMOVE"), "", window);
		diagActivityLogSetLoggingTimeAction = new DiagActivityLogSetLoggingTimeAction(
				Messages.getString("TOOL.DIAGSETLOGSTARTTIME"), window);
		diagActivityImportCASLogAction = new DiagActivityImportCASLogAction(
				Messages.getString("TOOL.DIAGACTIVITY_IMPORTCASLOG"), window);
		diagActivityAnalyzeCASLogAction = new DiagActivityAnalyzeCASLogAction(
				Messages.getString("TOOL.DIAGACTIVITY_CASLOGANAL"), window);
		diagActivityCASLogRunAction = new DiagActivityCASLogRunAction(Messages
				.getString("TOOL.DIAGACTIVITY_CASLOGRERUN"), window);

		// diagNewStatusWarningAction = new
		// DiagStatusWarningAction(Messages.getString("TREE.DIAGNEWSTATUSWARNING"),
		// window);
		diagUpdateStatusWarningAction = new DiagStatusWarningAction(Messages
				.getString("TREE.DIAGUPDATESTATUSWARNING"), window);
		diagRemoveStatusWarningAction = new DiagRemoveStatusWarningAction(
				Messages.getString("TREE.DIAGREMOVESTATUSWARNING"),
				"warningname1", window);
		diagStartStatusWarningAction = new DiagStartStatusWarningAction(
				Messages.getString("TREE.DIAGSTARTSTATUSWARNING"),
				"warningname1", window);
		diagStopStatusWarningAction = new DiagStopStatusWarningAction(Messages
				.getString("TREE.DIAGSTOPSTATUSWARNING"), "warningname1",
				window);
		diagStatusWarningLogViewAction = new DiagStatusWarningLogViewAction(
				Messages.getString("TOOL.DIAGLOG_DISPLAY"), window);

		diagRunDiagReportAction = new DiagDiagReportAction(Messages
				.getString("TREE.DIAGDIAGREPORT"), window);
		diagViewDiagReportAction = new DiagViewDiagReportAction(Messages
				.getString("TOOL.DIAGVIEWDIAGREPORT"), window);
		// diagRemoveLogAnalysisReportAction = new
		// DiagRemoveLogAnalysisReportAction(Messages.getString("TREE.DIAGREMOVELOGANALYSISREPORT"),
		// "logAnalysisReport", window);

		diagNewDiagReportTemplateAction = new DiagDiagReportUpdateTemplateAction(
				Messages.getString("TREE.DIAGDIAGREPORT_NEWTEMPLATE"), window);
		diagUpdateDiagReportTemplateAction = new DiagDiagReportUpdateTemplateAction(
				Messages.getString("TOOL.DIAG_UPDATETEMPLATE"), window);
		diagRemoveDiagReportTemplateAction = new DiagRemoveTemplateAction(
				Messages.getString("TREE.DIAGREMOVETEMPLATE"), "diagReport",
				window);
		diagRemoveDiagReportAction = new DiagRemoveDiagReportAction(Messages
				.getString("TREE.DIAGREMOVEDIAGREPORT"), "diagreport.rpt",
				window);
		diagDiagReportSetStartTimeAction = new DiagDiagReportSetStartTimeAction(
				Messages.getString("TOOL.DIAGSETDIAGSTARTTIME"), window);
		diagTroubleTraceAction = new DiagTroubleTraceAction(Messages
				.getString("TOOL.DIAGMAKEPACKAGE"), window);
		diagNewUserTroubleAction = new DiagNewUserTroubleAction(Messages
				.getString("TREE.DIAGNEWUSERTROUBLE"), window);
		diagRemoveTroubleAction = new DiagRemoveTroubleAction(Messages
				.getString("TREE.DIAGREMOVETROUBLEACTION"), window);

		tableDropAction = new TableDropAction(Messages
				.getString("TOOL.TABLEDROPACTION"), null);
		tableInsertAction = new TableInsertAction(Messages
				.getString("TOOL.TABLEINSERTACTION"), null);
		tableRenameAction = new TableRenameAction(Messages
				.getString("TOOL.TABLERENAMEACTION"), null);
		tableSelectCountAction = new TableSelectCountAction(Messages
				.getString("TOOL.TABLESELECTCOUNTACTION"), null);
		tableDeleteAction = new TableDeleteAction(Messages
				.getString("TOOL.TABLEDELETEACTION"), null);
		tableImportAction = new TableImportAction(Messages
				.getString("TOOL.TABLEIMPORTACTION"),
				"src/cubridmanager/image/QueryEditor/qe_import.png");
		tableExportAction = new TableExportAction(Messages
				.getString("TOOL.TABLEEXPORTACTION"), null);
		tablePropertyAction = new TablePropertyAction(Messages
				.getString("TOOL.TABLEPROPERTYACTION"), null);
		tableSelectAllAction = new TableSelectAllAction(Messages
				.getString("TOOL.TABLESELECTALLACTION"), null);

		// actions for protego
		protegoUserAddAction = new ProtegoUserAddAction(Messages
				.getString("MENU.ADDAUTHINFO"));
		protegoUserBulkAddAction = new ProtegoUserAddAction(Messages
				.getString("MENU.READFROMFILE"), true);
		protegoUserSaveAsFileAction = new ProtegoUserSaveAsFileAction(Messages
				.getString("BUTTON.SAVETOFILE"));
		protegoUserDeleteAction = new ProtegoUserDeleteAction(Messages
				.getString("MENU.REMOVE"));
		protegoUserChangeAction = new ProtegoUserChangeAction(Messages
				.getString("MENU.CHANGE"));
		protegoMTUserAddAction = new ProtegoMTUserAddAction(Messages
				.getString("MENU.ADD"));
		protegoMTUserDeleteAction = new ProtegoMTUserDeleteAction(Messages
				.getString("MENU.REMOVE"));
		protegoMTUserChangeAction = new ProtegoMTUserChangeAction(Messages
				.getString("MENU.CHANGE"));
		protegoAPIDAddAction = new ProtegoAPIDAddAction(Messages
				.getString("MENU.ADD"));
		protegoAPIDDeleteAction = new ProtegoAPIDDeleteAction(Messages
				.getString("MENU.REMOVE"));
		protegoAPIDChangeAction = new ProtegoAPIDChangeAction(Messages
				.getString("MENU.CHANGE"));
		protegoUserManagementRefreshAction = new ProtegoUserManagementRefreshAction(
				Messages.getString("BUTTON.REFRESH"));
		certificateLoginAction = new CertificateLoginAction("Certificate login", window);
		idPasswordLoginAction = new IdPasswordLoginAction("Maintain ID login", window);

		// File
		connectAction = new ConnectAction(Messages.getString("MENU.DOCONNECT"),
				window);
		register(connectAction);
		disconnectAction = new DisconnectAction(Messages
				.getString("MENU.DISCONNECT"), window);
		register(disconnectAction);

		register(newQueryAction = new QueryEditAction(Messages
				.getString("MENU.NEWQUERY"), "icons/queryedit_16.png", false));
		register(openQueryAction = new OpenQueryAction(Messages
				.getString("MENU.OPENQUERY"),
				"src/cubridmanager/image/QueryEditor/qe_open.png"));
		register(saveQueryAction = new SaveQueryAction(Messages
				.getString("MENU.SAVEQUERY"),
				"src/cubridmanager/image/QueryEditor/qe_save.png"));
		register(saveAsQueryAction = new SaveAsQueryAction(Messages
				.getString("MENU.SAVEASQUERY"),
				"src/cubridmanager/image/QueryEditor/qe_saveas.png"));

		managerExitAction = new ManagerExitAction(Messages
				.getString("MENU.MANAGEREXIT"), window);
		register(managerExitAction);

		// Edit
		register(undoAction = new UndoAction(Messages.getString("QEDIT.UNDO"),
				"src/cubridmanager/image/QueryEditor/qe_undo.png"));
		register(redoAction = new RedoAction(Messages.getString("QEDIT.REDO"),
				"src/cubridmanager/image/QueryEditor/qe_redo.png"));

		register(copyClipboardAction = new CopyClipboardAction(Messages
				.getString("QEDIT.COPY"),
				"src/cubridmanager/image/QueryEditor/qe_copy.png"));
		register(cutAction = new CutAction(Messages.getString("QEDIT.CUT"),
				"src/cubridmanager/image/QueryEditor/qe_cut.png"));
		register(pasteAction = new PasteAction(Messages
				.getString("QEDIT.PASTE"),
				"src/cubridmanager/image/QueryEditor/qe_paste.png"));

		register(findAction = new FindAction(Messages.getString("QEDIT.FIND"),
				"src/cubridmanager/image/QueryEditor/qe_find.png"));
		register(findNextAction = new FindNextAction(Messages
				.getString("QEDIT.FINDNEXT"),
				"src/cubridmanager/image/QueryEditor/qe_findnext.png"));
		register(replaceAction = new ReplaceAction(Messages
				.getString("QEDIT.REPLACE"),
				"src/cubridmanager/image/QueryEditor/qe_replace.png"));

		register(commentAddAction = new CommentAddAction(Messages
				.getString("QEDIT.COMMENT"),
				"src/cubridmanager/image/QueryEditor/qe_comment_input.png"));
		register(commentDeleteAction = new CommentDeleteAction(Messages
				.getString("QEDIT.UNCOMMENT"),
				"src/cubridmanager/image/QueryEditor/qe_comment_delete.png"));

		register(tabAddAction = new TabAddAction(Messages
				.getString("QEDIT.INDENT"),
				"src/cubridmanager/image/QueryEditor/qe_indent_remove.png"));
		register(tabDeleteAction = new TabDeleteAction(Messages
				.getString("QEDIT.UNINDENT"),
				"src/cubridmanager/image/QueryEditor/qe_indent.png"));

		// Tools
		scriptRunAction = new ScriptRunAction(Messages
				.getString("MENU.EXECUTESCRIPT"),
				"src/cubridmanager/image/QueryEditor/qe_script_go.png");
		oidNaviAction = new OidNaviAction(Messages.getString("MENU.OIDNAVI"),
				"src/cubridmanager/image/QueryEditor/qe_oid_navi.png");
		managerLogAction = new ManagerLogAction(Messages
				.getString("MENU.MANAGERLOG"), window);
		register(managerLogAction);
		userManagementAction = new UserManagementAction(Messages
				.getString("MENU.USERADMIN"), window);
		protegoUserManagementAction = new ProtegoUserManagementAction(Messages
				.getString("TITLE.PROTEGOUSERMANAGEMENT"),
				"src/cubridmanager/image/QueryEditor/qe_indent.png");

		if (!MainRegistry.isProtegoBuild()) {
			register(userManagementAction);
		} else {
			if (MainRegistry.isCertLogin()) {
				register(protegoUserManagementAction);
			}
		}

		queryOptAction = new QueryOptAction(
				Messages.getString("MENU.QUERYOPT"), window);
		register(queryOptAction);
	}

	protected void fillMenuBar(IMenuManager menuBar) {
		MenuManager fileMenu = new MenuManager(Messages.getString("MENU.FILE"),
				Messages.getString("MENU.FILE"));
		MenuManager editMenu = new MenuManager(Messages.getString("MENU.EDIT"),
				Messages.getString("MENU.EDIT"));
		MenuManager viewMenu = new MenuManager(Messages.getString("MENU.VIEW"),
				Messages.getString("MENU.VIEW"));
		MenuManager toolMenu = new MenuManager(Messages.getString("MENU.TOOL"),
				Messages.getString("MENU.TOOL"));
		actionMenu = new MenuManager(Messages.getString("MENU.ACTION"),
				Messages.getString("MENU.ACTION"));
		MenuManager optionMenu = new MenuManager(Messages
				.getString("MENU.OPTION"), Messages.getString("MENU.OPTION"));
		MenuManager helpMenu = new MenuManager(Messages.getString("MENU.HELP"),
				Messages.getString("MENU.HELP"));
		menuBar.add(fileMenu);
		fileMenu.add(connectAction);
		fileMenu.add(disconnectAction);
		fileMenu.add(new Separator());
		fileMenu.add(newQueryAction);
		fileMenu.add(openQueryAction);
		fileMenu.add(new Separator());
		fileMenu.add(saveQueryAction);
		fileMenu.add(saveAsQueryAction);
		fileMenu.add(new Separator());
		fileMenu.add(managerExitAction);
		fileMenu.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				enableQueryEditorMenuItem();
			}
		});

		// menuBar.add(new GroupMarker(IWorkbenchActionConstants.FILE_START));
		menuBar.add(editMenu);
		editMenu.add(undoAction);
		editMenu.add(redoAction);
		editMenu.add(new Separator());
		editMenu.add(copyClipboardAction);
		editMenu.add(cutAction);
		editMenu.add(pasteAction);
		editMenu.add(new Separator());
		editMenu.add(findAction);
		editMenu.add(findNextAction);
		editMenu.add(replaceAction);
		editMenu.add(new Separator());
		editMenu.add(commentAddAction);
		editMenu.add(commentDeleteAction);
		editMenu.add(new Separator());
		editMenu.add(tabAddAction);
		editMenu.add(tabDeleteAction);
		editMenu.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				enableQueryEditorMenuItem();
			}
		});

		menuBar.add(viewMenu);

		menuBar.add(toolMenu);
		toolMenu.add(loadAction);
		toolMenu.add(unloadAction);
		toolMenu.add(backupAction);
		toolMenu.add(restoreAction);
		toolMenu.add(renameAction);
		toolMenu.add(copyAction);
		toolMenu.add(optimizeAction);
		toolMenu.add(compactAction);
		toolMenu.add(checkAction);
		toolMenu.add(new Separator());
		toolMenu.add(scriptRunAction);
		toolMenu.add(tableImportAction);
		toolMenu.add(oidNaviAction);
		toolMenu.add(new Separator());
		toolMenu.add(autoBackupErrorLogAction);
		toolMenu.add(managerLogAction);
		toolMenu.add(new Separator());
		if (!MainRegistry.isProtegoBuild())
			toolMenu.add(userManagementAction);
		else {
			if (MainRegistry.isCertLogin())
				toolMenu.add(protegoUserManagementAction);
		}
		toolMenu.add(queryOptAction);

		menuBar.add(actionMenu);

		// Diag
		// diagMenu.add(diagDefaultStatusMonitorAction);
		// diagMenu.add(diagDefaultActivityMonitorAction);
		// diagMenu.add(diagDefaultDiagReportAction);

		/*
		 * Remove "ID password login".  if
		 * (MainRegistry.isProtegoBuild()) { menuBar.add(optionMenu);
		 * optionMenu.add(certificateLoginAction);
		 * optionMenu.add(idPasswordLoginAction); }
		 */

		menuBar.add(helpMenu);
		helpMenu.add(helpAction);
		helpMenu.add(querySampleAction);
		helpMenu.add(new Separator());
		helpMenu.add(serverVersionAction);
		helpMenu.add(cASVersionAction);
		helpMenu.add(new Separator());
		helpMenu.add(aboutAction);

	}

	protected void fillCoolBar(ICoolBarManager coolBar) {
		IToolBarManager toolbar = new ToolBarManager(SWT.FLAT | SWT.RIGHT);
		coolBar.add(new ToolBarContributionItem(toolbar, "main"));
		toolbar.add(queryEditActionOnToolbar);
		toolbar.add(new Separator());

		toolbar.add(refreshActionOnToolbar);
		toolbar.add(refreshIntervalActionOnToolbar);
		toolbar.add(new Separator());

		toolbar.add(startServerActionOnToolbar);
		toolbar.add(stopServerActionOnToolbar);
		toolbar.add(createActionOnToolbar);
		toolbar.add(deleteActionOnToolbar);
		toolbar.add(new Separator());

		toolbar.add(loadActionOnToolbar);
		toolbar.add(unloadActionOnToolbar);
		toolbar.add(backupActionOnToolbar);
		toolbar.add(restoreActionOnToolbar);
		toolbar.add(renameActionOnToolbar);
		toolbar.add(copyActionOnToolbar);
		toolbar.add(optimizeActionOnToolbar);
		toolbar.add(compactActionOnToolbar);
		toolbar.add(checkActionOnToolbar);
		toolbar.add(new Separator());

		toolbar.add(lockinfoActionOnToolbar);
		toolbar.add(traninfoActionOnToolbar);
		toolbar.add(new Separator());

		toolbar.add(serverVersionActionOnToolbar);
		toolbar.add(cASVersionActionOnToolbar);
	}

	private static void disable_all() {
		refreshActionOnToolbar.setEnabled(false);
		refreshIntervalActionOnToolbar.setEnabled(false);
		serverVersionActionOnToolbar.setEnabled(false);
		cASVersionActionOnToolbar.setEnabled(false);
		startServerActionOnToolbar.setEnabled(false);
		stopServerActionOnToolbar.setEnabled(false);
		createActionOnToolbar.setEnabled(false);
		deleteActionOnToolbar.setEnabled(false);
		loadActionOnToolbar.setEnabled(false);
		unloadActionOnToolbar.setEnabled(false);
		backupActionOnToolbar.setEnabled(false);
		restoreActionOnToolbar.setEnabled(false);
		renameActionOnToolbar.setEnabled(false);
		copyActionOnToolbar.setEnabled(false);
		optimizeActionOnToolbar.setEnabled(false);
		compactActionOnToolbar.setEnabled(false);
		checkActionOnToolbar.setEnabled(false);
		lockinfoActionOnToolbar.setEnabled(false);
		traninfoActionOnToolbar.setEnabled(false);

		connectAction.setEnabled(false);
		disconnectAction.setEnabled(false);
		if (!MainRegistry.isProtegoBuild())
			userManagementAction.setEnabled(false);
		managerLogAction.setEnabled(false);
		queryEditActionOnToolbar.setEnabled(false);
		dBLogoutAction.setEnabled(false);
		queryEditActionOnPopup.setEnabled(false);
		queryOptAction.setEnabled(false);
		refreshAction.setEnabled(false);
		createAction.setEnabled(false);
		sqlxinitAction.setEnabled(false);
		serverVersionAction.setEnabled(false);
		deleteAction.setEnabled(false);
		startServerAction.setEnabled(false);
		stopServerAction.setEnabled(false);
		loadAction.setEnabled(false);
		unloadAction.setEnabled(false);
		backupAction.setEnabled(false);
		restoreAction.setEnabled(false);
		renameAction.setEnabled(false);
		copyAction.setEnabled(false);
		optimizeAction.setEnabled(false);
		compactAction.setEnabled(false);
		checkAction.setEnabled(false);
		lockinfoAction.setEnabled(false);
		traninfoAction.setEnabled(false);
		refreshIntervalAction.setEnabled(false);
		cASVersionAction.setEnabled(false);
		// dBServerPropertyAction.setEnabled(false);
		// downloadFilesAction.setEnabled(false);
		createNewUserAction.setEnabled(false);
		backupPlanAction.setEnabled(false);
		// dBStatusMonitoringAction.setEnabled(false);
		queryPlanAction.setEnabled(false);
		autoBackupErrorLogAction.setEnabled(false);
		addVolumeAction.setEnabled(false);
		setAutoAddVolumeAction.setEnabled(false);
		addedVolumeLogAction.setEnabled(false);

		resetCASAdminLogAction.setEnabled(false);
		stopBrokerAction.setEnabled(false);
		// suspendBrokerAction.setEnabled(false);
		// resumeBrokerAction.setEnabled(false);
		// jobPriorityAction.setEnabled(false);
		restartAPServerAction.setEnabled(false);
		setParameterAction.setEnabled(false);
		// setParameterUsingChangerAction.setEnabled(false);
		startBrokerAction.setEnabled(false);
		deleteBrokerAction.setEnabled(false);

		removeAllErrLogAction.setEnabled(false);
		removeErrLogAction.setEnabled(false);
		removeAllAccessLogAction.setEnabled(false);
		removeAllScriptLogAction.setEnabled(false);
		removeLogAction.setEnabled(false);
		removeScriptLogAction.setEnabled(false);

		scriptRunAction.setEnabled(false);
		tableImportAction.setEnabled(false);
		oidNaviAction.setEnabled(false);

		newQueryAction.setEnabled(false);
		openQueryAction.setEnabled(false);
		certificateLoginAction.setEnabled(false);
		idPasswordLoginAction.setEnabled(false);
		protegoUserManagementAction.setEnabled(false);
	}

	public static void AdjustToolbar(int navi_id) {
		AuthItem workdb = null;
		byte adj_cubrid;

		if (navi_id == MainConstants.NAVI_CAS) {
			startServerActionOnToolbar.setToolTipText(Messages
					.getString("TOOL.STARTCAS"));
			startServerAction.setText(Messages.getString("TOOL.STARTCAS"));
			stopServerActionOnToolbar.setToolTipText(Messages
					.getString("TOOL.STOPCAS"));
			stopServerAction.setText(Messages.getString("TOOL.STOPCAS"));
		} else {
			startServerActionOnToolbar.setToolTipText(Messages
					.getString("TOOL.STARTSERVER"));
			startServerAction.setText(Messages.getString("TOOL.STARTSERVER"));
			stopServerActionOnToolbar.setToolTipText(Messages
					.getString("TOOL.STOPSERVER"));
			stopServerAction.setText(Messages.getString("TOOL.STOPSERVER"));
		}

		if (MainRegistry.isProtegoBuild())
			setCheckCertificationLogin(MainRegistry.isCertificateLogin);

		if (!MainRegistry.IsConnected) {
			disable_all();
			connectAction.setEnabled(true);
			if (MainRegistry.isProtegoBuild()) {
				certificateLoginAction.setEnabled(true);
				idPasswordLoginAction.setEnabled(true);
			}
		} else { // connected
			disable_all();
			if (MainRegistry.isProtegoBuild()) {
				protegoUserManagementAction.setEnabled(true);
			}
			disconnectAction.setEnabled(true);
			// dba or public authority exist
			queryEditActionOnToolbar.setEnabled(true);
			queryEditActionOnPopup.setEnabled(true);
			queryOptAction.setEnabled(true);

			newQueryAction.setEnabled(true);
			openQueryAction.setEnabled(true);

			if (OnlyQueryEditor.connectOldServer) {
				managerLogAction.setEnabled(false);
				// diagDefaultStatusMonitorAction.setEnabled(false);
				return;
			}

			if (!MainRegistry.isProtegoBuild()
					&& MainRegistry.UserID.equals("admin"))
				userManagementAction.setEnabled(true);

			managerLogAction.setEnabled(true);

			serverVersionAction.setEnabled(true);
			cASVersionAction.setEnabled(true);
			refreshAction.setEnabled(true);
			serverVersionActionOnToolbar.setEnabled(true);
			cASVersionActionOnToolbar.setEnabled(true);
			refreshActionOnToolbar.setEnabled(true);

			Current_auth = null;

			if (MainRegistry.isProtegoBuild()) {
				createAction.setEnabled(true);
				createActionOnToolbar.setEnabled(true);
			} else if ((MainRegistry.IsDBAAuth || MainRegistry.UserID
					.equals("admin"))
					&& navi_id == MainConstants.NAVI_CUBRID) {
				createAction.setEnabled(true);
				createActionOnToolbar.setEnabled(true);
				sqlxinitAction.setEnabled(true);					
			}

			if (navi_id == MainConstants.NAVI_CUBRID
					&& MainRegistry.Authinfo.size() > 0) {
				dBLogoutAction.setEnabled(true);
				if (CubridView.Current_db.length() <= 0) {
					adj_cubrid = MainConstants.AUTH_NONDBA;
				} else {
					int i1;
					for (i1 = 0; i1 < MainRegistry.Authinfo.size(); i1++) {
						workdb = (AuthItem) MainRegistry.Authinfo.get(i1);
						if (workdb.dbname.equals(CubridView.Current_db)) {
							Current_auth = workdb;
							if (workdb.isDBAGroup)
								break;
						}
					}
					if (i1 >= MainRegistry.Authinfo.size()) {
						adj_cubrid = MainConstants.AUTH_NONDBA;
					} else {
						if (workdb.status == MainConstants.STATUS_START) {
							adj_cubrid = MainConstants.AUTH_DBASTART;
							queryPlanAction.setEnabled(true);
							autoBackupErrorLogAction.setEnabled(true);
						} else {
							adj_cubrid = MainConstants.AUTH_DBASTOP;
						}
					}
				}

				scriptRunAction.setEnabled(true);
				tableImportAction.setEnabled(true);
				oidNaviAction.setEnabled(true);

				if (adj_cubrid == MainConstants.AUTH_DBASTART) {
					stopServerAction.setEnabled(true);
					backupAction.setEnabled(true);
					optimizeAction.setEnabled(true);
					lockinfoAction.setEnabled(true);
					traninfoAction.setEnabled(true);
					// dBServerPropertyAction.setEnabled(true);
					// downloadFilesAction.setEnabled(true);
					createNewUserAction.setEnabled(true);
					backupPlanAction.setEnabled(true);
					// dBStatusMonitoringAction.setEnabled(true);
					addVolumeAction.setEnabled(true);
					setAutoAddVolumeAction.setEnabled(true);
					addedVolumeLogAction.setEnabled(true);
					checkAction.setEnabled(true);
					unloadAction.setEnabled(true);
					removeAllErrLogAction.setEnabled(true);
					removeErrLogAction.setEnabled(true);

					stopServerActionOnToolbar.setEnabled(true);
					backupActionOnToolbar.setEnabled(true);
					optimizeActionOnToolbar.setEnabled(true);
					lockinfoActionOnToolbar.setEnabled(true);
					traninfoActionOnToolbar.setEnabled(true);
					checkActionOnToolbar.setEnabled(true);
					unloadActionOnToolbar.setEnabled(true);
				} else if (adj_cubrid == MainConstants.AUTH_DBASTOP) {
					if (MainRegistry.isProtegoBuild()) {
						copyAction.setEnabled(true);
						copyActionOnToolbar.setEnabled(true);
					} else if (MainRegistry.IsDBAAuth
							|| MainRegistry.UserID.equals("admin")) {
						copyAction.setEnabled(true);
						copyActionOnToolbar.setEnabled(true);
					}
					deleteAction.setEnabled(true);
					startServerAction.setEnabled(true);
					loadAction.setEnabled(true);
					unloadAction.setEnabled(true);
					backupAction.setEnabled(true);
					restoreAction.setEnabled(true);
					renameAction.setEnabled(true);
					optimizeAction.setEnabled(true);
					compactAction.setEnabled(true);
					checkAction.setEnabled(true);
					// dBServerPropertyAction.setEnabled(true);
					// downloadFilesAction.setEnabled(true);
					addVolumeAction.setEnabled(true);
					addedVolumeLogAction.setEnabled(true);
					removeAllErrLogAction.setEnabled(true);
					removeErrLogAction.setEnabled(true);

					deleteActionOnToolbar.setEnabled(true);
					startServerActionOnToolbar.setEnabled(true);
					loadActionOnToolbar.setEnabled(true);
					unloadActionOnToolbar.setEnabled(true);
					backupActionOnToolbar.setEnabled(true);
					restoreActionOnToolbar.setEnabled(true);
					renameActionOnToolbar.setEnabled(true);
					optimizeActionOnToolbar.setEnabled(true);
					compactActionOnToolbar.setEnabled(true);
					checkActionOnToolbar.setEnabled(true);
				}
			} // end NAVI_CUBRID
			else if (navi_id == MainConstants.NAVI_CAS) {
				if (MainRegistry.CASAuth != MainConstants.AUTH_NONE) {
					refreshIntervalAction.setEnabled(true);
					refreshIntervalActionOnToolbar.setEnabled(true);
				}
				if (MainRegistry.CASAuth == MainConstants.AUTH_DBA) {
					resetCASAdminLogAction.setEnabled(true);
					removeAllErrLogAction.setEnabled(true);
					removeAllAccessLogAction.setEnabled(true);
					removeAllScriptLogAction.setEnabled(true);
					removeLogAction.setEnabled(true);
					removeScriptLogAction.setEnabled(true);
					setParameterAction.setEnabled(true);
					
					if (MainRegistry.IsCASStart) {
						stopServerAction.setEnabled(true);
						stopServerActionOnToolbar.setEnabled(true);
					} else {
						startServerAction.setEnabled(true);
						startServerActionOnToolbar.setEnabled(true);
					}

					CASItem workcas;
					for (int i1 = 0, n = MainRegistry.CASinfo.size(); i1 < n; i1++) {
						workcas = (CASItem) MainRegistry.CASinfo.get(i1);
						if (workcas.broker_name.equals(CASView.Current_broker)) {
							if (workcas.status == MainConstants.STATUS_STOP) {
								if (MainRegistry.IsCASStart) {
									startBrokerAction.setEnabled(true);
								}
								deleteBrokerAction.setEnabled(true);
							} else {
								stopBrokerAction.setEnabled(true);
								// jobPriorityAction.setEnabled(true);
								restartAPServerAction.setEnabled(true);
							}
						}
					}
				}
			} else if (navi_id == MainConstants.NAVI_DIAG) {
				if (MainRegistry.DiagAuth != MainConstants.AUTH_NONE) {
					refreshIntervalAction.setEnabled(false);
					refreshIntervalActionOnToolbar.setEnabled(false);
				}
			}
		}
	}

	public static void setActionsMenu(IMenuManager manager) {
		if (!MainRegistry.IsConnected)
			return;

		manager.removeAll();
		if (!manager.getId().equals("contextMenu")) {
			manager.add(ApplicationActionBarAdvisor.refreshAction);
			manager.add(new Separator());
		}

		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			String Current_select = CubridView.Current_select;
			String Current_db = CubridView.Current_db;

			if (Current_select == null || Current_select.length() < 1)
				return;

			manager.add(new Separator());
			GroupMarker marker = new GroupMarker(
					IWorkbenchActionConstants.MB_ADDITIONS);
			manager.add(marker);

			if (Current_select.equals(DatabaseListInHost.ID)) {
				manager.add(ApplicationActionBarAdvisor.createAction);
				manager.add(new Separator());
				manager.add(ApplicationActionBarAdvisor.sqlxinitAction);
				manager.add(ApplicationActionBarAdvisor.serverVersionAction);				
			} else if (ApplicationActionBarAdvisor.Current_auth != null
					&& ApplicationActionBarAdvisor.Current_auth.dbuser.length() > 0) {
				if (Current_select.equals(DatabaseStatus.ID)) {
					if (MainRegistry.Authinfo_find(Current_db).setinfo == false)
						return;

					manager.add(ApplicationActionBarAdvisor.dBLogoutAction);
					manager.add(new Separator());

					if (MainRegistry.Authinfo_find(Current_db).status == MainConstants.STATUS_START)
						manager
								.add(ApplicationActionBarAdvisor.queryEditActionOnPopup);

					manager.add(ApplicationActionBarAdvisor.startServerAction);
					manager.add(ApplicationActionBarAdvisor.stopServerAction);
					manager.add(new Separator());

					MenuManager dbopMgr = new MenuManager(Messages
							.getString("POPUP.DATABASEOPERATION"),
							"dbop_contextMenu");
					dbopMgr.add(ApplicationActionBarAdvisor.loadAction);
					dbopMgr.add(ApplicationActionBarAdvisor.unloadAction);
					dbopMgr.add(ApplicationActionBarAdvisor.backupAction);
					dbopMgr.add(ApplicationActionBarAdvisor.restoreAction);
					dbopMgr.add(ApplicationActionBarAdvisor.renameAction);
					dbopMgr.add(ApplicationActionBarAdvisor.copyAction);
					dbopMgr.add(ApplicationActionBarAdvisor.optimizeAction);
					dbopMgr.add(ApplicationActionBarAdvisor.compactAction);
					dbopMgr.add(ApplicationActionBarAdvisor.checkAction);
					GroupMarker dbop = new GroupMarker(
							IWorkbenchActionConstants.MB_ADDITIONS);
					manager.add(dbop);
					manager.add(dbopMgr);

					MenuManager tranMgr = new MenuManager(Messages
							.getString("POPUP.LOCKTRANSACTION"),
							"tran_contextMenu");
					tranMgr.add(ApplicationActionBarAdvisor.lockinfoAction);
					tranMgr.add(ApplicationActionBarAdvisor.traninfoAction);
					GroupMarker tran = new GroupMarker(
							IWorkbenchActionConstants.MB_ADDITIONS);
					manager.add(tran);
					manager.add(tranMgr);

					// manager.add(ApplicationActionBarAdvisor.dBServerPropertyAction);
					// manager.add(ApplicationActionBarAdvisor.downloadFilesAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.deleteAction);
				} else if (Current_select.equals(DBUsers.ID)) {
					if (MainRegistry.Authinfo_find(Current_db).status == MainConstants.STATUS_START)
						manager.add(ApplicationActionBarAdvisor.createNewUserAction);
				} else if (Current_select.equals(DBUsers.RESERVED)) {
					manager.add(ApplicationActionBarAdvisor.userPropertyAction);
				} else if (Current_select.equals(DBUsers.USERS)) {
					manager.add(ApplicationActionBarAdvisor.deleteUserAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.userPropertyAction);
				} else if (Current_select.equals(JobAutomation.BACKJOBS)) {
					manager.add(ApplicationActionBarAdvisor.backupPlanAction);
					manager.add(ApplicationActionBarAdvisor.autoBackupErrorLogAction);
				} else if (Current_select.equals(JobAutomation.QUERYJOBS)) {
					manager.add(ApplicationActionBarAdvisor.queryPlanAction);
				} else if (Current_select.equals(JobAutomation.BACKJOB)) {
					manager.add(ApplicationActionBarAdvisor.deleteBackupPlanAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.updateBackupPlanAction);
				} else if (Current_select.equals(JobAutomation.QUERYJOB)) {
					manager.add(ApplicationActionBarAdvisor.deletequeryPlanAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.updatequeryPlanAction);
				} else if (Current_select.equals(DBSpace.ID)) {
					manager.add(ApplicationActionBarAdvisor.setAutoAddVolumeAction);
					manager.add(ApplicationActionBarAdvisor.addedVolumeLogAction);
				} else if (Current_select.equals(DBSpace.VOL_GENERAL)) {
					manager.add(ApplicationActionBarAdvisor.addVolumeAction);
				} else if (Current_select.equals(DBSchema.USER_TABLE)) {
					if (MainRegistry.Authinfo_find(Current_db).status == MainConstants.STATUS_START)
						manager.add(ApplicationActionBarAdvisor.newClassAction);
				} else if (Current_select.equals(DBSchema.USER_VIEW)) {
					if (MainRegistry.Authinfo_find(Current_db).status == MainConstants.STATUS_START)
						manager.add(ApplicationActionBarAdvisor.newViewAction);
				} else if (Current_select.equals(DBSchema.SYS_OBJECT)) {
					try {
						if (MainRegistry.Authinfo_find(Current_db).status == MainConstants.STATUS_START) {
							TreeObject parent = ((TreeObject) ((IStructuredSelection) CubridView.viewer
									.getSelection()).getFirstElement()).getParent();
							tablePropertyAction.setText(parent.getName()
									.concat(Messages.getString("TOOL.TABLEPROPERTYACTION")));

							manager.add(ApplicationActionBarAdvisor.tableSelectAllAction);
							manager.add(ApplicationActionBarAdvisor.tableSelectCountAction);
							manager.add(new Separator());
							manager.add(ApplicationActionBarAdvisor.tablePropertyAction);
						}
					} catch (Exception e) {

					}
				} else if (Current_select.equals(DBSchema.USER_OBJECT)) {
					try {
						TreeObject parent = ((TreeObject) ((IStructuredSelection) CubridView.viewer
								.getSelection()).getFirstElement()).getParent();
						tablePropertyAction.setText(parent.getName().concat(Messages.getString("TOOL.TABLEPROPERTY_EDITACTION")));
						if (MainRegistry.Authinfo_find(Current_db).status == MainConstants.STATUS_START) {
							manager.add(ApplicationActionBarAdvisor.tableSelectAllAction);
							manager.add(ApplicationActionBarAdvisor.tableSelectCountAction);
							manager.add(new Separator());
							if (parent.getID().equals(DBSchema.USER_TABLE)) {
								manager.add(ApplicationActionBarAdvisor.tableInsertAction);
								manager.add(ApplicationActionBarAdvisor.tableDeleteAction);
								manager.add(new Separator());
								manager.add(ApplicationActionBarAdvisor.tableImportAction);
							}
							manager.add(ApplicationActionBarAdvisor.tableExportAction);
							if (!parent.getID().equals(DBSchema.USER_OBJECT)) {
								manager.add(new Separator());
								manager.add(ApplicationActionBarAdvisor.tableDropAction);
								manager.add(ApplicationActionBarAdvisor.tableRenameAction);
								manager.add(ApplicationActionBarAdvisor.tablePropertyAction);
							}
						}
					} catch (Exception e) {

					}
				} else if (Current_select.equals(DBTriggers.ID)) {
					manager.add(ApplicationActionBarAdvisor.newTriggerAction);
				} else if (Current_select.equals(DBTriggers.OBJ)) {
					manager.add(ApplicationActionBarAdvisor.dropTriggerAction);
					manager.add(ApplicationActionBarAdvisor.alterTriggerAction);
				} else if (Current_select.equals(DBLogs.ID)) {
					manager.add(ApplicationActionBarAdvisor.removeAllErrLogAction);
				} else if (Current_select.equals(DBLogs.OBJ)) {
					manager.add(ApplicationActionBarAdvisor.logViewAction);
					manager.add(ApplicationActionBarAdvisor.removeErrLogAction);
				}
			}
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			String Current_select = CASView.Current_select;

			if (Current_select.length() <= 0)
				return;
			manager.add(new Separator());
			GroupMarker marker = new GroupMarker(
					IWorkbenchActionConstants.MB_ADDITIONS);
			manager.add(marker);

			if (Current_select.equals(BrokerList.ID)) {
				if (ApplicationActionBarAdvisor.startServerAction.isEnabled())
					manager.add(ApplicationActionBarAdvisor.startServerAction);
				if (ApplicationActionBarAdvisor.stopServerAction.isEnabled())
					manager.add(ApplicationActionBarAdvisor.stopServerAction);
				manager.add(new Separator());
				manager.add(ApplicationActionBarAdvisor.refreshIntervalAction);
				manager.add(ApplicationActionBarAdvisor.setParameterAction);
				manager.add(ApplicationActionBarAdvisor.cASVersionAction);
			} else if (Current_select.equals(BrokerStatus.ID)) {
				manager.add(ApplicationActionBarAdvisor.refreshIntervalAction);
			} else if (Current_select.equals(BrokerJob.ID)) {
				if (ApplicationActionBarAdvisor.startBrokerAction.isEnabled())
					manager.add(ApplicationActionBarAdvisor.startBrokerAction);
				if (ApplicationActionBarAdvisor.stopBrokerAction.isEnabled())
					manager.add(ApplicationActionBarAdvisor.stopBrokerAction);
				// if
				// (ApplicationActionBarAdvisor.suspendBrokerAction.isEnabled())
				// manager.add(ApplicationActionBarAdvisor.suspendBrokerAction);
				// if
				// (ApplicationActionBarAdvisor.resumeBrokerAction.isEnabled())
				// manager.add(ApplicationActionBarAdvisor.resumeBrokerAction);
				if (ApplicationActionBarAdvisor.deleteBrokerAction.isEnabled())
					manager.add(ApplicationActionBarAdvisor.deleteBrokerAction);
				// manager.add(ApplicationActionBarAdvisor.jobPriorityAction);
				manager.add(new Separator());
				manager.add(ApplicationActionBarAdvisor.restartAPServerAction);
				manager.add(new Separator());
				manager.add(ApplicationActionBarAdvisor.refreshIntervalAction);
			} else if (Current_select.equals(CASLogs.LOGS_ACCESS))
				manager.add(ApplicationActionBarAdvisor.removeAllAccessLogAction);
			else if (Current_select.equals(CASLogs.LOGS_ERROR))
				manager.add(ApplicationActionBarAdvisor.removeAllErrLogAction);
			else if (Current_select.equals(CASLogs.LOGS_SCRIPT))
				manager.add(ApplicationActionBarAdvisor.removeAllScriptLogAction);
			else if (Current_select.equals(CASLogs.ADMINOBJ)) {
				manager.add(ApplicationActionBarAdvisor.logViewAction);
				manager.add(ApplicationActionBarAdvisor.resetCASAdminLogAction);
			} else if (Current_select.equals(CASLogs.LOGSOBJ)) {
				manager.add(ApplicationActionBarAdvisor.logViewAction);
				manager.add(ApplicationActionBarAdvisor.removeLogAction);
			} else if (Current_select.equals(CASLogs.SCRIPTOBJ)) {
				manager.add(ApplicationActionBarAdvisor.logViewAction);
				manager.add(ApplicationActionBarAdvisor.removeScriptLogAction);
				/*
				 * ACTIVITY PROFILE
				 * 
				 * manager.add(ApplicationActionBarAdvisor.diagActivityImportCASLogAction);
				 */
			}
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_DIAG) {
			String Current_select = DiagView.Current_select;
			String Current_job = DiagView.Current_job;

			if (Current_select == null)
				return;
			manager.add(new Separator());
			GroupMarker marker = new GroupMarker(
					IWorkbenchActionConstants.MB_ADDITIONS);
			manager.add(marker);

			if (Current_select.equals(StatusTemplate.ID)) {
				if (Current_job.equals(Messages.getString("TREE.STATUSMONITOR"))) {
					manager.add(ApplicationActionBarAdvisor.diagNewStatusMonitorTemplateAction);
					/*
					 * manager.add(new Separator());
					 * manager.add(ApplicationActionBarAdvisor.diagDefaultStatusMonitorAction);
					 */
				} else {
					ApplicationActionBarAdvisor.diagUpdateStatusMonitorTemplateAction.dialogTemplateName = Current_job;
					manager.add(ApplicationActionBarAdvisor.diagUpdateStatusMonitorTemplateAction);
					ApplicationActionBarAdvisor.diagStatusMonitorAction.dialogTemplateName = Current_job;
					manager.add(ApplicationActionBarAdvisor.diagStatusMonitorAction);
					manager.add(new Separator());
					ApplicationActionBarAdvisor.diagRemoveStatusTemplateAction.templateName = Current_job;
					manager.add(ApplicationActionBarAdvisor.diagRemoveStatusTemplateAction);
				}
			} else if (Current_select.equals(ActivityTemplate.ID)) {
				if (Current_job.equals(Messages.getString("TREE.ACTIVITYMONITOR"))) {
					
				} else if (Current_job.equals(Messages.getString("TREE.ACTIVITYTEMPLATE"))) {
					manager.add(ApplicationActionBarAdvisor.diagNewActivityMonitorTemplateAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.diagDefaultActivityMonitorAction);
				} else {
					ApplicationActionBarAdvisor.diagActivityMonitorAction.dialogTemplateName = Current_job;
					manager.add(ApplicationActionBarAdvisor.diagActivityMonitorAction);
					ApplicationActionBarAdvisor.diagUpdateActivityTemplateAction.dialogTemplateName = Current_job;
					manager.add(ApplicationActionBarAdvisor.diagUpdateActivityTemplateAction);
					manager.add(new Separator());
					ApplicationActionBarAdvisor.diagRemoveActivityTemplateAction.templateName = Current_job;
					manager
							.add(ApplicationActionBarAdvisor.diagRemoveActivityTemplateAction);
				}
			} else if (Current_select.equals(ActivityLogs.ID)) {
				if (Current_job.equals(Messages.getString("TREE.ACTIVITYLOGS"))) {
				} else if (Current_job.equals(Messages.getString("TREE.CASLOGS"))) {
					DiagActivityAnalyzeCASLogAction.logFile = "";
					manager.add(ApplicationActionBarAdvisor.diagActivityAnalyzeCASLogAction);
				} else {
					ApplicationActionBarAdvisor.diagActivityLogDisplayLogAction.logFileName = Current_job;
					manager.add(ApplicationActionBarAdvisor.diagActivityLogDisplayLogAction);
					manager.add(ApplicationActionBarAdvisor.diagActivityLogAnalyzeLogAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.diagActivityLogSetLoggingTimeAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.diagActivityLogRemoveLogAction);
				}
			} else if (Current_select.equals(CASLogs.SCRIPTOBJ)) {
				ArrayList casinfo = MainRegistry.CASinfo;
				boolean find = false;
				for (int i = 0, n = casinfo.size(); i < n; i++) {
					ArrayList loginfo = ((CASItem) casinfo.get(i)).loginfo;
					for (int j = 0, m = loginfo.size(); j < m; j++) {
						if (((LogFileInfo) loginfo.get(j)).filename.equals(Current_job)) {
							find = true;
							DiagActivityAnalyzeCASLogAction.logFile 
							= DiagActivityCASLogRunAction.logFile 
							= ((LogFileInfo) loginfo.get(j)).path;
							break;
						}
					}
					if (find)
						break;
				}
				manager.add(ApplicationActionBarAdvisor.diagActivityAnalyzeCASLogAction);
				manager.add(ApplicationActionBarAdvisor.diagActivityCASLogRunAction);
				// manager.add(new Separator());
				// manager.add(ApplicationActionBarAdvisor.diagActivityImportCASLogAction);
			} else if (Current_select.equals(DiagTemplate.ID)) {
				if (Current_job.equals(Messages.getString("TREE.DIAGDIAGTEMPLATE"))) {
					manager.add(ApplicationActionBarAdvisor.diagNewDiagReportTemplateAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.diagDefaultDiagReportAction);
				} else {
					manager.add(ApplicationActionBarAdvisor.diagRunDiagReportAction);
					manager.add(ApplicationActionBarAdvisor.diagUpdateDiagReportTemplateAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.diagRemoveDiagReportTemplateAction);
				}
			} else if (Current_select.equals(DiagReport.ID)) {
				if (Current_job.equals(Messages.getString("TREE.DIAGDIAGREPORT"))) {

				} else {
					manager.add(ApplicationActionBarAdvisor.diagViewDiagReportAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.diagDiagReportSetStartTimeAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.diagRemoveDiagReportAction);
				}
			} else if (Current_select.equals(StatusWarning.ID)) {
				manager.add(ApplicationActionBarAdvisor.diagUpdateStatusWarningAction);
				manager.add(new Separator());
				manager.add(ApplicationActionBarAdvisor.diagStatusWarningLogViewAction);
			} else if (Current_select.equals(LogAnalyze.ID)) {
				// manager.add(ApplicationActionBarAdvisor.newAnalyzeAction);
			} else if (Current_select.equals(ServiceReport.ID)) {
				if (Current_job.equals(Messages.getString("TREE.TROUBLETRACE"))) {

				} else {
					manager.add(ApplicationActionBarAdvisor.diagTroubleTraceAction);
					manager.add(new Separator());
					manager.add(ApplicationActionBarAdvisor.diagRemoveTroubleAction);
				}
			}
		}
	}

	public static void enableQueryEditorMenuItem() {
		disableAllQueryEditorMenuItem();
		if (MainRegistry.getCurrentQueryEditor() != null) {
			newQueryAction.setEnabled(true);
			openQueryAction.setEnabled(true);
			saveQueryAction.setEnabled(true);
			saveAsQueryAction.setEnabled(true);
			undoAction.setEnabled(true);
			redoAction.setEnabled(true);
			copyClipboardAction.setEnabled(true);
			cutAction.setEnabled(true);
			pasteAction.setEnabled(true);
			findAction.setEnabled(true);
			findNextAction.setEnabled(true);
			replaceAction.setEnabled(true);
			commentAddAction.setEnabled(true);
			commentDeleteAction.setEnabled(true);
			tabAddAction.setEnabled(true);
			tabDeleteAction.setEnabled(true);
		}
	}

	public static void disableAllQueryEditorMenuItem() {
		saveQueryAction.setEnabled(false);
		saveAsQueryAction.setEnabled(false);
		undoAction.setEnabled(false);
		redoAction.setEnabled(false);
		copyClipboardAction.setEnabled(false);
		cutAction.setEnabled(false);
		pasteAction.setEnabled(false);
		findAction.setEnabled(false);
		findNextAction.setEnabled(false);
		replaceAction.setEnabled(false);
		commentAddAction.setEnabled(false);
		commentDeleteAction.setEnabled(false);
		tabAddAction.setEnabled(false);
		tabDeleteAction.setEnabled(false);
	}

	public static void setCheckCertificationLogin(boolean check) {
		if (MainRegistry.isProtegoBuild()) {
			if (check) {
				idPasswordLoginAction.setImageDescriptor(null);
				certificateLoginAction
						.setImageDescriptor(cubridmanager.CubridmanagerPlugin
								.getImageDescriptor("/icons/event.png"));
			} else {
				certificateLoginAction.setImageDescriptor(null);
				idPasswordLoginAction
						.setImageDescriptor(cubridmanager.CubridmanagerPlugin
								.getImageDescriptor("/icons/event.png"));
			}
		}
	}
}
