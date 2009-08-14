
TEST Configuration 
================

 o M2(milestone #2) TEST Configuration 

	- heartbeat node #1(Master) 	: xen4
	- heartbeat node #2(1st Slave) 	: xen5
	- heartbeat node #3(2nd Slave)	: xen6
	- db 			    			: nbd

 /---------------------------\		         /---------------------------\		
 | Master node             	 |		         | 1st Slave node          	 |		
 |      				     |               |     					 	 |
 | /---- master/slave -----\ |  		     | /---- master/slave -----\ | 
 | | 					   | |<- Heartbeat ->| |					   | |
 | | cub_nbd:0(master)	   | |  		     | | cub_nbd:1(1st slave)  | |
 | | 					   | |  		     | | 					   | |
 | \-----------------------/ |  		     | \-----------------------/ |
 |    					     | 		         |   					 	 |
 | /---- group #1 ---------\ |  		     | /---- group #3 ---------\ | 
 | | cub_nbd_0_lw_1	       | |  		     | | cub_nbd_1_lw_0		   | |
 | | cub_nbd_0_la_1	       | |  		     | | cub_nbd_1_la_0		   | |
 | \-----------------------/ |  		     | \-----------------------/ |
 |    					     |  		     |   					     |
 | /---- group #2 ---------\ |  		     | /---- group #4 ---------\ | 
 | | cub_nbd_0_lw_2	       | |  		     | | cub_nbd_2_lw_0		   | |
 | | cub_nbd_0_la_2	       | |  		     | | cub_nbd_2_la_0		   | |
 | \-----------------------/ |  		     | \-----------------------/ |
 |    					     |  		     |   					     |
 \---------------------------/ 			     \---------------------------/		
                       /\                         /\
                        \                         /
                         \                       /
                      Heartbeat              Heartbeat
                           \                   / 
                            \                 /
					         \/              \/ 
                       /---------------------------\
                       | 2nd Slave node            |
                       |      				       |
                       | /---- master/slave -----\ |
                       | | 					     | |
                       | | cub_nbd:2(2nd slave)  | |
                       | | 					     | |
                       | \-----------------------/ |
                       |    					   |
                       | /---- group #5 ---------\ |
                       | | cub_nbd_2_lw_0	     | |
                       | | cub_nbd_2_la_0	     | |
                       | \-----------------------/ |
                       |    					   |
                       | /---- group #6 ---------\ |
                       | | cub_nbd_2_lw_1	     | |
                       | | cub_nbd_2_la_1	     | |
                       | \-----------------------/ |
                       |    					   |
                       \---------------------------/




File List
================

 o config/ha.cf						- M2 heartbeat node and basic configuration
 o config/authkeys					- common heartbeat authentication key  
 o config/logd.cf					- common heartbeat log configuration
 o ocf/*							- heartbeat ocf(open cluster framework) files
 o resources/crm_config-M2.xml		- crm config 
 o resources/constraints_with_colocation-M2.xml		- M2 constraints config 
 o resources/resources-M2.xml						- M2 resources config

Other Useful Samples
========================
 . resources/resources-using-normal-user-M2.xml		- normal user resources
 . resources/constraints-M2.xml						- another constraints
 


How to Apply M2 Heartbeat Configuration
===========================================

  * (A) on all nodes
  * (O) on one node

	1) (A) stop heartbeat service

		sh> /etc/init.d/heartbeat stop 


	2) (A) copy common configuration and ocf files 
		
		sh> cp config/ha.cf /etc/ha.d/ha.cf
		sh> cp config/authkeys /etc/ha.d
		sh> cp config/logd.cf /etc/ha.d
		sh> cp ocf/* /usr/lib/ocf/resource.d/heartbeat
		sh> vi /etc/ha.d/ha.cf	
				-> change ucast and node 


	3) (A) start heartbeat service 

		sh> /etc/init.d/heartbeat start


	4) (O) change database and logpath value in constraints_with_colocation-M2.xml and resources-M2.xml


		- xen4 	: NODE #1 
		- xen5 	: NODE #2    
		- xen6 	: NODE #3    
		- nbd   : DB #1

		* replace NODE #1, NODE #2, NODE #3, DB #1 to your node-name and db-name


		sh> cd resources
		sh> sed 's/xen4/NODE #1/g' constraints_with_colocation-M2.xml \
			> | sed 's/xen5/NODE #2/g' \
			> | sed 's/xen6/NODE #3/g' \
			> | sed 's/nbd/DB #1/g' \
			> > constraints_with_colocation-M2.TMP.xml	
		sh> sed 's/xen4/NODE #1/g' resources-M2.xml \
			> | sed 's/xen5/NODE #2/g' \
			> | sed 's/xen6/NODE #3/g' \
			> | sed 's/nbd/DB #1/g' \
			> > resources-M2.TMP.xml	


	5) (O) apply constraints and resources

		sh> cibadmin -o crm_config -R -x ./crm_config-M2.xml
		sh> cibadmin -o constraints -R(or -C) -x ./constraints_with_colocation-M2.TMP.xml
		sh> cibadmin -o resources -R(or -C) -x ./resources-M2.TMP.xml



 
