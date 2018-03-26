# linux_4.12.0
linux_4.12.0 with container QOS module

ver 1.0
-can set each container's network bandwidth using a weight.
-network bandwidth is dynamically and propotionally allocated using weight
-can set each container's maximum, minimum bandwidth. They recieve a percentage value as an input.
(ex, if you set maximum bandwidth 50, then container gets 50% of overall bandwidth at maximum. overall bandwidth must be adjusted depending on the device which the module is installed on).

how to use
-1. install module to kernel using "insmod vif.ko"
-2. run or start docker container
	must install module first because containers which are executed before module installation are not affected by module
-3. use proc file system to set weight, min, max bandwidth of each container if needed
	-ex)"echo 50 > /proc/oslab/vif1/max_credit" sets maximum bandwidth of first container 50
	-ex)"cat /proc/oslab/vif2/weight" print weight of second container
