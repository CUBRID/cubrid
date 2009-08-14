/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search
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
package com.cubrid.cubridmanager.ui.cubrid.procedure;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * Message bundle classes. Provides convenience methods for manipulating
 * messages.
 * 
 * @author pangqiren 2009-3-2
 * 
 */
public class Messages extends
		NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID
				+ ".cubrid.procedure.Messages", Messages.class);
	}
	public static String errInput;
	// add Function params
	public static String titleAddFuncParamDialog;
	public static String msgEditFuncParamDialog;
	public static String titleEditFuncParamDialog;
	public static String lblParameterName;
	public static String lblSqlType;
	public static String lblSpecialJavaType;
	public static String lblJavaType;
	public static String lblParamModel;
	public static String msgAddFuncParamDialog;
	public static String errInputParameterName;
	public static String errInputParameterNameLength;
	public static String errInputParameterNameValid;
	public static String errInputParameterNameDuplicate;
	public static String errInputSqlType;
	public static String msgSelectJavaConfirm;
	public static String msgSelectSpecialJavaConfirm;
	public static String errValidJavaFunctionName;
	// Edit function
	public static String tabItemFuncSetting;
	public static String tabItemSQLScript;
	public static String lblFunctionName;
	public static String tblColFunctionParamName;
	public static String tblColFunctionParamType;
	public static String tblColFunctionJavaParamType ;
	public static String tblColFunctionModel;
	public static String lblJavaFunctionName;
	public static String lblReturnJavaType;
	public static String msgEditFunctionDialog;
	public static String msgAddFunctionDialog;
	public static String lblReturnSQLType;
	public static String btnAddParameter;
	public static String btnEditParameter;
	public static String btnDropParameter;
	public static String btnUpParameter;
	public static String btnDownParameter;
	public static String titleSuccess;
	public static String msgEditFunctionSuccess;
	public static String msgAddFunctionSuccess;
	public static String errInputFunctionName;
	public static String errInputFunctionNameLength;
	public static String errInputJavaFunctionName;
	public static String errInputSelectSqlType;
	public static String errInputSpecialJavaType;
	public static String errInputSelectJavaType;
	public static String msgVoidReturnType;
	// Edit procedure
	public static String tabItemProcSetting;
	public static String lblProcedureName;
	public static String tabItemProcSQLScript;
	public static String tblColProcedureParamName;
	public static String tblColProcedureParamType;
	public static String tblColProcedureJavaParamType ;
	public static String tblColProcedureModel;
	public static String msgEditProcedureDialog;
	public static String msgAddProcedureDialog;
	public static String msgEditProcedureSuccess;
	public static String msgAddProcedureSuccess;
	public static String errInputProcedureName;
	public static String errInputJavaProcedureName;
	//delete function
	public static String msgSureDropFunction;
	public static String errSelectFunction;
	public static String msgSureDropProcedure;
	public static String errSelectProcedure;
	public static String msgDeleteProcedureSuccess;
	public static String msgDeleteFunctionSuccess;
	public static String errDuplicateName;
	public static String errNotExistName;
}