#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <csse2310a3.h>

// pipe data structure
typedef struct Pipe {
    char* name;
    int number;
    int input;
    int output;
    int error;
} Pipe;

// invalid jobs data structure
typedef struct InvalidJobs {
    int* invJobs;
    int invjobCount;
} InvalidJobs;

// pipes file descriptors data structure
typedef struct FdPipes {
    int** fd;
} FdPipes;

// timeout data structure
typedef struct Timeouts {
    int* times;
} Timeouts;

// function declarations
void signal_handler(void);
int check_args(int, char** argv);
int check_verbose(int);
void check_files(int, char** argv, int);
void check_valid_pipes(Pipe*, int*, int*);
Pipe* read_file(char*, int*, InvalidJobs*, int*, Pipe*);
int check_stdin(char** line);
int check_stdout(char** line);
int check_timeout(char** line);
void check_pipe(char** line, int*, int*, Pipe*, InvalidJobs*);
void check_pipe_input(char** line, int*, int*, Pipe*, InvalidJobs*, 
        int*, int*);
void check_pipe_output(char** line, int*, int*, Pipe*, InvalidJobs*, 
        int*, int*);
void check_invalid_pipe(Pipe*, int, int*, int*, InvalidJobs*);
void invalid_line(int, char*);
void invalid_read(char*);
void invalid_write(char*);
void verbose_mode(char*, InvalidJobs*, Timeouts*, int, int*, int*);
char* insert_time(char*, int, int);
void check_jobs(int);
void create_pipes(int, FdPipes*);
void reassign_pipe_numbers(Pipe*, int*);
int* setup_processes(int, int*, FdPipes*, Pipe*, int*, int, int, char**, int*, 
        InvalidJobs*, pid_t*, Timeouts*, time_t*, int*, int*, int*);
int* create_process(char*, Pipe*, FdPipes*, int*, int*, int*, InvalidJobs*, 
        pid_t*, Timeouts*, time_t*, int*, int*, int*);
void wait_for_process(pid_t*, Timeouts*, int, int*, time_t*, FdPipes*, int);
void prepare_for_wait(FdPipes*, int);
void exit_status(int*, int, int, int*, int*);
int exec_job(char** line, Pipe*, FdPipes*, int*, int);
void close_pipe_fds(FdPipes*, int);
void assign_pipes(char*, Pipe*, FdPipes*, int*, int);
void free_alloc_mem(int, int, Pipe*, FdPipes*, pid_t*, InvalidJobs*, Timeouts*,
        time_t*, int*);

// global variable for sighup signal
int sighup = 0;

// handle sighup signal
void handle_sighup(int s) {
    sighup = 1;
}

// the main function
int main(int argc, char** argv) {

    signal_handler();

    int verbose = check_args(argc, argv);
    int count = check_verbose(verbose);

    check_files(argc, argv, count);

    Pipe* pipes = (Pipe*)malloc(0);
    int pipeCount = 1, jobCount = 0, validPipes = 0;

    InvalidJobs invalidJobs;
    invalidJobs.invjobCount = 0;
    invalidJobs.invJobs = (int*)malloc(sizeof(int));

    for (int i = count; i < argc; i++) {
        pipes = read_file(argv[i], &jobCount, &invalidJobs, 
                &pipeCount, pipes);
    }

    check_invalid_pipe(pipes, pipeCount, &validPipes, &jobCount, 
            &invalidJobs);

    int execCount = jobCount - invalidJobs.invjobCount;

    Timeouts timeouts;
    timeouts.times = (int*)malloc(execCount * sizeof(int));

    int timeCount = 1, invalidCount = 0;
    for (int i = count; i < argc; i++) {
        verbose_mode(argv[i], &invalidJobs, &timeouts, verbose, &timeCount, 
                &invalidCount);
    }

    FdPipes fdPipes;
    fdPipes.fd = (int**)malloc(validPipes * sizeof(int*));

    for (int i = 0; i < validPipes; i++) {
        fdPipes.fd[i] = (int*)malloc(2 * sizeof(int));
    }

    int jobNumber = 0, execNumber = 0, execFail = 0;
    pid_t* pids = (pid_t*)malloc(execCount * sizeof(pid_t));
    time_t* times = (time_t*)malloc(execCount * sizeof(time_t));
    int* validJobNumbers = malloc(0);

    validJobNumbers = setup_processes(execCount, &validPipes, &fdPipes, pipes, 
            &pipeCount, count, argc, argv, &jobNumber, &invalidJobs, pids, 
            &timeouts, times, validJobNumbers, &execNumber, &execFail);

    if (!execFail) {
        wait_for_process(pids, &timeouts, execCount, validJobNumbers, times, 
                &fdPipes, validPipes);
    }

    free_alloc_mem(pipeCount, validPipes, pipes, &fdPipes, pids, &invalidJobs, 
            &timeouts, times, validJobNumbers);

    if (execFail) {
        return 255;
    }

    check_jobs(execCount);
    return 0;
}

