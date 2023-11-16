Link to github: https://github.com/jtfox142/OS-3/blob/main/oss.c

This program simulates an operating system's scheduler. It calculates priority for each running process by
dividing the time a process has been able to run by the time it has been in the system, which yields a value
between 0 and 1, inclusive. If a process has a run time of 0, then it is assigned a priority of 1. A process table
is maintained to keep track of data pertaining to each individual process, stored in process control blocks.

The executable takes four arguments. The first designates the total number of child processes to run. The second
dictates the maximum number of children that can run simultaneously. The third establishes the time interval that the
program must wait before attempting to launch another child process. The fourth and final argument designates a file name
that the program will use to store logging information.

Example to run the program: ./oss -n 4 -s 2 -t 5 -f log.txt

I used more global variables than necessary and have not yet put the finishing touches on this project,
but it does the thing.

MADE BY JT FOX
10/31/23

Updated 11/15/23