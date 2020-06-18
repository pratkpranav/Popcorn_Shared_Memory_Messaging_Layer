# Popcorn_Shared_Memory_Messaging_Layer
Shared Memory based Messaging Layer on Linux Popcorn

first_phase.c : It contains kernel space code for using ivshmem based shared memory between two VMs
How to run it:
1. start the ivshmem server on host first with commands(even before starting the VMs): ivshmem-server -F -v
2. After starting the server on host, Then start both the VMs with these command added to the initial command: -device ivshmem-doorbell,chardev=ivshmem -chardev socket,path=/tmp/ivshmem_socket,id=ivshmem (Do not leave space after or before equalto sign)
3. Rename the object files with the name of file you want to run.
4. Uncomment Read and Write lines (uncomment read line on one VM and write line on another so as to check ivshmem shared memory) of initialize function.
5. call make on both VMs
6. first run sudo insmod first_phase.ko(on VM where you are writing on the shared memory)
7. then run sudo insmod first_phase.ko(on VM where you are reading on the shared memory)
run dmesg command on second VM you will be able to see what you have written from other VM.


second_phase.c: Adding Interrupts and Locks. STILL UNDER CONSTRUCTION.....