// sets up signal handler for when program receives a sighup signal
void signal_handler(void) {

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_sighup;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
}

// checks command line arguements
// takes "argc" and "argv" (command line args) as parameters
// returns 1 if "-v" is specified, else returns 0
int check_args(int argc, char** argv) {

    char* invErrMsg = "Usage: jobrunner [-v] jobfile [jobfile ...]";
    int error = 0;
    int verbose = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            if (i != 1) {
                error = 1;
            }
            verbose = 1;
        }
    }

    if ((argc == 1) || ((argc == 2) && verbose)) {
        error = 1;
    }

    if (error) {
        fprintf(stderr, "%s\n", invErrMsg);
        exit(1);
    }
    return verbose;
}

// checks if verbose mode was specified in the command lines
// takes the verbose int variable as a parameter, is 1 if -v was specified
// returns the position of the first file in the command line
int check_verbose(int verbose) {

    if (verbose) {
        return 2;
    } else {
        return 1;
    }
}

// checks for invalid files given as command line arguements
// takes command line args as parameters
// also takes "count" which indicates the position of the first filename
void check_files(int argc, char** argv, int count) {
    char* invFileMsg1 = "jobrunner: file \"";
    char* invFileMsg2 = "\" can not be opened";

    for (; count < argc; count++) {
        FILE* file = fopen(argv[count], "r");

        if (file == NULL) {
            fprintf(stderr, "%s%s%s\n", invFileMsg1, argv[count], invFileMsg2);
            exit(2);
        }
        fclose(file);
    }
}

// reads the contents of the files given in the job files
// checks for invalid files specified as standard input
// takes filename, number of jobs and number of pipes as parameters
// also takes an array of "Pipe" data structs as parameter
// returns new ptr to "pipes" array in case memory is reallocated
Pipe* read_file(char* filename, int* jobCount, InvalidJobs* invalidJobs, 
        int* pipeCount, Pipe* pipes) {

    FILE* file = fopen(filename, "r");
    char* line;
    char** lineSplit;
    int count = 1, input = 0, output = 0, timeout = 0;

    while ((line = read_line(file)) != NULL) {
        if (strlen(line) == 0 || isspace((int)line[0]) != 0) {
            ;
        } else if (line[0] != '#') {
            lineSplit = split_by_commas(line);
            int i;
            for (i = 0; lineSplit[i] != NULL; i++) {
                if (i == 1) {
                    input = check_stdin(lineSplit);
                } else if (i == 2) {
                    output = check_stdout(lineSplit);
                } else if (i == 3) {
                    timeout = check_timeout(lineSplit);
                }
            }

            if (input == -2 || output == -2) {
                invalidJobs->invJobs = (int*)realloc(invalidJobs->invJobs, 
                        (invalidJobs->invjobCount + 1) * sizeof(int));
                invalidJobs->invJobs[invalidJobs->invjobCount] = *jobCount + 1;
                (invalidJobs->invjobCount)++;
            }
            (*jobCount)++;

            if (i > 2) {
                pipes = (Pipe*)realloc(pipes, (*pipeCount + 1) * sizeof(Pipe));
                check_pipe(lineSplit, pipeCount, jobCount, pipes, invalidJobs);
            } else {
                input = -1;
            }
            free(lineSplit);
        }
        free(line);

        if (input == -1 || output == -1 || timeout == -1) {
            for (int i = 0; i < (*pipeCount) - 1; i++) {
                free(pipes[i].name);
            }
            free(pipes);
            fclose(file);
            invalid_line(count, filename);
            exit(3);
        }
        count++;
    }
    fclose(file);
    return pipes;
}

