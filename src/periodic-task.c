// A minimal periodic task skeleton.
// (c) 2020 Bjoern Brandenburg <bbb@mpi-sws.org>
// Released into the public domain.
//
// Compile with: cc -Wall -o periodic-task periodic-task.c
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

struct periodic_task;
void process_activations(void);
void sleep_until_next_activation(struct periodic_task *tsk);
void process_one_activation(struct periodic_task *tsk);
void timespec_add(struct timespec *a, struct timespec *b);

int main(int argc, char** argv) {
	// do whatever one-time initialization, argument parsing, etc. is necessary
	process_activations();
}

// the state that we need to keep track of to ensure
// periodic activations
struct periodic_task {
	// just for convenience, we keep track of the
	// sequence number of the current job
	unsigned long current_job_id;

	// desired separation of consecutive activations
	struct timespec period;

	// time at which the task became fist operational
	struct timespec first_activation;

	// time at which the current instance was (supposed
	// to be) activated
	struct timespec current_activation;

	// flag to let applications terminate themselves
	int terminated;
};

// for example, 100ms
#define PERIOD_IN_NANOS (100UL * 1000000UL)

// the “periodic task”
void process_activations(void) {
	int err;
	struct periodic_task tsk;

	// to match the real-time theory, the job count starts at “1”
	tsk.current_job_id = 1;

	// run until application logic tells us to shut down
	tsk.terminated = 0;

	// note the desired period
	tsk.period.tv_sec = 0;
	tsk.period.tv_nsec = PERIOD_IN_NANOS;

	// record time of first job
	err = clock_gettime(CLOCK_MONOTONIC, &tsk.first_activation);
	assert(err == 0);
	tsk.current_activation = tsk.first_activation;

	// execute a sequence of jobs until app shuts down (if ever)
	while (!tsk.terminated) {
		// wait until release of next job
		sleep_until_next_activation(&tsk);
		// call the actual application logic
		process_one_activation(&tsk);
		// advance the job count in preparation of the next job
		tsk.current_job_id++;
		// compute the next activation time
		timespec_add(&tsk.current_activation, &tsk.period);
	}
}

// wait until it’s time for the next “job” of the task
void sleep_until_next_activation(struct periodic_task *tsk) {
	int err;
	do {
	  // perform an absolute sleep until tsk->current_activation
		err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &tsk->current_activation, NULL);
		// if err is nonzero, we might have woken up too early
	} while (err != 0 && errno == EINTR);
	assert(err == 0);
}

// one “job” of the task
void process_one_activation(struct periodic_task *tsk) {
	// application logic goes here
	printf("Hello real-time world! This is job #%lu.\n",
	       tsk->current_job_id);
}

// helper to add to a struct timespec value
void timespec_add(struct timespec *a, struct timespec *b) {
	a->tv_sec += b->tv_sec;
	a->tv_nsec += b->tv_nsec;
	if (a->tv_nsec >= 1000000000UL) {
		a->tv_sec++;
		a->tv_nsec %= 1000000000UL;
	}
}
