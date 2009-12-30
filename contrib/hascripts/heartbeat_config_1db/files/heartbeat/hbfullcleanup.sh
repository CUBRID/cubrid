#!/bin/sh
#NODES="xen4 xen5 xen6"
NODES=`crmadmin -N | awk '{print $3}'`
for i in $NODES
do
        crm_resource -C -r cubrid_nbd:0 -H $i
        crm_resource -C -r cubrid_nbd:1 -H $i
        crm_resource -C -r cubrid_nbd:2 -H $i
        crm_resource -C -r cubrid_nbd_0_lw1 -H $i
        crm_resource -C -r cubrid_nbd_0_la1 -H $i
        crm_resource -C -r cubrid_nbd_0_lw2 -H $i
        crm_resource -C -r cubrid_nbd_0_la2 -H $i
        crm_resource -C -r cubrid_nbd_1_lw0 -H $i
        crm_resource -C -r cubrid_nbd_1_la0 -H $i
        crm_resource -C -r cubrid_nbd_1_lw2 -H $i
        crm_resource -C -r cubrid_nbd_1_la2 -H $i
        crm_resource -C -r cubrid_nbd_2_lw0 -H $i
        crm_resource -C -r cubrid_nbd_2_la0 -H $i
        crm_resource -C -r cubrid_nbd_2_lw1 -H $i
        crm_resource -C -r cubrid_nbd_2_la1 -H $i
done 