// checks files specified as standard input
// takes one line from a job file as a parameter
// returns -1 if no stdin ard was specified in the job files
// returns -2 if the file was invalid
// returns 1 is file exists
// returns 0 if pipe is specified or if no stdin file is specified
int check_stdin(char** line) {

    if (line[1][0] == '@') {
        return 0;
    } else if (strcmp(line[1], "-") != 0) {
        
        if (strlen(line[1]) == 0) {
            return -1;
        }

        int fd = open(line[1], O_RDONLY);
        if (fd == -1) {
            invalid_read(line[1]);
            return -2;
        }
        close(fd);
        return 1;
    }
    return 0;
}

// checks files specified as standard output
// takes one line from a job file as a parameter
// returns -1 if no stdout arg was specified in the job files
// returns -2 if the file was invalid
// returns 1 if a stdout file has been specified
// returns 0 if pipe is specified or if no stdout file is specified
int check_stdout(char** line) {

    if (line[2][0] == '@') {
        return 0;
    } else if (strcmp(line[2], "-") != 0) {
        if (strlen(line[2]) == 0) {
            return -1;
        }

        int fd = open(line[2], O_WRONLY | O_CREAT | O_TRUNC, 
                S_IRWXU | S_IRGRP);
        if (fd == -1) {
            invalid_write(line[2]);
            return -2;
        }
        close(fd);
        return 1;
    }
    return 0;
}

// checks for timeout specified in the job files
// takes the specified timeout value as a parameter in string form
// returns 0 is no timeout value was specified, else returns timeout value
// returns -1 if invalid characters (non-integers) are specified as timeout
int check_timeout(char** line) {

    for (int i = 0; line[i] != NULL; i++) {
        if (i == 3) {

            for (int j = 0; line[i][j] != '\0'; j++) {
                if (!isdigit((int)line[i][j])) {
                    return -1;
                }
            }

            return atoi(line[i]);
        }
    }
    return 0;
}

// checks for pipes specified in the job files
// takes the current line being read from as a parameter
// takes the number of pipes and number of jobs as parameters
// takes the an array of the "Pipe" data struct as a parameter
void check_pipe(char** line, int* pipeCount, int* jobCount, Pipe* pipes, 
        InvalidJobs* invalidJobs) {

    int input = -1, output = -1, existingPipe1 = 0, existingPipe2 = 0;

    check_pipe_input(line, &input, &existingPipe1, pipes, invalidJobs, 
            pipeCount, jobCount);

    check_pipe_output(line, &output, &existingPipe2, pipes, invalidJobs,
            pipeCount, jobCount);

    if (line[1][0] == '@') {

        if (output != -1) {
            pipes[*(pipeCount) - 1].error = 1;
        } 

        if (!existingPipe2) {
            int len = strlen(line[1]) + 1;
            pipes[*(pipeCount) - 1].name = (char*)malloc(len * sizeof(Pipe));
            strcpy(pipes[*(pipeCount) - 1].name, line[1]);
            pipes[*(pipeCount) - 1].output = (*jobCount);
            pipes[*(pipeCount) - 1].input = -1;
            pipes[*(pipeCount) - 1].error = 0;
            pipes[*(pipeCount) - 1].number = (*pipeCount);
            (*pipeCount)++;
        }
    }

    if (line[2][0] == '@') {

        if (input != -1) {
            pipes[*(pipeCount) - 1].error = 1;
        } 

        if (!existingPipe1) {
            int len = strlen(line[2]) + 1;
            pipes[*(pipeCount) - 1].name = (char*)malloc(len * sizeof(Pipe));
            strcpy(pipes[*(pipeCount) - 1].name, line[2]);

            pipes[*(pipeCount) - 1].input = (*jobCount);
            pipes[*(pipeCount) - 1].output = -1;
            pipes[*(pipeCount) - 1].error = 0;
            pipes[*(pipeCount) - 1].number = (*pipeCount);

            (*pipeCount)++;
        }
    }
}

