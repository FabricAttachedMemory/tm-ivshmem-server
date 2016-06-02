# Extending Emulation of FAM for The Machine

## Description

This repo delivers a server that enhances [Fabric-Attached Memory Emulation](https://github.com/FabricAttachedMemory/Emulation/).  Familiarity with the concepts in that repo, [particularly  IVSHMEM](https://github.com/FabricAttachedMemory/Emulation/wiki/Emulation-via-Virtual-Machines)
is strongly recommended.

This server was originally written by Cam McDonnell as part of a larger exerciser suite for IVSHMEM.  
It's currently [hosted on Github[(https://github.com/cmacdonell/ivshmem-code/).  
The Machine effort only uses the ivshmem-server directory of that repo, and that's what you see here.

The emulation employs QEMU virtual machines performing the role of "nodes" in The Machine.  Inter-Virtual Machine Shared Memory (IVSHMEM) is configured across all the "nodes" so they see a shared, global memory space.  This space can be accessed via mmap(2) and will behave just the the memory centric-computing on The Machine.

[The original ivshmem-server](https://github.com/cmacdonell/ivshmem-code/tree/master/ivshmem-server) communicates with a VM guest as directed by qemu command line options.  The server passes an open file descriptor representing a memory object, usually just a pre-allocated POSIX shared memory object.   tm-ivshmem-server (this repo) extends that by allowing a regular file to be used as backing store for the common IVSHMEM.  This allows two things:

* True persistence of emulated global NVM (ie, power off of a laptop loses /dev/shm contents)
* Backing store limited only by available file system space (instead of 1/2 physical RAM).

As of QEMU 2.5, the test suite (and in particular, the ivshmem-server) has been subsumed into the QEMU project.
Many things have changed in that version of ivshmem-server.  Patches will be submitted to the project to add 
the regular-file capabilities discussed here.   This version of the server will work with the first
connection of a QEMU >= 2.5 guest, but subsequent connections will fail.

## Setup and Execution

This section is mostly a copy of the original README, modified to reflect new options.

This server is only supported on Linux.

To use the shared memory server, first compile it.  Running 'make' should
accomplish this.  An executable named 'ivshmem_server' will be built.

to display the options run:

./ivshmem_server -h

### Options
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
	processor, revision of QEMU (before 2.0 - 2.4), and available file system space.

    -n <#>
    	Number of MSI pseudo-interrupts to use in IV messaging.  Not germane to
	shared pseudo-NVM.

    -p <path on host>
        Unix domain socket to listen on.  The qemu-kvm chardev needs to connect on
        this socket. (default: '/tmp/ivshmem_socket')

    -s <string>
        POSIX shared object to create that is the shared memory (default: 'ivshmem')

    -t
        Truncate the object to the (new) size given in -m.
