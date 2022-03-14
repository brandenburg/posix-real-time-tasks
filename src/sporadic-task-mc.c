// An extended sporadic task skeleton that reacts to:
// - two UDP sockets,
// - two UNIX signals, and
// - STDIN.
//
// (c) 2021 Bjoern Brandenburg <bbb@mpi-sws.org>
// Released into the public domain.
//
// Compile with: cc -Wall -o sporadic-task-mc sporadic-task-mc.c
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/signalfd.h>

// the two arbitrary port numbers the task will be activated by
#define UDP_LISTEN_PORT1 2006
#define UDP_LISTEN_PORT2 1619
// what interface to listen on -- we simply listen on all interfaces available
#define UDP_LISTEN_HOST INADDR_ANY

// number of channels the task reacts to
#define NUM_EVENT_SOURCES 5

// the channel IDs
#define CH_UDP1 0
#define CH_UDP2 1
#define CH_STDIN 2
#define CH_SIGUSR1 3
#define CH_SIGINT 4

struct sporadic_task;
void process_activations(void);
void open_event_channels(struct sporadic_task *tsk);
void wait_for_next_activation(struct sporadic_task *tsk);
void process_channels(struct sporadic_task *tsk);
void process_one_activation(int channel, struct sporadic_task *tsk);

int open_nonblocking_udp_socket(int host_addr, int port_number);
int open_stdin_nonblocking(void);
int open_signalfd_nonblocking(int signum);


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

	// the sources of the events that this sporadic task reacts to
	struct pollfd event_sources[NUM_EVENT_SOURCES];

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

	// open the file descriptors that yield events that this task
	// will react to and store them in the task's pollfd array
	open_event_channels(&tsk);

	// execute a sequence of jobs until app shuts down (if ever)
	while (!tsk.terminated) {
		// wait until occurrence of the next event
		wait_for_next_activation(&tsk);
		// call the actual application logic
		process_channels(&tsk);
	}
}


// wait until it’s time for the next “job” of the task
void wait_for_next_activation(struct sporadic_task *tsk) {
	int err;

	// Wait for data to become available, ignoring spurious
	// wake-ups due signals.
	do {
		err = poll(tsk->event_sources, NUM_EVENT_SOURCES, -1);
	} while (err < 0);
}


// check all event channels for pending events
void process_channels(struct sporadic_task *tsk) {
	for (int channel = 0; channel < NUM_EVENT_SOURCES; channel++) {
		if (tsk->event_sources[channel].revents) {
			process_one_activation(channel, tsk);
			// advance the job count in preparation of the next job
			tsk->current_job_id++;
		}
	}
}

// one “job” of the task
void process_one_activation(int channel, struct sporadic_task *tsk) {
	#define MAX_PAYLOAD 1024
	char event_payload[MAX_PAYLOAD];
	ssize_t payload_size;

	// IMPORTANT: we need to consume the activation
	//            that triggered this call
	payload_size = read(tsk->event_sources[channel].fd, &event_payload,
	                    MAX_PAYLOAD);
	assert(payload_size >= 0);

	// check if the port was closed
	tsk->terminated |= payload_size == 0;

	// arbitrary application logic goes here
	printf("Hello real-time world! "
	       "This is job #%lu, acting on %ld bytes of input "
	       "on channel #%d. \n",
	       tsk->current_job_id, payload_size, channel);

	if (channel == CH_SIGINT) {
		printf("Termination requested via SIGINT; shutting down...\n");
		tsk->terminated = 1;
	}
}


// open all file descriptors
void open_event_channels(struct sporadic_task *tsk) {

	// UDP port 1
	tsk->event_sources[CH_UDP1].fd = open_nonblocking_udp_socket(UDP_LISTEN_HOST, UDP_LISTEN_PORT1);
	assert(tsk->event_sources[CH_UDP1].fd >= 0);
	printf("Listening on UDP port %d...\n", UDP_LISTEN_PORT1);

	// UDP port 2
	tsk->event_sources[CH_UDP2].fd = open_nonblocking_udp_socket(UDP_LISTEN_HOST, UDP_LISTEN_PORT2);
	assert(tsk->event_sources[CH_UDP2].fd >= 0);
	printf("Listening on UDP port %d...\n", UDP_LISTEN_PORT2);

	// STDIN
	tsk->event_sources[CH_STDIN].fd = open_stdin_nonblocking();
	assert(tsk->event_sources[CH_STDIN].fd >= 0);
	printf("Listening on STDIN...\n");

	// SIGUSR1
	tsk->event_sources[CH_SIGUSR1].fd = open_signalfd_nonblocking(SIGUSR1);
	assert(tsk->event_sources[CH_SIGUSR1].fd >= 0);
	printf("Listening for SIGUSR1...\n");

	// SIGINT
	tsk->event_sources[CH_SIGINT].fd = open_signalfd_nonblocking(SIGINT);
	assert(tsk->event_sources[CH_SIGINT].fd >= 0);
	printf("Listening for SIGINT...\n");

	// note that the task needs to be activated when data becomes
	// available for reading
	for (int i = 0; i < NUM_EVENT_SOURCES; i++) {
		tsk->event_sources[i].events = POLLIN;
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

// create nonblocking copy of the STDIN file descriptor
int open_stdin_nonblocking(void) {
	// duplicate STDIN file descriptor
	int fd = dup(fileno(stdin));
	// mark as nonblocking
	int err = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (err < 0) {
		perror("mark stdin as nonblocking");
		return err;
	}

	return fd;
}

// set up a given signal for delivery via a file descriptor
int open_signalfd_nonblocking(int signum) {
	int err;
	sigset_t sigset;

	// initialize the signal set
	err = sigemptyset(&sigset);
	if (err)
		return err;
	err = sigaddset(&sigset, signum);
	if (err)
		return err;

	// suppress the default signal action
	err = sigprocmask(SIG_BLOCK, &sigset, NULL);
	if (err)
		return err;

	// open signal file descriptor for this signal
	return signalfd(-1, &sigset, SFD_NONBLOCK);
}