// checks that specified pipes have only one input
// takes the current line, input status, existing pipe status as parameters
// also takes the pipes and invalid jobs data structure, pipes and jobs count
void check_pipe_input(char** line, int* input, int* existingPipe1, 
        Pipe* pipes, InvalidJobs* invalidJobs, int* pipeCount, int* jobCount) {

    int in0, in1;
    for (int i = 0; i < (*pipeCount) - 1; i++) {

        in0 = 0;
        in1 = 0;
        if (strcmp(pipes[i].name, line[2]) == 0) {
            *input = pipes[i].input;
            (*existingPipe1)++;
            pipes[i].input = (*jobCount);

            for (int j = 0; j < invalidJobs->invjobCount; j++) {

                if (pipes[i].input == invalidJobs->invJobs[j]) {
                    in0 = 1;
                }

                if (*input == invalidJobs->invJobs[j]) {
                    in1 = 1;
                }
            }

            if (!in0 && *input != -1) {
                pipes[i].error = 1;

                invalidJobs->invJobs = (int*)realloc(invalidJobs->invJobs, 
                        (invalidJobs->invjobCount + 1) * sizeof(int));

                int pipesInput = pipes[i].input;
                invalidJobs->invJobs[invalidJobs->invjobCount] = pipesInput;
                (invalidJobs->invjobCount)++;
            }
            if (!in1 && *input != -1) {
                pipes[i].error = 1;

                invalidJobs->invJobs = (int*)realloc(invalidJobs->invJobs, 
                        (invalidJobs->invjobCount + 1) * sizeof(int));

                invalidJobs->invJobs[invalidJobs->invjobCount] = *input;
                (invalidJobs->invjobCount)++;
            }
        }
    }
}

// checks that specified pipes have only one output
// takes the current line, output status, existing pipe status as parameters
// also takes the pipes and invalid jobs data structure, pipes and jobs count
void check_pipe_output(char** line, int* output, int* existingPipe2, 
        Pipe* pipes, InvalidJobs* invalidJobs, int* pipeCount, int* jobCount) {
    int out0, out1;

    for (int i = 0; i < (*pipeCount) - 1; i++) {
        out0 = 0;
        out1 = 0;
        if (strcmp(pipes[i].name, line[1]) == 0) {
            *output = pipes[i].output;
            (*existingPipe2)++;
            pipes[i].output = (*jobCount);

            for (int j = 0; j < invalidJobs->invjobCount; j++) {

                if (pipes[i].output == invalidJobs->invJobs[j]) {
                    out0 = 1;
                }

                if (*output == invalidJobs->invJobs[j]) {
                    out1 = 1;
                }
            }

            if (!out0 && *output != -1) {
                pipes[i].error = 1;

                invalidJobs->invJobs = (int*)realloc(invalidJobs->invJobs, 
                        (invalidJobs->invjobCount + 1) * sizeof(int));

                int pipesOutput = pipes[i].output;
                invalidJobs->invJobs[invalidJobs->invjobCount] = pipesOutput;
                (invalidJobs->invjobCount)++;
            }

            if (!out1 && *output != -1) {
                pipes[i].error = 1;

                invalidJobs->invJobs = (int*)realloc(invalidJobs->invJobs, 
                        (invalidJobs->invjobCount + 1) * sizeof(int));

                invalidJobs->invJobs[invalidJobs->invjobCount] = *output;
                (invalidJobs->invjobCount)++;
            }
        }
    }
}

