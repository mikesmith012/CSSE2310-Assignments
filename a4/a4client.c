// a4client.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <tinyexpr.h>
#include <csse2310a3.h>
#include <csse2310a4.h>

#define MSG_SIZE 1000

// Job data structure
// stores related information about each integration job
typedef struct Job {
    char* expr;
    double lower;
    double upper;
    double segments;
    double threads;
} Job;

// function declarations
void cmd_line_args(int, char** argv, int*, int*, int*, int*);
int setup_connection(char** argv, int);
void read_file(char*, Job*, int*, int, int);
void read_stdin(Job*, int*, int, int);
int check_blank_lines(char* line);
void get_job_info(char*, char** lineSplit, int*, Job*, int, int, int);
int check_job_info(char**, int*, int*, Job*);
int check_alpha(char*, int);
int check_job(Job*, int, int);
int send_validation_request(Job*, int, int);
void send_integration_request(Job*, int, int);
char* get_integration_response(int, char*, Job*, int*);
void construct_integration_request(int, char*, Job*);
void print_ans(Job*, char*, int);

// the main function
int main(int argc, char** argv) {
    int verbose = 0, filePos = 0, portNum = 0, jobCount = 0, portIndex = 0;

    // checks the command line arguments and connects to server
    cmd_line_args(argc, argv, &verbose, &filePos, &portNum, &portIndex);
    int fdServer = setup_connection(argv, portIndex);

    Job job;
    // gets jobs from jobfile or standard input
    if (filePos == 2 || filePos == 3) {
        read_file(argv[filePos], &job, &jobCount, fdServer, verbose);
        return 0;
    }
    read_stdin(&job, &jobCount, fdServer, verbose);

    // frees manually allocated memory
    free(job.expr);
    return 0;
}

// checks command line arguments
// takes argc and argv from the command line
// also takes the following integer pointers:
//      - verbose: whether or not "-v" was specified
//      - filePos: the position of the file in the command line
//      - portNum: the port number
//      - portIndex: the position of the port number in the command line
// function will exit program with status 4 is file in unable to be opened
void cmd_line_args(int argc, char** argv, int* verbose, int* filePos, 
        int* portNum, int* portIndex) {

    char* errMsg = "Usage: intclient [-v] portnum [jobfile]";
    int error = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) {
            if (i != 1) {
                error = 1;
            }
            *verbose = 1;
        }
    }

    if (argc == 1 || (argc == 2 && *verbose) || (argc > 4 && (*verbose)) ||
            (argc > 3 && !(*verbose))) {
        error = 1;
    }

    if (error) {
        fprintf(stderr, "%s\n", errMsg);
        exit(1);
    }

    if (*verbose) {
        sscanf(argv[2], "%d", portNum);
        *portIndex = 2;
        if (argc > 3) {
            *filePos = 3;
        }
    } else if (!(*verbose)) {
        sscanf(argv[1], "%d", portNum);
        *portIndex = 1;
        if (argc > 2) {
            *filePos = 2;
        }
    }

    if (*filePos == 2 || *filePos == 3) {
        FILE* file = fopen(argv[*filePos], "r");
        if (file == NULL) {
            fprintf(stderr, "intclient: unable to open \"%s\" for reading\n",
                    argv[*filePos]);
            exit(4);
        }
        fclose(file);
    }
}

// sets up the connection to communicate with clients
// takes the argv array from the command line
// also takes the position of the port number specified in the command line
// returns the file descriptor for the connection
// function will exit program if client is unable to connect to server
int setup_connection(char** argv, int portIndex) {

    struct addrinfo* ai = 0;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo(NULL, (const char*)argv[portIndex], &hints, &ai);
    int fdClient = socket(AF_INET, SOCK_STREAM, 0);
    int err = connect(fdClient, (struct sockaddr*)ai->ai_addr, 
            sizeof(struct sockaddr));

    if (err) {
        fprintf(stderr, "intclient: unable to connect to port %s\n", 
                argv[portIndex]);
        freeaddrinfo(ai);
        exit(2);
    }

    freeaddrinfo(ai);
    return fdClient;
}

