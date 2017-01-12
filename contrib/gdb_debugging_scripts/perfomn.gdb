#
# Performance monitor scripts.
#
define perfmon_print_stat
  p pstat_Global.global_stats[pstat_Metadata[$arg0].start_offset]
  end