// checks for invalid pipes that are missing a read or write end
// takes an array of the "Pipe" data struct and num of pipes as parameters
void check_invalid_pipe(Pipe* pipes, int pipeCount, int* validPipes, 
        int* jobCount, InvalidJobs* invalidJobs) {

    char* invPipeMsg1 = "Invalid pipe usage \"";
    char* invPipeMsg2 = "\"";
    int in, out;

    for (int i = 0; i < pipeCount - 1; i++) {
        in = 0;
        out = 0;
        if (pipes[i].input == -1 || pipes[i].output == -1 ||
                pipes[i].error == 1) {

            for (int j = 0; j < invalidJobs->invjobCount; j++) {

                if (pipes[i].input == invalidJobs->invJobs[j]) {
                    in = 1;
                }

                if (pipes[i].output == invalidJobs->invJobs[j]) {
                    out = 1;
                }
            }

            int input = pipes[i].input;
            int output = pipes[i].output;
            if (!in && input != -1) {

                invalidJobs->invJobs = (int*)realloc(invalidJobs->invJobs, 
                        (invalidJobs->invjobCount + 1) * sizeof(int));

                invalidJobs->invJobs[invalidJobs->invjobCount] = input;
                (invalidJobs->invjobCount)++;
            }
            if (!out && output != -1) {

                invalidJobs->invJobs = (int*)realloc(invalidJobs->invJobs, 
                        (invalidJobs->invjobCount + 1) * sizeof(int));

                invalidJobs->invJobs[invalidJobs->invjobCount] = output;
                (invalidJobs->invjobCount)++;
            }

            fprintf(stderr, "%s%s%s\n", invPipeMsg1, pipes[i].name + 1, 
                    invPipeMsg2);

            pipes[i].error = 1;

        } else {
            (*validPipes)++;
        }
    }
}

// prints the invalid line message
// takes the filename, takes the line number named "count" as parameters
void invalid_line(int count, char* filename) {

    char* invLineMsg1 = "jobrunner: invalid job specification on line ";
    char* invLineMsg2 = " of \"";
    char* invLineMsg3 = "\"";

    fprintf(stderr, "%s%d%s%s%s\n", invLineMsg1, count, invLineMsg2, filename, 
            invLineMsg3);
}

// prints an "invalid read" message to stderr
// takes the filename of the unreadable file as a parameter
void invalid_read(char* filename) {
    char* invReadMsg1 = "Unable to open \"";
    char* invReadMsg2 = "\" for reading";
    fprintf(stderr, "%s%s%s\n", invReadMsg1, filename, invReadMsg2);
}

// prints an "invalid write" message to stderr
// takes the filename of the unwritable file as a parameter
void invalid_write(char* filename) {
    char* invWriteMsg1 = "Unable to open \"";
    char* invWriteMsg2 = "\" for writing";
    fprintf(stderr, "%s%s%s\n", invWriteMsg1, filename, invWriteMsg2);
}

// prints jobfile specs to stderr if -v is specified on the command line
// takes the filename and the InvalidJobs data struct as parameters
// also takes the timeouts array as a parameter
void verbose_mode(char* filename, InvalidJobs* invalidJobs, Timeouts* timeouts,
        int verbose, int* timeCount, int* invalidCount) {

    FILE* file = fopen(filename, "r");
    char* line;
    char** lineSplit;
    int pass;

    while ((line = read_line(file)) != NULL) {
        pass = 0;

        if (strlen(line) == 0 || isspace((int)line[0]) != 0) {
            ;
        } else if (line[0] != '#') {

            for (int i = 0; i < invalidJobs->invjobCount; i++) {
                if (*timeCount == invalidJobs->invJobs[i]) {
                    pass = 1;
                    (*invalidCount)++;
                }
            }

            if (!pass) {
                int verLen = strlen(line) + 1;
                char* verLine = (char*)malloc(verLen * sizeof(char));
                strcpy(verLine, line);
                lineSplit = split_by_commas(line);

                for (int i = 0; i < verLen; i++) {
                    if (verLine[i] == ',') {
                        verLine[i] = ':';
                    }
                }

                int time = 0, count = 0;
                for (int i = 0; lineSplit[i] != NULL; i++) {
                    if (i == 3) {
                        time = check_timeout(lineSplit);
                    }
                    count = i;
                }

                timeouts->times[*(timeCount) - (*invalidCount) - 1] = time;
                if (verbose) {
                    verLine = insert_time(verLine, time, count);
                    fprintf(stderr, "%d:%s\n", *timeCount, verLine);
                    fflush(stderr);
                }

                free(lineSplit);
                free(verLine);
            }
            (*timeCount)++;
        }
        free(line);
    }
    fclose(file);
}

