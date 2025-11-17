#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/queue.h>
#include <semaphore.h>
#include <stdbool.h>

int numStudents = 0;			//variables to store command line args
int numTutors = 0;
int numChairs = 0;
int numHelp = 0;

int freeChairs = 0;				//number of open chairs
int currentStudents = 0;        //number of students currently being tutored
int totalSessions = 0;			//number of completed tutoring sessions
int totalRequests = 0;			//total number of students that have entered the priority queue
int threadsComplete = 0;		//number of student threads that have exited

sem_t mut_chair;                //lock for the freeChairs variable
sem_t mut_current;              //lock for the currentStudents variable
sem_t mut_total;                //lock for the totalRequests variable
sem_t mut_complete;             //lock for the threadsComplete variable

sem_t *mut_pQ;                   //lock for the priority queue
sem_t mut_wQ;                   //lock for the waiting queue
sem_t mut_tQ;                   //lock for the tutor queue

sem_t sem_coordinator_s;          //semaphore for a student to signal co-ordinator
sem_t sem_coordinator_t;          //semaphore for a tutor to signal co-ordinator



/* queues will only need to store an integer so that the student/tutor structs can be accessed from a global array doing it this way made it easier to keep track of the student and tutor thread semaphores
*/
/*
 *MOVED THE STAILQ into the student struct since its just going to be easier to pull the student directly from the waiting queue
 *
//Defining a FIFO queue using the linux queue
struct student_id{
	//put any individual student info here
	STAILQ_ENTRY(customer) waitingQueue;//Allows the student struct to be used in a FIFO queue
};

STAILQ_HEAD(stailhead, student_id);

struct stailhead head;
*/


// structure to hold information about students
struct Student{
	int id;						//id for student (equivilent to its position in student array)
	int times_helped;				//variable to track how many times a student thread should queue
	int tutor_mark;					//temportary storage to track the id of the tutor that helped this student
	sem_t sem_student;				//semaphore to signal a student
	pthread_t thread_student;			//pthread for a student
	STAILQ_ENTRY(Student) queueNode;//Allows the student struct to be used in a FIFO queue
	char name[50];

};

// structure to hold information about tutors
struct Tutor{
	int id;										//id for a tutor (equivilent to its position in tutor array)
	bool busy;// Used by the coordinator to check if the tutor is available to help 
	sem_t busy_mutex;// To prevent coordinator posting to a tutor that just got a new assignment
	sem_t sem_tutor;							//semaphore to signal a tutor
	pthread_t thread_tutor;					    //pthread for a tutor
	char name[50];
};

struct Student *student_array = NULL;					//pointer to array of student structs (this will be dynamically allocated in main)
struct Tutor *tutor_array = NULL;						//pointer to array of tutor structs (this will be dynamically allocated in main)

STAILQ_HEAD(FIFO_QUEUE, Student);

struct FIFO_QUEUE waiting_queue_head;
struct FIFO_QUEUE *priority_queue_heads;

void *student_thread(void *arg)					//function for student threads takes student id (int) as a parameter
{
	struct Student *self = arg;
	int id = self->id; 
	while(1)
	{	
		//printf("student %d online \n", id);
		sem_wait(&mut_wQ);																		//acquire chair and waiting queue locks

		//printf("student %d got queue mut\n", id);
		sem_wait(&mut_chair);								
		//printf("student %d got chair mut\n", id);
		if(freeChairs <= 0)
		{
			sem_post(&mut_chair);																
			sem_post(&mut_wQ);																	//release locks
			printf("Student %d found no empty chair. Will come again later\n",id);
			sleep(1);
			continue;
		}
		else
		{
			freeChairs -= 1;
			printf("Student %d takes a seat. Empty chairs remaining = %d\n",id,freeChairs);
			//add this students id to the waiting queue
			STAILQ_INSERT_TAIL(&waiting_queue_head, &student_array[id], queueNode);

			sem_post(&mut_chair);					//release chair and waiting queue locks
			sem_post(&mut_wQ);

			sem_post(&sem_coordinator_s);				//signal the co-ordinator that a student is waiting to be queued
			sem_wait(&(student_array[id].sem_student));		//wait for signal from a tutor to leave chair and recieve tutoring
			//printf("Student %d is being tutored\n", id);
			//Free up the chair before we go get tutored
			sem_wait(&mut_chair);
			freeChairs++;
			sem_post(&mut_chair);

			printf("Student %d receives help from Tutor %d\n",id,student_array[id].tutor_mark);
			//sem_wait(&(student_array[id].sem_student));		//wait for signal from a tutor that tutoring is complete
			student_array[id].times_helped += 1;                                                 // update help needed
			student_array[id].tutor_mark = 0;                                                   // reset tutor mark 

			if(student_array[id].times_helped >= numHelp)												//exit thread when help is no longer needed
			{
				sem_wait(&mut_complete);                                                        //acquire lock
				threadsComplete ++;                                                             //update number of completed jobs
				sem_post(&mut_complete);                                                        //release lock
				//printf("STUDENT %d DONE\n", id);
				pthread_exit(NULL);                                                             //exit
			}
		}
	}
	////printf("Student: %d  Help Needed: %d\n", id, student_array[id].times_helped);
    //pthread_exit(NULL);
	return NULL;
}

