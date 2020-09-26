/*
Phase 1b
*/

#include "phase1Int.h"
#include "usloss.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

static void checkInKernelMode();
static void add_child(int pid);
static void enqueue(int pid);

// Implementing a circularly linked list for best queue structure
typedef struct Node {
    int          val;
    struct Node* next;
} Node;

typedef struct PCB {
    int             cid;                // context's ID
    int             cpuTime;            // process's running time
    char            name[P1_MAXNAME+1]; // process's name
    int             priority;           // process's priority
    P1_State        state;              // state of the PCB
    int             tag;
    // more fields here
    int             (*func)(void *);
    void            *arg;
    int             parentPid;          // The process ID of the parent
    Node            *childrenPids;      // The children process IDs of the process
    int             numChildren;        // The total number of children
    Node           *quitChildren;      // The children who have quit
    int             numQuit;            // The number of children who have quit
    int             sid;
    
} PCB;

int currentPID = 0;
static PCB processTable[P1_MAXPROC];   // the process table
static Node *readyQueue;               // pointer to last item in circular ready queue

void P1ProcInit(void)
{
    P1ContextInit();
    for (int i = 0; i < P1_MAXPROC; i++) {
        processTable[i].sid = -1;
        processTable[i].cid = 0;
        processTable[i].cpuTime = 0;
        processTable[i].priority = 0;
        processTable[i].state = P1_STATE_FREE;
    }
    // initialize everything else

}

// Halting the program for an illegal message
static void IllegalMessage(int n, void *arg){
    P1_Quit(1024);
}

static void checkInKernelMode() {
    // Checking if we are in kernal mode
    if(!(USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE)){
        USLOSS_IntVec[USLOSS_ILLEGAL_INT] = IllegalMessage;
        USLOSS_IllegalInstruction();
    }
}

static void launch(void *arg) {
    // int pid = (int) *arg;  FIGURE THIS OUT
    int pid = 0;
    int retVal = processTable[pid].func(processTable[pid].arg);
    P1_Quit(retVal);
}

int P1_GetPid(void) 
{
    return readyQueue->val;
}

void reEnableInterrupts(int enabled) {
    if (enabled == TRUE) {
        P1EnableInterrupts();
    }
}

int P1_Fork(char *name, int (*func)(void*), void *arg, int stacksize, int priority, int tag, int *pid ) 
{
    // check for kernel mode
    checkInKernelMode();

    // disable interrupts
    int val = P1DisableInterrupts();

    // check all parameters
    // checking if tag is 0 or 1
    if( tag != 0 && tag != 1){
        reEnableInterrupts(val);
        return P1_INVALID_TAG;
    }

    // checking priority
    if(priority < 1 || priority > 6){
        reEnableInterrupts(val);
        return P1_INVALID_PRIORITY;
    }

    // checking stacksize
    if( stacksize < USLOSS_MIN_STACK){
        reEnableInterrupts(val);
        return P1_INVALID_STACK;
    }

    // checking if name is null
    if(name == NULL){
        reEnableInterrupts(val);
        return P1_NAME_IS_NULL;
    }

    if(sizeof(name) > P1_MAXNAME){
        reEnableInterrupts(val);
        return P1_NAME_TOO_LONG;
    }

    int i;
    for (i=0; i<P1_MAXPROC; i++) {
        // checking if there are duplicates 
        if(processTable[i].state != P1_STATE_FREE && strcmp(name,processTable[i].name) == 0){
            reEnableInterrupts(val);
            return P1_DUPLICATE_NAME;
        }
        // create a context using P1ContextCreate
        // allocate and initialize PCB
        if (processTable[i].state == P1_STATE_FREE) {
            *pid = i;
            int cid;
            int retVal = P1ContextCreate(launch, pid, stacksize, &cid);
            if (retVal != P1_SUCCESS) {
                reEnableInterrupts(val);
                return retVal;
            }
            processTable[i].childrenPids = NULL;
            processTable[i].cid = cid;
            processTable[i].cpuTime = 0;
            strcpy(processTable[i].name,name);
            processTable[i].state = P1_STATE_READY;
            processTable[i].numQuit = 0;
            processTable[i].tag = tag;
            processTable[i].priority = priority;
            processTable[i].parentPid = 0;
            processTable[i].numChildren = 0;
            // Setting the first fork
            if(i == 0 && priority != 6){
                return P1_INVALID_PRIORITY;
            }else if (i != 0){
                add_child(i);
            }
            
            enqueue(i);
            // if this is the first process or this process's priority is higher than the 
            //    currently running process call P1Dispatch(FALSE)
            int oldPriority = processTable[currentPID].priority;
            if(priority < oldPriority){
                currentPID = i;
                // P1Dispatch(FALSE);
            }
            reEnableInterrupts(val);
            return P1_SUCCESS;
        }
    }
    reEnableInterrupts(val);
    return P1_TOO_MANY_PROCESSES;
}

