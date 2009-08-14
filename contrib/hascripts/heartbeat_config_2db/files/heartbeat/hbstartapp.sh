#!/bin/sh
cibadmin -M -X '<nvpair id="ms_cubrid_nbd_metaattr_target_role" name="target_role" value="started"/>'
sleep 5
cibadmin -M -X '<nvpair id="group_cubrid_nbd_0_logdup1_metaattr_target_role" name="target_role" value="started"/>'
cibadmin -M -X '<nvpair id="group_cubrid_nbd_0_logdup2_metaattr_target_role" name="target_role" value="started"/>'
cibadmin -M -X '<nvpair id="group_cubrid_nbd_1_logdup0_metaattr_target_role" name="target_role" value="started"/>'
cibadmin -M -X '<nvpair id="group_cubrid_nbd_1_logdup2_metaattr_target_role" name="target_role" value="started"/>'
cibadmin -M -X '<nvpair id="group_cubrid_nbd_2_logdup0_metaattr_target_role" name="target_role" value="started"/>'
cibadmin -M -X '<nvpair id="group_cubrid_nbd_2_logdup1_metaattr_target_role" name="target_role" value="started"/>'

