/***** INCLUDES *****/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>

/***** MACROS *****/
#define MAX_ARGS 24
#define MANDATORY_NEEDED 3
#define VERBOSE_FALSE 0
#define VERBOSE_TRUE 1

#define OUT_PIPE_FD_DEFAULT 0
#define IN_PIPE_FD_DEFAULT 0

#define PIPE_CHAR '@'
#define PIPE_READ 0
#define PIPE_WRITE 1

#define SIGHUP_NOT_SENT 0
#define SIGHUP_SENT 1

/***** Structures *****/
/*
    Structure storing the file count, 
    and the char* array of all the
    file names from the command line
*/
typedef struct FileList_t {

    /* Keep Track of file Count*/
    int filecount;

    /* Array of file names */
    char** filenames;

} FileList;

/*
    Struct storing the file streams of
    all valid files
*/
typedef struct FileStreams_t {
    /* An array of File Pointers */
    FILE** streams;

    /* Number of file pointers we have */
    int numStreams;

    /* Array of file names (must match dim of numStreams) */
    char** filenames;
} FileStreams;

/*
    Structure that stores the command line
    data, such as verbose flag
    and file list data
*/
typedef struct Arguments_t{

    /* Verbose Flag */
    int verboseflag;

    /* File List structure instance */
    FileList filelist;

} Arguments;

/*
    Structure that contains job line data
    for each job given in a file
*/
typedef struct JobLineData_t {
    /* Program Name for Job*/
    char* program;

    /* Program Standard Input File */
    char* file_stdin;

    /* Program Standard Output File */
    char* file_stdout;

    /* Program Timeout (s)*/
    int timeout;

    /* String array of additional args */
    char** extra_args;

    /* Number of additional arguments */
    int num_extra_args;

    /* Input File Descriptor */
    int fdInput;

    /* Output File Descriptor */
    int fdOutput;

    /* Runnable Boolean (for validity) */
    bool runnable;

    /* Process ID (from forking)*/
    pid_t pId;

    /* Process Status */
    int status;

    /* 
        Timer Start/ End Variables
        for timeout/ sig handling
    */
    time_t tStart;
    time_t tEnd;

    /* Job Number */
    int job_number;
} JobLineData;

/*
    Structure to store all JobLine 
    Structures and a count of them
*/
typedef struct AllJobData_t { 

    /* Pointer to the JobLineData Struct*/
    JobLineData* joblinedata;

    /* Total number of jobs*/
    int totaljobs;
} AllJobData;

/* Hash Table Item for key value pairs*/
typedef struct HashTableItem_t {

    /* Character key (pipe name) */
    char* key;

    /* Int Value occurence count */
    int value;
} HashTableItem;

/* Hash Table Item for key value pairs*/
typedef struct HashTable_t {

    /* Pointer to HashTableItem struct */
    HashTableItem* items;

    /* Count of Items in Hash Table */
    int itemcount;

    /* Current Hash Table Capacity*/
    int capacity;
} HashTable;

/* Pipe Data Structure */
typedef struct PipeData_t {

    /* Name of the pipe (@alice)*/
    char* pipeName;

    /* Boolean to indicate if it is runnable */
    bool isRunnable;

    /* Int array containing both file descriptors */
    int pipeFd[2];
} PipeData;

/* PipeList Structure */
typedef struct PipeList_t {

    /* Count of pipes in the list */
    int pipecount;

    /* A pointer to a PipeData Struct*/
    PipeData* pipedata;
} PipeList;

/* Global Signal State variable (default) */
int signal_state = SIGHUP_NOT_SENT;

/************* FUNC PROTOTYPES *************/
char* read_line(FILE* stream);
char** split_by_commas(char* jobLine);
void extract_files_for_reading(int argc, char** argv, FileList* filelist);
void process_command_line(int argc, char** argv, Arguments* arguments);
FileStreams* open_files_for_reading(Arguments* arguments);
AllJobData* process_job_lines(FileStreams* filestreams);
void initialise_jobline(JobLineData* jobline);
void assign_job_line_data(JobLineData* jobline, char** args_in, char* filename, int linenum, int jobnumber);
void check_for_job_line_errors(char* joblinestr);
void close_all_files(FileStreams* filestreams);
void open_job_output_writing(JobLineData* jobline);
void open_job_input_reading(JobLineData* jobline);
void add_item_to_ht(HashTable* hashtable, char* key);
HashTable* initialise_hash_table(void);
int search_for_key_ht(HashTable* hashtable, char* key);
void increment_value(HashTable* hashtable, char* key);
void process_jobs_and_hash(HashTable* inputHashTable, HashTable* outputHashTable, AllJobData* alljobs);
PipeList* validate_pipes(HashTable* inputHashTable, HashTable* outputHashTable);
void check_pipes(HashTable* inputHashTable, HashTable* outputHashTable);
void create_and_assign_pipes(PipeList* pipelist, AllJobData* alljobs);
void redirect_input_and_output(JobLineData* job);
void run_all_jobs(AllJobData* alljobs);
void create_processes_and_run_job(JobLineData* job);
void close_unused_pipe_ends(JobLineData* job);
void initialise_signal_handler(void);
void sighup_handler(int signal);
void kill_processes_on_sighup(AllJobData* alljobs); 
void monitor_verbose_data(AllJobData* alljobs, Arguments* args);
void wait_for_processes_and_timeouts(AllJobData* alljobs);
void process_timeouts(AllJobData* alljobs);
/*******************************************/