// reads the job file if one was specified
// takes the filename, pointer to a job data struct and the number of jobs
// also takes the server file descriptor and verbose mode as parameters
void read_file(char* filename, Job* job, int* jobCount, int fdServer, 
        int verbose) {

    FILE* file = fopen(filename, "r");
    char* line;
    char** lineSplit;
    int lineNum = 1;

    while ((line = read_line(file)) != NULL && !feof(file)) {
        if (check_blank_lines(line)) {
            ;
        } else if (line[0] != '#') {
            char* lineCpy = (char*)malloc((strlen(line) + 1) * sizeof(char));
            strcpy(lineCpy, line);
            lineSplit = split_by_commas(line);
            get_job_info(lineCpy, lineSplit, jobCount, job, lineNum, fdServer, 
                    verbose);
            free(lineSplit);
            free(lineCpy);
        }
        free(line);
        lineNum++;
    }
    fclose(file);
}

// reads from standard input
// takes a pointer to the job struct, number of jobs, server file descriptor
// and verbose mode as parameters
void read_stdin(Job* job, int* jobCount, int fdServer, int verbose) {

    char* line;
    char** lineSplit;
    int lineNum = 1;

    while ((line = read_line(stdin)) != NULL && !feof(stdin)) {
        if (check_blank_lines(line)) {
            ;
        } else if (line[0] != '#') {
            char* lineCpy = (char*)malloc((strlen(line) + 1) * sizeof(char));
            strcpy(lineCpy, line);
            lineSplit = split_by_commas(line);
            get_job_info(lineCpy, lineSplit, jobCount, job, lineNum, fdServer, 
                    verbose);
            free(lineSplit);
            free(lineCpy);
        }
        free(line);
        lineNum++;
    }
}

// checks if the specified line is blank
// takes in the character pointer 'line' that need to be checked
// returns 1 if the line is blank or has a blank space, 0 otherwise
int check_blank_lines(char* line) {

    if (!strlen(line)) {
        return 1;
    }

    int space = 1;
    for (int i = 0; line[i] != '\0'; i++) {
        if (!isspace((int)line[i])) {
            space &= 0;
        }
    }

    return space;
}

// gets the job information and stores in the job data structure
// otherwise prints a corresponding error message to stderr
// takes the splitted line, number of jobs, ptr to job struct and line number
// also takes the server file descriptor and verbose mode as parameters
void get_job_info(char* line, char** lineSplit, int* jobCount, Job* job, 
        int lineNum, int fdServer, int verbose) {

    char* errMsg = "intclient: syntax error on line";
    int error = 0;
    int i = check_job_info(lineSplit, &error, jobCount, job);

    if (job->segments > INT_MAX || job->threads > INT_MAX) {
        error = 1;
    }

    if (i != 5 || error) {
        fprintf(stderr, "%s %d\n", errMsg, lineNum);
    } else if (check_job(job, lineNum, fdServer)) {
        ;
    } else {
        send_integration_request(job, fdServer, verbose);
    }
    (*jobCount)++;
}

// checks that all parameters of the job information is valid
// takes the splitted line, pointer to error value, number of jobs
// and a pointer to the job struct as parameters
// returns the numbers of fields specified for the job information
int check_job_info(char** lineSplit, int* error, int* jobCount, Job* job) {
    int i;
    for (i = 0; lineSplit[i] != NULL; i++) {

        if (!strlen(lineSplit[i])) {
            *error = 1;
            break;
        }

        if (i == 0) {
            int len = strlen(lineSplit[i]) + 1;
            if (*jobCount) {
                free(job->expr);
            }
            job->expr = (char*)malloc(len * sizeof(char));
            strcpy(job->expr, lineSplit[i]);
        } else if (i == 1) {
            if (check_alpha(lineSplit[i], 1)) {
                *error = 1;
                break;
            }
            sscanf(lineSplit[i], "%lf", &(job->lower));
        } else if (i == 2) {
            if (check_alpha(lineSplit[i], 1)) {
                *error = 1;
                break;
            }
            sscanf(lineSplit[i], "%lf", &(job->upper));
        } else if (i == 3) {
            if (check_alpha(lineSplit[i], 0)) {
                *error = 1;
                break;
            }
            sscanf(lineSplit[i], "%lf", &(job->segments));
        } else if (i == 4) {
            if (check_alpha(lineSplit[i], 0)) {
                *error = 1;
                break;
            }
            sscanf(lineSplit[i], "%lf", &(job->threads));
        }
    }
    return i;
}