void *coordinator_thread()
{
	//printf("Coordinator online\n");
	struct Student *first_student = NULL; // What we use to store the current student we are helping
	int tutor_index = 0;//index that we use to find an available tutor initializing here due to way we look for tutors (below)
	while(1){
		//printf("Coordinator waiting\n");
		sem_wait(&mut_complete);													//acquire lock
		
		//exit when threads are complete (need to fix this does not cause error but the assignment requires all threads to exit)
		if(threadsComplete == numStudents)
		{
			sem_post(&mut_complete);												//release lock
			pthread_exit(NULL);
		}
		sem_post(&mut_complete);													//release lock

		//printf("Coord waiting for a student\n");
		sem_wait(&sem_coordinator_s);//Wait for a student to request help
				
		//printf("Coord received a student\n");
		//Put the first requesting student into the correct priority queue
		sem_wait(&mut_wQ);
		totalRequests++;	
		first_student = STAILQ_FIRST(&waiting_queue_head);
		STAILQ_REMOVE_HEAD(&waiting_queue_head, queueNode);

		//printf("Coord popped student %d\n", first_student->id);
		sem_post(&mut_wQ);
		
		int priority = first_student->times_helped;
		//Putting the student into the appropriate priority queue according to help needed (lower = better)
		sem_wait(&mut_pQ[priority]);
		STAILQ_INSERT_TAIL(&priority_queue_heads[priority], first_student, queueNode);
		//printf("inserted into PQ\n");
		//printf("%p", (void *) STAILQ_FIRST(&priority_queue_heads[priority]));
		sem_post(&mut_pQ[priority]);

		printf("Student %d with priority %d in queue. Waiting students = %d. Total help requested so far = %d.\n",first_student->id, priority, numChairs - freeChairs, totalRequests);
		//The following code looks for an available tutor and signals them to wake up
		//  Steps
		//	1. Checks if current tutor index is available
		//	2. If yes, we break and signal out of the loop
		//	3. If no, increment tutor_index and loop back if we hit the last tutor
		//
		while(1){
			//printf("Coord checking tutor %d\n", tutor_index);
			sem_wait(&tutor_array[tutor_index].busy_mutex);// Wait for mutex so we don't double assign
			if (!tutor_array[tutor_index].busy){ break; }
			sem_post(&tutor_array[tutor_index].busy_mutex);
			tutor_index = (tutor_index + 1) % numTutors; //Increments the index and loops it back to 0 if we hit the end of the array 
		}
		//printf("Coord found Tutor %d\n", tutor_index);
		tutor_array[tutor_index].busy = true;
		sem_post(&tutor_array[tutor_index].busy_mutex);
		// Now we signal the selected available tutor
		sem_post(&tutor_array[tutor_index].sem_tutor);
		
		sem_wait(&mut_complete);
		if(threadsComplete == numStudents)
		{
			//printf("COORD DONE\n");
			sem_post(&mut_complete);
			pthread_exit(NULL);
		}
		sem_post(&mut_complete);


	}
}

void *tutor_thread(void *arg)					//function for tutor threads takes tutor id (int) as a parameter
{
	struct Tutor *self = arg;
	int id = self->id; 
	int i = 0; //used for PQ loop index
	struct Student *to_tutor = NULL;
	sem_post(&tutor_array[id].busy_mutex);
	while(1)
	{
		//printf("Tutor %d online\n", id);
		sem_wait(&mut_complete);													//acquire lock
		if(threadsComplete == numStudents)				//exit when threads are complete (need to fix this does not cause error but the assignment requires all threads to exit)
		{
			sem_post(&mut_complete);												//release lock
			pthread_exit(NULL);
		}
		sem_post(&mut_complete);													//release lock
		
		// enter tutor waiting queue

		sem_post(&sem_coordinator_t);												// notify co-ordinator that a tutor is ready
		sem_wait(&(tutor_array[id].sem_tutor));				// wait for co-ordinator to wake this thread
		//printf("Tutor %d woken up\n", id);
		//Update busy status
		sem_wait(&tutor_array[id].busy_mutex);
		tutor_array[id].busy = true;
		//printf("Tutor %d busy\n", id);
		sem_post(&tutor_array[id].busy_mutex);
		
		//Loop through the priority queues, looking for the first available student in priority order (lower = better)
		for(i = 0; i <numHelp; i++){
			//printf("Tutor %d checking priority level %d\n", id, i);
			sem_wait(&mut_pQ[i]);// Acquire lock 
			//printf("Tutor %d checking priority level %d\n", id, i);
			to_tutor = STAILQ_FIRST(&priority_queue_heads[i]);
			if(to_tutor == NULL) {
				sem_post(&mut_pQ[i]);
				continue;
			}
			STAILQ_REMOVE_HEAD(&priority_queue_heads[i], queueNode);
			sem_post(&mut_pQ[i]);
			break;
		}
		//printf("Found the priority\n");
		sem_wait(&mut_current);	
		currentStudents ++;			//update students currently being helped	
		sem_post(&mut_current);
		
		student_array[to_tutor->id].tutor_mark = id;			//mark student with tutor id (used for output by student thread)
		sem_post(&(student_array[to_tutor->id].sem_student));

		sleep(.0001);							//wait to simulate tutoring time
		//printf("%p", (void *) to_tutor);
		//printf("did the work\n");
		sem_wait(&mut_current);														//acquire locks for required variables
		sem_wait(&mut_total);
		currentStudents -= 1;															//update variables
		totalSessions++;

		printf("Student %d tutored by tutor %d. Total sessions being tutored = %d. Total sessions being tutored by all = %d\n",to_tutor->id,id,currentStudents,totalSessions);	//output

		sem_post(&mut_total);
		sem_post(&mut_current);														//release locks

		sem_wait(&mut_complete);
		if(threadsComplete == numStudents)
		{
			//printf("TUTOR %d DONE\n", id);
			sem_post(&mut_complete);
			pthread_exit(NULL);
		}
		sem_post(&mut_complete);

		//printf("did not close out\n");

		//sem_post(&(student_array[to_tutor->id].sem_student));
		sem_wait(&tutor_array[id].busy_mutex);
		tutor_array[id].busy = false;
		sem_post(&tutor_array[id].busy_mutex);
		
	}
	
   	 pthread_exit(NULL);
	return NULL;
}


