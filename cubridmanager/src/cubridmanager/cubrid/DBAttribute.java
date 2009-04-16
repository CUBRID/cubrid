/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubridmanager.cubrid;

import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

import cubridmanager.CommonTool;
import cubridmanager.Messages;

public class DBAttribute {
	public String name;
	public String type;
	public String inherit;
	public boolean isIndexed;
	public boolean isNotNull;
	public boolean isShared;
	public boolean isUnique;
	public String defaultval;

	public DBAttribute(String p_name, String p_type, String p_inherit,
			boolean p_isIndexed, boolean p_isNotNull, boolean p_isShared,
			boolean p_isUnique, String p_defaultval) {
		name = new String(p_name);
		type = new String(p_type);
		inherit = new String(p_inherit);
		isIndexed = p_isIndexed;
		isNotNull = p_isNotNull;
		isShared = p_isShared;
		isUnique = p_isUnique;
		defaultval =p_defaultval;
		resetDefault();
	}
	private void resetDefault(){
		if(type.equalsIgnoreCase("timestamp")){			
			try {
				long timestamp = CommonTool.getTimestamp(defaultval);
				defaultval = CommonTool.getTimestampString(timestamp,
						"yyyy/MM/dd a hh:mm:ss");
			} catch (ParseException e) {				
				// do nothing
			}					
		}else if(type.equalsIgnoreCase("date")){
			try {
				long timestamp = CommonTool.getDate(defaultval);
				defaultval = CommonTool.getTimestampString(timestamp,
						"yyyy/MM/dd");
			} catch (ParseException e) {
				// do nothing
			}							
		}else if(type.equalsIgnoreCase("time")){
			try {
				long timestamp = CommonTool.getTime(defaultval);
				defaultval = CommonTool.getTimestampString(timestamp,
						"a hh:mm:ss");
			} catch (ParseException e) {
				// do nothing
			}							
		}else if(type.toLowerCase().startsWith("char")){ //include character and character varying
			if(defaultval==null||defaultval.equals("")){
				//do noting
			}else{
				defaultval="'"+defaultval+"'";
			}			
		}if(type.toLowerCase().startsWith("national")){ //include national character and national character varying
			//TODO
			//do nothing for the server client does not support bit and nchar					
		}else if(type.toLowerCase().startsWith("bit")){ //include bit and bit varying
			//TODO
			//do nothing for the server client does not support bit and nchar
		}else if(type.toUpperCase().startsWith("NUMERIC")||
				type.toUpperCase().startsWith("INTEGER")||
				type.toUpperCase().startsWith("SMALLINT")||
				type.toUpperCase().startsWith("FLOAT")||
				type.toUpperCase().startsWith("DOUBLE")||
				type.toUpperCase().startsWith("MONETARY")){
			//do nothing for it is pretty good.
		}		
	}
	/**
	 * to format customs' many types of attribute default value into standard attribute default value
	 * @param atttype String attribute type
	 * @param attdeft String attribute default value
	 * @return String standard attribute default value
	 * @throws ParseException
	 */
	public static String formatDefault(String atttype,String attdeft) throws ParseException{
		
		if(atttype.equalsIgnoreCase("timestamp")){			
				if(attdeft.equalsIgnoreCase("systimestamp") ||attdeft.equalsIgnoreCase("sys_timestamp")){
					return "systimestamp";
				}else if(attdeft.equalsIgnoreCase("currenttimestamp") || attdeft.equalsIgnoreCase("current_timestamp")){
					return "current_timestamp";
				}else if(attdeft.toLowerCase().startsWith("timestamp")||attdeft.equals("")){
					return attdeft;				
				}else{
					try {
						Long.parseLong(attdeft);
						return attdeft;
					} catch (NumberFormatException nfe) {
						try {
								return String.valueOf(CommonTool.getTimestamp(attdeft));
						} catch (ParseException e) {
							if(CommonTool.validateTimestamp(attdeft, "HH:mm:ss mm/dd")
									||CommonTool.validateTimestamp(attdeft, "mm/dd HH:mm:ss")
									||CommonTool.validateTimestamp(attdeft, "hh:mm:ss a mm/dd")
									||CommonTool.validateTimestamp(attdeft, "mm/dd hh:mm:ss a")){
								return "TIMESTAMP'"+attdeft+"'";
							}else{
								throw e;
							}
						}
					}										
				}
		}else if(atttype.equalsIgnoreCase("date")){			
				if(attdeft.equalsIgnoreCase("sysdate") ||attdeft.equalsIgnoreCase("sys_date")){
					return "sysdate";
				}else if(attdeft.equalsIgnoreCase("currentdate") || attdeft.equalsIgnoreCase("current_date")){
					return "current_date";
				}else if(attdeft.toLowerCase().startsWith("date")||attdeft.equals("")){
					return attdeft;				
				}else{
					try {
						long timestamp = CommonTool.getDate(attdeft);
						return "DATE'"
								+ CommonTool.getTimestampString(timestamp,
										"MM/dd/yyyy''");
					} catch (ParseException e) {
						if(CommonTool.validateTimestamp(attdeft, "MM/dd")){
							return  "DATE'"+attdeft+"'";
						}else{
							throw e;
						}
					}
					
				}
		}else if(atttype.equalsIgnoreCase("time")){			
				if(attdeft.equalsIgnoreCase("systime") ||attdeft.equalsIgnoreCase("sys_time")){
					return "systime";
				}else if(attdeft.equalsIgnoreCase("currenttime") || attdeft.equalsIgnoreCase("current_time")){
					return "current_time";
				}else if(attdeft.toLowerCase().startsWith("time")||attdeft.equals("")){
					return attdeft;				
				}else{
					try {
						Long.parseLong(attdeft);
						return attdeft;
					} catch (NumberFormatException nfe) {
						long timestamp=CommonTool.getTime(attdeft);
						return  "TIME'"+CommonTool.getTimestampString(timestamp, "HH:mm:ss''");
					}										
				}
		}else if(atttype.toLowerCase().startsWith("char")||atttype.toLowerCase().startsWith("varchar")){
			if(attdeft.startsWith("'")&&attdeft.endsWith("'")||attdeft.equals("")){
				return attdeft;
			}else{
				return "'"+attdeft.replaceAll("'", "''")+"'";
			}
		}else if(atttype.toLowerCase().startsWith("nchar")||atttype.toLowerCase().startsWith("nchar varying")){
			if(attdeft.startsWith("N'")&&attdeft.endsWith("'")||attdeft.equals("")){
				return attdeft;
			}else{
				return "N'"+attdeft.replaceAll("'", "''")+"'";
			}
		}else if(atttype.toLowerCase().startsWith("bit")||atttype.toLowerCase().startsWith("bit varying")){
			if(attdeft.startsWith("B'")&&attdeft.endsWith("'")||attdeft.startsWith("X'")&&attdeft.endsWith("'")||attdeft.equals("")){
				return attdeft;
			}else{
				return "B'"+attdeft+"'";
			}
		}else if (atttype.toLowerCase().startsWith("set_of")||atttype.toLowerCase().startsWith("multiset_of")||atttype.toLowerCase().startsWith("sequence_of")) {
			if (attdeft.startsWith("{")&&attdeft.endsWith("}")||attdeft.equals(""))
				return attdeft;
			else {
				//assert every set/multiset/sequence has only type
				assert(-1==atttype.indexOf(","));
				int index=atttype.indexOf("(");
				String subtype=atttype.substring(index+1,atttype.length()-1);
				StringBuffer bf=new StringBuffer();
				if(-1==attdeft.indexOf(",")){
					bf.append(formatDefault(subtype,attdeft));
				}else{
					String[] values=attdeft.split(",");
					for (int j = 0; j < values.length; j++) {	                    
						String value=values[j];
						if(j>0){
							bf.append(",");
						}
						bf.append(formatDefault(subtype,value));
					}
				}
				return "{"+bf.toString()+"}";
			}
		}			
		return attdeft;		
	}
	
}