/*
    main()
    This is the main loop processing function
*/
int main(int argc, char** argv) {

    /* Initialise Signal Handler ofr SIGHUP */
    initialise_signal_handler();

    /* Initialise Arguments as 1D Pointer to Args struct*/
    Arguments* arguments = (Arguments*)malloc(sizeof(Arguments) * 1);

    /* Check command line validity, and extract command line data */
    process_command_line(argc, argv, arguments);

    /* Create file streams */
    FileStreams*  filestreams = open_files_for_reading(arguments);

    /* initialise All Job Data Structure */
    AllJobData* alljobs = process_job_lines(filestreams);
    
    /* Process Verbose */
    monitor_verbose_data(alljobs, arguments);

    /* Initialise HashTable to store input and output references of pipes */
    HashTable* inputPipeHashTable = initialise_hash_table();
    HashTable* outputPipeHashTable = initialise_hash_table();
    
    /* Add data to HashTable */
    process_jobs_and_hash(inputPipeHashTable, outputPipeHashTable, alljobs);

    /* Check pipe errors, and return a list of valid pipes*/
    PipeList* pipelist = validate_pipes(inputPipeHashTable, outputPipeHashTable);

    /* Creating pipe file descriptors, and assigning them to the jobs */
    create_and_assign_pipes(pipelist, alljobs);

    /* Execute all jobs */
    run_all_jobs(alljobs);

    /* Wait for child processes to end/ be killed, and process timeouts*/
    // wait_for_processes_and_timeouts(alljobs);

    /* Close all file streams After reading all data*/
    close_all_files(filestreams);

    /* Free data structures */
    free(arguments); free(filestreams); free(alljobs); free(pipelist);
    free(inputPipeHashTable); free(outputPipeHashTable);

    return 0;
}

/*
  process_command_line():
  Function that processes the command line for the arguments
  and the necessary data (files etc)

  @param: int argc: The number of arguments
  @param: char** argv: The array of arguments
  @param: Arguments*L An arguments pointer to an args struct
  containing all info needed for the program to progress

  @retval None
*/
void process_command_line(int argc, char** argv, Arguments* arguments) {

    /* Initialise FileList as 1D Pointer to FileList struct*/
    FileList* filelist = (FileList*)malloc(sizeof(FileList) * 1);

    /* Extract File List data from command line first */
    extract_files_for_reading(argc, argv, filelist);

    /* 
        Updating the filelist struct member of the
        arguments structure, by dereferencing the
        filelist structure (to access the value)
    */
    arguments->filelist = *filelist;

    /* Skip over executable name ./a.out*/
    argv++; argc--;

    /* Check if there is zero arguments after function name */
    if(argc == 0) {
        fprintf(stderr, "Usage: jobrunner [-v] jobfile [jobfile ...]\n");
        exit(1);
    }

    /* Check if next argument contains a verbose flag */
    if(strcmp(argv[0], "-v") == 0) {
        arguments->verboseflag = VERBOSE_TRUE;

        /* Navigate across the arguments */
        argv++; argc--;
    } else {
        arguments->verboseflag = VERBOSE_FALSE;
    }

    /* Check if there is zero arguments after verbose */
    if(argc == 0) {
        fprintf(stderr, "Usage: jobrunner [-v] jobfile [jobfile ...]\n");
        exit(1);
    }

    /* Check for excess verbose flags after initial position argc = 1*/
    for(int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "-v") == 0) {
            fprintf(stderr, "Usage: jobrunner [-v] jobfile [jobfile ...]\n");
            exit(1);
        }
    }
    /* Free File List and its contents */
    free(filelist);
}

