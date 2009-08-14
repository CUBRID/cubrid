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
package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.util.List;
/**
 * to provide methods for generating default name for PK,FK,...
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */
public class SystemNamingUtil {
	/**
	 * default name for reverse unique index
	 * 
	 * @param tableName
	 * @param attrList
	 * @return
	 */
	public static String getReverseUniqueName(String tableName,
			List<String> attrList) {
		assert (attrList.size() > 0);
		StringBuffer bf=new StringBuffer();
		bf.append("ru_").append(tableName);
		for(String attr:attrList){
			bf.append("_").append(attr);
		}
		return bf.toString();
	}
	/**
	 * default name for unique index
	 * 
	 * @param tableName
	 * @param attrList
	 * @return
	 */
	public static String getUniqueName(String tableName,
			List<String> ruleList) {
		assert (ruleList.size() > 0);
		StringBuffer bf=new StringBuffer();
		bf.append("u_").append(tableName);		
		for(String rule:ruleList){
			String[] strs=rule.split(" ");
			bf.append("_").append(strs[0]);
			if(strs[1].equalsIgnoreCase("DESC")){
				bf.append("_d");
			}			
		}
		return bf.toString();
	}
	/**
	 * default name for PK
	 * 
	 * @param tableName
	 * @param attrList
	 * @return
	 */
	public static String getPKName(String tableName,
			List<String> attrList) {
		assert (attrList.size() > 0);
		StringBuffer bf=new StringBuffer();
		bf.append("pk_").append(tableName);
		for(String attr:attrList){
			bf.append("_").append(attr);
		}
		return bf.toString();
	}
	/**
	 * default name for FK
	 * 
	 * @param tableName
	 * @param attrList
	 * @return
	 */
	public static String getFKName(String tableName,
			List<String> attrList) {
		assert (attrList.size() > 0);
		StringBuffer bf=new StringBuffer();
		bf.append("fk_").append(tableName);
		for(String attr:attrList){
			bf.append("_").append(attr);
		}
		return bf.toString();
	}
	/**
	 * default name for index
	 * 
	 * @param tableName
	 * @param ruleList
	 * @return
	 */
	public static String getIndexName(String tableName,
			List<String> ruleList) {
		assert (ruleList.size() > 0);
		StringBuffer bf=new StringBuffer();
		bf.append("i_").append(tableName);
		for(String rule:ruleList){
			String[] strs=rule.split(" ");
			bf.append("_").append(strs[0]);
			if(strs[1].equalsIgnoreCase("DESC")){
				bf.append("_d");
			}			
		}
		return bf.toString();
	}
	/**
	 * default name for reverse index
	 * 
	 * @param tableName
	 * @param ruleList
	 * @return
	 */
	public static String getReverseIndexName(String tableName,
			List<String> attrList) {
		assert (attrList.size() > 0);
		StringBuffer bf=new StringBuffer();
		bf.append("ri_").append(tableName);
		for(String attr:attrList){
			bf.append("_").append(attr);
		}
		return bf.toString();
	}
}
