/*
 * A stand-alone shared memory server for inter-VM shared memory for KVM
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/signalfd.h>
#include "send_scm.h"

#define DEFAULT_SOCK_PATH "/tmp/ivshmem_socket"
#define DEFAULT_SHM_OBJ "ivshmem"

#define DEBUG 1

#define S_IRW_ALL (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

typedef struct server_state {
    vmguest_t *live_vms;
    int nr_allocated_vms;
    long shm_size;
    long live_count;
    long total_count;
    int shm_fd;
    char * sockpath;
    char * shmobj;
    char * filepath;
    int maxfd, conn_socket;
    long msi_vectors;
    int sigfd;
    bool daemonize, truncate;
} server_state_t;

void usage_die(char const *prg) {
    fprintf(stderr,
    	    "usage: %s [-d] [-h] [-p <unix socket>] [-f <file> | -s <shmobj>]"
            "[-m XXXX[M|G|T]] [-n X] [-t]\n",
	    prg);
    fprintf(stderr, "-d		daemonize\n");
    fprintf(stderr, "-h		help\n");
    fprintf(stderr, "-f	path	use normal file as backing store\n");
    fprintf(stderr, "-m XXXX	size of backing store\n");
    fprintf(stderr, "-n X	number of MSI vectors\n");
    fprintf(stderr, "-s	name	use Posix shared memory as backing store\n");
    fprintf(stderr, "-t		with -f and -m: truncate file to given size\n");
    exit(1);
}

void add_new_guest(server_state_t * s) {
    struct sockaddr_un remote;
    socklen_t t = sizeof(remote);
    long i, j;
    int vm_sock;
    long new_posn;
    long neg1 = -1;

    vm_sock = accept(s->conn_socket, (struct sockaddr *)&remote, &t);

    if ( vm_sock == -1 ) {
        perror("accept");
        exit(1);
    }

    new_posn = s->total_count;

    if (new_posn == s->nr_allocated_vms) {
        printf("increasing vm slots\n");
        s->nr_allocated_vms = s->nr_allocated_vms * 2;
        if (s->nr_allocated_vms < 16)
            s->nr_allocated_vms = 16;
        s->live_vms = realloc(s->live_vms,
                    s->nr_allocated_vms * sizeof(vmguest_t));

        if (s->live_vms == NULL) {
            fprintf(stderr, "realloc failed - quitting\n");
            exit(-1);
        }
    }

    s->live_vms[new_posn].posn = new_posn;
    printf("[NC] Live_vms[%ld]\n", new_posn);
    s->live_vms[new_posn].efd = malloc(sizeof(int));
    for (i = 0; i < s->msi_vectors; i++) {
        s->live_vms[new_posn].efd[i] = eventfd(0, 0);
        printf("\tefd[%ld] = %d\n", i, s->live_vms[new_posn].efd[i]);
    }
    s->live_vms[new_posn].sockfd = vm_sock;
    s->live_vms[new_posn].alive = 1;


    sendPosition(vm_sock, new_posn);
    sendUpdate(vm_sock, neg1, sizeof(long), s->shm_fd);
    printf("[NC] trying to send fds to new connection\n");
    sendRights(vm_sock, new_posn, sizeof(new_posn), s->live_vms, s->msi_vectors);

    printf("[NC] Connected (count = %ld).\n", new_posn);
    for (i = 0; i < new_posn; i++) {
        if (s->live_vms[i].alive) {
            // ping all clients that a new client has joined
            printf("[UD] sending fd[%ld] to %ld\n", new_posn, i);
            for (j = 0; j < s->msi_vectors; j++) {
                printf("\tefd[%ld] = [%d]", j, s->live_vms[new_posn].efd[j]);
                sendUpdate(s->live_vms[i].sockfd, new_posn,
                        sizeof(new_posn), s->live_vms[new_posn].efd[j]);
            }
            printf("\n");
        }
    }

    s->total_count++;
}

void create_listening_socket(server_state_t * s) {

    struct sockaddr_un local;
    int len;

    if (s->sockpath == NULL)
        s->sockpath = strdup(DEFAULT_SOCK_PATH);
    printf("listening socket: %s\n", s->sockpath);

    if ((s->conn_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket() failed");
        exit(1);
    }

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, s->sockpath);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(s->conn_socket, (struct sockaddr *)&local, len) == -1) {
        perror("bind() failed");
        exit(1);
    }

    if (listen(s->conn_socket, 5) == -1) {
        perror("listen() failed");
        exit(1);
    }
    chmod(local.sun_path, S_IRW_ALL);
    s->maxfd = s->conn_socket;

    return;

}

server_state_t * parse_args(int argc, char **argv) {
    server_state_t * s;
    int c;

    if (!(s = calloc(1, sizeof(server_state_t)))) {
    	perror("Cannot allocate memory");
	exit(1);
    }
    s->msi_vectors = 1;
    s->sigfd = -1;

    while ((c = getopt(argc, argv, "df:hp:s:m:n:t")) != -1) switch (c) {
    
    case 'd':			// daemonize (go into background)
    	    s->daemonize = true;
	    break;

    case 'f':			// name of file
            s->filepath = optarg;
            break;

    case 'h':
    default:
            usage_die(argv[0]);

    case 'm':			// size of shared memory object
        {
            uint64_t value;
            char *ptr;

            value = strtoul(optarg, &ptr, 10);
            switch (*ptr) {

            case 0: case 'M': case 'm':
                    value <<= 20;
                    break;

            case 'G': case 'g':
                    value <<= 30;
                    break;

            case 'T': case 't':
                    value <<= 40;
                    break;

            default:
                    fprintf(stderr, "invalid multiplier: %s\n", optarg);
		    usage_die(argv[0]);
            }
            s->shm_size = value;
            break;
        }

    case 'n':			// number of MSI vectors
            s->msi_vectors = atol(optarg);
            break;

    case 'p':			// path to listening socket
            s->sockpath = optarg;
            break;

    case 's':			// name of shared memory object
            s->shmobj = optarg;
            break;

    case 't':			// force file to size given by -m
	    s->truncate = true;
	    break;
    }
    
    //Idiot checks

    if (s->shmobj && s->filepath) {
    	fprintf(stderr, "Need exactly one of -f | -s\n");
	usage_die(argv[0]);
    }

    // At 1TB, lspci -> Region 2: Memory at <ignored> (64-bit, prefetchable)
    // at least on an i440 model VM.  512G takes at least 9 or 10G RAM
    // to hotadd (struct page and vmemmap).  QEMU needs patch to go beyond
    // 40-bit addressing (> mmap() max of 512G).

    if (s->shm_size) {
	if (1 << 20 > s->shm_size || s->shm_size > 16L * (1L << 40))) {
	    fprintf(stderr, "Limits: 1M <= size <= 16T\n");
	    usage_die(argv[0]);
	}

    	// shmem must be power of 2 (ie, only 1 bit may be set)
	if (s->shmobj && (!(!(s->shm_size & (s->shm_size - 1))))) {
	    fprintf(stderr, "%lu is not a power of 2\n", s->shm_size);
	    usage_die(argv[0]);
	}
    }

    return s;	// needs to be free'd
}

void print_vec(server_state_t * s, const char * c) {

    int i, j;

#if DEBUG
    printf("%s (%ld) = ", c, s->total_count);
    for (i = 0; i < s->total_count; i++) {
        if (s->live_vms[i].alive) {
            for (j = 0; j < s->msi_vectors; j++) {
                printf("[%d|%d] ", s->live_vms[i].sockfd, s->live_vms[i].efd[j]);
            }
        }
    }
    printf("\n");
#endif

}

int find_set(fd_set * readset, int max) {

    int i;

    for (i = 1; i < max; i++) {
        if (FD_ISSET(i, readset)) {
            return i;
        }
    }

#if DEBUG
    printf("nothing set\n");
#endif

    return -1;

}

/* open shared memory object or normal file */

