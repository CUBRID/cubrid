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

package com.cubrid.cubridmanager.core.cubrid.dbspace.model;

import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.IModel;

public class DbSpaceInfoList implements
		IModel {

	private static final Logger logger = LogUtil.getLogger(DbSpaceInfoList.class);
	private String dbname = null;

	private int pagesize = 0;

	private int freespace = 0;

	/*
	 * the volume space info list of the database
	 */
	private List<DbSpaceInfo> spaceinfo = null;

	public String getTaskName() {
		return "dbspaceinfo";
	}

	public String getDbname() {
		return dbname;
	}

	public void setDbname(String dbname) {
		this.dbname = dbname;
	}

	public int getPagesize() {
		return pagesize;
	}

	public void setPagesize(int pagesize) {
		this.pagesize = pagesize;
	}

	public synchronized List<DbSpaceInfo> getSpaceinfo() {
		return spaceinfo;
	}

	/**
	 * 
	 * 
	 * @return
	 */
	public synchronized Map<String, DbSpaceInfo> getSpaceInfoMap() {
		Map<String, DbSpaceInfo> map = new TreeMap<String, DbSpaceInfo>();		
		if (spaceinfo == null)
			return map;
		for (DbSpaceInfo bean : spaceinfo) {
			String type = bean.getType().toUpperCase();
			if (map.containsKey(type)) {
				DbSpaceInfo model = map.get(type);
				int free = model.getFreepage();
				int totl = model.getTotalpage();
				model.setFreepage(free + bean.getFreepage());
				model.setTotalpage(totl + bean.getTotalpage());
				model.plusVolumeCount();
			} else {
				DbSpaceInfo model = new DbSpaceInfo();
				try {
					CommonTool.copyBean2Bean(bean, model);
				} catch (IllegalAccessException e) {
					logger.error(e);
				} catch (InvocationTargetException e) {
					logger.error(e);
				}
				model.setSpacename("");
				model.plusVolumeCount();
				map.put(type, model);
			}
		}
		return map;
	}

	public synchronized void setSpaceinfo(List<DbSpaceInfo> spaceinfoList) {
		this.spaceinfo = spaceinfoList;
	}

	public synchronized void addSpaceinfo(DbSpaceInfo info) {
		if (spaceinfo == null)
			spaceinfo = new ArrayList<DbSpaceInfo>();
		if (!spaceinfo.contains(info))
			spaceinfo.add(info);
	}

	public synchronized void removeSpaceinfo(DbSpaceInfo info) {
		if (spaceinfo != null)
			spaceinfo.remove(info);
	}

	public int getFreespace() {
		return freespace;
	}

	public void setFreespace(int freespace) {
		this.freespace = freespace;
	}

}