/*
    open_files_for_reading():
    Function that takes in the arguments found from the command line
    and processes every file, by opening it for reading, upon success
    it is assed to the Array of file pointers, otherwise an error
    is invoked and the program is exited with error status 2
*/
FileStreams* open_files_for_reading(Arguments* arguments) {

    /* Remember to free this elsewhere */
    FileStreams* filestreams = (FileStreams*)malloc(sizeof(FileStreams) * 1);

    /* Assign struct members (allocate as needed) */
    filestreams->numStreams = arguments->filelist.filecount;
    filestreams->streams = (FILE**)malloc(filestreams->numStreams * sizeof(FILE*));
    filestreams->filenames= (char**)malloc(filestreams->numStreams * sizeof(char*));

    /* Now open every file, and add the stream to the array of stream pointers */
    for(int i = 0; i < arguments->filelist.filecount; i++) {
        FILE* current_file = fopen(arguments->filelist.filenames[i], "r");

        if(current_file == NULL) {
            fprintf(
                stderr,
                "jobrunner: file \"%s\" can not be opened\n",
                arguments->filelist.filenames[i]\
            );
            exit(2);
        }
        filestreams->streams[i] = current_file;
        filestreams->filenames[i] = arguments->filelist.filenames[i];
    }
    return filestreams;
}

AllJobData* process_job_lines(FileStreams* filestreams) {

    /* Initialize AllJobData struct pointer to store info */
    AllJobData* alljobdata = (AllJobData*)malloc(sizeof(AllJobData));

    /* Initialize Job Line Data Struct */
    alljobdata->joblinedata = NULL;
    alljobdata->totaljobs = 0;

    /* Initialize Line Num to 1 */
    int job_number = 1;
    int linenum = 1;

    /* Read every file, and every line of every file */
    for (int i = 0; i < filestreams->numStreams; i++) {
        /* Initialize char* word to store file line data */
        char* word;

        while ((word = read_line(filestreams->streams[i])) != NULL) {
            /* Comment found, continue to next iteration */
            if(word[0] == '#') {
                continue;
            }

            /* Split the line by commas, to access elements */
            char** line_data_arr = split_by_commas(word);

            /* Create a new JobLineData struct for this line */
            JobLineData* linedata = (JobLineData*)malloc(sizeof(JobLineData));

            /* Assign members & check for issues*/
            assign_job_line_data(linedata, line_data_arr, filestreams->filenames[i], linenum, job_number);

            /* Increment Total Jobs by 1 */
            alljobdata->totaljobs++;

            /* Increase space in struct member by 1 */
            alljobdata->joblinedata = (JobLineData*)realloc(
                alljobdata->joblinedata,
                sizeof(JobLineData) * alljobdata->totaljobs
            );

            /* Set the joblinedata at a given index to the new linedata struct */
            alljobdata->joblinedata[alljobdata->totaljobs - 1] = *linedata;

            /* Increment Line Number & Job Number */
            job_number++;
            linenum++;

            /* Free allocated memory for line_data_arr and linedata */
            free(line_data_arr);
            free(linedata);
        }
        /* Reset Line Number */
        linenum = 1;
    }
    return alljobdata;
}

/*
    initialise_jobline(JoblineData* jobline)
*/

void initialise_jobline(JobLineData* jobline) {
    /*
        Initialise Job File data
    */
    jobline->program = NULL;
    jobline->file_stdin = NULL;
    jobline->file_stdout = NULL;
    jobline->timeout = 0;
    jobline->extra_args = NULL;
    jobline->num_extra_args = 0;
    jobline->runnable = false;
    jobline->fdInput = 0;
    jobline->fdOutput = 0;
    jobline->pId = -1;
    jobline->tStart = 0;
    jobline->tEnd = 0;
}

