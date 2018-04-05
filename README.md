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

1. kernel compile in linux-4.12 folder
   - You must either apply kernel patch or install linux kernel-4.12 uploaded here. 
   - We recommend installing linux-4.12 because kernel might fail to compile on other version of linux.
   - After you download kernel source code, change directory to linux folder and compile kernel using command below.

	'''
	make
	make install
	'''

   - Add "-j <number of core>" option to compile faster.

	'''make -j 4'''

2. install scheduling module 
  - "lkm" folder has a module source code, header file, Makefile.
  - change current directory to lkm folder and compile a module using "make". 
  - If compile is done, you would get a loadable kernel module "vif.ko".
  - Use "insmod vif.ko" command to install a module to kernel.
  - Use "lsmod" command to show which loadable kernel modules are currently loaded.
  - Use "rmmod vif.ko" command to remove a module.

## how to use ----working on.....
  - run or start docker container
	must install module first because containers which are executed before module installation are not affected by module
  - use proc file system to set weight, min, max bandwidth of each container if needed
	-ex)"echo 50 > /proc/oslab/vif1/max_credit" sets maximum bandwidth of first container 50
	-ex)"cat /proc/oslab/vif2/weight" print weight of second container
	