static void enqueue(int pid){
    if(pid == 0){
        readyQueue = (Node*)malloc(sizeof(Node)); 
        readyQueue->val = pid;
        readyQueue->next = readyQueue;
    }else{
        Node *node = (Node*)malloc(sizeof(Node));
        node->val = pid;
        node->next = readyQueue->next;
        readyQueue->next = node;
    }
}

static void add_child(int pid){

    int currentpid = currentPID;
    do {
        Node *new_child =  (Node*)malloc(sizeof(Node)); 
        new_child->val = pid;
        if(processTable[currentpid].childrenPids == NULL){
            new_child->next = new_child;
        } else{
            new_child->next = processTable[currentpid].childrenPids->next;
            processTable[currentpid].childrenPids->next = new_child;
            processTable[currentpid].numChildren ++;
        }
        currentpid = processTable[currentpid].parentPid;
    } while(currentpid != 0);

}

void 
P1_Quit(int status) 
{
    // check for kernel mode
    checkInKernelMode();
    // disable interrupts
    int ret = P1DisableInterrupts();
    // remove from ready queue, set status to P1_STATE_QUIT
    int currentPid = readyQueue->val;
    readyQueue++;
    ret = P1SetState(currentPid, P1_STATE_QUIT, 0);
    if (ret != P1_SUCCESS) {
        USLOSS_Halt(1);
    }

    // if first process verify it doesn't have children, otherwise give children to first process
    if (currentPid == 0 && processTable[currentPid].numChildren > processTable[currentPid].numQuit) {
        USLOSS_Console("First process quitting with children, halting.\n");
        USLOSS_Halt(1);
    }
    if (currentPid > 0) {
        Node* head = processTable[0].childrenPids->next;
        processTable[0].childrenPids->next = processTable[currentPid].childrenPids->next;
        processTable[currentPid].childrenPids->next = head;
        processTable[0].numChildren += processTable[currentPid].numChildren;
        processTable[0].numQuit += processTable[currentPid].numQuit;
    }
    // add ourself to list of our parent's children that have quit
    Node* head = processTable[processTable[currentPid].parentPid].quitChildren->next;
    Node* quitNode = malloc(sizeof(Node));
    quitNode->val = currentPid;
    quitNode->next = head;
    processTable[processTable[currentPid].parentPid].quitChildren->next = quitNode;
    processTable[processTable[currentPid].parentPid].numQuit += 1;
    // if parent is in state P1_STATE_JOINING set its state to P1_STATE_READY
    if (processTable[processTable[currentPid].parentPid].state == P1_STATE_JOINING) {
        ret = P1SetState(processTable[currentPid].parentPid, P1_STATE_READY, 0);
        if (ret != P1_SUCCESS) {
            USLOSS_Halt(1);
        }
    }
    P1Dispatch(FALSE);
    // should never get here
    assert(0);
}


int 
P1GetChildStatus(int tag, int *pid, int *status) 
{
    int result = P1_SUCCESS;
    // if (pid < 0 || P1_MAXPROC <= pid || processTable[pid].state == P1_STATE_FREE) {
    //     return P1_INVALID_PID;
    // }
    
    return result;
}

int
P1SetState(int pid, P1_State state, int sid) 
{
    if (pid < 0 || P1_MAXPROC <= pid || processTable[pid].state == P1_STATE_FREE) {
        return P1_INVALID_PID;
    }
    if (state != P1_STATE_READY && state != P1_STATE_JOINING && state != P1_STATE_BLOCKED
        && state != P1_STATE_QUIT) {
            return P1_INVALID_STATE;
        }
    if (state == P1_STATE_READY) {
        // adding to the ready queue
        Node *head = readyQueue->next;
        Node *readyNode = malloc(sizeof(Node));
        readyNode->val = pid;
        readyNode->next = head;
        readyQueue->next = readyNode;
    }
    processTable[pid].state = state;
    return P1_SUCCESS;
}

void
P1Dispatch(int rotate)
{
    int i;
    // int cid=-1;
    int maxPriority = 7;
    for (i=0; i<P1_MAXPROC; i++) {
        if (processTable[i].priority < maxPriority) {

        }
    }

    // select the highest-priority runnable process
    // call P1ContextSwitch to switch to that process
}

int
P1_GetProcInfo(int pid, P1_ProcInfo *info)
{
    int         result = P1_SUCCESS;
    if (pid < 0 || P1_MAXPROC <= pid || processTable[pid].state == P1_STATE_FREE) {
        return P1_INVALID_PID;
    }
    // strcmp(info->name,processTable[pid].name);
    info->sid = processTable[pid].sid;
    info->state = processTable[pid].state;
    info->priority = processTable[pid].priority;
    info->tag = processTable[pid].tag;
    info->cpu = processTable[pid].cpuTime;
    info->parent = processTable[pid].parentPid;
    info->numChildren = processTable[pid].numChildren;
    int i;
    Node *head = processTable[pid].childrenPids;
    for(i = 0; i < processTable[pid].numChildren; i++){
        info->children[i] = head->val;
        head = head->next;
    }
    return result;
}
