#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "phase1.h"
#include <string.h>

struct PCB
{
    int pid;
    char name[MAXNAME];
    int status;
    int priority;
    int stackSize;
    // 0 = running, 1 = ready/runnable, 2 = blocked in join,
    // 3 = blocked in zap, 4 = blocked in testcase
    // 5 = terminated
    int runStatus;
    USLOSS_Context context;
    char *stack;
    bool isDead;

    int (*funcPtr)(void *);
    void *arg;
    int retVal;

    struct PCB *nextRunQueue;
    struct PCB *prevRunQueue;

    struct PCB *parent;
    struct PCB *newestChild;
    struct PCB *prevSibling;
    struct PCB *nextSibling;
};

/* ----------------------- Global variables ---------------------- */
struct PCB runQueue;
struct PCB *currProc = NULL;
struct PCB procTable[MAXPROC];
char initStack[USLOSS_MIN_STACK];
int nextPid = 1;
int lastPid = -1;
int filledSlots = 0;
unsigned int gOldPsr;
int switchTime = -81;

// run queues, p1Head means priority 1 head pointer
struct PCB *p1Head;
struct PCB *p1Tail;
struct PCB *p2Head;
struct PCB *p2Tail;
struct PCB *p3Head;
struct PCB *p3Tail;
struct PCB *p4Head;
struct PCB *p4Tail;
struct PCB *p5Head;
struct PCB *p5Tail;
struct PCB *p6Head;
struct PCB *p6Tail;

/* --------------------- Function prototypes --------------------- */
void getNextPid(void);
unsigned int disableInterrupts(void);
void restoreInterrupts(unsigned int);
int init(void *);
int testcaseMainWrapper(void *args);
void sporkTrampoline(void);
void enforceKernelMode();
void addChild(struct PCB *parent, struct PCB *child);
void addToQueue(struct PCB *proc);
void removeFromQueue(void);
void rotateQueue(void);
void TEMP_switchTo(int pid);

/* --------------------- phase 1b functions --------------------- */

void quit(int status)
{
    if (currProc->newestChild != NULL)
    {
        USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", currProc->pid);
        USLOSS_Halt(1);
    }
    enforceKernelMode(2);

    currProc->status = status;
    currProc->isDead = true;
    currProc->runStatus = 5;

    if (currProc->parent->runStatus == 2)
        unblockProc(currProc->parent->pid);

    removeFromQueue();

    USLOSS_Console("fffee: %s %d\n", currProc->name, currProc->priority);
    USLOSS_Console("ff22: %d %d\n", p2Head == NULL, p2Tail == NULL);

    USLOSS_Console("\n");

    dispatcher();
}

void zap(int pid)
{
}

/*
 * Since there are no parameters, the reason for blocking gets set
 * before calling this function by updating the runStatus field.
 */
void blockMe(void)
{
    unsigned int oldPsr = disableInterrupts();
    enforceKernelMode(3);
    if (currProc->runStatus == 0)
    { // this means blockMe was called by testcase, not another kernel function
        currProc->runStatus = 4;
    }
    removeFromQueue();
    restoreInterrupts(oldPsr);
    dispatcher();
}

int unblockProc(int pid)
{
    unsigned int oldPsr = disableInterrupts();
    enforceKernelMode(4);
    struct PCB *proc = &procTable[pid % MAXPROC];
    int runStatus = proc->runStatus;
    // check if proc is not blocked or doesn't exist
    if (proc->pid != pid) // the proc does not exist
        return -2;
    else if ((runStatus == 0) || (runStatus == 1) || (runStatus == 5)) // proc is not blocked
        return -2;

    // add to run queue
    proc->runStatus = 1;
    addToQueue(proc);

    dispatcher();
    restoreInterrupts(oldPsr);

    return proc->pid;
}

