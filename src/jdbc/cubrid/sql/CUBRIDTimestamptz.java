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

import java.sql.Timestamp;
import cubrid.sql.CUBRIDTimestamp;
import cubrid.jdbc.jci.UJCIUtil;
import cubrid.jdbc.jci.UJCIUtil.TimeInfo;
import cubrid.jdbc.driver.*;
import java.text.SimpleDateFormat;
import java.text.DateFormat;
import java.util.TimeZone;
import java.util.Calendar;

public class CUBRIDTimestamptz extends CUBRIDTimestamp {
    private static final long serialVersionUID = 6217189754717078421L;

	private String timezone;


	public CUBRIDTimestamptz(long time, boolean isDatetime, String str_timezone) {
		super (time, isDatetime);
		this.isDatetime = isDatetime;
		this.timezone = str_timezone;
	}

	public CUBRIDTimestamptz(String str_CUBRIDTimestamptz) throws CUBRIDException{
		super(0, false);

		TimeInfo timeinfo = new TimeInfo();
		long time = 0;

		timeinfo = UJCIUtil.parseStringTime(str_CUBRIDTimestamptz);
		Timestamp tmptime = Timestamp.valueOf(timeinfo.time);
		time = tmptime.getTime();
		if (timeinfo.isPM){
			time += 43200000; // 12 hours in milliseconds
		}
		
		Calendar cal = Calendar.getInstance();
		int utcOffset = (cal.get(Calendar.ZONE_OFFSET) + cal.get(Calendar.DST_OFFSET));

		time = time + utcOffset;
		
		setTime(time);
		this.timezone = timeinfo.timezone;		
		this.isDatetime = timeinfo.isDatetime;
	}

	public static CUBRIDTimestamptz valueOf (CUBRIDTimestamp t, String str_timezone) {
		long tmp_time = t.getTime();

		CUBRIDTimestamptz cubrid_ts_tz = new CUBRIDTimestamptz (tmp_time, !CUBRIDTimestamp.isTimestampType (t), str_timezone);

		return cubrid_ts_tz;
	}
	
	public static CUBRIDTimestamptz valueOf(String str_timestamp, boolean isdt, String str_timezone) {
		Timestamp tmptime = Timestamp.valueOf(str_timestamp);
		Calendar cal = Calendar.getInstance();
		int utcOffset = (cal.get(Calendar.ZONE_OFFSET) + cal.get(Calendar.DST_OFFSET));

		CUBRIDTimestamptz cubrid_ts_tz = new CUBRIDTimestamptz(tmptime.getTime() + utcOffset, isdt, str_timezone);
		return cubrid_ts_tz;
	}


	private String timestamptoString() {
		SimpleDateFormat df;
		int millis = this.getNanos() / 1000000;
		String millisString ="";

		/* for milliseconds, we don't print trailing zeros */
		df = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.");
		df.setTimeZone(TimeZone.getTimeZone("UTC"));
		
		if ((millis % 10) != 0) {
			millisString = String.format("%03d", millis);
		} else if (millis % 100 != 0) {
			millisString = String.format("%02d", millis / 10);
		} else {
			millisString = String.format("%01d", millis / 100);
		}
			
		return df.format(this) + millisString;
	}

	public String toString() {
		if (timezone.isEmpty()) {
			return timestamptoString();
		}
		else {
			return timestamptoString() + " " + timezone;
		}
	}

	public String getTimezone() {
		return timezone;
	}
	
	public long getUnixTime () {
		String dateString = timestamptoString ();
		String adjustedTimezone;
		
		java.util.Date parsedDate;
		
		if (timezone.charAt(0) == '+' || timezone.charAt(0) == '-') {
			adjustedTimezone = "GMT" + timezone;
		} else if (timezone.indexOf(" ") > 0) {
			adjustedTimezone = timezone.substring(timezone.indexOf(" ") + 1);
		} else {
			adjustedTimezone = timezone;
		}

		dateString = timestamptoString () + " " + adjustedTimezone;
        	DateFormat dateFormatLocal = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS z");
        	try {
			parsedDate = dateFormatLocal.parse(dateString);
		} catch (java.text.ParseException e) {
			return -1;
		}

		return parsedDate.getTime();

	}

}
