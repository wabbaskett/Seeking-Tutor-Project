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
sem_t mut_sessions;             //lock for the freeChairs variable
sem_t mut_total;                //lock for the totalRequests variable
sem_t mut_complete;             //lock for the threadsComplete variable

sem_t mut_pQ;                   //lock for the priority queue
sem_t mut_wQ;                   //lock for the waiting queue
sem_t mut_tQ;                   //lock for the tutor queue

sem_t sem_coordinator_s;          //semaphore for a student to signal co-ordinator
sem_t sem_coordinator_t;          //semaphore for a tutor to signal co-ordinator



/* queues will only need to store an integer so that the student/tutor structs can be accessed from a global array doing it this way made it easier to keep track of the student and tutor thread semaphores
*/
//Defining a FIFO queue using the linux queue
struct student_id{
	//put any individual student info here
	STAILQ_ENTRY(customer) studentQueue; //Singly linked tail queue for FIFO
};

STAILQ_HEAD(stailhead, student_id);

struct stailhead head;



// structure to hold information about students
typedef struct {
	int id;										//id for student (equivilent to its position in student array)
	int help_needed;							//variable to track how many times a student thread should queue
	int tutor_mark;								//temportary storage to track the id of the tutor that helped this student
	sem_t sem_student;							//semaphore to signal a student
	pthread_t thread_student;					//pthread for a student
	char name[50];

}Student;

// structure to hold information about tutors
typedef struct {
	int id;										//id for a tutor (equivilent to its position in tutor array)
	sem_t sem_tutor;							//semaphore to signal a tutor
	pthread_t thread_tutor;					    //pthread for a tutor
	char name[50];
}Tutor;

Student *student_array = NULL;					//pointer to array of student structs (this will be dynamically allocated in main)
Tutor *tutor_array = NULL;						//pointer to array of tutor structs (this will be dynamically allocated in main)


void *student_thread(void *arg)					//function for student threads takes student id (int) as a parameter
{
	int id = *((int *)arg); 
	while(1)
	{											
		sem_wait(&mut_wQ);																		//acquire chair and waiting queue locks
		sem_wait(&mut_chair);								
		if(freeChairs == 0)
		{
			sem_post(&mut_wQ);																	//release locks
			sem_post(&mut_chair);																
			printf("Student %d found no empty chair. Will come again later\n",id);
			sleep(1);
			continue;
		}
		else
		{
			freeChairs -= 1;
			printf("Student %d takes a seat. Empty chairs remaining = %d\n",id,freeChairs);
			//add this students id to the waiting queue
			sem_post(&mut_chair);																//release chair and waiting queue locks
			sem_post(&mut_wQ);

			sem_post(&sem_coordinator_s);														//signal the co-ordinator that a student is waiting to be queued
			sem_wait(&(student_array[id].sem_student));											//wait for signal from a tutor that tutoring is complete

			printf("Student %d receives help from Tutor %d\n",id,student_array[id].tutor_mark);
			student_array[id].help_needed -= 1;                                                 // update help needed
			student_array[id].tutor_mark = 0;                                                   // reset tutor mark 

			if(student_array[id].help_needed == 0)												//exit thread when help is no longer needed
			{
				sem_wait(&mut_complete);                                                        //acquire lock
				threadsComplete ++;                                                             //update number of completed jobs
				sem_post(&mut_complete);                                                        //release lock
				pthread_exit(NULL);                                                             //exit
			}
		}
	}
	//printf("Student: %d  Help Needed: %d\n", id, student_array[id].help_needed);
    //pthread_exit(NULL);
	return NULL;
}

void *coordinator_thread()
{
	return NULL;
}