void dispatcher(void)
{
    unsigned int oldPsr = disableInterrupts();
    gOldPsr = oldPsr; // keep track of old psr in global variable

    struct PCB *oldProc = currProc;
    struct PCB *switchTo = NULL;

    bool doRotate = true;

    if (p1Head != NULL)
        switchTo = p1Head;
    else if (p2Head != NULL)
        switchTo = p2Head;
    else if (p3Head != NULL)
        switchTo = p3Head;
    else if (p4Head != NULL)
        switchTo = p4Head;
    else if (p5Head != NULL)
        switchTo = p5Head;
    else
    {
        doRotate = false;
        switchTo = p6Head;
    }

    if (currProc != NULL)
    {

        USLOSS_Console("ff: %s %d %d\n", switchTo->name, switchTo->runStatus, switchTo->priority);
        USLOSS_Console("FE: %s\n", currProc->name);
    }

    if ((oldProc != NULL && oldProc->pid == switchTo->pid) || (currProc != NULL && currProc->runStatus != 5 && switchTo->priority >= oldProc->priority && currentTime() < switchTime + 80))
        return;

    USLOSS_Console("eoijf\n");

    if (doRotate)
        rotateQueue();

    currProc = switchTo;
    switchTime = currentTime();

    // when call dispatcher for first time, don't save state
    if (oldProc == NULL)
        USLOSS_ContextSwitch(NULL, &switchTo->context);
    else
        USLOSS_ContextSwitch(&oldProc->context, &switchTo->context);

    restoreInterrupts(oldPsr);
}

/* --------------------- phase 1a functions updated in phase 1b --------------------- */
void phase1_init(void)
{
    unsigned int oldPsr = disableInterrupts();

    memset(procTable, 0, sizeof(procTable));

    getNextPid();
    int index = nextPid % MAXPROC;
    struct PCB *initProc = &procTable[index];

    initProc->pid = nextPid;
    strcpy(initProc->name, "init");
    initProc->priority = 6;
    initProc->stackSize = USLOSS_MIN_STACK;
    initProc->runStatus = 1;
    initProc->funcPtr = &init;
    initProc->stack = initStack;
    initProc->isDead = false;
    initProc->arg = NULL;
    initProc->nextRunQueue = NULL;
    initProc->prevRunQueue = NULL;

    USLOSS_ContextInit(&(procTable[index].context), initProc->stack, USLOSS_MIN_STACK, NULL, &sporkTrampoline);
    filledSlots++;

    p6Head = initProc;
    p6Tail = initProc;

    // currProc = initProc;
    restoreInterrupts(oldPsr);
}

int spork(char *name, int (*func)(void *), void *arg, int stacksize, int priority)
{
    enforceKernelMode(1);

    // disable interrupts for new process creation
    unsigned int oldPsr = disableInterrupts();

    // check if procTable is full
    if (filledSlots == 50)
    {
        return -1;
    }
    getNextPid();

    int pid = nextPid;
    int slot = pid % MAXPROC;

    USLOSS_Console("foeij: %d %s \n", pid, name);

    if (procTable[slot].pid != 0 || strlen(name) > MAXNAME || priority < 1 || priority > 5)
        return -1;
    if (stacksize < USLOSS_MIN_STACK)
        return -2;

    struct PCB *newProc = &procTable[slot];

    strcpy(newProc->name, name);
    newProc->pid = pid;
    newProc->priority = priority;
    newProc->stackSize = stacksize;
    newProc->stack = malloc(stacksize);
    newProc->runStatus = 1;
    newProc->isDead = false;
    newProc->funcPtr = func;
    newProc->arg = arg;
    newProc->nextRunQueue = NULL;
    newProc->prevRunQueue = NULL;

    addChild(currProc, newProc);

    USLOSS_ContextInit(&newProc->context, newProc->stack, stacksize, NULL, &sporkTrampoline);

    // add to run queue, set run queue pointers
    addToQueue(newProc);

    filledSlots++;
    restoreInterrupts(oldPsr);

    dispatcher();

    return pid;
}

int join(int *status)
{
    unsigned int oldPsr = disableInterrupts();

    if (status == NULL)
        return -3;

    // iterate through children, looking for a dead one
    struct PCB *next = currProc->newestChild;
    if (next == NULL)
        return -2;

    int index, pid;
    while (next != NULL)
    {
        if (next->isDead)
        {
            *status = next->status;
            pid = next->pid;
            index = pid % MAXPROC;
            // next is an only child
            if ((next == currProc->newestChild) && (next->nextSibling == NULL))
                currProc->newestChild = NULL;

            // next has next siblings
            else if (next->nextSibling != NULL)
            {
                // next is newestChild
                if (next == currProc->newestChild)
                {
                    currProc->newestChild = next->nextSibling;
                    (next->nextSibling)->prevSibling = NULL;
                }
                // next is a middle child
                else
                {
                    (next->prevSibling)->nextSibling = next->nextSibling;
                    (next->nextSibling)->prevSibling = next->prevSibling;
                }
            }
            // next does not have next siblings
            else
                (next->prevSibling)->nextSibling = NULL;

            free(next->stack);
            memset(&procTable[index], 0, sizeof(struct PCB));
            filledSlots--;
            return pid;
        }
        else
            next = next->nextSibling;
    }
    // after while loop, means there are no dead children, so block
    currProc->runStatus = 2;
    restoreInterrupts(oldPsr);
    blockMe();
    return 0;
}

