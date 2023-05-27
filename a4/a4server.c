// a4server.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <tinyexpr.h>
#include <csse2310a3.h>
#include <csse2310a4.h>

#define CONNECTION_LIMIT 100
#define MSG_SIZE 1000

// Thread data structure which stored an array of thread id's and an
// index value that can be used to reference a given thread id
typedef struct Thread {
    int index;
    pthread_t* threadID;
} Thread;

// data structure for verbose mode which contains the body of the -v output, 
// the length of the output and whether verbose mode is active
typedef struct Verbose {
    char* body;
    int len;
    int verbose;
} Verbose;

// Statistics data structure used to store the program information to date
// the information includes:
//  - current number of connected clients
//  - total number of expressions checked
//  - total number of successfully completed integration jobs
//  - total number of rejected integration jobs (bad validation requests)
//  - total number of threads since the start of program
typedef struct Statistics {
    int connectedClients;
    int expressionsChecked;
    int completedJobs;
    int badJobs;
    int totalThreads;
} Statistics;

// parameters data structure for client threads which contains all necessary
// data which needs to be passed through to the client thread.
// includes the client file desriptor 'fd', the maximum number of threads
// 'maxThreads', the current number of computational threads 'threadCount', 
// a pointer to the Thread data struct, a sem 'limit' and a mutex 'lock'
typedef struct ThreadParams {
    int* fd;
    int maxThreads;
    int threadCount;
    Thread* index;
    Statistics* stats;
    sem_t* limit;
    pthread_mutex_t* lock;
} ThreadParams;

// parameters data structure for integration threads which contains all 
// necessary data which needs to be passed through to each integration thread.
// Includes the expression to be integrated 'expression', the 'upper' and 
// 'lower' bounds, the number of 'segments' and the number of 'threads', 
// the response 'status', the integration answer 'ans', whether or not
// 'verbose' is active, a semaphore to thread limiting and a mutex for
// mutal exclusion.
typedef struct IntParams {
    char* expression;
    double* lower;
    double* upper;
    int segments;
    int threads;
    Thread* index;
    int* status;
    double* ans;
    Verbose* verbose;
    sem_t* limit;
    pthread_mutex_t* lock;
} IntParams;

// job data structure which stores all the necessary information about 
// each integral job.
// Contains the expression to be integrated 'expr', the 'lower' and 'upper'
// bounds, the number of 'segments' and the number of 'threads'.
typedef struct Job {
    char* expr;
    double lower;
    double upper;
    int segments;
    int threads;
} Job;

// function declarations
void* handle_client(void*);
int get_request(char*, int, ThreadParams*);
int handle_validation_request(char*, int, int, char*);
int handle_integration_request(char*, int, int, char*, ThreadParams*);
int get_verbose_info(char*);
void get_job_info(Job*, char*);
int check_expr(char*);
double integrate_expr(Job*, int*, Verbose*, ThreadParams*);
double create_threads(Job*, IntParams*, pthread_t*, ThreadParams*);
void verbose_output(IntParams*);
void* integrate_thread(void*);
void cmd_line_args(int, char** argv, double*, double*);
int check_alpha(char*);
int setup_connection(char** argv);
void process_connection(int, int);

// global variable for sighup signal
int sighup = 0;

// signal handler function for the sighup signal
void handle_sighup(int s) {
    sighup = 1;
}

// the main function
int main(int argc, char** argv) {

    // sets up the sighup signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sighup;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);

    // checks command line args
    double portNum = 0, maxThreads = 0;
    cmd_line_args(argc, argv, &portNum, &maxThreads);

    // sets up connection
    int fd = setup_connection(argv);
    process_connection(fd, maxThreads);

    return 0;
}

// thread function which handled the statistics reporting functionality
// loops forever, will print and flush statistics to standard error upon
// receiving a sighup signal
void* handle_stats(void* v) {
    ThreadParams p = *(ThreadParams*)v;

    while (1) {
        if (sighup) {
            fprintf(stderr, "Connected clients:%d\n", 
                    p.stats->connectedClients);

            fprintf(stderr, "Expressions checked:%d\n", 
                    p.stats->expressionsChecked);

            fprintf(stderr, "Completed jobs:%d\n", p.stats->completedJobs);
            fprintf(stderr, "Bad jobs:%d\n", p.stats->badJobs);
            fprintf(stderr, "Total threads:%d\n", p.stats->totalThreads);

            fflush(stderr);
            sighup = 0;
        }
    }

    pthread_exit(NULL);
}

