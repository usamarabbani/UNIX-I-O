/*
 ============================================================================
 Name        : hw6pipes.c
 Author      : CS 149 Group 6
 Version     :
 Copyright   : Student
 Description : HW6, which involves the use of fork and pipes to allow child
 processes to communicate with the parent process.
 ============================================================================
 */
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/select.h>
#include <math.h>
#include <sys/wait.h>
#include <string.h>
#include <pthread.h>

#define NUM_PLAIN_PROCS 4
#define MAX_SLEEP 2
#define STR_LIM 511

/**
 * This structure simply contains some parameters needed to print to a pipe
 */
typedef struct
{
    char* str;//critical part of the string to be printed
    int messageCount;//The current message number
    FILE* pipeFile;//The FILE pointer to the pipe
} w_struct;

const double MAXRUNTIME = 30;
int childIndex = -1;
int pipeArr[NUM_PLAIN_PROCS + 1][2]; //the last pipe is for the keyboard process
struct timeval start;
char* fileName = "output.txt";

/**
 * Returns the elapsed time since start was initialized by
 * the gettimeofday function. This has unpredictable results if start
 * wasn't initialized.
 * @return the number of seconds since the start time
 */
double getElapsedSeconds()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    double seconds = (double) now.tv_sec - start.tv_sec;
    double useconds = (double) now.tv_usec - start.tv_usec;
    double result = seconds + (useconds / 1000000);
    return round(result * 1000.0) / 1000.0; //This rounds to the nearest thousandth
}

/**
 * Initializes the pipe array. Remember to close them all at the end.
 */