void open_backing_store(server_state_t *s) {
    struct stat buf;
    mode_t umask_old;

    if (!(s->shmobj || s->filepath))
        s->shmobj = strdup(DEFAULT_SHM_OBJ);

    printf("shared object: %s\n", s->shmobj ? s->shmobj : s->filepath);
    if (s->shm_size)
    	printf("requested object size: %lu (bytes)\n", s->shm_size);
   
    umask_old = umask(0);
    if (s->shmobj) {		// System preserves file on graceless exit
    	if ((s->shm_fd = shm_open(s->shmobj, O_CREAT|O_RDWR, S_IRW_ALL)) < 0)
    	{
            fprintf(stderr, "shm_open(%s) failed: %s\n",
	    	s->shmobj, strerror(errno));
            exit(1);
    	}
    } else if (s->filepath) {	// but keep using shm_fd
	sigset_t mask;

    	if ((s->shm_fd = open(s->filepath, O_CREAT|O_RDWR, S_IRW_ALL)) < 0)
    	{
            fprintf(stderr, "open(%s) failed: %s\n",
	    	s->filepath, strerror(errno));
            exit(1);
    	}

	// from the man page example
	sigemptyset(&mask);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
	    perror("sigprocmask() failed");
	else {
	    if ((s->sigfd = signalfd(-1, &mask, 0)) == -1)
	    	perror("signalfd() failed");
	    }
    }

    // deal with size
    if (fstat(s->shm_fd, &buf) == -1) {
    	perror("fstat() failed");
	exit(1);
    }

    if (!s->shm_size) 
    	s->shm_size = buf.st_size;
    else if (s->truncate) {
	    if (ftruncate(s->shm_fd, s->shm_size) == -1)
		perror("ftruncate() failed");
    	    if (fstat(s->shm_fd, &buf) == -1) {
    		perror("fstat() failed");
		exit(1);
    	    }
    }

    printf("backing store is %lu bytes\n", buf.st_size);

    umask(umask_old);
}