// handles client threads
// takes a pointer to the ThreadParams data structure as the parameter
// will call exit on current thread if client has disconnected or if
// client sends an invalid http request.
void* handle_client(void* v) {

    ThreadParams p = *(ThreadParams*)v;
    int status = 200, fdClient = 0;

    for (int i = 0; p.index[i].index != -1; i++) {
        if (*(p.index[i].threadID) == pthread_self()) {
            fdClient = p.fd[i];
        }
    }

    while (1) {
        char request[MSG_SIZE];
        int len = get_request(request, fdClient, &p);
        char* met;
        char* addr; 
        char* requestBody;

        HttpHeader** requestHeaders;
        int err = parse_HTTP_request(request, len, &met, &addr, 
                &requestHeaders, &requestBody);

        if (err == -1) {
            close(fdClient);
            pthread_mutex_lock(p.lock);
            p.stats->connectedClients--;
            pthread_mutex_unlock(p.lock);
            pthread_exit(NULL);
        } else if (!err || (strncmp(request, "GET /validate", 10) &&
                strncmp(request, "GET /integrate", 10))) {
            status = 400;
        }

        if (!strncmp(request, "GET /validate", 10) || status == 400) {
            pthread_mutex_lock(p.lock);
            status = handle_validation_request(request, fdClient, 
                    status, addr);
            p.stats->expressionsChecked++;

            if (status == 400) {
                p.stats->badJobs++;
            }

            pthread_mutex_unlock(p.lock);
        } 

        if (!strncmp(request, "GET /integrate", 10) && status == 200) {
            pthread_mutex_lock(p.lock);
            status = handle_integration_request(request, fdClient, status, 
                    addr, &p);

            if (status == 200) {
                p.stats->completedJobs++;
            }

            pthread_mutex_unlock(p.lock);
        }
        status = 200;
    }
}

// receives the requests sent from intclient
// takes a pointer to the request string and the client file descriptor
// as parameters
// returns the length of the request
// will exit current thread if eof is received from client file descriptor
int get_request(char* request, int fdClient, ThreadParams* p) {
    int len = 0, bytes = 0;
    do {
        if ((bytes = read(fdClient, request + len, MSG_SIZE)) < 1) {
            close(fdClient);
            pthread_mutex_lock(p->lock);
            p->stats->connectedClients--;
            pthread_mutex_unlock(p->lock);
            pthread_exit(NULL);
        }

        len += bytes;
        if (request[len - 1] == '\n' && 
                (request[len - 2] == '\n' || request[len - 3] == '\n')) {
            break;
        }

    } while (1);

    request[len] = '\0';
    return len;
}

// handles validation requests sent from intclient
// takes the request, client file descriptor, response status and
// the address string as parameters.
// returns the new status after validation
// also sends response back to the client
int handle_validation_request(char* request, int fdClient, int status,
        char* addr) {

    char* statusExplanation;
    char* okMsg = "OK";
    char* badRequestMsg = "Bad Request";
    int okLen = strlen(okMsg) + 1;
    int badRequestLen = strlen(badRequestMsg) + 1;

    if (status == 200) {
        char** split = split_by_char(addr, '/', 3);
        char* expr = malloc(0);

        for (int j = 0; split[j] != NULL; j++) {
            if (j == 2) {
                int exprLen = strlen(split[j]);
                expr = realloc(expr, (exprLen + 1) * sizeof(char));
                strcpy(expr, split[j]);
            }
        }

        status = check_expr(expr);
        free(expr);
        statusExplanation = (char*)malloc(okLen * sizeof(char));
        strcpy(statusExplanation, okMsg);

    } else if (status == 400) {
        statusExplanation = (char*)malloc(badRequestLen * sizeof(char));
        strcpy(statusExplanation, badRequestMsg);
    }

    char* responseBody = NULL;
    HttpHeader** responseHeaders = NULL;
    char* response = construct_HTTP_response(status, statusExplanation,
            responseHeaders, responseBody);

    write(fdClient, response, strlen(response));
    free(response);
    free(statusExplanation);

    return status;
}