/* --------------------- phase 1a functions --------------------- */
int init(void *)
{
    unsigned int oldPsr = disableInterrupts();

    // start services
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();
    phase5_mmu_pageTable_alloc(currProc->pid);
    phase5_mmu_pageTable_free(currProc->pid, NULL);

    // create testcase_main proc
    int pid = spork("testcase_main", &testcaseMainWrapper, NULL, USLOSS_MIN_STACK, 3);
    currProc->newestChild = &procTable[pid % MAXPROC];

    dispatcher();

    // call join to clean up procTable
    int deadPid = 1;
    int status, index;
    while (deadPid > 0)
    {
        deadPid = join(&status);
        index = deadPid % MAXPROC;
        memset(&procTable[index], 0, sizeof(struct PCB));
    }

    if (deadPid == -2)
    {
        USLOSS_Console("ERROR: init has no more children, terminating simulation\n");
        USLOSS_Halt(1);
        return 0;
    }
    else if (deadPid == -3)
    {
        USLOSS_Console("ERROR: status pointer is null");
    }

    restoreInterrupts(oldPsr);

    return 0;
}
/*
void TEMP_switchTo(int pid)
{
    unsigned int oldPsr = disableInterrupts();
    gOldPsr = oldPsr; // keep track of old psr in global variable

    struct PCB *oldProc = currProc;
    struct PCB *switchTo = &procTable[pid % MAXPROC];
    currProc = switchTo;

    if (currProc->pid == 1)
        USLOSS_ContextSwitch(NULL, &switchTo->context);
    else
        USLOSS_ContextSwitch(&oldProc->context, &switchTo->context);

    restoreInterrupts(oldPsr);
}
*/

void quit_phase_1a(int status, int switchToPid)
{
    if (currProc->newestChild != NULL)
    {
        USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", currProc->pid);
        USLOSS_Halt(1);
    }
    enforceKernelMode(2);

    currProc->status = status;
    currProc->isDead = true;

    TEMP_switchTo(currProc->parent->pid);

    while (true)
        ;
}

void dumpProcesses(void)
{
    unsigned int oldPsr = disableInterrupts();

    printf("PID  PPID  NAME              PRIORITY  STATE\n");

    char state[16];
    int pid, ppid, priority, status;
    char name[MAXNAME];

    for (int i = 0; i < MAXPROC; i++)
    {
        struct PCB *proc = &procTable[i];
        pid = proc->pid;
        if (pid != 0)
        {
            status = proc->status;
            if (proc->pid == currProc->pid)
            {
                snprintf(state, sizeof(state), "Running");
            }
            else if (status == 0)
            {
                snprintf(state, sizeof(state), "Runnable");
            }
            else
            {
                snprintf(state, sizeof(state), "Terminated(%d)", status);
            }
            if (pid == 1)
            {
                ppid = 0;
            }
            else
            {
                ppid = (proc->parent)->pid;
            }
            strcpy(name, proc->name);
            priority = proc->priority;
            printf("%4d  %4d  %-17s %-9d %s\n", pid, ppid, name, priority, state);
        }
    }
    restoreInterrupts(oldPsr);
}

/* ------------ Helper functions, not defined in spec ------------ */
/*
 * Removes the current process from its run queue, and updates the nextRunQueue and prevRunQueue fields. Only processes that were running get removed, so always remove the head of the run queue.
 */
void rotateQueue()
{
    removeFromQueue();
    addToQueue(currProc);
}

