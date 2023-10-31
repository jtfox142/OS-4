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

int main(int argc, char** argv) {

	/*
	TODO: 
		* Create random number generator (RNG)
		* Use RNG to decide which action worker takes
			* Use all of time sent by parent
				* Send back timeUsed (will equal the time sent to child by parent)
			* Use % of time sent by parent, go to blocked queue
				* Send back time used (positive remainder)
			* Use % of time sent by parent, terminate
				* Send back time NOT used (negative value, equal to timeUsed - totalTime)
	*/

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
	
	//the TTL sent by oss
	int nanoseconds;
	nanoseconds = atoi(argv[1]);	
       	
	pid_t parentPid = getppid();
	pid_t myPid = getpid();

	int msgReceived; //set to 1 when message comes in from parent
	msgReceived = 0;
	while(!msgReceived) {
		if(msgrcv(msqid, &buf, sizeof(msgbuffer), myPid, 0) >= 0)
			msgReceived = 1;
	}

	//Send message back to parent
	buf.mtype = parentPid;
	buf.intData = parentPid;
	buf.msgData = 0;//TODO: fill in return value to parent
	if(msgsnd(msqid, &buf, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
		printf("msgsnd to parent failed.\n");
		exit(1);
	}

	return EXIT_SUCCESS;
}