/*
    assign_job_line_data():
    Function that assigns the jobline data at default
    then further updates struct members as they are
    discovered, an error occurs when an invalid job line
    is discovered (insufficient mandatory members) or
    a timeout integer less than zero

    @param JobLineData*: JobLineData struct pointer
    to the struct we wish to update
    @param char**: A Char* array of elements after
    comma seperating
*/
void assign_job_line_data(JobLineData* jobline, char** args_in, char* filename, int linenum, int jobnumber) {

    int mandatory_count = 0;
    bool timeoutValid = true;

    /* Initialise Job Line Data */
    initialise_jobline(jobline);

    /* Set Job Number */
    jobline->job_number = jobnumber;

    /* Iterate arguments to access members */
    for (int index = 0; args_in[index] != NULL; index++) {
        if (index == 0) {
            /* Update program name, and increment mandatory count*/
            jobline->program = strdup(args_in[index]);
            mandatory_count++;
        } else if (index == 1) {
            /* Update standard in, and increment mandatory count*/
            jobline->file_stdin = strdup(args_in[index]);
            mandatory_count++;
        } else if (index == 2) {
            /* Update standard out, and increment mandatory count*/
            jobline->file_stdout = strdup(args_in[index]);
            mandatory_count++;

        /*  If the 3rd index is a number, set new timeout, else ignore*/
        } else if ((index == 3) && (atoi(args_in[index]))) {

            int timeout = atoi(args_in[index]);
            /* Check timeout is valid*/
            if(timeout < 0) {
                timeoutValid = false;
            }
            jobline->timeout = timeout;

        /* Additional arguments proceed the timeout*/
        } else {
            // Resize the extra_args array
            jobline->extra_args = (char**)realloc(jobline->extra_args, sizeof(char*) * (jobline->num_extra_args + 1));

            // Duplicate and store the new argument
            jobline->extra_args[jobline->num_extra_args] = strdup(args_in[index]);

            // Increment the number of extra_args
            jobline->num_extra_args++;
        }
    }
    /* Open File for Reading */
    open_job_input_reading(jobline);

    /* Open File for Writing */
    open_job_output_writing(jobline);


    /* Check validity after processing mandatory components*/
    if((!timeoutValid) || (mandatory_count != MANDATORY_NEEDED)) {
        fprintf(
            stderr, 
            "jobrunner: invalid job specification on line %d of \"%s\"\n", 
            linenum,
            filename
        );
        free(jobline);
        exit(3);
    }
}

/*
    extract_files_for_reading():
    Function that extracts all files from command line
    irrespective of their validity.

    @param: int argc: Number of elements in argv
    @param char* argv: An array of arguments
    @param FileList*: A Pointer to a FileList

    @retval None
*/
void extract_files_for_reading(int argc, char** argv, FileList* filelist) {
    /* Skip ./a.out executable name */
    argv++; argc--;

    /* File list index counter */
    int fileCount = 0;

    // Allocate memory for the filenames array
    filelist->filenames = (char**)malloc(sizeof(char*) * argc);

    for (int i = 0; i < argc; i++) {
        /* Ignore verbose flag (for obvious reasons) */
        if (strcmp(argv[i], "-v") == 0) {
            continue;
        }

        /* Allocate memory for the filename */
        filelist->filenames[fileCount] = strdup(argv[i]);

        /* Increment File Count, and assign new count */
        filelist->filecount = ++fileCount;
    }
}

/*
    split_by_commas():
    Function that splits a string (char*) by commas and
    returns an array of strings after the split

    @param: char* line: A Line we wish to split by commas
    @retval: char**: An array of strings after splitting
    by comams
*/
char** split_by_commas(char* line) {
    char** args = (char**)malloc(sizeof(char*) * MAX_ARGS);

    const char* delimiter = ",";
    char* token = strtok(line, delimiter);
    int argIndex = 0;

    while (token != NULL && argIndex < MAX_ARGS - 1) {
        args[argIndex++] = strdup(token);
        token = strtok(NULL, delimiter);
    }

    args[argIndex] = NULL;  // Null-terminate the array
    return args;
}

/*
    read_line():
    Function that takes in a file stream, and reads the contents
    line by line,  it removes the newline character at termination
    and returns the string (char*) read from a file

    @param: FILE*: A file stream of an opened file
    @retval char*: A String read from that file
*/
char* read_line(FILE* stream) {
    char* line = NULL;
    size_t len = 0;
    size_t read;
    if ((read = getline(&line, &len, stream)) != -1) {
        if (line[read - 1] == '\n') {
            line[read - 1] = '\0';  // Remove the newline character
        }
        return line;
    }
    return NULL;
}

/*
    close_all_files()
*/
void close_all_files(FileStreams* filestreams) {
    
    /* Close all files */
    for (int i = 0; i < filestreams->numStreams; i++) {
        fclose(filestreams->streams[i]);
    }
}

/*
    open_job_input_reading():
    Function that obtains a file descriptor based on
    the input file, it checks if the file is STDIN, 
    or a pipe, else it opens the file and assigns a fd

    @params JobLineData*: A Pointer to a JobLineData struct
    that stores the given files input_file path

    @retval int: The input file descriptor for the specified file
*/
void open_job_input_reading(JobLineData* jobline) {

    /* Store the jobline file standard input */
    char* input_file = jobline->file_stdin;

    /* File Descriptor */
    int inputFd;

    /* Check if the stdin is a pipe */
    if (input_file[0] == '@') {
        inputFd = IN_PIPE_FD_DEFAULT;
    } else if (input_file[0] == '-') {
        inputFd = STDIN_FILENO;
    } else {
        /* Try open file for reading with given permissions and obtain a file descriptor */
        inputFd = open(input_file, O_RDONLY);

        if (inputFd < 0 || access(input_file, R_OK) != 0) {
            fprintf(stderr, "Unable to open \"%s\" for reading\n", input_file);
            inputFd =  -1; // Return an error code
        }
    }
    jobline->fdInput = inputFd;
}