void removeFromQueue()
{
    int priority = currProc->priority;
    struct PCB *newHead = currProc->nextRunQueue;

    if (priority == 1)
    {
        p1Head = newHead;
        if (newHead == NULL)
            p1Tail = NULL;
    }
    else if (priority == 2)
    {
        p2Head = newHead;
        if (newHead == NULL)
            p2Tail = NULL;
    }
    else if (priority == 3)
    {
        p3Head = newHead;
        if (newHead == NULL)
            p3Tail = NULL;
    }
    else if (priority == 4)
    {
        p4Head = newHead;
        if (newHead == NULL)
            p4Tail = NULL;
    }
    else
    {
        p5Head = newHead;
        if (newHead == NULL)
            p5Tail = NULL;
    }

    if (newHead != NULL)
    {
        newHead->prevRunQueue = NULL;
        currProc->nextRunQueue = NULL;
    }
}

/*
 * Adds the proc to the appropriate run queue, and sets the
 * nextRunQueue and prevRunQueue fields. All processes get added to the end of the run queue.
 */
void addToQueue(struct PCB *proc)
{
    int priority = proc->priority;
    struct PCB *oldTail;
    if (priority == 1)
    {
        oldTail = p1Tail;
        p1Tail = proc;
        if (oldTail == NULL)
            p1Head = proc;
    }
    else if (priority == 2)
    {
        oldTail = p2Tail;
        p2Tail = proc;
        if (oldTail == NULL)
            p2Head = proc;
    }
    else if (priority == 3)
    {
        oldTail = p3Tail;
        p3Tail = proc;
        if (oldTail == NULL)
            p3Head = proc;
    }
    else if (priority == 4)
    {
        oldTail = p4Tail;
        p4Tail = proc;
        if (oldTail == NULL)
            p4Head = proc;
    }
    else
    {
        oldTail = p5Tail;
        p5Tail = proc;
        if (oldTail == NULL)
            p5Head = proc;
    }

    if (oldTail != NULL)
        oldTail->nextRunQueue = proc;
    proc->prevRunQueue = oldTail;
    proc->nextRunQueue = NULL;
}

/*
 * Checks the nextPid to see if it maps to a position in the
 * procTable that is alredy filled. It keeps checking for a blank
 * spot in the procTable and updates the global variable.
 */
int getpid()
{
    return currProc->pid;
}

void sporkTrampoline()
{
    restoreInterrupts(gOldPsr);
    currProc->retVal = currProc->funcPtr(currProc->arg);
}

// add child process to parent
void addChild(struct PCB *parent, struct PCB *child)
{
    if (parent->newestChild != NULL)
    {
        child->nextSibling = parent->newestChild;
        child->nextSibling->prevSibling = child;
    }
    child->parent = parent;
    parent->newestChild = child;
}

void getNextPid(void)
{
    // check if nextPid already in use
    while (procTable[nextPid % MAXPROC].pid != 0 || nextPid <= lastPid)
    {
        nextPid++;
    }
    lastPid = nextPid;
}

/*
 * Checks to make sure that kernel code does not get called while in
 * user mode. Takes an integer parameter to identify which
 * kernel-mode function the process tried to call.
 * 1 = spork
 * 2 = quit_phase_1a
 * 3 = blockMe
 * 4 = unblockProc
 */
void enforceKernelMode(int i)
{
    if (!(USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()))
    {
        if (i == 1)
            USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        else if (i == 2)
            USLOSS_Console("ERROR: Someone attempted to call quit_phase_1a while in user mode!\n");
        else if (i == 3)
            USLOSS_Console("ERROR: Someone attempted to call blockMe while in user mode!\n");
        else if (i == 4)
            USLOSS_Console("ERROR: Someone attempted to call unblockProc while in user mode!\n");

        USLOSS_Halt(1);
    }
}

/*
 * Disables interrupts and return the old PSR value, so that interrupts
 * can be restored in the future.
 */
unsigned int disableInterrupts(void)
{
    unsigned int oldPsr = USLOSS_PsrGet();
    USLOSS_PsrSet(oldPsr & ~USLOSS_PSR_CURRENT_INT);

    return oldPsr;
}

/*
 * Restores interrupts to the value they were before they got disabled.
 */
void restoreInterrupts(unsigned int oldPsr)
{
    USLOSS_PsrSet(oldPsr | USLOSS_PSR_CURRENT_INT);
}

/*
 * Creates a wrapper around testcase_main so that spork can be called. Gets
 * called by init().
 */
int testcaseMainWrapper(void *args)
{
    testcase_main();
    USLOSS_Halt(0);
    return 0;
}