// inserts timeout value to job table for verbose mode
// taked the line and the corresponding timeout value as parameters
// returns an potentially reallocated char pointer for the line
char* insert_time(char* line, int time, int index) {
    int len = strlen(line) + 1, count = 0;

    if (line[len - 1] != ':' && index < 3) {
        line = (char*)realloc(line, (len + 1) * sizeof(char));
        strcat(line, ":");
        len++;
    }

    for (int i = 1; i < len; i++) {
        if ((line[i] == line[i - 1] && line[i] == ':') || 
                (line[i] == '\0' && line[i - 1] == ':')) {
            count = i;
            break;
        }
    }

    if (!count) {
        return line;

    } else {
        char* temp = (char*)malloc((len + 1) * sizeof(char));
        strncpy(temp, line, count);

        temp[count] = time + '0';
        strcpy(temp + count + 1, line + count);

        line = (char*)realloc(line, (len + 1) * sizeof(char));
        strcpy(line, temp);

        free(temp);
    }
    return line;
}

// checks for no jobs specified by the job files
// takes the number of jobs named "count" as a parameter
// exits with status 4 there are no jobs
void check_jobs(int count) {
    char* noJobsMsg = "jobrunner: no runnable jobs";

    if (!(count > 0)) {
        fprintf(stderr, "%s\n", noJobsMsg);
        exit(4);
    }
}

// creates pipes for child processes
// takes number of pipes, "Pipe*" struct, number of jobs as parameters
// also takes a 2D int arr of pipe file descriptors as a parameter
void create_pipes(int validPipes, FdPipes* fdPipes) {
    for (int i = 0; i < validPipes; i++) {

        if (pipe(fdPipes->fd[i]) == -1) {
            fprintf(stderr, "pipe error\n");
        }

    }
}

// reassigns the pipe numbers, disregards invalid pipes
// takes an array of Pipe data structs and total number of pipes as parameters
void reassign_pipe_numbers(Pipe* pipes, int* pipeCount) {
    int count = 0;
    for (int i = 0; i < *pipeCount - 1; i++) {

        if (pipes[i].error) {
            pipes[i].number = -1;
        } else {
            pipes[i].number = count;
            count++;
        }

    }
}

// calls helper functions to set up pipes before processes are created
int* setup_processes(int execCount, int* validPipes, FdPipes* fdPipes, 
        Pipe* pipes, int* pipeCount, int count, int argc, char** argv, 
        int* jobNumber, InvalidJobs* invalidJobs, pid_t* pids, 
        Timeouts* timeouts, time_t* times, int* validJobNumbers, 
        int* execNumber, int* execFail) {

    if (execCount > 0) {
        create_pipes(*validPipes, fdPipes);
        reassign_pipe_numbers(pipes, pipeCount);

        for (int i = count; i < argc; i++) {

            validJobNumbers = create_process(argv[i], pipes, fdPipes, 
                    pipeCount, validPipes, jobNumber, invalidJobs, pids, 
                    timeouts, times, validJobNumbers, execNumber, execFail);

            if (*execFail) {
                break;
            }
        }
    }
    return validJobNumbers;
}