void do_server(server_state_t * s) {
    fd_set readset;
    int ret, handle, i;
    char buf[1024];

    for(;;) {

        print_vec(s, "vm_sockets");

        FD_ZERO(&readset);
        /* conn socket is in Live_vms at posn 0 */
        FD_SET(s->conn_socket, &readset);
        for (i = 0; i < s->total_count; i++) {
            if (s->live_vms[i].alive != 0) {
                FD_SET(s->live_vms[i].sockfd, &readset);
            }
        }
        if (s->sigfd >= 0)
		FD_SET(s->sigfd, &readset);

        printf("\nPID %d waiting (maxfd = %d)\n", getpid(), s->maxfd);

        ret = select(s->maxfd + 1, &readset, NULL, NULL, NULL);

        if (ret == -1) {
            perror("select()");
        }

        handle = find_set(&readset, s->maxfd + 1);
        if (handle == -1) continue;

	if (handle == s->sigfd) {	// a read will clear the signal
		struct signalfd_siginfo fdsi;

		if (read(s->sigfd, &fdsi, sizeof(fdsi)) != sizeof(fdsi)) {
			perror("read sigfd");
			exit(4);
		}
		switch(fdsi.ssi_signo) {
		case SIGHUP:
			printf("HUP\n");
			break;
		case SIGINT:
		case SIGQUIT:
			printf("INT or QUIT\n"); // Like Ctrl-C
			close(s->sigfd);
			close(s->shm_fd);
			exit(0);
			break;
		default:
			printf("WTF\n");	// shouldn't happen :-)
			break;
		}
		continue;
	}

        if (handle == s->conn_socket) {

            printf("[NC] new connection\n");
            FD_CLR(s->conn_socket, &readset);

            /* The Total_count is equal to the new guests VM ID */
            add_new_guest(s);

            /* update our the maximum file descriptor number */
            s->maxfd = s->live_vms[s->total_count - 1].sockfd > s->maxfd ?
                            s->live_vms[s->total_count - 1].sockfd : s->maxfd;

            s->live_count++;
            printf("Live_count is %ld\n", s->live_count);

        } else {
            /* then we have received a disconnection */
            int recv_ret;
            long i, j;
            long deadposn = -1;

            recv_ret = recv(handle, buf, 1, 0);

            printf("[DC] recv returned %d\n", recv_ret);

            /* find the dead VM in our list and move it to the dead list. */
            for (i = 0; i < s->total_count; i++) {
                if (s->live_vms[i].sockfd == handle) {
                    deadposn = i;
                    s->live_vms[i].alive = 0;
                    close(s->live_vms[i].sockfd);

                    for (j = 0; j < s->msi_vectors; j++) {
                        close(s->live_vms[i].efd[j]);
                    }

                    free(s->live_vms[i].efd);
                    s->live_vms[i].sockfd = -1;
                    break;
                }
            }

            for (j = 0; j < s->total_count; j++) {
                /* update remaining clients that one client has left/died */
                if (s->live_vms[j].alive) {
                    printf("[UD] sending kill of fd[%ld] to %ld\n",
                                                                deadposn, j);
                    sendKill(s->live_vms[j].sockfd, deadposn, sizeof(deadposn));
                }
            }

            s->live_count--;

            /* close the socket for the departed VM */
            close(handle);
        }

    }
}

int main(int argc, char ** argv) {
    server_state_t * s;

    // All of these setup routines may exit with an error.

    s = parse_args(argc, argv);
    
    create_listening_socket(s);

    open_backing_store(s);

    if (s->daemonize) {
    	if (daemon(false, false) == -1)
		perror("daemon() failed");
    }

    do_server(s);

    return 0;
}
