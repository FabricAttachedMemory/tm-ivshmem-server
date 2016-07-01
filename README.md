# Extending Emulation of FAM for The Machine

## Description

This repo delivers an auxiliary daemon that enhances [Fabric-Attached Memory Emulation](https://github.com/FabricAttachedMemory/Emulation/).  Familiarity with the concepts in that repo, [particularly  IVSHMEM](https://github.com/FabricAttachedMemory/Emulation/wiki/Emulation-via-Virtual-Machines) is strongly recommended.

This daemon was originally written by Cam McDonnell as part of a larger exercise suite for IVSHMEM version circa 2.0.
It's currently [hosted on Github[(https://github.com/cmacdonell/ivshmem-code/).  The Machine effort only uses the ivshmem-server directory of that repo, and that's what you see here.

The emulation employs QEMU virtual machines performing the role of "nodes" in The Machine.  Inter-Virtual Machine Shared Memory (IVSHMEM) is configured across all the "nodes" so they see a physical memory area shared between them.  This space behaves almost identically to the the memory-centric computing on The Machine.

[The original ivshmem-server](https://github.com/cmacdonell/ivshmem-code/tree/master/ivshmem-server) communicates with a VM guest as directed by qemu command line options.  The server passes an open file descriptor representing a memory object, usually just a pre-allocated POSIX shared memory object.   tm-ivshmem-server (this repo) extends that by allowing a regular file to be used as backing store for the common IVSHMEM.  This provides two advantages over /dev/shm-backed IVSHME, especially useful on laptops or smaller-RAM systems:

* True persistence of emulated global NVM (ie, power off of a laptop loses /dev/shm contents)
* Backing store limited only by available file system space (instead of 1/2 physical RAM).

### QEMU versions

tm_ivshmem_server will work with QEMU versions through 2.4.  Additionally,
starting at 2.4, QEMU gained the ability to access an IVSHMEM backing file by name, rather than
needing an fd as passed in by an auxiliary ivshmem_server.
[More details can be seen in the QEMU changelog](https://github.com/qemu/qemu/commit/7d4f4bdaf785dfe9fc41b06f85cc9aaf1b1474ee).

As of QEMU 2.5, the original ivshmem-server has been subsumed into the QEMU project and its protocol and behavior has changed.  This repo does not track those
changes.  Since the "native" named-file feature is now built-in to QEMU, there is no further need for tm_ivshmem_server to achieve Fabric Attached Memory Emulation backed by a regular file.   Syntax examples for QEMU 2.5 will be given later.

## Setup and Execution

This section is mostly a copy of the original README, modified to reflect new options.

This server is only supported on Linux.  To use the shared memory server, first compile it.  Running 'make' should accomplish this.  An executable named 'tm_ivshmem_server' will be built.

To display the options run:

./tm_ivshmem_server -h

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
        processor, revision of QEMU (1.9 - 2.4), and available file system space.

    -n <#>
        Number of MSI pseudo-interrupts to use in IV messaging.  Not germane to
        shared pseudo-NVM of The Machine.

    -p <path on host>
        Unix domain socket to listen on.  The qemu-kvm chardev needs to connect on
        this socket. (default: '/tmp/ivshmem_socket')

    -s <string>
        POSIX shared object to create that is the shared memory (default: 'ivshmem')

    -t
        Truncate the object to the (new) size given in -m.  Not needed for an
        existing object of acceptable size.

## Configuring QEMU

### Versions 1.9 - 2.4

QEMU invocation for the default [(POSIX shmem) mode of IVSHMEM is discussed here]( 
https://github.com/FabricAttachedMemory/Emulation/blob/master/README.md#ivshmem-connectivity-between-all-vms).
Fabric-Attached Memory Emulation is achieved via the stanza

    -device ivshmem,shm=fabric_emulation,size=1024
    
Naturally, connecting to tm_ivshmem_server is a little more complex.  QEMU needs the UNIX-domain socket and the size used by tm_ivshmem_server (-p and -m options).  The QEMU invocation stanza comes as two parts:

    -chardev socket,path=/var/run/ivshmem.sock,id=GlobalNVM -device ivshmem,chardev=GlobalNVM,size=64G 

### Version 2.5

tm_ivshmem_server is no longer needed for IVHSHMEM backed by a regular file.
The backing file should be created before QEMU invocation.  For a 16G file,
something similar to this will work:

	$ fallocate -l 16G /var/lib/GlobalNVM

Fabric-Attached Memory Emulation is achieved via the stanza pair

	-object memory-backend-file,size=16G,mem-path=/var/tmp/GlobalNVM,id=GlobalNVM,share=on
	-device ivshmem,x-memdev=GlobalNVM

### Version 2.6

The format is highly similar to that of QEMU 2.5.  The "x-memdev" keyword
is now just "memdev".

## Files

| Filename | Description |
|----------|-------------|
| COPYING | License file (GPLv2) |
| Makefile | The usual suspect |
| node05.xml.file | The output from "virsh dumpxml" for a VM named node05.  This guest is configured to use the regular file mode of IVSHMEM and needs tm__ivshmem_server running first. |
| node05.xml.shm | The output from "virsh dumpxml" for a VM named node05.  This guest is configured to use the default mode of IVSHMEM and does not need tm__ivshmem_server at all. |
| send_scm.[ch] | Support routines for exchanging messages across the socket | 
| tm_ivshmem_server.c | The core of the server, all mods are in here |