// handles integration request sent from client
// takes the request, client file descriptor, response status,
// the address string and a pointer to thread params as parameters
// also sends response back to the client along with the integration result
// returns the current status after integration job has been done
int handle_integration_request(char* request, int fdClient, int status, 
        char* addr, ThreadParams* p) {

    int verbose = get_verbose_info(request);
    Job job;
    get_job_info(&job, addr);

    char* statusExplanation;
    char* okMsg = "OK";
    char* badRequestMsg = "Bad Request";
    int okLen = strlen(okMsg) + 1;
    int badRequestLen = strlen(badRequestMsg) + 1;
    char* responseBody = (char*)malloc(0);

    Verbose v;
    v.body = responseBody;
    v.len = 0;
    v.verbose = verbose;
    double ans = 0;

    if (status == 200) {
        ans = integrate_expr(&job, &status, &v, p);
        statusExplanation = (char*)malloc(okLen * sizeof(char));
        strcpy(statusExplanation, okMsg);
    } else if (status == 400) {
        statusExplanation = (char*)malloc(badRequestLen * sizeof(char));
        strcpy(statusExplanation, badRequestMsg);
    }

    char buf[MSG_SIZE];
    sprintf(buf, "%lf\n", ans);
    int bufLen = strlen(buf) + 1;
    buf[bufLen] = '\0';
    v.len += bufLen;
    v.body = (char*)realloc(v.body, v.len * sizeof(char));

    if (v.verbose) {
        strcat(v.body, buf);
    } else {
        strcpy(v.body, buf);
    }

    HttpHeader** responseHeaders = NULL;
    char* response = construct_HTTP_response(status, statusExplanation,
            responseHeaders, v.body);

    write(fdClient, response, strlen(response));
    free(v.body);
    free(response);
    free(statusExplanation);
    free(job.expr);

    return status;
}

// determines whether or not verbose mode was specified by intclient
// takes the request string sent from intclient as a parameter
// returns 'verbose' which is 1 is verbose mode is active, 0 otherwise
int get_verbose_info(char* request) {

    int requestLen = strlen(request);
    int count = 0, verbose = 0, len = 0;
    char* buffer = (char*)malloc(0);

    for (int i = 0; i < requestLen; i++) {

        if (count == 1) {
            buffer = (char*)realloc(buffer, (++len + 1) * sizeof(char));
            buffer[len - 1] = request[i];
            buffer[len] = '\0';
        }

        if (request[i] == '\n') {
            count++;
        }

    }

    if (strcmp(buffer, "X-Verbose: yes\n") == 0) {
        verbose = 1;
    }

    free(buffer);
    return verbose;
}

// gets the job information and stores in job struct
// takes a pointer to a job struct and the address string as parameters
void get_job_info(Job* job, char* addr) {

    job->lower = 0;
    job->upper = 0;
    job->segments = 0;
    job->threads = 0;
    job->expr = malloc(0);

    char** split = split_by_char(addr, '/', 7);
    for (int i = 0; split[i] != NULL; i++) {

        if (i == 2) {
            sscanf(split[i], "%lf", &(job->lower));
        } else if (i == 3) {
            sscanf(split[i], "%lf", &(job->upper));
        } else if (i == 4) {
            sscanf(split[i], "%d", &(job->segments));
        } else if (i == 5) {
            sscanf(split[i], "%d", &(job->threads));
        } else if (i == 6) {
            int exprLen = strlen(split[i]) + 1;
            job->expr = realloc(job->expr, exprLen * sizeof(char));
            strcpy(job->expr, split[i]);
        }

    }
}

// checks for invalid expressions
// takes the expression to be validated as a parameter
// returns the response status, 200 if OK, 400 if bad expression
int check_expr(char* expression) {

    double x;
    te_variable vars[] = {{"x", &x}};

    int err;
    te_expr* expr = te_compile(expression, vars, 1, &err);

    if (expr) {
        te_free(expr);
        return 200;
    } else {
        return 400;
    }

}

// integrates the expression sent by intclient
// takes a pointer to the job data struct, a pointer to the response status,
// a pointer to the Verbose data struct and a thread params pointer as params
// returns the result of the integration
double integrate_expr(Job* job, int* status, Verbose* v, 
        ThreadParams* threadP) {

    IntParams p;
    pthread_mutex_t lock;
    p.lock = &lock;
    pthread_mutex_init(p.lock, NULL);
    pthread_t* tid = (pthread_t*)malloc(job->threads * sizeof(pthread_t));

    p.expression = job->expr;
    p.segments = job->segments / job->threads;
    p.threads = job->threads;
    p.status = status;
    p.verbose = v;
    p.ans = (double*)malloc(job->threads * sizeof(double));
    p.lower = (double*)malloc(job->threads * sizeof(double));
    p.upper = (double*)malloc(job->threads * sizeof(double));
    p.index = (Thread*)malloc((job->threads + 1) * sizeof(Thread));

    double ans = create_threads(job, &p, tid, threadP);    
    verbose_output(&p);

    free(tid);
    free(p.ans);
    free(p.lower);
    free(p.upper);
    free(p.index);

    sem_destroy(p.limit);
    pthread_mutex_destroy(p.lock);

    return ans;
}

