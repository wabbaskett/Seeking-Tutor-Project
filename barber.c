#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/queue.h>
#include <semaphore.h>
#include <stdbool.h>

int numStudents = 0;
int numTutors = 0;
int numChairs = 0;
int numHelp = 0;

//Defining a FIFO queue using the linux queue
struct student{
	//put any individual student info here
	STAILQ_ENTRY(customer) studentQueue; //Singly linked tail queue for FIFO
};

STAILQ_HEAD(stailhead, student);

struct stailhead head;

int
main(int argc, char *argv[])
{
	pthread_t *studentThreads;

	//reject if number of passed parameters is incorrect
	if (argc < 5) {
		fprintf(stderr, "%s: %s numStudents numTutors numChairs numHelp\n", argv[0], argv[0]);
		exit(-1);
	}
	
	numStudents = atoi(argv[1]);
	numTutors = atoi(argv[2]);
	numChairs = atoi(argv[3]);
	numHelp = atoi(argv[4]);

	studentThreads = malloc(sizeof(/*STUDENT THREAD NAME*/) * numStudents);


	//Initializing the student threads
	for(i = 0; i < numStudents; i++) {
		assert(pthread_create(&studentThreads[i], NULL, /*STUDENT THREAD NAME*/, (void *) customers[i]) == 0);
	}
  	
	//Waiting for all the students to finish
	for(i = 0; i < numStudents; i++) {
		assert(pthread_join(studentThreads[i], &value) == 0);

	}
}
