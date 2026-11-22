--
--  Copyright 2016 CUBRID Corporation
--
--   Licensed under the Apache License, Version 2.0 (the "License");
--   you may not use this file except in compliance with the License.
--   You may obtain a copy of the License at
--
--       http://www.apache.org/licenses/LICENSE-2.0
--
--   Unless required by applicable law or agreed to in writing, software
--   distributed under the License is distributed on an "AS IS" BASIS,
--   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
--   See the License for the specific language governing permissions and
--   limitations under the License.
--

-- System metadata version 1 to 2.
--
-- CBRD-27508 (parallel applylogdb): the stream ID of _db_ha_apply_info and three catalog classes.
-- Same definitions as system_catalog_initializer::get_ha_apply_info, get_ha_apply_stat, get_ha_apply_worker_stat
-- and get_ha_applied_tx_marker (schema_system_catalog_install.cpp). upgradedb marks the new classes as system
-- classes after the scripts run.

-- A class attribute allocates IDs even after every apply-info row has been removed.
ALTER TABLE _db_ha_apply_info ADD CLASS ATTRIBUTE last_stream_id BIGINT DEFAULT 0;
ALTER TABLE _db_ha_apply_info ADD COLUMN stream_id BIGINT FIRST;
UPDATE _db_ha_apply_info a SET stream_id =
  (SELECT COUNT(*) + 1 FROM _db_ha_apply_info b
   WHERE b.db_name < a.db_name OR (b.db_name = a.db_name AND b.copied_log_path < a.copied_log_path));
UPDATE CLASS _db_ha_apply_info SET last_stream_id =
  (SELECT COALESCE(MAX(stream_id), 0) FROM _db_ha_apply_info);
ALTER TABLE _db_ha_apply_info MODIFY stream_id BIGINT NOT NULL;
ALTER TABLE _db_ha_apply_info ADD UNIQUE (stream_id);

-- parallel apply statistics of each applylogdb on this node, one row per applier
CREATE TABLE _db_ha_apply_stat (
  db_name VARCHAR(255) NOT NULL,
  copied_log_path VARCHAR(4096) NOT NULL,
  start_time DATETIME,
  last_update_time DATETIME,
  worker_count INTEGER,
  apply_state VARCHAR(32),
  done_hold_since DATETIME,
  commit_read_count BIGINT,
  dispatch_direct_count BIGINT,
  dispatch_parked_count BIGINT,
  inflight_count INTEGER,
  pending_count INTEGER,
  task_count BIGINT,
  dependency_wait_count BIGINT,
  dependency_wait_usec BIGINT,
  worker_wait_count BIGINT,
  worker_wait_usec BIGINT,
  queue_wait_usec BIGINT,
  result_collect_wait_usec BIGINT,
  total_task_usec BIGINT,
  total_worker_busy_usec BIGINT,
  out_of_order_completion_count BIGINT,
  running_worker_count INTEGER,
  max_running_worker_count INTEGER,
  max_task_queue_count INTEGER,
  max_pending_count INTEGER,
  observation_usec BIGINT,
  concurrency_usec VARCHAR(1024),
  UNIQUE (db_name, copied_log_path)
) DONT_REUSE_OID;

GRANT SELECT ON _db_ha_apply_stat TO information_schema;

-- per-worker statistics of each applylogdb on this node, one row per running worker
CREATE TABLE _db_ha_apply_worker_stat (
  db_name VARCHAR(255) NOT NULL,
  copied_log_path VARCHAR(4096) NOT NULL,
  worker_idx INTEGER NOT NULL,
  start_time DATETIME,
  last_update_time DATETIME,
  state VARCHAR(16),
  applied_task_count BIGINT,
  applied_row_count BIGINT,
  busy_usec BIGINT,
  queue_wait_usec BIGINT,
  error_count BIGINT,
  applying_commit_lsa_pageid BIGINT,
  applying_commit_lsa_offset INTEGER,
  applying_master_commit_time DATETIME,
  applying_since DATETIME,
  last_applied_commit_lsa_pageid BIGINT,
  last_applied_commit_lsa_offset INTEGER,
  last_applied_master_commit_time DATETIME,
  last_applied_time DATETIME,
  last_error_code INTEGER,
  last_error_time DATETIME,
  UNIQUE (db_name, copied_log_path, worker_idx)
) DONT_REUSE_OID;

GRANT SELECT ON _db_ha_apply_worker_stat TO information_schema;

-- commits an applylogdb on this node has applied above its stored position: range or list markers
CREATE TABLE _db_ha_applied_tx_marker (
  stream_id BIGINT NOT NULL,
  commit_pageid BIGINT NOT NULL,
  commit_offset INTEGER NOT NULL,
  applied_lsas VARCHAR(4096),
  UNIQUE (stream_id, commit_pageid, commit_offset)
) DONT_REUSE_OID;