/*
    open_job_output_writing():
    Function that obtains a file descriptor based on
    the input file, it checks if the file is STDOUT, 
    or a pipe, else it opens the file and assigns a fd

    @params JobLineData*: A Pointer to a JobLineData struct
    that stores the given files output_file path

    @retval int: The output file descriptor for the specified file
*/
void open_job_output_writing(JobLineData* jobline) {

    /* Store the jobline file standard output */
    char* output_file = jobline->file_stdout;

    /* File Descriptor (assigned based on given file) */
    int outputFd;

    /* Check if the stdout is a pipe or it's STDOUT */
    if (output_file[0] == '@') {
        outputFd = OUT_PIPE_FD_DEFAULT;

    } else if (output_file[0] == '-') {
        outputFd = STDOUT_FILENO;

    } else {
        /* Try open file for writing with given permissions and obtain a file descriptor */
        outputFd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);

        if (outputFd < 0 || access(output_file, W_OK) != 0) {
            fprintf(stderr, "Unable to open \"%s\" for writing\n", output_file);
            outputFd = -1; // Return an error code
        }
    }
    jobline->fdOutput = outputFd;
}

/*
    initialise_hash_table():
    Function that initialises a HashTable data structure

    @retval HashTable*: A pointer to a HT struct, that is
    pre-initialised
*/
HashTable* initialise_hash_table(void) {

    /* Allocate memory for the HashTable structure */
    HashTable* hashtable = (HashTable*)malloc(sizeof(HashTable));
    if (hashtable == NULL) {
        // Handle memory allocation error
        return NULL;
    }

    /* Initialise hashtable defaults */
    hashtable->itemcount = 0;
    hashtable->capacity = 10;
    hashtable->items = (HashTableItem*)malloc(sizeof(HashTableItem) * hashtable->capacity);

    if (hashtable->items == NULL) {

        /* Handle error in malloc */
        free(hashtable);
        return NULL;
    }
    return hashtable;
}

/*
    add_item_to_ht():
    Function that adds a key, value item to the hash table
    @param char* key: A String for the key (i.e. @pipe)
    @param int: Value (count of that key in the table)

    @retval None
*/
void add_item_to_ht(HashTable* hashtable, char* key) {

    /* Increase hash table capacity if exceeded! */
    if (hashtable->itemcount >= hashtable->capacity) {
        /* Double the capacity (you can choose a different resizing strategy) */
        hashtable->capacity *= 2;
        hashtable->items = (HashTableItem*)realloc(hashtable->items, sizeof(HashTableItem) * hashtable->capacity);

        if (hashtable->items == NULL) {
            /* Handle error in malloc */
            return;
        }
    }

    /* Create a new HashTableItem */
    HashTableItem* item = (HashTableItem*)malloc(sizeof(HashTableItem));

    /* Initialize the HashTableItem */
    item->key = strdup(key);
    item->value = 1;

    /* Add the item to the hash table */
    hashtable->items[hashtable->itemcount++] = *item;
}

/*
    search_for_key_ht():
    Function that checks if a key exists

    @retval HashTable*: A pointer to a HT struct being used
    @param char*: Key that we are searching for
*/
int search_for_key_ht(HashTable* hashtable, char* key) {

    for(int i = 0; i < hashtable->itemcount; i++) {
        
        /* Key Found */
        if(strcmp(hashtable->items[i].key, key) == 0) {
            return 1;
        }
    }
    return 0;
}

void increment_value(HashTable* hashtable, char* key) {
    for (int i = 0; i < hashtable->itemcount; i++) {
        if (strcmp(hashtable->items[i].key, key) == 0) {
            // Key found, increment the value
            hashtable->items[i].value++;
            return; // Exit the function after incrementing
        }
    }
    // Key not found, you can handle this case if needed
}