void initPipes()
{
    int i;
    for (i = 0; i < NUM_PLAIN_PROCS + 1; i++)
    {
        if (pipe(pipeArr[i]))
        {
            fprintf(stderr, "Pipe failed.\n");
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * Splits the current elapsed time into minutes and remaining seconds
 * @param the minute value to be filled
 * @param the second value to be filled
 */
void currentMinSec(int* minVal, double* secVal)
{
    double timeElapsed = getElapsedSeconds();
    int intSec = floor(timeElapsed);
    *secVal = timeElapsed - intSec;
    *minVal = intSec / 60;
    intSec %= 60;
    *secVal += intSec;
}

/**
 * A function pointer that uses a thread to write to a pipe,
 * independently of the creating thread's call to sleep.
 */
void* writeThreadFun(void* param)
{
    //First, we must extract all values from the parameter data struct
    w_struct* wData = (w_struct*) param;
    char* str = wData->str;
    int messageCount = wData->messageCount;
    FILE* pipeFile = wData->pipeFile;
    int minVal;
    double secVal;
    currentMinSec(&minVal, &secVal);

    if (messageCount > 0)//Include messageCount if this value is important to the process
    {
        fprintf(pipeFile, "%d:%06.3f: Child %d %s %d\n", minVal, secVal,
                childIndex + 1, str, messageCount);
    }
    else
    {
        fprintf(pipeFile, "%d:%06.3f: Child %d :: %s\n", minVal, secVal,
                childIndex + 1, str);
    }
    fflush(pipeFile);
    pthread_exit(NULL);
}

/**
 * Writes to the pipe by creating another thread and immediately joining it
 * A wrapper for writeThreadFun
 * @param str the message to be sent to the pipe
 * @param messageCount the number of messages thus far. If this is negative, then it isn't included in the message
 * @param pipeFile the pipe to be written to
 */
void writeToPipe(char* str, int messageCount, FILE* pipeFile)
{
    //All parameters are placed into a struct to pass into the thread
    w_struct writeData = { str, messageCount, pipeFile };
    pthread_t writeThr;
    pthread_create(&writeThr, NULL, writeThreadFun, (void*) &writeData);
    //This isn't the intended use of threads, since there is barely any parallelism
    //But it ensures sleep doesn't interfere with printing operations
    pthread_join(writeThr, NULL);
}

/**
 * The function run by a plain child that writes a simple message to a pipe for
 * MAXRUNTIME seconds.
 * This can terminate the whole process
 * without returning to main.
 */
void plainChildFun()
{
    //Using FILE is less buggy compared to the bare file descriptor
    FILE * pipeFile = fdopen(pipeArr[childIndex][1], "w");
    if (pipeFile == NULL)
    {
        fprintf(stderr, "Error in opening pipe file.\n");
        exit(EXIT_FAILURE);
    }
    /*
    This ensures all processes have a different random seed.
    start was initialized in the parent process and is the same for all children
    but childIndex is different for all children
     */
    srand((unsigned) start.tv_sec + childIndex);
    int messageCount = 0;
    while (getElapsedSeconds() < MAXRUNTIME)
    {
        messageCount++;
        writeToPipe("message", messageCount, pipeFile);
        sleep(rand() % (MAX_SLEEP + 1));
    }
    fclose(pipeFile);
    exit(EXIT_SUCCESS);
}

/**
 * Reads keyboard input and writes it the the given pipe file
 * @param pipeFile the pipe to be written to
 */
void promptForInput(FILE* pipeFile)
{
    char msg[STR_LIM / 2];
    printf(
            "You have about %.3f seconds left. Please enter a custom message before time runs out.\n",
            MAXRUNTIME - getElapsedSeconds());
    fflush(stdout);
    fgets(msg, STR_LIM / 2, stdin);
    if (getElapsedSeconds() < MAXRUNTIME) writeToPipe(msg, -1, pipeFile);
    else printf("It's too late for any more keyboard input.\n");
}

/**
 * The function run by the keyboard child, which takes keyboard input
 * and formats it into a message to send to a pipe. This process terminates in
 * MAXRUNTIME seconds.
 */
void keyboardChildFun()
{
    FILE * pipeFile = fdopen(pipeArr[childIndex][1], "w");
    if (pipeFile == NULL)
    {
        fprintf(stderr, "Error in opening keyboard pipe file.\n");
        exit(EXIT_FAILURE);
    }
    while (getElapsedSeconds() < MAXRUNTIME)
    {
        promptForInput(pipeFile);
    }
    fclose(pipeFile);
    exit(EXIT_SUCCESS);
}

/**
 * Spawns NUM_PLAIN_PROCS processes and one keyboard process.
 */
void spawnChildren()
{
    int i = 0;
    for (i = 0; i < NUM_PLAIN_PROCS + 1; i++)
    {
        pid_t pid = fork();
        if (!pid)//pid == 0 if in child created by fork
        {
            childIndex = i;
            if (i < NUM_PLAIN_PROCS)
            {
                plainChildFun();
            }
            else //last child should read from keyboard
            {
                keyboardChildFun();
            }
        }
        else if(pid < 0) //Check if fork failed
        {
            fprintf(stderr, "Error in fork %d.\n", i);
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * Initializes a file descriptor set to contain the 0 side of the pipes
 * @param readSet the fd set to be configured
 * @return the max fd found, which is needed for the select call
 */
int initPipeFDSet(fd_set* readSet)
{
    FD_ZERO(readSet);
    int maxFD = 0;
    int i;
    for (i = 0; i < NUM_PLAIN_PROCS + 1; i++)
    {
        FD_SET(pipeArr[i][0], readSet);
        if (pipeArr[i][0] > maxFD)
        {
            maxFD = pipeArr[i][0];
        }
    }
    return maxFD;
}

/**
 * Writes the parent process timestamp to the file
 * @param output the output file
 */
void printParentTimeStmp(FILE* output)
{
    int minVal;
    double secVal;
    currentMinSec(&minVal, &secVal);
    fprintf(output, "\n%d:%06.3f: Parent received message:\n", minVal, secVal);
}

/**
 * Converts all the pipe FDs into FILE pointers and opens them for writing
 * @param pipeFiles the array of FILE pointers
 */
void openAllPipeFiles(FILE** pipeFiles)
{
    int i;
    for (i = 0; i < NUM_PLAIN_PROCS + 1; i++)
    {
        if ((pipeFiles[i] = fdopen(pipeArr[i][0], "r")) == NULL)
        {
            fprintf(stderr, "Error in opening pipe file.\n");
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * Closes all of the pipe FILE pointers
 * @param pipeFiles the array of FILE pointers
 */
void closeAllPipeFiles(FILE** pipeFiles)
{
    int i;
    for (i = 0; i < NUM_PLAIN_PROCS + 1; i++)
    {
        fclose(pipeFiles[i]);
    }
}

/**
 * Sets the maximum time that the select call can block
 * @param timeOut the struct containing the blocking duration
 */
void setSelectTimeOut(struct timeval* timeOut)
{
    timeOut->tv_sec = MAX_SLEEP + 1;
    timeOut->tv_usec = 0;
}

/**
 * Reads all ready members of the pipe fd set
 * @param pipeFiles the array of FILE pointers
 * @param outFile the output FILE pointer
 * @param readyToReadSet the fd set showing ready to read pipes
 */
void readReadySet(FILE** pipeFiles, FILE* outFile, fd_set* readyToReadSet)
{
    int fdIndex;
    for (fdIndex = 0; fdIndex < NUM_PLAIN_PROCS + 1; fdIndex++)
    {
        if (FD_ISSET(pipeArr[fdIndex][0], readyToReadSet))
        {
            char childMsg[STR_LIM];
            fgets(childMsg, STR_LIM, pipeFiles[fdIndex]);
            //I was getting extra nextline strings for the keyboard output. This ensures I can avoid them.
            if (strlen(childMsg) > 1)
            {
                printParentTimeStmp(outFile);
                fputs(childMsg, outFile);
            }
        }
    }
}

/**
 * Calls the select function to find readable pipes, if any.
 * @param pipeFiles the array of FILE pointers
 * @param outFile the output FILE pointer
 * @return 1 if no pipes were read in time, indicating that the task is done
 * and no more pipe reading is possible.
 */
int selectAndRead(FILE** pipeFiles, FILE* outFile)
{
    fd_set readyToReadSet;
    int maxFD = initPipeFDSet(&readyToReadSet);
    struct timeval timeOut;
    setSelectTimeOut(&timeOut);
    int numReadyFD = select(maxFD + 1, &readyToReadSet, NULL, NULL, &timeOut);
    //Determine what to do based on the number of ready descriptors
    if (numReadyFD < 0) //Error occurred
    {
        fprintf(stderr, "Error in selecting ready files.\n");
        closeAllPipeFiles(pipeFiles);
        fclose(outFile);
        exit(EXIT_FAILURE);
    }
    else if (numReadyFD > 0) //At least one ready pipe
    {
        readReadySet(pipeFiles, outFile, &readyToReadSet);
    }
    return !numReadyFD; //If there is no ready pipe found in time, returns 1
}

/**
 * Reads all input from the pipes for the duration of MAXRUNTIME.
 */
void readPipes()
{
    FILE* outFile;
    if ((outFile = fopen(fileName, "w")) == NULL)
    {
        fprintf(stderr, "Error in opening output file.\n");
        exit(EXIT_FAILURE);
    }
    FILE* pipeFiles[NUM_PLAIN_PROCS + 1];
    openAllPipeFiles(pipeFiles);
    fprintf(outFile,
            "Group 6, Homework 6: I/O with Unix Pipes and Child Processes\n");
    int done = 0;
    while (!done && getElapsedSeconds() < MAXRUNTIME)
    {
        done = selectAndRead(pipeFiles, outFile);
    }
    closeAllPipeFiles(pipeFiles);
    fclose(outFile);
}

/**
 * This program spawns a fixed number of child processes that
 * continuously write a generic timestamped message to their associated pipe.
 * The program also spawns a single child that takes keyboard input, timestamps it,
 * and writes it to a pipe. Whenever any pipe receives input, the parent
 * writes its own timestamped message and the received message to an output file.
 * Each child process should only run for MAXRUNTIME seconds.
 */
int main(void)
{
    initPipes();
    gettimeofday(&start, NULL);
    spawnChildren();
    readPipes();
    int status;
    pid_t terminatePid;
    while ((terminatePid = wait(&status)) > 0)
    {
        printf("PID: %d, Status: %d\n", terminatePid, status);
    }
    return EXIT_SUCCESS;
}
