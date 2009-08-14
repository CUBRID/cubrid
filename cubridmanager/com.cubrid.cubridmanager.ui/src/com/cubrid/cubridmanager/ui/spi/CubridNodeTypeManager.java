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
package com.cubrid.cubridmanager.ui.spi;

import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;

/**
 * 
 * This class is responsible to manage cubrid node type
 * 
 * @author pangqiren
 * @version 1.0 - 2009-7-14 created by pangqiren
 */
public class CubridNodeTypeManager {

	public final static CubridNodeType[] canRefreshNodeTypeArr = {
			CubridNodeType.SERVER, CubridNodeType.DATABASE_FOLDER,
			CubridNodeType.DATABASE, CubridNodeType.USER_FOLDER,
			CubridNodeType.USER, CubridNodeType.JOB_FOLDER,
			CubridNodeType.GENERIC_VOLUME_FOLDER,
			CubridNodeType.DATA_VOLUME_FOLDER,
			CubridNodeType.INDEX_VOLUME_FOLDER,
			CubridNodeType.TEMP_VOLUME_FOLDER,
			CubridNodeType.ACTIVE_LOG_FOLDER,
			CubridNodeType.ARCHIVE_LOG_FOLDER, CubridNodeType.GENERIC_VOLUME,
			CubridNodeType.DATA_VOLUME, CubridNodeType.INDEX_VOLUME,
			CubridNodeType.TEMP_VOLUME, CubridNodeType.BACKUP_PLAN_FOLDER,
			CubridNodeType.QUERY_PLAN_FOLDER, CubridNodeType.DBSPACE_FOLDER,
			CubridNodeType.TABLE_FOLDER,
			CubridNodeType.USER_PARTITIONED_TABLE_FOLDER,
			CubridNodeType.VIEW_FOLDER, CubridNodeType.SYSTEM_TABLE_FOLDER,
			CubridNodeType.SYSTEM_TABLE, CubridNodeType.USER_TABLE,
			CubridNodeType.SYSTEM_VIEW_FOLDER, CubridNodeType.SYSTEM_VIEW,
			CubridNodeType.USER_VIEW, CubridNodeType.STORED_PROCEDURE_FOLDER,
			CubridNodeType.STORED_PROCEDURE_FUNCTION_FOLDER,
			CubridNodeType.STORED_PROCEDURE_PROCEDURE_FOLDER,
			CubridNodeType.TRIGGER_FOLDER, CubridNodeType.SERIAL_FOLDER,
			CubridNodeType.BROKER_FOLDER, CubridNodeType.BROKER,
			CubridNodeType.BROKER_SQL_LOG,
			CubridNodeType.STATUS_MONITOR_FOLDER,
			CubridNodeType.STATUS_MONITOR_TEMPLATE, CubridNodeType.LOGS_FOLDER,
			CubridNodeType.LOGS_BROKER_FOLDER,
			CubridNodeType.LOGS_BROKER_ACCESS_LOG,
			CubridNodeType.LOGS_BROKER_ERROR_LOG,
			CubridNodeType.LOGS_BROKER_ADMIN_LOG_FOLDER,
			CubridNodeType.LOGS_BROKER_ADMIN_LOG,
			CubridNodeType.LOGS_MANAGER_ACCESS_LOG,
			CubridNodeType.LOGS_MANAGER_ERROR_LOG,
			CubridNodeType.LOGS_SERVER_FOLDER,
			CubridNodeType.LOGS_SERVER_DATABASE_FOLDER,
			CubridNodeType.LOGS_SERVER_DATABASE_LOG };

	public static boolean isCanRefresh(CubridNodeType nodeType) {
		for (CubridNodeType type : canRefreshNodeTypeArr) {
			if (type == nodeType) {
				return true;
			}
		}
		return false;
	}
}