/*
    process_jobs_and_hash():
    Function that adds a new item to the respective
    hash table, whether it is input or output.
    This will add the "pipe" to the hashtable.
    If the pipe already exists, it will increment
    the count for that given key.

    @param HashTable*: A pointer to a input HT being used
    @param HashTable*: A pointer to a output HT being used
    @param AllJobData*: A pointer to all job data

    @retval None
*/
void process_jobs_and_hash(HashTable* inputHashTable, HashTable* outputHashTable, AllJobData* alljobs) {

    /* Process STDIN Pipes*/
    for (int i = 0; i < alljobs->totaljobs; i++) {
        char* pipeInName = strdup(alljobs->joblinedata[i].file_stdin);

        if (pipeInName[0] == PIPE_CHAR) {

            /* Item does not exist in map */
            if(!search_for_key_ht(inputHashTable, pipeInName)) {
                add_item_to_ht(inputHashTable, pipeInName);

            /* Item does exist in map, increase count */
            } else if(search_for_key_ht(inputHashTable, pipeInName)) {
                increment_value(inputHashTable, pipeInName);
            }
        }
    }

    /* Process STDOUT Pipes */
    for (int i = 0; i < alljobs->totaljobs; i++) {
        char* pipeOutName = strdup(alljobs->joblinedata[i].file_stdout);

        if (pipeOutName[0] == PIPE_CHAR) {
            
            /* Item does not exist in map */
            if(!search_for_key_ht(outputHashTable, pipeOutName)) {
                add_item_to_ht(outputHashTable, pipeOutName);

            /* Item does exist in map, increase count */
            } else if(search_for_key_ht(outputHashTable, pipeOutName)) {
                increment_value(outputHashTable, pipeOutName);
            }
        }
    }
}

/*
    validate_pipes():
    This function checks the pipes between the input and output
    hash tables, if there is references between both hash tables
    then, create a PipeData element, and append it to the PipeList
    This list will contain all valid pipes.

    @param HashTable*: A pointer to a input HT being used
    @param HashTable*: A pointer to a output HT being used

    @retval PipeList*: A list containing all pipe data
*/
PipeList* validate_pipes(HashTable* inputHashTable, HashTable* outputHashTable) {
    /* Allocate memory for pipe list, and then initialise defaults */
    PipeList* pipelist = (PipeList*)malloc(sizeof(PipeList));
    pipelist->pipedata = NULL;
    pipelist->pipecount = 0;


    /* Check for valid pipes first */
    check_pipes(inputHashTable, outputHashTable);

    /* Iterate across the input hash table */
    for (int i = 0; i < inputHashTable->itemcount; i++) {
        /* Allocate memory for each Pipe */
        PipeData* pipedata = (PipeData*)malloc(sizeof(PipeData)); 

        /* Set the pipe name as the key we are checking */
        pipedata->pipeName = strdup(inputHashTable->items[i].key);

        /* Pipe is invalid, as it only has one reference */
        if (!search_for_key_ht(outputHashTable, pipedata->pipeName)) {

            /* Invalid Pipe, free its memory*/
            free(pipedata);

        } else {
            pipedata->isRunnable = true;

            /* Reallocate memory for new pipe to be added */
            pipelist->pipedata = (PipeData*)realloc(pipelist->pipedata, sizeof(PipeData) * (pipelist->pipecount + 1));

            /* */
            pipelist->pipedata[pipelist->pipecount] = *pipedata;
            pipelist->pipecount++;
        }
    }
    return pipelist;
}

/*
    check_pipes()
    Function that outputs errors for offending pipes
    with only one reference
*/
void check_pipes(HashTable* inputHashTable, HashTable* outputHashTable) {
    for (int i = 0; i < inputHashTable->itemcount; i++) {
        if (!search_for_key_ht(outputHashTable, inputHashTable->items[i].key)) {
             fprintf(stderr, "Invalid pipe usage \"%s\"\n", inputHashTable->items[i].key);
        }
    }

    for (int i = 0; i < outputHashTable->itemcount; i++) {
        if (!search_for_key_ht(inputHashTable, outputHashTable->items[i].key)) {
            fprintf(stderr, "Invalid pipe usage \"%s\"\n", outputHashTable->items[i].key);
        }
    }
}

