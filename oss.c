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

// FUNCTIONS

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

int randNumGenerator(int max) {
	srand(time(NULL));
	return ((rand() % max) + 1);
}

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

	int totalChildren;
	int runningChildren;
	totalChildren = 0;
	runningChildren = 0;	

	//vars for fetching worker termTime values
	const int maxNano = 1000000000;
	int randNumS, randNumNano;

	//char str for sending randNum values to the worker
	char secStr[sizeof(int)];
	char nanoStr[sizeof(int)];

	//initialize child processes
	while(runningChildren < simul) { 
  		pid_t childPid = fork();                

      		if(childPid == 0) {
			randNumS = randNumGenerator(timelimit);
			randNumNano = randNumGenerator(maxNano);
			snprintf(secStr, sizeof(int), "%d", randNumS);
			snprintf(nanoStr, sizeof(int), "%d", randNumNano);
			execlp("./worker", secStr, nanoStr,  NULL);
       			exit(1);
       		}
		else {
			startPCB(runningChildren, childPid, shm_ptr);
			runningChildren++;
			totalChildren++;
		}
       	}
      
	//outputTimer ensures that output occurs every half second
	int outputTimer;
	outputTimer = 0;
	int halfSecond = 500000000;

	//iterator to keep track of the next child in rotation
	int nextChild;
	nextChild = 0;
	do {
		incrementClock(shm_ptr);

		if(abs(shm_ptr[1] - outputTimer) >= halfSecond){
			outputTimer = shm_ptr[1];
			printf("\nOSS PID:%d SysClockS:%d SysClockNano:%d\n", getpid(), shm_ptr[0], shm_ptr[1]); 
			outputTable();
		}
		

		int status;
		int pid = waitpid(-1, &status, WNOHANG); //will return 0 if no processes have terminated
		if(pid) {
			printf("\n\n\nI died\n"); //This is never output to the terminal, meaning that waitpid never catches a terminated process
			endPCB(pid); //sets processTable.occupied to 0
			runningChildren--;
			if(totalChildren < arraySize) {
				pid_t childPid = fork(); //Launches child
				if(childPid == 0) {
					randNumS = randNumGenerator(timelimit);
					randNumNano = randNumGenerator(maxNano);
					snprintf(secStr, sizeof(int), "%d", randNumS);
					snprintf(nanoStr, sizeof(int), "%d", randNumNano);
					execlp("./worker", secStr, nanoStr, NULL);
					exit(1);
				}
				else {
					startPCB(totalChildren, childPid, shm_ptr);
					runningChildren++;
					totalChildren++;
				}
			}
		}

		//gets the pid of the next child in the rotation
		int msgPid;
		msgPid = processTable[nextChild].pid;

		buf.mtype = msgPid;
		buf.intData = msgPid;

		if(msgsnd(msqid, &buf, sizeof(msgBuffer) - sizeof(long), 0) == -1) {
			perror("msgsnd to child failed\n");
			exit(1);
		}

		sendingOutput(nextChild, msgPid, fptr);

		msgBuffer rcvbuf;
		rcvbuf.mtype = 1;
		rcvbuf.intData = 0;
		rcvbuf.msgData = 1;

		//ATTENTION PROFESSOR
		//For whatever reason, my code seems to break here. 
		//It's very possible that this is not the actual spot it is broken, but this is where
		//it gets stuck at the very least. What appears to be happening is that waitpid doesn't catch
		//that any children have terminated, and it gets stuck waiting here for a new message that will
		//never come. I have messed with it for multiple hours now. I cannot get it to work.
		//Very frustrating.
		//Sorry for the spaghetti code
		if(msgrcv(msqid, &rcvbuf, sizeof(msgBuffer), getpid(), 0) == -1) {
			perror("failed to receive message in parent\n");
			exit(1);
		}

		receivingOutput(nextChild, msgPid, fptr, rcvbuf);

		//keeps nextChild within the bounds of the array
		if(nextChild == arraySize - 1)
			nextChild = 0;
		else if(processTable[nextChild + 1].occupied == 0){
			nextChild = 0;
			int i;
			for(i = 0; i < arraySize; i++) {
				if(processTable[nextChild].occupied == 0)
					nextChild++;
				else
					break;
			}
		}
		else
			nextChild++;

	} while(runningChildren);	

	printf("DID I BREAK THE LOOP?\n"); //No. No you did not.
	pid_t wpid;
	int status = 0;
	while((wpid = wait(&status)) > 0);
	terminateProgram(SIGTERM);
	return EXIT_SUCCESS;
}


