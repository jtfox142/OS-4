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
#define SCHEDULED_TIME 10

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
//Help function
void help();
//Process table functions
void initializeProcessTable();
void initializePCB(pid_t pid, int simulatedClock[]);
void processEnded(int pidNumber);
void outputTable();
//OSS functions
void incrementClock(int *shm_ptr);
void launchChild(int simulatedClock[], int maxSimulChildren);
//TODO: int calculatePriority();
int scheduleProcess(pid_t process, msgBuffer buf);
void receiveMessage(pid_t process, msgBuffer buf);
//Program end functions
void terminateProgram(int signum);
void sighandler(int signum);
//Log file functions
void sendingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file);
void receivingOutput(int chldNum, int chldPid, int systemClock[2], FILE *file, msgBuffer rcvbuf);
//Queue fucntions
void enqueue(int element, struct queue *queue);
int dequeue(struct queue *queue);
//Checking functions
int checkChildren(int maxSimulChildren);
int stillChildrenToLaunch();
int childrenInSystem();
int findBufferIndex(pid_t pid);


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
	//TODO: i don't think i need this: msgBuffer buf[processTableSize];
	msgBuffer buf;

	//allocates memory for the processTable stored in global memory
	processTable = calloc(processTableSize, sizeof(struct PCB));
	//sets all pids in the process table to 0
	initializeProcessTable();

	//stillChildrenToLaunch checks if we have initialized the final PCB yet. 
	//childrenInSystem checks if any PCBs remain occupied
	while(stillChildrenToLaunch() || childrenInSystem()) {
		//calls another function to check if runningChildren < simul, and if so, launches a new child.
		launchChild(simulatedClock, simul);

		//TODO: checks to see if a blocked process should be changed to ready
		//checkBlockedQueue();

		//TODO: calculates priorities of ready processes (look in notes). returns the highest priority pid
		//pid_t priority;
		//priority = calculatePriorities();

		pid_t priority = processTable[0].pid;
		printf("first pid: %d\n", priority);
		//schedules the process with the highest priority
		if(!scheduleProcess(priority, buf)) {
			perror("Failed to schedule process");
			exit(1);
		}
		else
			printf("message sent from parent.\n");

		//TODO: Waits for a message back AND UPDATES APPROPRIATE STRUCTURES
		receiveMessage(priority, buf);

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
			initializePCB(newChild, simulatedClock);
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
	printf("Checking for children to launch\n");
	if(processTable[processTableSize - 1].pid == 0) {
		printf("Children to launch is true\n");
		return 1;
	}
	printf("Children to launch is false\n");
	return 0;
}

//Returns 1 if any children are running. Returns 0 otherwise
int childrenInSystem() {
	printf("Checking for children in system\n");
	for(int count = 0; count < processTableSize; count++) {
		if(processTable[count].occupied) {
			printf("children in system is true\n");
			return 1;
		}
	}
	printf("Children in system is false\n");
	return 0;
}

//returns the buffer index corresponding to a given pid
int findBufferIndex(pid_t pid) {
	for(int count = 0; count < processTableSize; count++) {
		if(processTable[count].pid == pid)
			return count;
	}
	return 0;
}

//"Schedules" a process by sending it a message to indicate that it should run
//Returns 1 if a process was successful scheduled
int scheduleProcess(pid_t process, msgBuffer buf) {
	/*TODO: int index = findBufferIndex(process);
	if(!index) {
		printf("Attempted to schedule a process that has not started\n");
		return 0;
	}
	buf[index].mtype = process;
	buf[index].intData = process;
	buf[index].msgData = 10;*/

	buf.mtype = process;
	buf.intData = process;
	buf.msgData = SCHEDULED_TIME;

	if(msgsnd(msqid, &buf, sizeof(msgBuffer) - sizeof(long), 0) == -1) {
		perror("msgsnd to child failed\n");
		exit(1);
	}
	
	return 1;
}

//Receives a message back from child that indicates how much time the child used and if it is blocked
//Updates process table accordingly
void receiveMessage(pid_t process, msgBuffer buf) {
	printf("waiting on message from child\n");
	if(msgrcv(msqid, &buf, sizeof(msgBuffer), process, 0) == -1) {
			perror("msgrcv from child failed\n");
			exit(1);
	}

	printf("message received from child: %d\n", buf.msgData);
	/*if(buf.msgData == SCHEDULED_TIME) {
		processTable[process].occupied = 0;
	}
	else if(buf.msgData > 0) {
		processTable[process].blocked = 1;
	}
	processTable[process].serviceTimeNano = processTable[process].serviceTimeNano + buf.msgData;*/

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
