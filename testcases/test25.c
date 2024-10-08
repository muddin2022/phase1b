/* Tests blockMe, unblockProc, and zap
 * testcase_main creates XXp1 at priority 5
 * XXp1 creates:
 *    3 instances of XXp2 at prority 2
 *    XXp3 at priority 3
 *    XXp4 at priority 4
 * Each XXp2 instance calls blockMe()
 * XXp3 zap's pid 5
 * XXp4 zap's pid 7
 * at this point, only XXp1 can run
 * XXp1 calls unblockProc for each instance of XXp2
 * Each instance of XXp2 calls isZapped.
 * Each instance of XXp2 quit's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <usloss.h>
#include <phase1.h>

int XXp1(void *);
int XXp2(void *);
int XXp3(void *);
int XXp4(void *);

int zapTarget1, zapTarget2;

int testcase_main()
{
    int pid1, kid_status;

    USLOSS_Console("testcase_main(): started\n");
// TODO    USLOSS_Console("EXPECTATION: TBD\n");
    USLOSS_Console("QUICK SUMMARY: TODO\n");

    pid1 = spork("XXp1", XXp1, "XXp1", USLOSS_MIN_STACK, 3);
    pid1 = join(&kid_status);
    USLOSS_Console("testcase_main: XXp1, pid = %d, done; testcase_main returning...\n", pid1);

    return 0;
}

int XXp1(void *arg)
{
    int  i, pid[10], result[10];

    USLOSS_Console("XXp1(): creating children\n");
    for (i = 0; i < 3; i++) {
        pid[i] = spork("XXp2", XXp2, NULL, USLOSS_MIN_STACK, 1);
    }

    zapTarget1 = pid[0];
    zapTarget2 = pid[2];

    USLOSS_Console("XXp1(): creating zapper children\n");
    pid[i] = spork("XXp3", XXp3, "XXp3", USLOSS_MIN_STACK, 2);
    pid[i] = spork("XXp4", XXp4, "XXp4", USLOSS_MIN_STACK, 2);

    // dumpProcesses();

    USLOSS_Console("XXp1(): unblocking children\n");
    for (i = 0; i < 3; i++)
        result[i] = unblockProc(pid[i]);

    for (i = 0; i < 3; i++)
        USLOSS_Console("XXp1(): after unblocking %d, result = %d\n", pid[i], result[i]);

    quit(0);
}

int XXp2(void *arg)
{
    USLOSS_Console("XXp2(): started, pid = %d, calling blockMe\n", getpid());
    blockMe();
    USLOSS_Console("XXp2(): pid = %d, after blockMe\n", getpid());

    USLOSS_Console("XXp2(): pid = %d\n", getpid());

    quit(0);
}

int XXp3(void *arg)
{
    USLOSS_Console("XXp3(): started, pid = %d, calling zap on pid %d\n", getpid(), zapTarget1);
    zap(zapTarget1);
    USLOSS_Console("XXp3(): after call to zap\n");

    return 0;
}

int XXp4(void *arg)
{
    USLOSS_Console("XXp4(): started, pid = %d, calling zap on pid %d\n", getpid(), zapTarget2);
    zap(zapTarget2);
    USLOSS_Console("XXp4(): after call to zap\n");

    return 0;
}

