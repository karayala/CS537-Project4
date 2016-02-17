#include "cs537.h"
#include "request.h"

pthread_mutex_t lock1;
pthread_cond_t empty;
pthread_cond_t notEmpty;

// 
// server.c: A very, very simple web server
//
// To run:
//  server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//



//Logic to produce data
//1. Locks
//2. Checks if buffer is full 
//2.1 IF buffer is full, waits until the buffer's empty again.
//2.2 ONCE buffer is empty, continues on.
//3. (assuming buffer is empty): Enqueues the connection file descriptor (passed into method).
//4. Signals that the buffer isn't empty anymore
//5. Unlocks/Returns back to main. 
int producerLogic(int connfd){
        pthread_mutex_lock(&lock1); //Lock!
        while(fullOrEmpty() == 1){
           pthread_cond_wait(&empty, &lock1);
        }

        enQ(connfd);
        pthread_cond_signal(&notEmpty);
        pthread_mutex_unlock(&lock1);
	return 0;
}


//**Logic to Consume Data:
//Note: This logic loops until all the worker threads go.
//1. locks
//2. Checks if buffer is empty. If it is, it waits until the buffer isn't empty. It doesn't move until the status change.s
//3. Dequeues the request number (connection file descriptor) for the thread.
//4. Signals that the queue is empty (or rather, able to produce).
//5. Unlocks
//6. Handles the request (server stuff happens)
//7. Closes the request
//8. Cycles through
void *consumerLogic(){
   while (1) {
      pthread_mutex_lock(&lock1);
      while (fullOrEmpty()==0){
         pthread_cond_wait(&notEmpty, &lock1);
      }
      int RequestNum = deQ();
      pthread_cond_signal(&empty);
      pthread_mutex_unlock(&lock1);
      requestHandle(RequestNum);
      Close(RequestNum);
   }
}


//Creates a list of threads and stores them inside a given array of threads
void createThreads(pthread_t *threadList, int threads){
    int i;
    for (i = 0; i<threads; i++){
        pthread_create(&threadList[i], NULL, consumerLogic, NULL); //instantiate 1 worker thread for each thread
    }

}
//Parses through the list of passed-in arguments
//Checks if user entered right number of arguments
//allocates port number, number of threads, and number of buffers
void getargs(int *portnum, int *threads, int *buffers, int argc, char *argv[])
{
    if (argc != 4) {
    	fprintf(stderr, "Usage: %s <port> <threads> <buffers> \n", argv[0]);
        exit(1);
    }
    
    *portnum = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *buffers = atoi(argv[3]);

    if(*threads < 0 || *buffers <0){
    	fprintf(stderr, "Please enter positive integers for threads and buffers\n");
	exit(1);
    }
    setMax(*buffers); //setMax sets the maximum number of buffers within the Buffer/Queue logic (cs537.c).  
}


int main(int argc, char *argv[])
{

    //*[A]* Initial Variable Declaration***************************
    int listenfd;
    int connfd;
    int portnum;
    int clientlen;
    int threads;
    int buffers;
    struct sockaddr_in clientaddr;
    ////////////////////////////////////////////////////////////////
     
    //*[B]* Initializations****************************************
    //1. obtains passed-in arguments
    //2. initializes the lock1. NULL implies that defaul mutex attributes will be used in initialization
    //3. Creates a list of threads with thread-size. 
    getargs(&portnum, &threads, &buffers,  argc, argv);
    pthread_mutex_init(&lock1, NULL);
    pthread_t threadList[threads];
    ////////////////////////////////////////////////////////////////   
 
    //*[C]* Thread Creation****************************************
    createThreads(threadList, threads);
    ////////////////////////////////////////////////////////////////

    //*[D]* Listen and produce the buffer with each request********
    listenfd = Open_listenfd(portnum);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        producerLogic(connfd);
    }
    ////////////////////////////////////////////////////////////////
}


    


 