/*
    create_and_assign_pipes()
    Function that creates pipe file descriptors
    (i.e. int fd[2] r/w) for a given pipe, this means
    that we can then assign and dup the correct
    file descriptors as given by the program

    @param PipeList*: A pointer to the PipeList struct
    that contains data on the valid pipes
*/
void create_and_assign_pipes(PipeList* pipelist, AllJobData* alljobs) {
    /* Iterate across all valid pipes */
    for (int i = 0; i < pipelist->pipecount; i++) {
        /* Create a pipe file descriptor for the given pipe */
        int pipeFileDescriptors[2];

        /* Create the pipe and check for errors */
        if (pipe(pipeFileDescriptors) == -1) {
            perror("Pipe creation failed.");
            printf(" Offending Pipe: %s\n", pipelist->pipedata[i].pipeName);
            exit(69);
        }

        /* Assign the read and write file descriptors to PipeData */
        pipelist->pipedata[i].pipeFd[0] = pipeFileDescriptors[0]; /* Reading End */
        pipelist->pipedata[i].pipeFd[1] = pipeFileDescriptors[1]; /* Writing End */
    }

    /* Process the inputs, and assign the correct FD's */
    for (int i = 0; i < alljobs->totaljobs; i++) {

        /* We have found a valid pipe for input */
        if (alljobs->joblinedata[i].file_stdin[0] == PIPE_CHAR) {
            for (int j = 0; j < pipelist->pipecount; j++) {

                /* Pipe Match found, assign write file descriptor */
                if (strcmp(alljobs->joblinedata[i].file_stdin, pipelist->pipedata[j].pipeName) == 0) {
                    alljobs->joblinedata[i].fdInput = pipelist->pipedata[j].pipeFd[PIPE_READ];
                    alljobs->joblinedata[i].runnable = true;
                    break;
                }
            }
        }

        /* Nika 45*/

        /* We have found a valid pipe for output */
        if (alljobs->joblinedata[i].file_stdout[0] == PIPE_CHAR) {
            for (int j = 0; j < pipelist->pipecount; j++) {
                
                /* Pipe Match found, assign write file descriptor */
                if (strcmp(alljobs->joblinedata[i].file_stdout, pipelist->pipedata[j].pipeName) == 0) {
                    alljobs->joblinedata[i].fdOutput = pipelist->pipedata[j].pipeFd[PIPE_WRITE];
                    alljobs->joblinedata[i].runnable = true;
                    break;
                }
            }
        }
    }
}

/*
    redirect_stderr()

    Redirecting STDERR to /DEV/NULL
*/
void redirect_stderr() {

    /* Open the dev/null file as write only */
    int devNull = open("/dev/null", O_WRONLY);

    /* Redirect STDERR */
    dup2(devNull, STDERR_FILENO);

    /* Close the file descriptor for /dev/null */
    close(devNull);
}

/*
    redirect_input_and_output()
    Function that redirects input/ output of the
    file descriptors, and redirects stderr to dev/null
*/
void redirect_job_input_and_output(JobLineData* jobline) {

    /* redirect stderr to dev/null first */
    redirect_stderr();

    /* Redirect STDIN */
    if (jobline->fdInput != STDIN_FILENO) {

        /* Checking for errors in redirection of stdin */
        if (dup2(jobline->fdInput, STDIN_FILENO) == -1) {
            // jobline->runnable = false;
        }
        close(jobline->fdInput);
    }

    /* Redirect STDOUT */
    if (jobline->fdOutput != STDOUT_FILENO) {

        /* Checking for errors in redirection of stdout */
        if (dup2(jobline->fdOutput, STDOUT_FILENO) == -1) {
            // jobline->runnable = false;
        }
        close(jobline->fdOutput);
    }
    return;
}

void create_processes_and_run_job(JobLineData* job) {
    /* Ignore jobs that aren't runnable */
    if (!job->runnable) {
        return;
    }

    /* Create a new process & assign */
    pid_t pid = fork();
    job->pId = pid;

    /* Creating process returned an error, return */
    if (pid == -1) {
        job->runnable = false;
        perror("fork");
        return;
    }

    /* We are in the child process */
    if (pid == 0) {

        /* Redirect STDIN, STDOUT, and STDERR */
        redirect_job_input_and_output(job);

        /* Execute the program (depending on arguments supplied) */

        if (execvp(job->program, job->extra_args) == -1) {
            perror("execvp");
            exit(255); // Handle the error appropriately
        }

    /* We are in the parent process */
    } else {

        /* Close unneeded File Descriptors */
        close_unused_pipe_ends(job);
        

        /* Start The time for the process */
        if(job->timeout > 0) {
            job->tStart = time(NULL);
        }
    }
}

void run_all_jobs(AllJobData* alljobs) {

    /* Create processes for each job */
    for(int i = 0; i < alljobs->totaljobs; i++) {

        /* Run Each Process */
        create_processes_and_run_job(&(alljobs->joblinedata[i]));
    }
    return;
}

void close_unused_pipe_ends(JobLineData* job) {
    /* Close unused pipe ends in the child process */
    if (job->fdInput != STDIN_FILENO) {
        close(job->fdInput);
    }
    if (job->fdOutput != STDOUT_FILENO) {
        close(job->fdOutput);
    }
    return;
}

