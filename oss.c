#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<time.h>
#include<signal.h>
#include<sys/msg.h>

#define PERMS 0644
#define MAX_CHILDREN 20

typedef struct msgBuffer {
	long mtype;
	int msgData;
	int intData;
} msgBuffer;

struct PCB {
	int occupied; //either true or false
	pid_t pid; //process id of this child
	int startTimeSeconds; //time when it was created
	int startTimeNano; //time when it was created
	int serviceTimeSeconds; //total seconds it has been scheduled
	int serviceTimeNano; //total nanoseconds it has been scheduled
	int eventWaitSeconds; //when does its events happen?
	int eventWaitNano; //when does its events happen?
	int blocked; //is this process waiting on an event?
};

struct queue {
	int front;
	int rear;
	int entries[MAX_CHILDREN];
};

// GLOBAL VARIABLES
//For storing each child's PCB. Memory is allocated in main
struct PCB *processTable;

//Message queue id
int msqid;
//Needed for killing all child processes
int processTableSize;

// FUNCTION PROTOTYPES
void help();
void initializeProcessTable();
void incrementClock(int *shm_ptr);
void terminateProgram(int signum);
void sighandler(int signum);
void initializePCB(pid_t pid, int simulatedClock[]);
void processEnded(int pidNumber);
void outputTable();
void sendingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file);
void receivingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file, msgBuffer rcvbuf);
int randNumGenerator(int max, int pid);
void enqueue(int element, struct queue *queue);
int dequeue(struct queue *queue);
void launchChild(int simulatedClock[], int maxSimulChildren);
int checkChildren(int maxSimulChildren);

int main(int argc, char** argv) {
	//signals to terminate program properly if user hits ctrl+c or 60 seconds pass
	alarm(60);
	signal(SIGALRM, sighandler);
	signal(SIGINT, sighandler);	

	int simulatedClock[2];

	//set clock to zero
    simulatedClock[0] = 0;
    simulatedClock[1] = 0;

	//message queue setup
	key_t key;
	system("touch msgq.txt");

	//get a key for our message queue
	if ((key = ftok("msgq.txt", 1)) == -1) {
		perror("ftok");
		exit(1);
	}

	//create our message queue
	if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1) {
		perror("msgget in parent");
		exit(1);
	}

	//Resources for the scheduler
	struct queue *readyQueue;
	struct queue *blockedQueue;

	//user input variables
	int option;
	int proc;
	int simul;
	int timelimit;
	FILE *fptr;

	while ((option = getopt(argc, argv, "hn:s:t:f:")) != -1) {
  		switch(option) {
   			case 'h':
    				help();
    				break;
   			case 'n':
    				proc = atoi(optarg);
    				break;
   			case 's':
				simul = atoi(optarg);
				break;
			case 't':
				timelimit = atoi(optarg);
				break;
			case'f':
				fptr = fopen(optarg, "a");
		}
	}
	
	//sets the global var equal to the user arg
	processTableSize = proc;

	//create a mesasge buffer for each child to be created
	msgBuffer buf[processTableSize];

	//allocates memory for the processTable stored in global memory
	processTable = calloc(processTableSize, sizeof(struct PCB));
	//sets all pids in the process table to 0
	initializeProcessTable();

	//TODO: stillChildrenToLaunch should check if we have initialized the final PCB yet. 
	//TODO: childrenInSystem should check if any PCBs remain occupied
	while(checkChildren(simul) || childrenInSystem()) {
		//TODO: calls another function to check if runningChildren < simul, and if so, launches a new child.
		launchChild(simulatedClock, simul);

		//TODO: checks to see if a blocked process should be changed to ready
		//checkBlockedQueue();

		//TODO: calculates priorities of ready processes (look in notes). returns the highest priority pid
		//pid_t priority;
		//priority = calculatePriorities();

		//TODO: schedules the process with the highest priority
		//scheduleProcess(priority);

		//TODO: Waits for a message back and updates appropriate structures
		//receiveMessage(priority);

		//TODO: Outputs the process table to a log file and the screen every half second, if the log file isn't full
		//if(checkTime() && checkOutput()) 
			//outputTable(fptr);
	}

	pid_t wpid;
	int status = 0;
	while((wpid = wait(&status)) > 0);
	terminateProgram(SIGTERM);
	return EXIT_SUCCESS;
}

// FUNCTION DEFINITIONS

//TODO: Update help message and README.md
void help() {
    printf("This program is designed to have a parent process fork off into child processes.\n");
	printf("The child processes use a simulated clock in shared memory to keep track of runtime.\n");
	printf("The runtime is a random number of seconds and nanoseconds between 1 and the time limit prescribed by the user.\n");
	printf("The child processes are only allowed to check the clock when they receive a message from the parent through a message queue.\n\n");
    printf("The executable takes four flags: [-n proc], [-s simul], [-t timelimit], and [-f logfile].\n");
    printf("The value of proc determines the total number of child processes to be produced.\n");
	printf("The value of simul determines the number of children that can run simultaneously.\n");
	printf("The value of timelimit determines the maximum number of seconds that a child process can take.\n");
	printf("The file name provided will be used as a logfile to which this program outputs.\n");
	printf("\nMADE BY JACOB (JT) FOX\nOctober 12th, 2023\n");
	exit(1);
}

