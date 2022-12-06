# forensicsFileRecovery

1. find where the drive is mounted 
  `sudo fdisk -l`
3. go to drive (/media/<user>/<drive>) and get inode
  `ls -il`
4. compile and run `main.c` 
```
gcc main.c -o out
sudo ./out /dev/sd<x> <inode>
```