// checks for alphabetical character and other non-numeric characters
// takes in the string to be checked and whether the number contains a decimal
// returns 0 if number does not contain any illegal characters, 
// returns 1 otherwise
// characters that can be used to represent numbers such as:
//  - '-' for negative sign
//  - 'e' for exponential
//  - '.' for decimal points
// are allowed to be present in the expression
int check_alpha(char* str, int decimal) {
    for (int i = 0; str[i] != '\0'; i++) {

        if (!isdigit(str[i]) && str[i] != '-' && str[i] != 'e') {
            if (decimal && str[i] == '.') {
                continue;
            }
            return 1;
        }
        
    }
    return 0;
}

// checks jobs specified from job files and stdin
// takes a pointer to the job struct, the line number
// and the server file desriptor as parameters
// returns 1 if job contains any invalid fields, 0 otherwise
// if job does contain any invalid fields, a corresponding error message
// will be printed to standard error
int check_job(Job* job, int lineNum, int fdServer) {

    char* exprErr = "intclient: spaces not permitted in expression";
    char* boundErr = "intclient: upper bound must be greater than lower bound";
    char* segErr = "intclient: segments must be a positive integer";
    char* threadErr = "intclient: threads must be a positive integer";
    char* mulErr = "intclient: segments must be an integer multiple"
            " of threads";

    for (int i = 0; job->expr[i] != '\0'; i++) {
        if (isspace((int)job->expr[i])) {
            fprintf(stderr, "%s (line %d)\n", exprErr, lineNum);
            return 1;
        }
    }

    if (job->lower >= job->upper) {
        fprintf(stderr, "%s (line %d)\n", boundErr, lineNum);
        return 1;
    }

    if (remainder(job->segments, 1) != 0 || job->segments <= 0) {
        fprintf(stderr, "%s (line %d)\n", segErr, lineNum);
        return 1;
    }

    if (remainder(job->threads, 1) != 0 || job->threads <= 0) {
        fprintf(stderr, "%s (line %d)\n", threadErr, lineNum);
        return 1;
    }

    if (remainder(job->segments / job->threads, 1) != 0) {
        fprintf(stderr, "%s (line %d)\n", mulErr, lineNum);
        return 1;
    }

    if (send_validation_request(job, lineNum, fdServer) == 1) {
        return 1;
    }

    return 0;
}

// sends the validation request to intserver to check its validity
// takes a pointer to the job struct, the current line number 
// and the server file descriptor as parameters
// returns 1 if the expression is found to be invalid by intserver
// returns 0 otherwise
// function will exit program with status 3 and print a corresponding
// error message to standard error if intserver disconnects
int send_validation_request(Job* job, int lineNum, int fdServer) {

    char* valErr = "intclient: bad expression";
    char request[MSG_SIZE];

    sprintf(request, "GET /validate/%s HTTP/1.1\r\n\r\n", job->expr);
    write(fdServer, request, strlen(request));

    int status, len = 0, bytes = 0;
    char* response = (char*)malloc(sizeof(char));
    response[0] = '\0';

    do {
        char buffer[MSG_SIZE];

        if ((bytes = read(fdServer, buffer, MSG_SIZE)) < 1) {
            free(response);
            free(job->expr);
            fprintf(stderr, "intclient: communications error\n");
            exit(3);
        }
        len += bytes;

        response = (char*)realloc(response, (len + 1) * sizeof(char));
        buffer[bytes] = '\0';

        strcat(response, buffer);
        response[len] = '\0';

        if (response[len - 1] == '\n' && 
                (response[len - 2] == '\n' || response[len - 3] == '\n')) {
            break;
        }

    } while (1);

    char* statusExplanation;
    char* body;

    HttpHeader** headers;
    int parseErr = parse_HTTP_response(response, len, &status, 
            &statusExplanation, &headers, &body);
    free(response);

    if (parseErr < 0) {
        free(job->expr);
        fprintf(stderr, "intclient: communications error\n");
        exit(3);
    }

    free(body);
    free(statusExplanation);
    free_array_of_headers(headers);

    if (status == 400) {
        fprintf(stderr, "%s \"%s\" (line %d)\n", valErr, job->expr, lineNum);
        return 1;
    }

    return 0;
}

