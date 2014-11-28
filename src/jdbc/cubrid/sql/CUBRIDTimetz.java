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

package cubrid.sql;

import java.sql.Time;

public class CUBRIDTimetz extends Time {
    private static final long serialVersionUID = 2346294268719129394L;
	
	private String timezone;

	public CUBRIDTimetz (long time,  String str_timezone) {
		super (time);
		this.timezone = str_timezone;
	}
	
	public static CUBRIDTimetz valueOf (Time t, String str_timezone) {
		long tmp_time = t.getTime();
		
		CUBRIDTimetz cubrid_time_tz = new CUBRIDTimetz (tmp_time, str_timezone); 

		return cubrid_time_tz;
	}
		
	public static CUBRIDTimetz valueOf(String str_time, String str_timezone) {
		Time tmptime = Time.valueOf(str_time);
		CUBRIDTimetz cubrid_time_tz = new CUBRIDTimetz(tmptime.getTime(), str_timezone); 
		return cubrid_time_tz;
	}
	
	public String toString() {
		if (timezone.isEmpty()) {
			return "" + super.toString();
		}
		else {
			return super.toString() + " " + timezone;
		}
	}

	public String getTimezone() {
		return timezone;
	}
}
