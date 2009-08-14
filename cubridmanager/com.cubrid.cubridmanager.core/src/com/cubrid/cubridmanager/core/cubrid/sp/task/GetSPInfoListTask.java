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
package com.cubrid.cubridmanager.core.cubrid.sp.task;

import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.jdbc.JDBCTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPArgsInfo;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPArgsType;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPInfo;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPType;

/**
 * 
 * This task is responsible to get all stored procedure
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-8 created by pangqiren
 */
public class GetSPInfoListTask extends
		JDBCTask {

	private String spName = null;
	private SPType spType = null;
	private List<SPInfo> spInfoList = new ArrayList<SPInfo>();
	private static final String GET_ALLSPINFO_SQL = "select sp.sp_name,                                   "
			+ "       sp.sp_type,                                   "
			+ "       sp.return_type,                               "
			+ "       sp.arg_count,                                 "
			+ "       sp.lang,                                      "
			+ "       sp.target,                                    "
			+ "       sp.owner,                                     "
			+ "       spargs.index_of,                              "
			+ "       spargs.arg_name,                              "
			+ "       spargs.data_type,                             "
			+ "       spargs.mode                                   "
			+ "  from db_stored_procedure sp                        "
			+ "  left outer join db_stored_procedure_args spargs    "
			+ "    on sp.sp_name =spargs.sp_name                    ";

	public GetSPInfoListTask(DatabaseInfo dbInfo) {
		super("GetAllSPInfoList", dbInfo,  false);
	}

	public void execute() {
		try {
			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			String sql = GET_ALLSPINFO_SQL;
			int paraCount = 0;
			Map<String, String> paraMap = new HashMap<String, String>();
			if (spName != null && !"".equals(spName)) {
				paraCount++;
				sql += " where sp.sp_name=?";
				paraMap.put("" + paraCount, spName);
			}
			if (spType != null && !"".equals(spType.getText())) {
				if (paraCount > 0) {
					sql += " and sp.sp_type=?";
				} else {
					sql += " where sp.sp_type=?";
				}
				paraCount++;
				if (spType == SPType.FUNCTION)
					paraMap.put("" + paraCount, SPType.FUNCTION.getText());
				else
					paraMap.put("" + paraCount, SPType.PROCEDURE.getText());
			}
			if (paraCount > 0) {
				stmt = connection.prepareStatement(sql);
				for (int i = 1; i <= paraCount; i++) {
					((PreparedStatement) stmt).setString(i, paraMap.get("" + i));
				}
			} else {
				stmt = connection.prepareStatement(sql);
			}

			rs = ((PreparedStatement) stmt).executeQuery();
			while (rs.next()) {
				String spName = rs.getString("sp_name");
				if (spName == null || spName.trim().length() <= 0) {
					continue;
				}
				String spType = rs.getString("sp_type");
				SPType type = null;
				if (spType != null
						&& spType.trim().equalsIgnoreCase("PROCEDURE")) {
					type = SPType.PROCEDURE;
				} else if (spType != null
						&& spType.trim().equalsIgnoreCase("FUNCTION")) {
					type = SPType.FUNCTION;
				}
				String returnType = rs.getString("return_type");
				String target = rs.getString("target");
				String language = rs.getString("lang");
				String owner = rs.getString("owner");
				SPInfo spInfo = getSPInfo(spInfoList, spName);
				if (spInfo == null) {
					spInfo = new SPInfo(spName, type, returnType, language,
							owner, target);
					spInfoList.add(spInfo);
				}
				String argName = rs.getString("arg_name");
				if (argName == null || argName.trim().length() <= 0) {
					continue;
				}
				// Get sp args
				int index = rs.getInt("index_of");
				String dataType = rs.getString("data_type");
				String mode = rs.getString("mode");
				SPArgsType spArgsType = null;
				if (mode != null && mode.trim().equalsIgnoreCase("IN")) {
					spArgsType = SPArgsType.IN;
				} else if (mode != null && mode.trim().equalsIgnoreCase("OUT")) {
					spArgsType = SPArgsType.OUT;
				} else if (mode != null
						&& mode.trim().equalsIgnoreCase("INOUT")) {
					spArgsType = SPArgsType.INOUT;
				}
				SPArgsInfo spArgsInfo = new SPArgsInfo(spName, argName, index,
						dataType, spArgsType);
				spInfo.addSPArgsInfo(spArgsInfo);
			}
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
	}

	public List<SPInfo> getSPInfoList() {
		return spInfoList;
	}

	private SPInfo getSPInfo(List<SPInfo> spInfoList, String spName) {
		for (int i = 0; i < spInfoList.size(); i++) {
			SPInfo spInfo = spInfoList.get(i);
			if (spInfo.getSpName().equals(spName)) {
				return spInfo;
			}
		}
		return null;
	}

	public String getSpName() {
		return spName;
	}

	public void setSpName(String spName) {
		this.spName = spName;
	}

	public SPType getSpType() {
		return spType;
	}

	public void setSpType(SPType spType) {
		this.spType = spType;
	}
}
