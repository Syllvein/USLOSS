/*************************************************************************
 * 
 *    Tests arm selection alg.  Requires usloss logging.
 * 
 *    FCFS: 6, 15, 3, 4, 7, 11, 2
 *    SSF:  6, 7, 4, 3, 2, 11, 15
 *    ELEVATOR: 6, 7, 11, 15, 4, 3, 2
 * 
*************************************************************************/
#include <stdio.h>
#include <string.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <libuser.h>
#include <assert.h>
#include <time.h>
#include <stdlib.h>



static char XXbuf[4][512];
int ubiq0(int);
int ubiq1(int);

int k1(char *arg)
{
    ubiq1(1);
    Terminate(2);
    return 0;
}

int k2(char *arg)
{
    ubiq1(2);
    Terminate(3);
    return 0;
}

int k3(char *arg)
{
    ubiq1(3);
    Terminate(4);
    return 0;
}

int k4(char *arg)
{
    ubiq1(4);
    Terminate(5);
    return 0;
}

int k5(char *arg)
{
    ubiq1(5);
    Terminate(6);
    return 0;
}

int k6(char *arg)
{
    ubiq1(6);
    Terminate(7);
    return 0;
}

int k7(char *arg)
{
    ubiq1(7);
    Terminate(7);
    return 0;
}
int k8(char *arg)
{
    ubiq1(8);
    Terminate(7);
    return 0;
}
int k9(char *arg)
{
    ubiq1(9);
    Terminate(7);
    return 0;
}
int k10(char *arg)
{
    ubiq1(10);
    Terminate(7);
    return 0;
}
int k11(char *arg)
{
    ubiq1(11);
    Terminate(7);
    return 0;
}
int k12(char *arg)
{
    ubiq1(12);
    Terminate(7);
    return 0;
}
int k13(char *arg)
{
    ubiq1(13);
    Terminate(7);
    return 0;
}
int k14(char *arg)
{
    ubiq1(14);
    Terminate(7);
    return 0;
}
int k15(char *arg)
{
    ubiq1(15);
    Terminate(7);
    return 0;
}
int cksum=0;

int ubiq0(int t)
{
    int status = -1;
    char buf[50];
    int z = t % 4;
    console("ubiq0: going to write to track %d\n",t);
    if (DiskWrite(XXbuf[z], 0, t, 4, 1, &status) < 0) {
	printf("ERROR: DiskPut\n");
    } 
    
    if (status != 0) { 
	sprintf(buf,"disk_put returned error   %d\n",t);
	printf(buf);
    }  
    else {
            console("ubiq0: after writing to track %d\n",t);
	    cksum+=t;
    }
    return 0;
}


int RandSizeAndTrack(int t)
{
    int status = -1;
    char buf[512*DISK_TRACK_SIZE];
    int curTime = clock();
    int track;
    int sector;
    int count;

    srand(curTime);

    track = rand() % 16;
    sector = rand() % DISK_TRACK_SIZE;
    count = rand() % DISK_TRACK_SIZE;
    console("RandSizeAndTrack: going to read %d sectors to track %d, sector %d\n",count, track, sector);
    if (DiskWrite(buf, 0, track, sector, count, &status) < 0) 
    {
	    printf("ERROR: DiskPut\n");
    } 
    
    if (status != 0) 
    { 
        sprintf(buf,"disk_put returned error   %d\n",t);
        printf(buf);
    }  
    else 
    {
        console("ubiq0: after writing to track %d\n",t);
	    cksum+=t;
    }

    return 0;
}

int Child1(char *arg)
{
   int status, pid;
   
   printf("\nChild1(): starting tests\n");
   printf("start4(): disk scheduling test, create 6 processes that write\n");
   printf("          3 to disk0         \n");
   printf("          3 to disk1         \n");

    strcpy(XXbuf[0],"One flew East\n");
    strcpy(XXbuf[1],"One flew West\n");
    strcpy(XXbuf[2],"One flew over the coo-coo's nest\n");
    strcpy(XXbuf[3],"--did it work?\n");

    // this should place the arm at 6, note the lower priotity should force to start
    Spawn("k6", k6, NULL, USLOSS_MIN_STACK, 3, &pid);  
    Wait(&pid, &status);

    Spawn("k15", k15, NULL, USLOSS_MIN_STACK, 1, &pid);  
    Spawn("k3", k3, NULL, USLOSS_MIN_STACK, 1, &pid);    
    Spawn("k4", k4, NULL, USLOSS_MIN_STACK, 1, &pid);    
    Spawn("k7", k7, NULL, USLOSS_MIN_STACK, 1, &pid);  
    Spawn("k11", k11, NULL, USLOSS_MIN_STACK, 1, &pid);    
    Spawn("k2", k2, NULL, USLOSS_MIN_STACK, 1, &pid);    

    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
/*
    Spawn("k3", k3, NULL, USLOSS_MIN_STACK, 1, &pid);
    Spawn("k2", k2, NULL, USLOSS_MIN_STACK, 1, &pid);
    Spawn("k4", k4, NULL, USLOSS_MIN_STACK, 1, &pid);
    Spawn("k1", k1, NULL, USLOSS_MIN_STACK, 1, &pid);
    Spawn("k6", k6, NULL, USLOSS_MIN_STACK, 1, &pid);
    Spawn("k5", k5, NULL, USLOSS_MIN_STACK, 1, &pid);

    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
    Wait(&pid, &status);
    printf("process %d quit with status %d\n", pid, status);
*/
    printf("start4(): done %d\n",cksum);
    Terminate(2);
   return 0;
} /* ChildDR0 */

int ubiq1(int t)
{
    int status = -1;
    char buf[50];
    int z = t % 4;

    console("ubiq1: going to write to track %d\n",t);
    if (DiskWrite(XXbuf[z], 1, t, 4, 1, &status) < 0) {
	printf("ERROR: DiskPut\n");
    } 
    
    if (status != 0) { 
	sprintf(buf,"disk_put returned error   %d\n",t);
	printf(buf);
    }  
    else {
            console("ubiq1: after writing to track %d\n",t);
	    cksum+=t;
    }
    return 0;
}

int start4(char *arg)
{
    int status, pid;

    Spawn("Child1", Child1, NULL, USLOSS_MIN_STACK, 1, &pid);
    Wait(&pid, &status);

    printf("Child1 quit with status %d\n", pid, status);

//    Spawn("Child1", Child1, NULL, USLOSS_MIN_STACK, 1, &pid);
//    Wait(&pid, &status);

    
    return 0;
} /* start4 */