// creates a process to be executed
// takes a file as "filename" to read a praameter
// also takes the number of pipes and the array of "Pipe" data structs
// calls the exec_job() function with executes a specified program
int* create_process(char* filename, Pipe* pipes, FdPipes* fdPipes, 
        int* pipeCount, int* validPipes, int* jobNumber, 
        InvalidJobs* invalidJobs, pid_t* pids, Timeouts* timeouts, 
        time_t* times, int* validJobNumbers, int* execNumber, int* execFail) {

    FILE* file = fopen(filename, "r");
    char* line;
    char** lineSplit;
    int invalidJob;

    while ((line = read_line(file)) != NULL) {
        invalidJob = 0;

        if (strlen(line) == 0 || isspace(line[0]) != 0) {
            ;
        } else if (line[0] != '#') {

            for (int i = 0; i < invalidJobs->invjobCount; i++) {
                if (invalidJobs->invJobs[i] == (*jobNumber) + 1) {
                    invalidJob = 1;
                }
            }

            if (invalidJob) {
                (*jobNumber)++;
                free(line);
                continue;
            }

            lineSplit = split_by_commas(line);
            times[*execNumber] = time(NULL);
            pid_t id = fork();

            if (id == -1) {
                ;
            } else if (id == 0) {
                *execFail = exec_job(lineSplit, pipes, fdPipes, pipeCount,
                        *validPipes);
            }

            free(lineSplit);
            pids[*execNumber] = id;
            validJobNumbers = (int*)realloc(validJobNumbers, 
                    (*execNumber + 1) * sizeof(int));

            validJobNumbers[*execNumber] = *jobNumber + 1;
            (*jobNumber)++;
            (*execNumber)++;
        }
        free(line);
        if (*execFail) {
            break;
        }
    }
    fclose(file);
    return validJobNumbers;
}

// waits for all child processes using paitpid()
// takes number of jobs, InvalidJobs data struct and pids array as parameters
void wait_for_process(pid_t* pids, Timeouts* timeouts, int execCount, 
        int* validJobNumbers, time_t* times, FdPipes* fdPipes, 
        int validPipes) {

    prepare_for_wait(fdPipes, validPipes);
    int count = 0;
    int* completedJobs = (int*)malloc(execCount * sizeof(int));

    while (1) {
        for (int i = 0; i < execCount; i++) {
            int status, aborted = 0, skip = 0, completed = 0;
            time_t start = time(NULL);

            for (int j = 0; j < count; j++) {
                if (completedJobs[j] == i) {
                    completed = 1;
                    break;
                }
            }

            if (completed) {
                continue;
            }

            while (waitpid(pids[i], &status, WNOHANG) != -1) {

                if (sighup) {
                    for (int j = 0; j < execCount; j++) {
                        kill(pids[j], SIGKILL);
                    }
                }

                double elapsed = difftime(time(NULL), times[i]);
                if (elapsed > timeouts->times[i] && timeouts->times[i] != 0) {
                    kill(pids[i], SIGABRT);
                    if (aborted) {
                        kill(pids[i], SIGKILL);
                    }
                    sleep(1);
                    aborted = 1;
                }

                if (aborted) {
                    ;
                } else if (difftime(time(NULL), start) > 0.01) {
                    skip = 1;
                    break;
                }
            }

            if (skip) {
                continue;
            }
            exit_status(validJobNumbers, i, status, completedJobs, &count);
        }

        if (count == execCount) {
            break;
        }
    }
    free(completedJobs);
}

// prepares for wait pid by flushing stdout and stderr
// closes all pipe file descriptors in the parent process
void prepare_for_wait(FdPipes* fdPipes, int validPipes) {
    fflush(stdout);
    fflush(stderr);
    close_pipe_fds(fdPipes, validPipes);
    sleep(1);
}

// prints the exit status information of finished jobs to stderr
// takes the array of valid jobs, index for that array and status as parameters
void exit_status(int* validJobNumbers, int index, int status, 
        int* completedJobs, int* count) {

    if (WIFEXITED(status)) {
        int exitStatus = WEXITSTATUS(status);
        fprintf(stderr, "Job %d exited with status %d\n", 
                validJobNumbers[index], exitStatus);

    } else if (WIFSIGNALED(status)) {
        int termStatus = WTERMSIG(status);
        fprintf(stderr, "Job %d terminated with signal %d\n", 
                validJobNumbers[index], termStatus);
    }

    completedJobs[*count] = index;
    (*count)++;
}

