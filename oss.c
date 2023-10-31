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

typedef struct msgBuffer {
	long mtype;
	int msgData;
	int intData;
} msgBuffer;

struct PCB {
	int occupied;
	pid_t pid;
	int startTimeSec;
	int startTimeNano;
};

// GLOBAL VARIABLES

//For storing each child's PCB. Memory is allocated in main
struct PCB *processTable;

//Shared memory variables
int sh_key;
int shm_id;
int *shm_ptr;

//Message queue id
int msqid;

//Needed for killing all child processes
int arraySize;

// FUNCTION PROTOTYPES

void help();
void incrementClock(int *shm_ptr);
void terminateProgram(int signum);
void sighandler(int signum);
void startPCB(int tableEntry, int pidNumber, int *time);
void endPCB(int pidNumber);
void outputTable();
void sendingOutput(int chldNum, int chldPid, FILE *file);
void receivingOutput(int chldNum, int chldPid, FILE *file, msgBuffer rcvbuf);
int randNumGenerator(int max, int pid);

int main(int argc, char** argv) {
	//signals to terminate program properly if user hits ctrl+c or 60 seconds pass
	alarm(60);
	signal(SIGALRM, sighandler);
	signal(SIGINT, sighandler);	

	//allocate shared memory
	sh_key = ftok("./oss.c", 0);
	shm_id = shmget(sh_key, sizeof(int) * 2, IPC_CREAT | 0666);
	if(shm_id <= 0) {
		printf("Shared memory allocation failed\n");
		exit(1);
	}

	//attach to shared memory
	shm_ptr = shmat(shm_id, 0 ,0);
	if(shm_ptr <= 0) {
		printf("Attaching to shared memory failed\n");
		exit(1);
	}
	
	//set clock to zero
        shm_ptr[0] = 0;
        shm_ptr[1] = 0;

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

	//user input vars
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
	arraySize = proc;

	//define a msgbuffer for each child to be created. Does it need to be size proc, or could it be size simul?
	msgBuffer buf;

	//allocates memory for the processTable stored in global memory
	processTable = calloc(arraySize, sizeof(struct PCB));

	pid_t wpid;
	int status = 0;
	while((wpid = wait(&status)) > 0);
	terminateProgram(SIGTERM);
	return EXIT_SUCCESS;
}

// FUNCTION DEFINITIONS

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

void incrementClock(int *shm_ptr) {
	shm_ptr[1] += 50000;
	if(shm_ptr[1] >= 1000000000) {
		shm_ptr[1] = 0;
		shm_ptr[0] += 1;
	}
}

void terminateProgram(int signum) {
	//detaches from and deletes shared memory
	shmdt(shm_ptr);
	shmctl(shm_id, IPC_RMID, NULL);

	//Kills any remaining active child processes
	int count;
	for(count = 0; count < arraySize; count++) {
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


void startPCB(int tableEntry, int pidNumber, int *time) {
	processTable[tableEntry].occupied = 1;
	processTable[tableEntry].pid = pidNumber;
	processTable[tableEntry].startTimeSec = time[0];
	processTable[tableEntry].startTimeNano = time[1];
}

void endPCB(int pidNumber) {
	int i;
	for(i = 0; i < arraySize; i++) {
		if(processTable[i].pid == pidNumber) {
			processTable[i].occupied = 0;
			return;
		}
	}
}

void outputTable() {
	printf("Process Table:\nEntry Occupied   PID\tStartS StartN\n");
	int i;
	for(i = 0; i < arraySize; i++) {
		printf("%d\t%d\t%d\t%d\t%d\t\n\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startTimeSec, processTable[i].startTimeNano);
	}
}

void sendingOutput(int chldNum, int chldPid, FILE *file) {
	fprintf(file, "OSS:\t Sending message to worker %d PID %d at time %d:%d\n", chldNum, chldPid, shm_ptr[0], shm_ptr[1]);
}

void receivingOutput(int chldNum, int chldPid, FILE *file, msgBuffer rcvbuf) {
	if(rcvbuf.msgData != 0) {
		fprintf(file, "OSS:\t Receiving message from worker %d PID %d at time %d:%d\n", chldNum, chldPid, shm_ptr[0], shm_ptr[1]);
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