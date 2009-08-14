package nbench.util.simple;

public class TimeZone {
	private int from_millis;
	private int to_millis;
	private int interval_millis;
	private int zone_begin;
	private int zone_end;

	/* left inclusive */
	public enum ZoneRelation {
		LEFT, IN, RIGHT 
	}

	public TimeZone(int from_secs, int to_secs, int interval_secs) {
		from_millis = from_secs * 1000;
		to_millis = to_secs * 1000;
		interval_millis = interval_secs * 1000;

		zone_begin = from_millis;
		adjust_zone_end();
	}

	private void adjust_zone_end() {
		zone_end = zone_begin + interval_millis;
		if (zone_end > to_millis) {
			zone_end = to_millis;
		}
	}

	public boolean outOfRange() {
		return zone_begin >= to_millis;
	}
	
	public void nextZone() {
		zone_begin = zone_end;
		adjust_zone_end();
	}
	
	public ZoneRelation getRelation(int time) {
		if(time < zone_begin) {
			return ZoneRelation.LEFT;
		} else if (time >= zone_end) {
			return ZoneRelation.RIGHT;
		} else {
			return ZoneRelation.IN;
		}
	}
	
	public String zoneToString() {
		return "[" + zone_begin + " ~ " + zone_end + "]";
	}
}
