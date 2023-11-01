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
#define SCHEDULED_TIME 10000000
#define ONE_SECOND 1000000000
#define STANDARD_CLOCK_INCREMENT 1050

typedef struct msgBuffer {
	long mtype;
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

// GLOBAL VARIABLES
//For storing each child's PCB. Memory is allocated in main
struct PCB *processTable;
//Resources for the scheduler
pid_t *readyQueue;
pid_t *blockedQueue;
//Self descriptive. Easier than passing it to functions that don't actually need it, just so that it can get 
//passed into the one that does.
int simulatedClock[2];
//Message queue id
int msqid;
//Needed for killing all child processes
int processTableSize;

// FUNCTION PROTOTYPES
//Help function
void help();
//Process table functions
void initializeProcessTable();
void initializePCB(pid_t pid);
void processEnded(int pidNumber);
void outputTable(FILE *fptr);
//OSS functions
void incrementClock(int timePassed);
void launchChild(int maxSimulChildren, pid_t *ready);
int calculatePriorities(pid_t *ready);
void scheduleProcess(pid_t process, msgBuffer buf);
void receiveMessage(pid_t process, msgBuffer buf, pid_t *blockedQueue);
void updateTable(pid_t process, msgBuffer rcvbuf, pid_t *blockedQueue);
void checkBlockedQueue(pid_t *blocked, pid_t *ready);
//Program end functions
void terminateProgram(int signum);
void sighandler(int signum);
//Log file functions
void sendingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file);
void receivingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file, msgBuffer rcvbuf);
//Queue functions
int addItemToQueue(pid_t *queue, pid_t itemToAdd);
int removeItemFromQueue(pid_t *queue, pid_t itemToRemove);
void initializeQueue(pid_t *queue);
//Helper functions
int checkChildren(int maxSimulChildren);
int stillChildrenToLaunch();
int childrenInSystem();
int findTableIndex(pid_t pid);
void calculateEventTime(pid_t process, int entry);
double priorityArithmetic(int currentEntry);
void checkTime(int *outputTimer, FILE *fptr);


int main(int argc, char** argv) {
	//signals to terminate program properly if user hits ctrl+c or 60 seconds pass
	alarm(60);
	signal(SIGALRM, sighandler);
	signal(SIGINT, sighandler);	

	//set clock to zero
    simulatedClock[0] = 0;
    simulatedClock[1] = 0;

	readyQueue = (pid_t*)malloc(processTableSize * sizeof(int));
	blockedQueue = (pid_t*)malloc(processTableSize * sizeof(int));
	

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
	initializeQueue(readyQueue);
	initializeQueue(blockedQueue);

	//create a mesasge buffer for each child to be created
	msgBuffer buf;

	//allocates memory for the processTable stored in global memory
	processTable = calloc(processTableSize, sizeof(struct PCB));
	//sets all pids in the process table to 0
	initializeProcessTable();

	int outputTimerStart;
	int *outputTimer = &outputTimerStart;
	*outputTimer = 0;

	//stillChildrenToLaunch checks if we have initialized the final PCB yet. 
	//childrenInSystem checks if any PCBs remain occupied
	while(stillChildrenToLaunch() || childrenInSystem()) {
		//calls another function to check if runningChildren < simul, and if so, launches a new child.
		launchChild(simul, readyQueue);

		//checks to see if a blocked process should be changed to ready
		checkBlockedQueue(blockedQueue, readyQueue);

		//calculates priorities of ready processes (look in notes). returns the highest priority pid
		pid_t priority;
		priority = calculatePriorities(readyQueue);

		//schedules the process with the highest priority
		scheduleProcess(priority, buf);	

		//Waits for a message back and updates appropriate structures
		receiveMessage(priority, buf, blockedQueue);

		// Outputs the process table to a log file and the screen every half second,
		checkTime(outputTimer, fptr);
	}

	pid_t wpid;
	int status = 0;
	while((wpid = wait(&status)) > 0);
	terminateProgram(SIGTERM);
	return EXIT_SUCCESS;
}

// FUNCTION DEFINITIONS