// creates the specified number of integration threads required for the job
// takes a pointer to the job data struct, pointer to the integration,
// pointer to the thread id's array and pointer to thread params as parameters
// returns the result of the integration
double create_threads(Job* job, IntParams* p, pthread_t* tid, 
        ThreadParams* threadP) {

    double step = (job->upper - job->lower) / job->threads;
    double ans = 0, currVal = job->lower, prevVal = 0;
    int joinCount = 0;

    for (int i = 0; i <= job->threads; i++) {
        if (i > 0) {

            if (threadP->maxThreads && 
                    threadP->threadCount == threadP->maxThreads) {
                int* retVal;
                pthread_join(tid[joinCount++], (void**)&retVal);
                threadP->threadCount--;
            }

            pthread_mutex_lock(p->lock);
            p->lower[i - 1] = prevVal;
            p->upper[i - 1] = currVal; 
            p->index[i - 1].index = i - 1;
            p->index[i - 1].threadID = &(tid[i - 1]);
            p->index[i].threadID = NULL;
            pthread_mutex_unlock(p->lock);

            pthread_create(&(tid[i - 1]), NULL, integrate_thread, p);
            threadP->stats->totalThreads++;
            threadP->threadCount++;
        }
        prevVal = currVal;
        currVal += step;
    }

    for (int i = joinCount; i < job->threads; i++) {
        int* retVal;
        pthread_join(tid[i], (void**)&retVal);
        threadP->threadCount--;
    }

    for (int i = 0; i < job->threads; i++) {
        ans += p->ans[i];
    }

    return ans;
}

// constructs the string required for verbose mode output
// takes a pointer to the IntParams data struct as a parameter
void verbose_output(IntParams* p) {

    if (p->verbose->verbose) {

        for (int i = 0; i < p->threads; i++) {

            char value[MSG_SIZE];
            char thread[MSG_SIZE];
            char lower[MSG_SIZE];
            char upper[MSG_SIZE];

            sprintf(value, "%lf\n", p->ans[i]);
            sprintf(lower, "%lf->", p->lower[i]);
            sprintf(upper, "%lf:", p->upper[i]);
            sprintf(thread, "thread %d:", p->index[i].index + 1);

            int valueLen = strlen(value);
            int threadLen = strlen(thread);
            int lowerLen = strlen(lower); 
            int upperLen = strlen(upper);

            value[valueLen] = '\0';
            lower[lowerLen] = '\0';
            upper[upperLen] = '\0';
            thread[threadLen] = '\0';

            p->verbose->len += valueLen + lowerLen + upperLen + threadLen + 1;
            p->verbose->body = (char*)realloc(p->verbose->body, 
                    p->verbose->len * sizeof(char));

            if (!i) {
                strcpy(p->verbose->body, thread);
            } else {
                strcat(p->verbose->body, thread);
            }

            strcat(p->verbose->body, lower);
            strcat(p->verbose->body, upper);
            strcat(p->verbose->body, value);
        }
    }
}

// integrates the expression for each thread
// takes a pointer to the IntParams data struct as a parameter
// calls pthread exit when the integration for the thread is done
void* integrate_thread(void* v) {
    IntParams p = *(IntParams*)v;
    
    int index = 0;
    for (int i = 0; p.index[i].threadID != NULL; i++) {
        if (*(p.index[i].threadID) == pthread_self()) {
            index = p.index[i].index;
        }
    }

    double x = p.lower[index]; 
    double step = (p.upper[index] - p.lower[index]) / p.segments; 

    double threadAns = 0;
    te_variable vars[] = {{"x", &x}};
    int err;

    pthread_mutex_lock(p.lock);
    te_expr* expr = te_compile(p.expression, vars, 1, &err);
    pthread_mutex_unlock(p.lock);

    if (expr) {
        double currVal = 0, prevVal = 0;

        for (int i = 0; i <= p.segments; i++) {
            currVal = te_eval(expr);

            if (i > 0 && !isnan(currVal)) {
                threadAns += (currVal + prevVal) * step / 2;
            }
            prevVal = currVal;
            x += step;
        }

        pthread_mutex_lock(p.lock);
        *(p.status) = 200;
        pthread_mutex_unlock(p.lock);
        te_free(expr);

    } else {
        pthread_mutex_lock(p.lock);
        *(p.status) = 400;
        pthread_mutex_unlock(p.lock);
    }

    pthread_mutex_lock(p.lock);
    p.ans[index] = threadAns;
    pthread_mutex_unlock(p.lock);

    pthread_exit(NULL);
}