// sends the integration request to intserver
// takes a pointer to the job struct, verbose mode 
// and the server file descriptor as parameters
// function will exit program with status 3 and print a corresponding
// error message to standard error if intserver disconnects
// program will print a seperate error message in integration fails
void send_integration_request(Job* job, int fdServer, int verbose) {

    char request[MSG_SIZE];
    construct_integration_request(verbose, request, job);
    write(fdServer, request, strlen(request));

    int status, len = 0;
    char* response = (char*)malloc(sizeof(char));
    response[0] = '\0';

    response = get_integration_response(fdServer, response, job, &len);
    char* statusExplanation;
    char* body;

    HttpHeader** headers;
    int err = parse_HTTP_response(response, len, &status, 
            &statusExplanation, &headers, &body);
    free(response);

    if (err < 0) {
        free(job->expr);
        fprintf(stderr, "intclient: communications error\n");
        exit(3);
    }

    free(statusExplanation);
    free_array_of_headers(headers);

    if (status == 200) {
        print_ans(job, body, verbose);
    } else {
        fprintf(stderr, "intclient: integration failed\n");
    }

    free(body);
}

// receives integration responses from intserver
// takes the server file descriptor, response string, pointer to the 
// job data struct and pointer to the response length as parameters
// returns the potentially reallocated pointer for the response
char* get_integration_response(int fdServer, char* response, 
        Job* job, int* len) {

    int count = 0, stop = 0, bytes = 0;

    do {
        char buffer[MSG_SIZE];

        if ((bytes = read(fdServer, buffer, MSG_SIZE)) < 1) {
            free(response);
            free(job->expr);
            fprintf(stderr, "intclient: communications error\n");
            exit(3);
        }
        *len += bytes;

        response = (char*)realloc(response, (*len + 1) * sizeof(char));
        buffer[bytes] = '\0';

        strcat(response, buffer);
        response[*len] = '\0';

        for (int i = 30; i < *len; i++) {
            if (response[i] == '\n') {
                if (i - count < 30 && i - count > 3) {
                    stop = 1;
                    break;
                }
                count = i;
            }
        }

        if (stop) {
            break;
        }

    } while (1);

    return response;
}

// helper function to construct the integration request
// takes the verbose mode, pointer to the request string
// and pointer to job as parameters
void construct_integration_request(int verbose, char* request, Job* job) {

    char* protocol = "HTTP/1.1";
    char* header = "X-Verbose: yes";

    if (verbose) {
        sprintf(request, "GET /integrate/%lf/%lf/%d/%d/%s %s\r\n%s\r\n\r\n", 
                job->lower, job->upper, (int)job->segments, (int)job->threads, 
                job->expr, protocol, header); 
    } else {
        sprintf(request, "GET /integrate/%lf/%lf/%d/%d/%s %s\r\n\r\n",
                job->lower, job->upper, (int)job->segments, (int)job->threads, 
                job->expr, protocol);
    }
}

// helper function to print the answer response from intserver
// takes a pointer to the job information, the string 'body' to be printed
// and verbose mode as parameters
void print_ans(Job* job, char* body, int verbose) {

    if (verbose) {
        int len = strlen(body), count = 0, verPos = 0, ansPos = 0;
        char verOut[len];
        char ansOut[len];

        for (int i = 0; i < len - 1; i++) {

            if (count < job->threads) {
                verOut[i] = body[i];
                verPos = i + 1;
            } else {
                ansOut[i - verPos] = body[i];
                ansPos = i + 1;
            }

            if (body[i] == '\n') {
                count++;
            }
        }

        verOut[verPos] = '\0';
        ansOut[ansPos - verPos] = '\0';

        fprintf(stdout, verOut);
        fprintf(stdout, "The integral of %s from %lf to %lf is %s\n", 
                job->expr, job->lower, job->upper, ansOut);
    } else {
        double ans;
        sscanf(body, "%lf", &ans);
        
        fprintf(stdout, "The integral of %s from %lf to %lf is %lf\n", 
                job->expr, job->lower, job->upper, ans);
    }
}
