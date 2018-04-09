# README

This repository includes loadable kernel module which dynamically allocates network bandwidth to docker containers. Our goal is to guarantee QOS requirement of containers through dynamic allocation of network resources in virtualization. We used scheduling method proposed in the paper [ANCS](https://www.hindawi.com/journals/sp/2016/4708195/abs/)*. 

> *ANCS: Achieving QoS through Dynamic Allocation of Network Resources in Virtualized Clouds




## Scheduling policies

This module allocates network bandwidth of containers dynamically and propotionally to weight. 
   - Bandwidth of each container is set to "(weight / total weight) * (bandwidth capacity)".
   - Ex) If we have running containers A,B,C with weight 1,2,2, then each container uses 20%, 40%, 40% of maximum bandwidth capacity.


You can set upper, lower limit of container's bandwidth.
   - If you give a percentage value to "maximum_bandwidth" using proc file system, then upper limit of container's bandwidth is set to (percentage value) * (bandwidth capacity).
   - Setting lower limit can be done by changing "minimum_bandwidth" as above.
   - Ex) If you set maximum bandwidth 50, then container gets 50% of bandwidth capacity. Bandwidth capacity must be adjusted depending on the device which the module is installed on).


This module supports work-conserving.
   - If there's container not fully using it's bandwidth, then remaining bandwidth is reallocated to other container's so that utilization of network resources can be maximized.





## Install guide

1. compile kernel in linux-4.12 folder
   
	- You must either apply kernel patch or install linux kernel-4.12 uploaded here. 
   	- We recommend installing linux-4.12 because compilation may fail on other version of linux.
   	- After you download kernel source code, change directory to linux folder and compile kernel using command below.
  
  			make
			make install

	
	- Add "-j <number of cores>" option to compile faster. Assume that there're 4 cores, use command below

			make -j 4


	- reboot with installed kernel.





2. install scheduling module 
   
	- "lkm" folder has a module source code, header file, Makefile.
	- change current directory to "lkm" folder and compile a module using "make". 
	- When compilation is done, you would get a loadable kernel module "vif.ko".
   
	- a command that add a module to kernel.
	
			insmod vif.ko
	

	- a command that shows which loadable kernel modules are currently loaded.
	
			lsmod
	
	- a command that removes a module.
	
			rmmod vif.ko
	

## How to use 

#### Run or start docker container after adding a module
   - must install a module first because containers which are executed before a module installation are not affected by a module


#### Use proc file system to set weight, min, max bandwidth of each container if needed
   - weight is set to 1 by default
   - min, max credit is set to 0 by default, meaning it has no upper, under limitaion of bandwidth.
    
  
#### Printing attributes of each container.
  
   - A command that prints weight of second container. vif stands for virtual interface.

			cat /proc/oslab/vif2/weight		
	
	
   - A command that prints a maximum bandwidth of first container in form of  percentage of bandwidth capacity.

			cat /proc/oslab/vif1/max_credit		
	
	
   - A command that prints a minimum bandwidth of first container in form of  percentage of bandwidth capacity.
	
			cat /proc/oslab/vif1/min_credit		
	
	
	
#### Setting attributes of each container
	
   - A command that sets a weight of first continaer "2". A bigger weight means a bigger priority.
	
			echo 2 > /proc/oslab/vif1/weight	
	

   - A command that sets maximum bandwidth of first container "50". 
   - meaning this container can get 50% of bandwidth capacity at maximum.
	
			echo 50 > /proc/oslab/vif1/max_credit 	
	
	
   - A command that sets minimum bandwidth of first container "30"
   - meaning this container must get 30% of bandwidth capacity at least.
	
			echo 30 > /proc/oslab/vif1/min_credit
	
