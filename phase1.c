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
    USLOSS_Context context;
    char *stack;
    bool isDead;

    int (*funcPtr)(void *);
    void *arg;
    int retVal;

    struct PCB *parent;
    struct PCB *newestChild;
    struct PCB *prevSibling;
    struct PCB *nextSibling;
};

/* ----------------------- Global variables ---------------------- */
struct PCB *currProc;
struct PCB procTable[MAXPROC];
char initStack[USLOSS_MIN_STACK];
int nextPid = 1;
int lastPid = -1;
int filledSlots = 0;
unsigned int gOldPsr;

/* --------------------- Function prototypes --------------------- */
void getNextPid(void);
unsigned int disableInterrupts(void);
void restoreInterrupts(unsigned int);
int init(void *);
int testcaseMainWrapper(void *args);
void sporkTrampoline(void);
void enforceKernelMode();
void addChild(struct PCB *parent, struct PCB *child);

/* --------------------- phase 1b functions --------------------- */

void quit(int status)
{
}

void zap(int pid)
{
}

void blockMe(void)
{
}

int unblockProc(int pid)
{
}

void dispatcher(void)
{
}

int currentTime(void)
{
}

/* --------------------- phase 1a functions --------------------- */
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
    initProc->funcPtr = &init;
    initProc->stack = initStack;
    initProc->isDead = false;

    initProc->arg = NULL;

    USLOSS_ContextInit(&(procTable[index].context), initProc->stack, USLOSS_MIN_STACK, NULL, &sporkTrampoline);
    filledSlots++;

    currProc = initProc;
    restoreInterrupts(oldPsr);
}

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
    USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to testcase_main() after using spork() to create it.\n");
    int pid = spork("testcase_main", &testcaseMainWrapper, NULL, USLOSS_MIN_STACK, 3);
    currProc->newestChild = &procTable[pid % MAXPROC];

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

    if (procTable[slot].pid != 0 || strlen(name) > MAXNAME || priority < 1 || priority > 5)
        return -1;
    if (stacksize < USLOSS_MIN_STACK)
        return -2;

    struct PCB *newProc = &procTable[slot];

    strcpy(newProc->name, name);
    newProc->pid = pid;
    newProc->priority = priority;
    newProc->stack = malloc(stacksize);
    newProc->isDead = false;
    newProc->funcPtr = func;
    newProc->arg = arg;

    addChild(currProc, newProc);

    USLOSS_ContextInit(&newProc->context, newProc->stack, stacksize, NULL, &sporkTrampoline);

    filledSlots++;
    restoreInterrupts(oldPsr);

    return pid;
}

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

int join(int *status)
{
    unsigned int oldPsr = disableInterrupts();

    if (status == NULL)
    {
        return -3;
    }

    // iterate through children, looking for a dead one
    struct PCB *next = currProc->newestChild;
    if (next == NULL)
    {
        return -2;
    }

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
            {
                currProc->newestChild = NULL;
            }
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
            {
                (next->prevSibling)->nextSibling = NULL;
            }

            free(next->stack);
            memset(&procTable[index], 0, sizeof(struct PCB));
            filledSlots--;
            return pid;
        }
        else
        {
            next = next->nextSibling;
        }
    }
    restoreInterrupts(oldPsr);
    return 0;
}

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
 */
void enforceKernelMode(int i)
{
    if (!(USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()))
    {
        if (i == 1)
            USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        else if (i == 2)
            USLOSS_Console("ERROR: Someone attempted to call quit_phase_1a while in user mode!\n");

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
    USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
    USLOSS_Halt(0);
    return 0;
}