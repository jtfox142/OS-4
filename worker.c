#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>

#define PERMS 0644

typedef struct msgbuffer {
	long mtype;
	int msgData;
	int intData;
} msgbuffer;

void output(int pid, int ppid, int sysClockS, int sysClockNano, int termTimeS, int termTimeNano) {
	printf("WORKER PID:%d PPID:%d SysclockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d\n", pid, ppid, sysClockS, sysClockNano, termTimeS, termTimeNano);	
}

int checkTime(int sysClockS, int sysClockNano, int termTimeS, int termTimeNano) {
	if(sysClockS < termTimeS)
		return 1;
	if(sysClockNano < termTimeNano)
		return 1;
	return 0;
}

int main(int argc, char** argv) {
	msgbuffer buf;
	buf.mtype = 1;
	buf.msgData = 0;
	buf.intData = 0;
	int msqid = 0;
	key_t key;

	// get a key for our message queue
	if ((key = ftok("msgq.txt", 1)) == -1) {
		perror("ftok");
		exit(1);
	}

	// create our message queue
	if ((msqid = msgget(key, PERMS)) == -1) {
		perror("msgget in child");
		exit(1);
	}
	
	int seconds;
	seconds = atoi(argv[0]);
	int nanoseconds;
	nanoseconds = atoi(argv[1]);	
       	
	pid_t ppid = getppid();
	pid_t pid = getpid();

	//get access to shared memory
	const int sh_key = ftok("./oss.c", 0);
	int shm_id = shmget(sh_key, sizeof(int) * 2, IPC_CREAT | 0666);
	int *shm_ptr = shmat(shm_id, 0, 0);

	int msgReceived; //set to 1 when message comes in from parent
	msgReceived = 0;
	while(!msgReceived) {
		if(msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) >= 0)
			msgReceived = 1;
	}

	//get term time after first message received
	int termTimeS;
        termTimeS = shm_ptr[0] + seconds;
        int termTimeNano;
        termTimeNano = shm_ptr[1] + nanoseconds;

	//print intial output
	printf("WORKER PID:%d PPID:%d Called with OSS: TermTimeS:%d TermTimeNano:%d\n", pid, ppid, termTimeS, termTimeNano);
	printf("--Received message\n");
	msgReceived = 0;	

	buf.mtype = getppid();
	buf.intData = getppid();
	if(msgsnd(msqid, &buf, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
		printf("msgsnd to parent failed.\n");
		exit(1);
	}

	int outputTimer;
      	outputTimer = shm_ptr[0];
	int outputCounter;
	outputCounter = 1;	

	int timeLeft;
	timeLeft = 1;
	
	//the program waits to receive another message before checking the clock and sending another message back
	while(timeLeft) { //timesUp will evaluate to false if the term time has not been reached, executing another loop
		buf.mtype = 1;
		buf.intData = 0;
		if(msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) {
			printf("msgrcv failed in child\n");
			exit(1);
		}
		else
		{
			timeLeft = checkTime(shm_ptr[0], shm_ptr[1], termTimeS, termTimeNano);
			if(timeLeft) {
				buf.mtype = getppid();
				buf.intData = getppid();
				buf.msgData = 1;
				if(msgsnd(msqid, &buf, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
					printf("msgsnd to parent failed\n");
					exit(1);
				}
			}
			if(shm_ptr[0] > outputTimer) {
				outputTimer = shm_ptr[0];
				output(pid, ppid, shm_ptr[0], shm_ptr[1], termTimeS, termTimeNano);
				printf("--%d seconds have passed since starting\n", outputCounter++);
			}
		}
	}
	
	//if term time has elapsed, the loop has terminated. Send back intData = 1 and final output
	output(pid, ppid, shm_ptr[0], shm_ptr[1], termTimeS, termTimeNano);
	printf("--Terminating\n");
	buf.msgData = 0;
	buf.mtype = getppid();
	buf.intData = getppid();
	if(msgsnd(msqid, &buf, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
		perror("msgsnd to parent failed\n");
		exit(1);
	}	

	//detach from shared memory
	shmdt(shm_ptr);
	return EXIT_SUCCESS;
}
