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
package com.cubrid.cubridmanager.ui.cubrid.dbspace;

import org.eclipse.osgi.util.NLS;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * 
 * Message bundle classes. Provides convenience methods for manipulating
 * messages.
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-2 created by lizhiqiang
 */
public class Messages extends NLS {

	static {
		NLS.initializeMessages(CubridManagerUIPlugin.PLUGIN_ID + ".cubrid.dbspace.Messages", Messages.class);
	}
	// add volume
	public static String errorPathMsg;
	public static String errorVolumeMsg;
	public static String errorPageMsg;
	public static String noPathMsg;
	public static String pathLblName;
	public static String pagesLblName;
	public static String volumeSizeLblName;
	public static String pathToolTip;
	public static String pagesToolTip;
	public static String volumeSizeToolTip;
	public static String dialogTitle;
	public static String dialogMsg;
	public static String purposeLbllName;
	public static String tempOfPurpose;
	public static String indexOfPurpose;
	public static String dataOfPurpose;
	public static String genericOfPurpose;

	// set auto add volume
	public static String setDialogTitle;
	public static String setDialogMsg;
	public static String dataGroupTitle;
	public static String dataUseAutoVolBtnText;
	public static String dataOutOfSpaceRateLbl;
	public static String dataExtPageLbl;
	public static String indexGroupTitle;
	public static String indexUseAutoVolBtnText;
	public static String indexOutOfSpaceRateLbl;
	public static String datavolumeLbl;
	public static String indexvolumeLbl;
	public static String indexExtPageLbl;
	
	public static String errorRate;
	public static String errorVolume;
	public static String errorPage;

	// volume folder editor
	public static String msgVolumeFolderInfo;
	public static String msgVolumeFolderName;
	public static String msgVolumeFolderUsedSize;
	public static String msgVolumeFolderSize;
	public static String msgVolumeFolderTotalSize;
	public static String msgVolumeFolderTotalPage;
	public static String tblVolumeFolderType;
	public static String tblVolumeFolderVolumeCount;
	public static String tblVolumeFolderFreeSize;
	public static String tblVolumeFolderTotalSize;
	public static String tblVolumeFolderPageSize;
	public static String chartMsgFreeSize;
	public static String chartMsgUsedSize;
	public static String lblDataBaseVersion;
	public static String lblDataBaseStatus;
	public static String lblDataBaseStartedStatus;
	public static String lblDataBaseStopStatus;
	public static String lblDataBaseUserAuthority;
	public static String lblDatabasePaseSize;
	public static String lblDatabaseTotalSize;
	public static String lblDatabaseRemainedSize;
	public static String lblSpaceLocation;
	public static String lblSpaceDate;
	public static String lblSpaceType;
	public static String lblFreeSize;
	
	//auto added volume logs
	public static String logsHolderGrpName;
	public static String volumeLogMsg;
	public static String volumeLogTtl;
	public static String shellVolumeLogTtl;
	public static String volumeNameInTbl;
	public static String numPagesInTbl;
	public static String statusInTbl;
	public static String purposeInTbl;
	public static String databaseInTbl;
	public static String timeInTbl;
	public static String cancelBtn;
	public static String refreshBtn;
}