//sets all initial pid values to 0
void initializeProcessTable() {
	for(int count = 0; count < processTableSize; count++) {
		processTable[count].pid = 0;
	}
}

//initializes values of the pcb
void initializePCB(pid_t pid, int simulatedClock[]) {
	int index;
	index = 0;

	while(processTable[index].pid != 0)
		index++;

	processTable[index].occupied = 1;
	processTable[index].pid = pid;
	processTable[index].startTimeSeconds = simulatedClock[0];
	processTable[index].startTimeNano = simulatedClock[1];
	processTable[index].serviceTimeSeconds = 0;
	processTable[index].serviceTimeNano = 0;
	processTable[index].eventWaitSeconds = 0;
	processTable[index].eventWaitNano = 0;
	processTable[index].blocked = 0;
}

//Checks to see if another child can be launched. If so, it launches a new child.
void launchChild(int simulatedClock[], int maxSimulChildren) {
	if(checkChildren(maxSimulChildren)) {
		pid_t newChild;
		newChild = fork();
		if(newChild < 0) {
			perror("Fork failed");
			exit(-1);
		}
		else if(newChild == 0) {
			execlp("./worker", NULL);
       		exit(1);
       		}
		else {
			initializePCB(newChild, simulatedClock);
		}
	}
}

//Returns 1 if more children can be launched, returns 0 otherwise
int checkChildren(int maxSimulChildren) {
	int runningChildren;
	runningChildren = 0;
	for(int count = 0; count < processTableSize; count++) {
		if(processTable[count].occupied)
			runningChildren++;
	}

	if(runningChildren < maxSimulChildren)
		return 1;

	return 0;
}

void incrementClock(int simulatedClock[]) {
	simulatedClock[1] += 50000;
	if(simulatedClock[1] >= 1000000000) {
		simulatedClock[1] = 0;
		simulatedClock[0] += 1;
	}
}

//TODO: close the file
void terminateProgram(int signum) {
	//Kills any remaining active child processes
	int count;
	for(count = 0; count < processTableSize; count++) {
		if(processTable[count].occupied)
			kill(processTable[count].pid, signum);
	}

	//Frees memory allocated for processTable
	free(processTable);
	processTable = NULL;

	// get rid of message queue
	if (msgctl(msqid, IPC_RMID, NULL) == -1) {
		perror("msgctl to get rid of queue in parent failed");
		exit(1);
	}

	printf("Program is terminating. Goodbye!\n");
	exit(1);
}

void sighandler(int signum) {
	printf("\nCaught signal %d\n", signum);
	terminateProgram(signum);
	printf("If you're seeing this, then bad things have happened.\n");
}

//updates the PCB of a process that has ended
void processEnded(int pidNumber) {
	int i;
	for(i = 0; i < processTableSize; i++) {
		if(processTable[i].pid == pidNumber) {
			processTable[i].occupied = 0;
			return;
		}
	}
}

//TODO: Increment time by a small amount every time OSS updates a data structure
void outputTable(FILE *file) {
	printf("Process Table:\nEntry Occupied   PID\tStartS StartN\n");
	int i;
	for(i = 0; i < processTableSize; i++) {
		printf("%d\t%d\t%d\t%d\t%d\t\n\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startTimeSeconds, processTable[i].startTimeNano);
		fprintf(file, "Process Table:\nEntry Occupied   PID\tStartS StartN\n%d\t%d\t%d\t%d\t%d\t\n\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startTimeSeconds, processTable[i].startTimeNano);
	}
}

void sendingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file) {
	fprintf(file, "OSS:\t Sending message to worker %d PID %d at time %d:%d\n", chldNum, chldPid, systemClock[0], systemClock[1]);
}

void receivingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file, msgBuffer rcvbuf) {
	if(rcvbuf.msgData != 0) {
		fprintf(file, "OSS:\t Receiving message from worker %d PID %d at time %d:%d\n", chldNum, chldPid, systemClock[0], systemClock[1]);
	}
	else {
		printf("OSS:\t Worker %d PID %d is planning to terminate.\n", chldNum, chldPid);	
		fprintf(file, "OSS:\t Worker %d PID %d is planning to terminate.\n", chldNum, chldPid);	
	}
}

int randNumGenerator(int max, int pid) {
	srand(pid);
	return ((rand() % max) + 1);
}

//I yanked some generic queue code from https://www.javatpoint.com/queue-in-c
//and then modified it to fit my needs
void enqueue(int element, struct queue *queue) {  
    if (queue->rear == processTableSize - 1) {  
        printf("Queue is full");  
        return;  
    }  
    if (queue->front == -1) {  
        queue->front = 0;  
    }  
    queue->rear++;  
    queue->entries[queue->rear] = element;  
}  
  
int dequeue(struct queue *queue) {  
    if (queue->front == -1 || queue->front > queue->rear) {  
        printf("Queue is empty");  
        return -1;  
    }  
    int element = queue->entries[queue->front];  
    queue->front++;  
    return element;  
}  