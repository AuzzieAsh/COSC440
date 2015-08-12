# COSC440  

## Install and Uninstall Module  
sudo insmod ./[name].ko  
sudo rmmod [name]  

## Change Permission and Owner  
sudo chmod 666 /dev/[name]  
sudo chown pi:pi /dev/[name]  

## Read and Write to /procs  
echo args >/procs/[name]  
cat /proc/[name]  

## Find Major Number    
cat /proc/devices | grep [name]  

## Print all Modules  
dmesg  
