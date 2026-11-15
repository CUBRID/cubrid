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

-- CBRD-27508: migrate a development DB that already has the path-keyed marker catalog.
-- Use this script instead of cbrd27508.sql when the earlier CBRD-27508 catalog script was already applied.
-- Matching markers are retained; orphan markers had no apply-info row and were discarded on applier startup.

-- A class attribute allocates IDs even after every apply-info row has been removed.
ALTER TABLE _db_ha_apply_info ADD CLASS ATTRIBUTE last_stream_id BIGINT DEFAULT 0;
ALTER TABLE _db_ha_apply_info ADD COLUMN stream_id BIGINT;
UPDATE _db_ha_apply_info a SET stream_id =
  (SELECT COUNT(*) + 1 FROM _db_ha_apply_info b
   WHERE b.db_name < a.db_name OR (b.db_name = a.db_name AND b.copied_log_path < a.copied_log_path));
UPDATE CLASS _db_ha_apply_info SET last_stream_id =
  (SELECT COALESCE(MAX(stream_id), 0) FROM _db_ha_apply_info);
ALTER TABLE _db_ha_apply_info MODIFY stream_id BIGINT NOT NULL;
ALTER TABLE _db_ha_apply_info ADD UNIQUE (stream_id);

ALTER TABLE _db_ha_applied_tx_marker ADD COLUMN stream_id BIGINT;
UPDATE _db_ha_applied_tx_marker m SET stream_id =
  (SELECT a.stream_id FROM _db_ha_apply_info a WHERE a.copied_log_path = m.copied_log_path);
DELETE FROM _db_ha_applied_tx_marker WHERE stream_id IS NULL;
ALTER TABLE _db_ha_applied_tx_marker MODIFY stream_id BIGINT NOT NULL;
ALTER TABLE _db_ha_applied_tx_marker ADD UNIQUE (stream_id, commit_pageid, commit_offset);
ALTER TABLE _db_ha_applied_tx_marker DROP COLUMN copied_log_path;