void initialise_signal_handler(void) {
    struct sigaction sighup;
    sigset_t blockMask;

    sigemptyset(&blockMask);
    sigaddset(&blockMask, SIGHUP);

    sighup.sa_mask = blockMask;
    sighup.sa_handler = sighup_handler;
    sighup.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGHUP, &sighup, NULL) == -1) {
        perror("Error in sigaction setup ");
    }
}

void sighup_handler(int signal) {
    signal_state = SIGHUP_SENT;
}

/*
    kill_processes_on_sighup()
    Function that checks if the handler has
    been called, if so it will kill all jobs
    with an assigned PID that are runnable
*/

void kill_processes_on_sighup(AllJobData* alljobs) {

    /* Check that SIGHUP has been sent */
    if(signal_state == SIGHUP_NOT_SENT) {
        return;
    }

    /*
        Else! Kill Jobs if sighup is called (like rambo)
    */
    for(int i = 0; i < alljobs->totaljobs; i++) {
        if(alljobs->joblinedata[i].runnable) {
            kill(alljobs->joblinedata[i].pId, SIGKILL);
        }
    }
}

void process_timeouts(AllJobData* alljobs) {

    bool sigabrt_sent = false;
    
    /* Check every job in the structure  */
    for(int i = 0; i < alljobs->totaljobs; i++) {
        
        /* Assign end time for each job */
        alljobs->joblinedata[i].tEnd = time(NULL);

        time_t timer_start = alljobs->joblinedata[i].tStart;
        time_t timer_end = alljobs->joblinedata[i].tEnd;

        int timeout = (alljobs->joblinedata[i].timeout) * 1000;

        /* Timer has exceeded time out value */
        if((timer_end - timer_start) > timeout) {

            /* SIGABRT not SENT, but still alive, therefore, kill*/
            if(!sigabrt_sent) {
                kill(alljobs->joblinedata[i].pId, SIGABRT);
                sigabrt_sent = true;

            /* SIGABRT SENT, but still alive, therefore, kill*/
            } else if(sigabrt_sent) {
                kill(alljobs->joblinedata[i].pId, SIGKILL);
            }
        }
    }
}

void wait_for_processes_and_timeouts(AllJobData* alljobs) {

    int jobs_to_run = alljobs->totaljobs;

    while(jobs_to_run != 0) {

        /* Check for SIGHUP */
        kill_processes_on_sighup(alljobs);

        /* Process the timeouts */
        process_timeouts(alljobs);

        /* Sleep 1s between next check */
        sleep(1);

        for(int i = 0; i < alljobs->totaljobs; i++) {

            if(alljobs->joblinedata[i].runnable) {

                if(waitpid(alljobs->joblinedata[i].pId, &alljobs->joblinedata[i].status, WNOHANG) > 0) {

                    /* Process exited with an exit status */
                    if (WIFEXITED(alljobs->joblinedata[i].status)) {
                        fprintf(
                            stderr, 
                            "Job %d exited with status %d\n", 
                            alljobs->joblinedata[i].job_number, 
                            WEXITSTATUS(alljobs->joblinedata[i].status)
                        );
                        fflush(stderr);

                    /* Process was terminated by signal */
                    } else if (WIFSIGNALED(alljobs->joblinedata[i].status)) {
                        fprintf(stderr, 
                            "Job %d terminated with signal %d\n", 
                            alljobs->joblinedata[i].job_number, 
                            WTERMSIG(alljobs->joblinedata[i].status)
                        );
                        fflush(stderr);
                    } 
                    /* Reduce jobs yet to run */
                    jobs_to_run--;
                }
            }
        }
    }
}

void monitor_verbose_data(AllJobData* alljobs, Arguments* args) {

    /* Return if verbose mode is inactive */
    if(args->verboseflag != VERBOSE_TRUE) {
        return;
    }

    /* Process verbose */
    for(int i = 0; i < alljobs->totaljobs; i++) {
        /* Process only jobs that can run */
        if(!alljobs->joblinedata[i].runnable) {
            continue;
        }


        /* Print Verbose Data */
        fprintf(
            stderr,
            "%d:%s:%s:%s:%d", 
            alljobs->joblinedata[i].job_number,
            alljobs->joblinedata[i].program,
            alljobs->joblinedata[i].file_stdin,
            alljobs->joblinedata[i].file_stdout,
            alljobs->joblinedata[i].timeout
        );

        /* Print excess args if they exist*/
        if(alljobs->joblinedata[i].extra_args != NULL) {
            for(int j  = 0; j < alljobs->joblinedata[i].num_extra_args; j++) {
                fprintf(stderr, ":%s", alljobs->joblinedata[i].extra_args[j]);
            }
        }
        fprintf(stderr, "\n");
    }
}