// executes a program specified in the job files
// takes a line from a jobfile as a parameter
// calls the execvp() function to execute a specified program
int exec_job(char** line, Pipe* pipes, FdPipes* fdPipes, int* pipeCount, 
        int validPipes) {
    int argCount = 0, execFail = 0;
    char** args = (char**)malloc(0);

    for (int i = 0; line[i] != NULL; i++) {
        if (i == 0 || i > 3) {
            args = (char**)realloc(args, (argCount + 1) * sizeof(char*));

            int count;
            for (count = 1; line[i][count - 1] != '\0'; count++) {
                ;
            }

            args[argCount] = (char*)malloc(count * sizeof(char));
            strcpy(args[argCount], line[i]);
            argCount++;

        } else if (i == 1) {

            if (line[i][0] == '@') {
                assign_pipes(line[i], pipes, fdPipes, pipeCount, i);
            } else if (strcmp(line[i], "-") != 0) {
                int fd0 = open(line[i], O_RDONLY);
                dup2(fd0, 0);
                close(fd0);
            }

        } else if (i == 2) {

            if (line[i][0] == '@') {
                assign_pipes(line[i], pipes, fdPipes, pipeCount, i);
            } else if (strcmp(line[i], "-") != 0) {
                int fd1 = open(line[i], O_WRONLY | O_CREAT | O_TRUNC, 
                        S_IRWXU | S_IRGRP);
                dup2(fd1, 1);
                close(fd1);
            }

        }
    }

    close_pipe_fds(fdPipes, validPipes);
    args = (char**)realloc(args, (argCount + 1) * sizeof(char*));
    args[argCount] = NULL;

    int fdError = open("/dev/null", O_WRONLY);
    dup2(fdError, 2);
    close(fdError);

    if (execvp(args[0], args) == -1) {
        execFail = 1;
    }

    for (int i = 0; i < argCount; i++) {
        free(args[i]);
    }
    free(args);

    if (execFail) {
        return 255;
    }

    return 0;
}

// closes all pipe file dsescriptors
// takes in the array of pipe file descriptors and the number of valid pipes
void close_pipe_fds(FdPipes* fdPipes, int validPipes) {
    for (int i = 0; i < validPipes; i++) {
        close(fdPipes->fd[i][0]);
        close(fdPipes->fd[i][1]);
    }
}

// assigns pipe end to corresponding job stdin or stdout
// takes tha array of Pipe data structs and pipe name as parameters
// also takes the pipes file descriptors and the number of valid pipes
void assign_pipes(char* name, Pipe* pipes, FdPipes* fdPipes, int* pipeCount, 
        int index) {

    for (int i = 0; i < (*pipeCount) - 1; i++) {
        if (strcmp(name, pipes[i].name) == 0) {

            if (pipes[i].number == -1 || pipes[i].error == 1) {
                ;
            } else if (index == 1) {
                dup2(fdPipes->fd[pipes[i].number][0], 0);
            } else if (index == 2) {
                dup2(fdPipes->fd[pipes[i].number][1], 1);
            }

        }
    }
}

// frees all manually allocated memory
void free_alloc_mem(int pipeCount, int validPipes, Pipe* pipes, 
        FdPipes* fdPipes, pid_t* pids, InvalidJobs* invalidJobs, 
        Timeouts* timeouts, time_t* times, int* validJobNumbers) {

    for (int i = 0; i < pipeCount - 1; i++) {
        free(pipes[i].name);
    }

    free(pipes);
    for (int i = 0; i < validPipes; i++) {
        free(fdPipes->fd[i]);
    }

    free(fdPipes->fd);
    free(pids);
    free(invalidJobs->invJobs);
    free(timeouts->times);
    free(times);
    free(validJobNumbers);
}