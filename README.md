Link to github: https://github.com/jtfox142/OS-3/blob/main/oss.c

The bad: Sometimes experiences overflow of the ready queue and exits prematurely. It does not kill off child processes or message queues when this happens. Does not launch more than one child because I did not have time to correctly implement the -t flag. 

The good: Not much. The message queue works better than in my project 3, but that's not saying much. When it doesn't end right away, which is about half the time, it outputs the table correctly to the screen. The code is more organized and slightly more readable than in project 3.

Example to run the program: ./oss -n 1 -s 1 -t 5 -f log.txt

Happy Halloween

MADE BY JT FOX
10/31/23