// A minimal sporadic task skeleton that listens to a UDP socket.
// (c) 2021 Bjoern Brandenburg <bbb@mpi-sws.org>
// Released into the public domain.
//
// Compile with: cc -Wall -o sporadic-task sporadic-task.c
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <fcntl.h>

// the arbitrary port number the task will be activated by
#define UDP_LISTEN_PORT 2006
// what interface to listen on -- we simply on all interfaces available
#define UDP_LISTEN_HOST INADDR_ANY


struct sporadic_task;
int open_nonblocking_udp_socket(int host_addr, int port_number);
void process_activations(void);
void wait_for_next_activation(struct sporadic_task *tsk);
void process_one_activation(struct sporadic_task *tsk);

int main(int argc, char** argv) {
	// do whatever one-time initialization, argument parsing, etc. is necessary

	// call the main loop
	process_activations();
	return 0;
}

// the state that we need to keep track of
struct sporadic_task {
	// just for convenience, we keep track of the
	// sequence number of the current job
	unsigned long current_job_id;

	// the source of the events that this sporadic task reacts to
	int event_source_fd;

	// flag to let applications terminate themselves
	int terminated;
};


// the “sporadic task”
void process_activations(void) {
	struct sporadic_task tsk;

	// to match the real-time theory, the job count starts at “1”
	tsk.current_job_id = 1;

	// run until the application logic tells us to shut down
	tsk.terminated = 0;

	// open the file descriptor that yields events that this task
	// will react to
	tsk.event_source_fd = open_nonblocking_udp_socket(UDP_LISTEN_HOST, UDP_LISTEN_PORT);
	assert(tsk.event_source_fd >= 0);

	// execute a sequence of jobs until app shuts down (if ever)
	while (!tsk.terminated) {
		// wait until occurrence of the next event
		wait_for_next_activation(&tsk);
		// call the actual application logic
		process_one_activation(&tsk);
		// advance the job count in preparation of the next job
		tsk.current_job_id++;
	}
}

// helper function to open a UDP socket
int open_nonblocking_udp_socket(int host_addr, int port_number) {
	struct sockaddr_in addr = {0};

	// open the socket
	int the_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (the_socket < 0) {
		perror("socket creation");
		return the_socket;
	}

	// bind it to the given host and port
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port_number);
	addr.sin_addr.s_addr = htonl(host_addr);
	int err = bind(the_socket, (struct sockaddr*) &addr, sizeof(addr));
	if (err < 0) {
		perror("binding to port");
		return err;
	}

	// mark as nonblocking
	err = fcntl(the_socket, F_SETFL, O_NONBLOCK);
	if (err < 0) {
		perror("mark socket as nonblocking");
		return err;
	}

	return the_socket;
}

// wait until it’s time for the next “job” of the task
void wait_for_next_activation(struct sporadic_task *tsk) {
	int err;

	// allocate a management structure for the `poll()` system call
	struct pollfd fd_of_interest;

	// note that we are interested in the event source
	fd_of_interest.fd = tsk->event_source_fd;
	// note that the task needs to be activated when data becomes
	// available for reading
	fd_of_interest.events = POLLIN;

	// Wait for data to become available, ignoring spurious
	// wake-ups due signals.
	do {
		err = poll(&fd_of_interest, 1, -1);
	} while (err < 0);

	// at this point, we should have an event available for consumption
	assert(fd_of_interest.revents == POLLIN);
}

// one “job” of the task
void process_one_activation(struct sporadic_task *tsk) {
	#define MAX_PAYLOAD 1024
	char event_payload[MAX_PAYLOAD];
	ssize_t payload_size;

	// IMPORTANT: we need to consume the activation
	//            that triggered this call
	payload_size = read(tsk->event_source_fd, &event_payload,
	                    MAX_PAYLOAD);
	assert(payload_size >= 0);

	// check if the port was closed
	tsk->terminated = payload_size == 0;

	// arbitrary application logic goes here
	printf("Hello real-time world! "
	       "This is job #%lu, acting on %ld bytes of input. \n",
	       tsk->current_job_id, payload_size);
}

