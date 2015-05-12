Using the ivshmem shared memory server
--------------------------------------

This server is only supported on Linux.

To use the shared memory server, first compile it.  Running 'make' should
accomplish this.  An executable named 'ivshmem_server' will be built.

to display the options run:

./ivshmem_server -h

Options
-------

    -d	
    	daemonize (instead of running in the foreground)

    -h  
    	print help message

    -f	<path on host>
    	Absolute pathname of a file to be used as true persistent backing store,
	as opposed to a shared memory object that disappears on a reboot.

    -m <#>
        size of the backing store in MBs (default: 1).  Multipliers are M, G, and T.
	Optional if you're just reusing an existing object.
	Shared memory objects are limited to half the size of your physical RAM.
	Normal files can be up to several terabytes, depending on your host
	processor and revision of QEMU.

    -n <#>
    	Number of MSI pseudo-interrupts to use in IV messaging.  Not germaine to
	shared pseudo-NVM.

    -p <path on host>
        Unix domain socket to listen on.  The qemu-kvm chardev needs to connect on
        this socket. (default: '/tmp/ivshmem_socket')

    -s <string>
        POSIX shared object to create that is the shared memory (default: 'ivshmem')

    -n <#>
        number of eventfds for each guest.  This number must match the
        'vectors' argument passed the ivshmem device. (default: 1)

   -t
   	Truncate the object to the (new) size given in -m.