// chacks the command line arguments
// takes argc and argv from the command line
// also takes the following integer pointers:
//      - port number 
//      - maximum number of threads
void cmd_line_args(int argc, char** argv, double* portNum, 
        double* maxThreads) {

    char* errMsg = "Usage: intserver portnum [maxthreads]";
    int error = 0;

    for (int i = 0; i < argc; i++) {
        if (i == 1) {

            if (check_alpha(argv[i])) {
                error = 1;
                break;
            }
            sscanf(argv[i], "%lf", portNum);
            error = 0;

        } else if (i == 2) {

            if (check_alpha(argv[i])) {
                error = 1;
                break;
            }

            sscanf(argv[i], "%lf", maxThreads);
            if (!(*maxThreads)) {
                error = 1;
                break;
            }

        } else {
            error = 1;
        }
    }

    if (remainder(*portNum, 1) != 0 || remainder(*maxThreads, 1) != 0 ||
            *portNum < 0 || *portNum > 65535 || *maxThreads < 0) {
        error = 1;
    }

    if (error) {
        fprintf(stderr, "%s\n", errMsg);
        exit(1);
    }
}

// checks for alphabetical characters and other non numeric characters
// takes the string to be checked as a parameter
// returns 0 is string does not contain any illegal characters, 1 otherwise
int check_alpha(char* str) {

    for (int i = 0; str[i] != '\0'; i++) {
        if (!isdigit(str[i])) {
            return 1;
        }
    }

    return 0;
}

// sets up the connection
// takes the argv array from the command line
// returns the file descriptor for the specified port number
// will exit with status 3 if there is an error when setting up the connection
int setup_connection(char** argv) {

    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo(NULL, (const char*)argv[1], &hints, &ai);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int optVal = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(int));
    bind(fd, (struct sockaddr*)ai->ai_addr, sizeof(struct sockaddr));
     
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    socklen_t addrLen = sizeof(struct sockaddr_in);
    getsockname(fd, (struct sockaddr*)&addr, &addrLen);
    int portNum = ntohs(addr.sin_port);

    int err = listen(fd, CONNECTION_LIMIT);
    if (err < 0 || !portNum) {
        fprintf(stderr, "intserver: unable to open socket for listening\n");
        free(ai);
        exit(3);
    } 

    fprintf(stderr, "%d\n", portNum);
    fflush(stderr);
    freeaddrinfo(ai);
    return fd;
}

// processes connections for each client
// takes in the file descriptor for the port number as parameters
void process_connection(int fd, int maxThreads) {

    struct sockaddr_in addrNew;
    socklen_t addrLen;

    ThreadParams p;
    sem_t limit;
    pthread_mutex_t lock;

    p.limit = &limit;
    p.lock = &lock;

    sem_init(p.limit, 0, CONNECTION_LIMIT);
    pthread_mutex_init(p.lock, NULL);

    p.fd = (int*)malloc(0);
    p.maxThreads = maxThreads;
    p.threadCount = 0;

    pthread_t* tid = (pthread_t*)malloc(0);
    p.index = (Thread*)malloc(0);

    Statistics stats;
    stats.connectedClients = 0;
    stats.expressionsChecked = 0;
    stats.completedJobs = 0;
    stats.badJobs = 0;
    stats.totalThreads = 0;

    p.stats = &stats;

    pthread_t statstid;
    pthread_create(&statstid, NULL, handle_stats, &p);
    pthread_detach(statstid);

    int count = 0;
    while (1) {

        addrLen = sizeof(struct sockaddr_in);
        p.fd = (int*)realloc(p.fd, (count + 1) * sizeof(int));
        p.fd[count] = accept(fd, (struct sockaddr*)&addrNew, &addrLen);
        tid = (pthread_t*)realloc(tid, (count + 1) * sizeof(pthread_t));

        p.index = (Thread*)realloc(p.index, (count + 2) * sizeof(Thread));
        p.index[count].index = count;
        p.index[count + 1].index = -1;
        p.index[count].threadID = &(tid[count]);
        p.index[count + 1].threadID = NULL;

        pthread_create(&(tid[count]), NULL, handle_client, &p);
        pthread_detach(tid[count]);
        pthread_mutex_lock(p.lock);

        p.stats->connectedClients++;
        pthread_mutex_unlock(p.lock);
        count++;
    }
    
    sem_destroy(&limit);
    pthread_mutex_destroy(&lock);
    free(p.fd);
}
