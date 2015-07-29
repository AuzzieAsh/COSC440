# COSC440  

## Install and Uninstall Module  
sudo insmod ./[name].ko  
sudo rmmod [name]  

## Change Permission and Owner  
sudo chmod 666 /dev/[name]  
sudo chown pi:pi /dev/[name]  

## Find Major Number  
cat /proc/devices | grep [name]  

## Print all Modules  
dmesg  