void *tutor_thread(void *arg)					//function for tutor threads takes tutor id (int) as a parameter
{
	int id = *((int *)arg); 
	while(1)
	{
		sem_wait(&mut_complete);													//acquire lock
		if(threadsComplete == numStudents)											//exit when threads are complete (need to fix this does not cause error but the assignment requires all threads to exit)
		{
			sem_post(&mut_complete);												//release lock
			pthread_exit(NULL);
		}
		sem_post(&mut_complete);													//release lock
		
		// enter tutor waiting queue

		sem_post(&sem_coordinator_t);												// notify co-ordinator that a tutor is ready
		sem_wait(&(tutor_array[id].sem_tutor));										// wait for co-ordinator to wake this thread

		sem_wait(&mut_pQ);														    // acquire priority queue lock chair, and current locks
		sem_wait(&mut_current);	
		sem_wait(&mut_chair);
		//get next student in priority queue
		currentStudents ++;															//update students currently being helped	
		freeChairs ++;
		sem_post(&mut_pQ);															// release locks
		sem_post(&mut_current);
		sem_post(&mut_chair);

		student_array[studentId].tutor_mark = id;									//mark student with tutor id (used for output by student thread)
		sleep(1);																	//wait to simulate tutoring time

		sem_wait(&mut_current);														//acquire locks for required variables
		sem_wait(&mut_total);
		sem_wait(&mut_sessions);
		currentStudents --;															//update variables
		totalSessions ++;

		printf("Student %d tutored by tutor %d. Total sessions being tutored = %d. Total sessions being tutored by all = %d\n",studentId,id,currentStudents,totalSessions);	//output

		sem_post(&mut_current);														//release locks
		sem_post(&mut_total);
		sem_post(&mut_sessions);

		sem_wait(&mut_complete);
		if(threadsComplete == numStudents)
		{
			sem_post(&mut_complete);
			pthread_exit(NULL);
		}
		sem_post(&mut_complete);


		sem_post(&(student_array[studentId].sem_student));

		
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
	sem_init(&mut_sessions,0,1);

	sem_init(&mut_pQ,0,1);																								//initialize locks for queues
	sem_init(&mut_wQ,0,1);
	sem_init(&mut_tQ,0,1);

	sem_init(&sem_coordinator_s,0,0);																					//initialize semaphore to signal co-ordinator
	sem_init(&sem_coordinator_t,0,0);

	freeChairs = numChairs;																								//set freeChairs to maximum number of chairs (all chairs are empty at start)
	pthread_t thread_coordinator;


	student_array = (Student *)malloc((numStudents + 1) * sizeof(Student));												//allocate memory for students
	tutor_array = (Tutor *)malloc((numTutors + 1) * sizeof(Tutor));														//allocate memory for tutors

	for(i = 1; i <= numStudents; i++)																					//build the student array
	{
        student_array[i].id = i;																						//initialize student id
		student_array[i].help_needed = numHelp;																			//initialize help required
		student_array[i].tutor_mark = 0;																				//initialize tutor mark
		sem_init(&(student_array[i].sem_student),0,0);																	//initialize semaphore to signal this student
		pthread_create(&(student_array[i].thread_student), NULL, *student_thread, (void *)&(student_array[i].id));		//create a new thread for this student

	}
	for(i = 1; i <= numTutors; i++)																						//build the tutor array
	{
		tutor_array[i].id = i;																							//initialize tutor id
		sem_init(&(tutor_array[i].sem_tutor),0,0);																		//initialize semaphore to signal this tutor
		pthread_create(&(tutor_array[i].thread_tutor), NULL, *tutor_thread, (void *)&(tutor_array[i].id));				//create a new thread for this tutor
	}
	pthread_create(&thread_coordinator, NULL, *coordinator_thread, (void *)&(tutor_array[i].id));

	//Initializing the student threads
	
	for(i = 1; i <= numStudents; i++){																				//wait for student threads to exit
    	pthread_join(student_array[i].thread_student,NULL);
    }
	//need to fix this later wont cause any errors currently but we need all threads to exit properly
	//for(i = 1; i<= numTutors; i ++){																						//wait for tutor threads to exit
	//pthread_join(tutor_array[i].thread_tutor,NULL);
	//}
	//pthread_join(thread_coordinator,NULL);


	free(student_array);																								//free memory for students and tutors
	free(tutor_array);
	student_array = NULL;
	tutor_array = NULL;
}
