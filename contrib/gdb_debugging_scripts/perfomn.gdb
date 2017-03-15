#
# Performance monitor scripts.
#

# perfmon_start_offset
# $arg0 (in) : PSTAT_ID
#
# Print the start offset for performance statistic
#
define perfmon_start_offset
  p pstat_Metadata[$arg0].start_offset
  end

# perfmon_value_at_offset
# $arg0 (in) : offset
#
# Print the global statistic value at offset
#  
define perfmon_value_at_offset
  p pstat_Global.global_stats[$arg0]
  end

# perfmon_print_stat
# $arg0 (in) : PSTAT_ID
#
# Print the value at start offset for given statistic
#
define perfmon_print_stat
  p pstat_Global.global_stats[pstat_Metadata[$arg0].start_offset]
  end

# perfmon_print_stat
# $arg0 (in) : PSTAT_ID
#
# Print the counter/timer values for given statistic
#  
define perfmon_print_time_stat
  p pstat_Global.global_stats[pstat_Metadata[$arg0].start_offset]
  p pstat_Global.global_stats[pstat_Metadata[$arg0].start_offset + 1]
  p pstat_Global.global_stats[pstat_Metadata[$arg0].start_offset + 2]
  if (pstat_Global.global_stats[pstat_Metadata[$arg0].start_offset] > 0)
    p pstat_Global.global_stats[pstat_Metadata[$arg0].start_offset + 1] / pstat_Global.global_stats[pstat_Metadata[$arg0].start_offset]
  else
    p 0
    end
  end