void checkTime(int *outputTimer, FILE *fptr) {
	printf("simulatedClock:%d\n",simulatedClock[1]);
	if(abs(simulatedClock[1] - *outputTimer) >= ONE_SECOND / 2){
			*outputTimer = simulatedClock[1];
			printf("\nOSS PID:%d SysClockS:%d SysClockNano:%d\n", getpid(), simulatedClock[0], simulatedClock[1]); 
			outputTable(fptr);
		}
}

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
void initializePCB(pid_t pid) {
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
void launchChild(int maxSimulChildren, pid_t *ready) {
	if(checkChildren(maxSimulChildren) && stillChildrenToLaunch()) {
		pid_t newChild;
		newChild = fork();
		if(newChild < 0) {
			perror("Fork failed");
			exit(-1);
		}
		else if(newChild == 0) {
			char fakeArg[sizeof(int)];
			snprintf(fakeArg, sizeof(int), "%d", 1);
			execlp("./worker", fakeArg, NULL);
       		exit(1);
       		}
		else {
			initializePCB(newChild);
			if(!addItemToQueue(ready, newChild)) {
				perror("Failed to add child to ready queue\n");
				exit(1);
			}
		}
	}
}

//Returns 1 if the maximum number of running children has not been reached, returns 0 otherwise
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

//If the maximum number of children has not been reached, return true. Otherwise return false
int stillChildrenToLaunch() {
	if(processTable[processTableSize - 1].pid == 0) {
		return 1;
	}
	return 0;
}

//Returns 1 if any children are running. Returns 0 otherwise
int childrenInSystem() {
	for(int count = 0; count < processTableSize; count++) {
		if(processTable[count].occupied) {
			return 1;
		}
	}
	return 0;
}

//returns the buffer index corresponding to a given pid
int findTableIndex(pid_t pid) {
	for(int count = 0; count < processTableSize; count++) {
		if(processTable[count].pid == pid)
			return count;
	}
	return 0;
}

//"Schedules" a process by sending it a message to indicate that it should run
//Returns 1 if a process was successful scheduled
void scheduleProcess(pid_t process, msgBuffer buf) {
	incrementClock(STANDARD_CLOCK_INCREMENT);
	buf.mtype = process;
	buf.intData = SCHEDULED_TIME;

	if(msgsnd(msqid, &buf, sizeof(msgBuffer) - sizeof(long), 0) == -1) {
		perror("msgsnd to child failed\n");
		exit(1);
	}
}

//Receives a message back from child that indicates how much time the child used and if it is blocked
//Updates process table accordingly
void receiveMessage(pid_t process, msgBuffer buf, pid_t *blockedQueue) {
	msgBuffer rcvbuf;
	if(msgrcv(msqid, &rcvbuf, sizeof(msgBuffer), getpid(), 0) == -1) {
			perror("msgrcv from child failed\n");
			exit(1);
	}
	updateTable(process, rcvbuf, blockedQueue);
}

//Updates the process control table
void updateTable(pid_t process, msgBuffer rcvbuf, pid_t *blockedQueue) {
	int entry = findTableIndex(process);
	if(rcvbuf.intData < 0) {
		processTable[entry].occupied = 0;
	}
	else if(rcvbuf.intData < SCHEDULED_TIME) {
		processTable[entry].blocked = 1;
		addItemToQueue(blockedQueue, processTable[entry].pid);
		calculateEventTime(process, entry);
	}
	processTable[entry].serviceTimeNano = processTable[entry].serviceTimeNano + abs(rcvbuf.intData);
	if(processTable[entry].serviceTimeNano > ONE_SECOND) {
		processTable[entry].serviceTimeSeconds = processTable[entry].serviceTimeSeconds + 1;
		processTable[entry].serviceTimeNano = processTable[entry].serviceTimeNano - ONE_SECOND;
	}
}

//Calculates the event wait time for a blocked process
void calculateEventTime(pid_t process, int entry) {
	const int SEC_MAX = 5;
	//1000 milliseconds = 1 second
	const int NANO_MAX = ONE_SECOND;

	srand(process);
	processTable[entry].eventWaitSeconds = simulatedClock[0] + ((rand() % SEC_MAX) + 1);
	processTable[entry].eventWaitNano = simulatedClock[1] + ((rand() % NANO_MAX) + 1);
}

void incrementClock(int timePassed) {
	simulatedClock[1] += timePassed;
	if(simulatedClock[1] >= ONE_SECOND) {
		simulatedClock[1] -= ONE_SECOND;
		simulatedClock[0] += 1;
	}
}

//checks to see if a blocked process should be changed to ready
void checkBlockedQueue(pid_t *blocked, pid_t *ready) {
	int entry;
	for(int count = 0; count < processTableSize; count++) {
		if(blockedQueue[count] != -1) {
			entry = findTableIndex(blockedQueue[count]);
			if(!processTable[entry].occupied)
				continue;
			if(processTable[entry].eventWaitSeconds >= simulatedClock[0] && processTable[entry].eventWaitNano > simulatedClock[1]) {
				if(!removeItemFromQueue(blocked, processTable[entry].pid)) {
					perror("Item not found in blocked queue");
				}

				if(!addItemToQueue(ready, processTable[entry].pid)) {
					perror("ready queue overflow\n");
					exit(1);
				}
			}
		}
	}
}

pid_t calculatePriorities(pid_t *ready) {
	pid_t priorityPid;
	priorityPid = readyQueue[0];
	double highestPriority;
	highestPriority = 0;
	pid_t currentPid;
	double currentPriority;

	//for each entry in the readyqueue, calculate the priority. if the current priority > highest, it = highest

	for(int count = 0; count < processTableSize; count++) {
		currentPid = readyQueue[count];
		if(currentPid = -1)
			currentPriority = -1;
		else
			currentPriority = priorityArithmetic(findTableIndex(currentPid));
		if(currentPriority >= highestPriority) {
			highestPriority = currentPriority;
			priorityPid = currentPid;
		}
	}

	return priorityPid;
}

double priorityArithmetic(int currentEntry) {
	double serviceTime = processTable[currentEntry].serviceTimeSeconds + (processTable[currentEntry].serviceTimeNano / ONE_SECOND);
	double timeInSystem = processTable[currentEntry].startTimeSeconds + (processTable[currentEntry].startTimeNano / ONE_SECOND);
	return (serviceTime / timeInSystem); 
}

//TODO: close the file
void terminateProgram(int signum) {
	//Kills any remaining active child processes
	int count;
	for(count = 0; count < processTableSize; count++) {
		if(processTable[count].occupied)
			kill(processTable[count].pid, signum);
	}

	//Frees allocated memory
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

void outputTable(FILE *file) {
	printf("Process Table:\nEntry Occupied   PID\tStartS StartN\tServiceS\tServiceN\tWaitS\tWaitN\tBlocked\n");
	int i;
	for(i = 0; i < processTableSize; i++) {
		printf("%d\t%d\t%d\t%d\t%d%d\t%d\t%d\t%d\t%d\t\n\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startTimeSeconds, processTable[i].startTimeNano, processTable[i].serviceTimeSeconds, processTable[i].serviceTimeSeconds, processTable[i].eventWaitSeconds, processTable[i].eventWaitNano, processTable[i].blocked);
		fprintf(file, "Process Table:\nEntry Occupied   PID\tStartS StartN\tServiceS\tServiceN\tWaitS\tWaitN\tBlocked\n\n%d\t%d\t%d\t%d\t%d%d\t%d\t%d\t%d\t%d\t\n\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startTimeSeconds, processTable[i].startTimeNano, processTable[i].serviceTimeSeconds, processTable[i].serviceTimeSeconds, processTable[i].eventWaitSeconds, processTable[i].eventWaitNano, processTable[i].blocked);
	}
}

void sendingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file) {
	fprintf(file, "OSS:\t Sending message to worker %d PID %d at time %d:%d\n", chldNum, chldPid, systemClock[0], systemClock[1]);
}

void receivingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file, msgBuffer rcvbuf) {
	if(rcvbuf.intData != 0) {
		fprintf(file, "OSS:\t Receiving message from worker %d PID %d at time %d:%d\n", chldNum, chldPid, systemClock[0], systemClock[1]);
	}
	else {
		printf("OSS:\t Worker %d PID %d is planning to terminate.\n", chldNum, chldPid);	
		fprintf(file, "OSS:\t Worker %d PID %d is planning to terminate.\n", chldNum, chldPid);	
	}
}

//tried implementing an actual queue. didn't make sense because the operations
//to be done on it didn't work like a queue would (like assigning priorities).
//Noodled with it anyway for like 3 hours before giving up and doing it the
//easy way. it's just a simulater, anyway.
int addItemToQueue(pid_t *queue, pid_t itemToAdd) {
	for(int count = 0; count < processTableSize; count++) {
		if(queue[count] == -1) {
			queue[count] = itemToAdd;
			return 1;
		}
	}
	printf("queue full\n");
	return 0;
}

int removeItemFromQueue(pid_t *queue, pid_t itemToRemove) {
	for(int count = 0; count < processTableSize; count++) {
		if(queue[count] == itemToRemove) {
			queue[count] = -1;
			return 1;
		}
	}
	printf("pid not found in queue\n");
	return 0;
}

void initializeQueue(pid_t *queue) {
	int count;
	for(count = 0; count < processTableSize; count++) {
		queue[count] = -1;
	}
}