int
main(int argc, char *argv[])
{
	int i;

	//reject if number of passed parameters is incorrect
	if (argc < 5) {
		fprintf(stderr, "%s: %s numStudents numTutors numChairs numHelp\n", argv[0], argv[0]);
		exit(-1);
	}
	
	numStudents = atoi(argv[1]);
	numTutors = atoi(argv[2]);
	numChairs = atoi(argv[3]);
	numHelp = atoi(argv[4]);

	sem_init(&mut_chair,0,1);																							//initialize locks for global variables
	sem_init(&mut_current,0,1);
	sem_init(&mut_total,0,1);
	sem_init(&mut_complete,0,1);
	
	mut_pQ = (sem_t *) malloc(numHelp * sizeof(sem_t));
	for(i = 0; i < numHelp; i++){
		sem_init(&mut_pQ[i],0,1);	//initialize locks for queues
	}
	sem_init(&mut_wQ,0,1);
	sem_init(&mut_tQ,0,1);

	sem_init(&sem_coordinator_s,0,0);																					//initialize semaphore to signal co-ordinator
	sem_init(&sem_coordinator_t,0,0);

	freeChairs = numChairs;																								//set freeChairs to maximum number of chairs (all chairs are empty at start)
	pthread_t thread_coordinator;

	STAILQ_INIT(&waiting_queue_head);

	priority_queue_heads = (struct FIFO_QUEUE *) malloc(numHelp * sizeof(struct FIFO_QUEUE));
	for(i = 0; i < numHelp; i++){
		STAILQ_INIT(&priority_queue_heads[i]);
	}


	student_array = (struct Student *)malloc(numStudents * sizeof(struct Student));												//allocate memory for students
	tutor_array = (struct Tutor *)malloc(numTutors * sizeof(struct Tutor));														//allocate memory for tutors

	for(i = 0; i < numStudents; i++)																					//build the student array
	{
		student_array[i].id = i;																						//initialize student id
		student_array[i].times_helped = 0;																			//initialize help required
		student_array[i].tutor_mark = 0;																				//initialize tutor mark
		sem_init(&(student_array[i].sem_student),0,0);																	//initialize semaphore to signal this student
		pthread_create(&(student_array[i].thread_student), NULL, *student_thread, &student_array[i]);		//create a new thread for this student

	}
	for(i = 0; i < numTutors; i++)																						//build the tutor array
	{
		tutor_array[i].id = i;																							//initialize tutor id
		tutor_array[i].busy = false;//Initialize availability
		sem_init(&(tutor_array[i].busy_mutex),0,0);		//initialize mutex for this tutor's busy flag 
		sem_init(&(tutor_array[i].sem_tutor),0,0);		//initialize semaphore to signal this tutor
		pthread_create(&(tutor_array[i].thread_tutor), NULL, *tutor_thread, &tutor_array[i]);				//create a new thread for this tutor
	}

	pthread_create(&thread_coordinator, NULL, *coordinator_thread, NULL);

	
	for(i = 0; i < numStudents; i++){																				//wait for student threads to exit
		pthread_join(student_array[i].thread_student,NULL);
	}
	/*
	//need to fix this later wont cause any errors currently but we need all threads to exit properly
	for(i = 0; i< numTutors; i++){																						//wait for tutor threads to exit
		pthread_join(tutor_array[i].thread_tutor,NULL);
	}
	pthread_join(thread_coordinator,NULL);
	*/
	
	free(student_array);																								//free memory for students and tutors
	free(tutor_array);
	student_array = NULL;
	tutor_array = NULL;
}
