/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.ui.cubrid.table;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * To access and store messages for international requirement
 * 
 * @author moulinwang
 * @version 1.0 - 2009-5-5 created by moulinwang
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".cubrid.table.Messages", Messages.class);
	}
	public static String btnAdd;
	public static String btnAutoIncr;
	public static String btnCancel;
	public static String btnDel;
	public static String btnDelete;
	public static String btnEdit;
	public static String btnNotNull;
	public static String btnPK;
	public static String btnShared;
	public static String btnShowInherit;
	public static String btnUnique;
	public static String colColumnName;
	public static String colDataType;
	public static String colName;
	public static String colOrder;
	public static String colRefColumn;
	public static String colSchemaType;
	public static String colUseColumn;
	public static String dataNewKey;
	public static String errColumnExist;
	public static String errColumnName;
	public static String errColumnNotEdit;
	public static String errDataType;
	public static String errDataTypeImcompatible;
	public static String errDataTypeInCompatible;
	public static String errDataTypeNotFound;
	public static String errExistIndex;
	public static String errExistReverseIndex;
	public static String errExistReverseUniqueIndex;
	public static String errExistUniqueIndex;
	public static String errInvalidDataType;
	public static String errInvalidFile;
	public static String errMultColumnsNotSet;
	public static String errMultiBytes;
	public static String errNoColumnInTable;
	public static String errNoNameForCacheColumn;
	public static String errNoSelectedColumn;
	public static String errNoSelectedSuperClass;
	public static String errOneColumnNotSet;
	public static String errPrecisionGreaterSize;
	public static String errSelectDataType;
	public static String errSelectMoreColumn;
	public static String errSelectMoreColumns;
	public static String errSelectTableWithPK;
	public static String grpColumnType;
	public static String grpConstaint;
	public static String grpFieldDesc;
	public static String grpOnUpdate;
	public static String infoGeneralTab;
	public static String infoIndexesTab;
	public static String infoInheritTab;
	public static String infoOwner;
	public static String infoSQLScriptTab;
	public static String infoSuccess;
	public static String infoSuperClasses;
	public static String infoSystemSchema;
	public static String infoType;
	public static String infoUserSchema;
	public static String lblAlias;
	public static String lblCacheColumnName;
	public static String lblClassType;
	public static String lblColumn;
	public static String lblColumnName;
	public static String lblColumns;
	public static String lblColumnType;
	public static String lblConflicts;
	public static String lblDataType;
	public static String lblDefault;
	public static String lblDefaultValue;
	public static String lblFK;
	public static String lblFKName;
	public static String lblFTableName;
	public static String lblFTablePK;
	public static String lblIncr;
	public static String lblIncrement;
	public static String lblIndexes;
	public static String lblIndexName;
	public static String lblIndexType;
	public static String lblInstanceType;
	public static String lblOwner;
	public static String lblPKName;
	public static String lblPrecision;
	public static String lblQuerySpec;
	public static String lblResolution;
	public static String lblResolutionType;
	public static String lblSchemaType;
	public static String lblSeed;
	public static String lblSelectColumns;
	public static String lblSelectTables;
	public static String lblShared;
	public static String lblSharedValue;
	public static String lblSize;
	public static String lblSuperClass;
	public static String lblSuperInfo;
	public static String lblSuperList;
	public static String lblTableName;
	public static String lblTipSuperClass;
	public static String lblType;
	public static String MSG_WARNING;
	public static String MSG_INFORMATION;
	public static String msgAddColumn;
	public static String msgSelectSupers;
	public static String msgSetPK;
	public static String msgSetResolution;
	public static String msgTitleAddColumn;
	public static String msgTitleAddFK;
	public static String msgTitleAddIndex;
	public static String msgAddIndex;
	public static String msgTitleEditColumn;
	public static String msgTitleSetPK;
	public static String msgTitleSetResolution;
	public static String msgTitleSetSupers;
	public static String schemaTypeClass;
	public static String sqlConnectionError;

	//empty table message
	public static String confirmTableDeleteWarn;
	public static String resultTableDeleteInformantion1;
	public static String resultTableDeleteInformantion2;

	//select count(*) message
	public static String selectCountTitle;
	public static String selectCountResult1;
	public static String selectCountResult2;

	//insert instance dialog
	public static String insertButtonName;
	public static String clearButtonName;
	public static String closeButtonName;
	//column names
	public static String metaAttribute;
	public static String metaDomain;
	public static String metaConstaints;
	public static String metaValue;
	//Title
	public static String insertInstanceWindowTitle;
	public static String insertInstanceMsgTitle;
	public static String insertInstanceMsg;
	public static String systemSchema;
	public static String tblcolColumnName;
	public static String tblcolDataType;
	public static String tblcolTableName;
	public static String tblColumnAlias;
	public static String tblColumnAutoIncr;
	public static String tblColumnCacheColumn;
	public static String tblColumnClass;
	public static String tblColumnColumnName;
	public static String tblColumnColumns;
	public static String tblColumnDataType;
	public static String tblColumnDefault;
	public static String tblColumnDeleteRule;
	public static String tblColumnFK;
	public static String tblColumnForeignColumnName;
	public static String tblColumnForeignTable;
	public static String tblColumnIndexName;
	public static String tblColumnIndexRule;
	public static String tblColumnIndexType;
	public static String tblColumnInherit;
	public static String tblColumnName;
	public static String tblColumnNotNull;
	public static String tblColumnOnColumns;
	public static String tblColumnPK;
	public static String tblColumnShared;
	public static String tblColumnSuperTable;
	public static String tblColumnTableName;
	public static String tblColumnType;
	public static String tblColumnUnique;
	public static String tblColumnUpdateRule;
	public static String tblcolUseColumn;
	public static String tblcolUseTable;
	public static String tipChooseDataType;
	public static String tipInput;
	public static String tipResolutionTable;
	public static String tipSuperClassTable;
	public static String titleAddColumn;
	public static String titleAddFK;
	public static String msgAddFK;
	public static String titleAddIndex;
	public static String titleEditColumn;
	public static String titleSetPK;
	public static String titleSetSuperTables;
	public static String titleTitleSetResolution;

	public static String totalInsertedCountMsg1;
	public static String totalInsertedCountMsg2;
	public static String insertedCountMsg1;
	public static String insertedCountMsg2;
	public static String insertFailed1;
	public static String insertFailed2;
	public static String insertDataTypeErrorMsg;
	public static String insertNotNullErrorMsg;

	//export data function
	public static String exportResultOK1;
	public static String exportResultOK2;
	public static String exportResultTitle;
	public static String exportLimit;
	public static String exportColumnCountOver;
	public static String exportColumnCountOverWarnInfo;
	public static String exportFileOverwriteQuestionMSG;
	public static String exportFileOverwriteQuestionTitle;
	public static String exportMonitorMsg;

	//import data function
	public static String importErrorHead;
	public static String importShellTitle;
	public static String importButtonName;
	public static String cancleImportButtonName;
	public static String importDataMsgTitle;
	public static String importDataMsg;
	public static String importTargetTable;
	public static String importOpenFileBTN;
	public static String importFileNameLBL;
	public static String importCommitLinesLBL;
	public static String importMappingExcel;
	public static String importFirstLineFLAG;
	public static String importExcelcolumns;
	public static String importTableColumns;
	public static String importDeleteExcelColumnBTN;
	public static String importUpTableColumnBTN;
	public static String importDownTableColumnBTN;
	public static String importDeleteTableColumnBTN;
	public static String importSelectTargetTableERRORMSG;
	public static String importSelectFileERRORMSG;
	public static String importColumnCountMatchERRORMSG;
	public static String importNoExcelColumnERRORMSG;
	public static String importNoTableColumnERRORMSG;

	public static String importSuccessCountMSG1;
	public static String importSuccessCountMSG2;
	public static String importInvalidCountMSG1;
	public static String importInvalidCountMSG2;
	public static String importInvalidColumnCountMSG;
	public static String importRollBackMSG;
	public static String importInsertCountMSG1;
	public static String importInsertCountMSG2;
	public static String importWarnRollback;

	//rename table name
	public static String renameTable;
	public static String renameView;
	public static String renameInvalidTableNameMSG;
	public static String renameMSGTitle;
	public static String renameDialogMSG;
	public static String renameNewTableName;
	public static String renameShellTitle;
	public static String renameOKBTN;
	public static String renameCancelBTN;

	//drop table
	public static String dropWarnMSG;
	public static String dropTable;
	public static String dropView;
	public static String newTableMsgTitle;
	public static String newViewMsgTitle;
	public static String newTableMsg;
	public static String newViewMsg;
	public static String newTableButtonName;
	public static String cancleButtonName;
	public static String newTableShellTitle;
	public static String newViewShellTitle;
	public static String editTableMsgTitle;
	public static String editViewMsgTitle;
	public static String editTableMsg;
	public static String editViewMsg;
	public static String editTableShellTitle;
	public static String editViewShellTitle;
	public static String typeTable;
	public static String typeView;
	public static String userSchema;
	public static String errExistColumn;
	public static String errParseValue2DataType;
	public static String btnOK;
	public static String editAttrShellTitle;
	public static String dataTypeInSet;
	public static String addAttrShellTitle;
	public static String invalidTimestamp;
	public static String invalidDate;
	public static String invalidTime;
	public static String btnUp;
	public static String btnDown;

	//Create view
	public static String errInput;
	public static String errInputViewName;
	public static String errInputNameLength;
	public static String errInputValidViewName;
	public static String errAddSpecification;
	public static String errClickValidate;
	public static String titleSuccess;
	public static String msgSuccessCreateView;
	public static String msgSuccessEditView;
	public static String tabItemGeneral;
	public static String tabItemSQLScript;
	public static String lblViewName;
	public static String lblViewOwnerName;
	public static String lblQueryList;
	public static String lblSelectQueryList;
	public static String tblColViewName;
	public static String tblColViewDataType;
	public static String tblColViewDefaultType;
	public static String tblColViewDefaultValue;
	public static String msgPropertyInfo;
	public static String msgEditInfo;
	public static String btnAddParameter;
	public static String btnDeleteParameter;
	public static String btnEditParameter;
	public static String btnValidateColumn;
	public static String lblTableNameColumns;
	//add query
	public static String titleAddQueryDialog;
	public static String titleEditQueryDialog;
	public static String grpQuerySpecification;
	public static String msgAddQueryDialog;
	public static String errFileCannotDelete;
	public static String errFileCannotRename;
	public static String grpOnDelete;
	public static String errNoTableName;
	public static String btnOnCacheObject;

	public static String typeClass;
	public static String typeInstance;
	public static String typeShared;
	public static String typeUnique;
	public static String typeNotNull;
	public static String titleSchemEditPart;
	public static String errColumnNotDrop;
	public static String errNoDefaultOnClassColumnNotNull;
	public static String errFKNotDrop;
	public static String errIndexNotDrop;
	public static String invalidDatetime;
	public static String errInheritItself;
	public static String errExistTable;
	public static String errExistView;
	public static String errExistLocColumn;
	public static String errExistResolution;
	public static String msgEditColumn;
	public static String errColumnExistInFK;
	public static String errNumber;
	public static String errRange;
	public static String errIncrement;
	public static String errNotEnoughtColumns;
	public static String exportCharacterCountExceedWarnInfo;

}
