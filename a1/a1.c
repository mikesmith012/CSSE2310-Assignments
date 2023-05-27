#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Creates a parameters struct to store the info of input parameters
struct Parameters {
    int alpha;
    int len;
    int longest;
    char include;
    char* letters;
    char* filename;
};

// Function prototypes
char** check_parameters(struct Parameters par, char** matchingWords, int, 
        char** longestWords, int*);
int check_arg_errors(int argc, char** argv);
struct Parameters init_parameters(struct Parameters par, char*);
int check_include_error(struct Parameters par);
int check_invalid(int argc, char** argv, int*, int*, int*, int);
int check_invalid_command(int argc, char** argv);
int check_invalid_dict(char*);
int check_letters_arg(int argc, char** argv);
int check_alphabetic_chars(int argc, char** argv);
struct Parameters handle_args(int argc, char** argv, struct Parameters par);
void handle_letters_arg(char*, char*);
char** open_dict_file(char*, char**, int*);
char** copy_file_words(char**, char**, int);
void to_lower_case(char**, int);
void letters_alpha_order(char*);
int* compare_words(char**, char*, int*, int, int*, char);
int compare_characters(char*, char*, char);
int is_not_alpha(char);
int check_include(char, char);
void get_matching_words(char**, char**, int*, int);
void sort_words(char**, int, int);
void sort_ascii(char**, int);
int compare_alpha(const void*, const void*);
int compare_len(const void*, const void*);
int str_length(char*);
char** sort_longest(char**, char**, int, int*);
void standard_output(char**, int);
void free_alloc_mem(char**, int);

// The main function
int main(int argc, char** argv) {

    char defaultDict[] = "/usr/share/dict/words";
    struct Parameters par;

    // Checks for common errors in the arguments
    int error = check_arg_errors(argc, argv);
    if (error) {
        return error;
    }

    // Initialises and handles the info in the 'par' struct
    par = init_parameters(par, defaultDict);
    par = handle_args(argc, argv, par); 
    handle_letters_arg(par.letters, &par.include);

    if (check_invalid_dict(par.filename)) {
        fprintf(stderr, "unjumble: file \"%s\" can not be opened\n", 
                par.filename);
        return 2;
    }

    // Creates all arrays and counters nessecary to keep track of the words
    char** fileWords = (char**)malloc(0);
    int lineCount = 1, wordCount = 0, longestCount = 0;
    fileWords = open_dict_file(par.filename, fileWords, &lineCount);

    char** fileWordsCopy = (char**)malloc(0);
    fileWordsCopy = copy_file_words(fileWords, fileWordsCopy, lineCount);
    to_lower_case(fileWordsCopy, lineCount);

    int* indices = (int*)malloc(0);
    indices = compare_words(fileWordsCopy, par.letters, indices, 
            lineCount, &wordCount, par.include);

    char** matchingWords = (char**)malloc(wordCount * sizeof(char*));
    get_matching_words(fileWords, matchingWords, indices, wordCount);
    char** longestWords = (char**)malloc(0);

    // Checks the user specified parameters
    longestWords = check_parameters(par, matchingWords, wordCount, 
            longestWords, &longestCount);

    // Frees all allocated memory
    free(par.letters);
    free(par.filename);
    free(indices);
    free_alloc_mem(longestWords, longestCount);
    free_alloc_mem(matchingWords, wordCount);
    free_alloc_mem(fileWordsCopy, lineCount - 1);
    free_alloc_mem(fileWords, lineCount - 1);

    // Returns 10 if no matching words are found
    if (wordCount == 0) {
        return 10;
    }
    return 0;
}

// Checks the user specified parameters and outputs accordingly
char** check_parameters(struct Parameters par, char** matchingWords, 
        int wordCount, char** longestWords, int* longestCount) {

    // Programs prints nothing is no words are matching.
    // Otherwise it'll print all the matching words to stdout.
    if (wordCount == 0) {
        ;
    } else if (par.alpha) {
        sort_words(matchingWords, wordCount, par.len);
        standard_output(matchingWords, wordCount);
    } else if (par.len) {
        sort_words(matchingWords, wordCount, par.len);
        standard_output(matchingWords, wordCount);
    } else if (par.longest) {
        longestWords = sort_longest(matchingWords, longestWords, wordCount, 
                longestCount);
        sort_words(longestWords, *longestCount, par.longest);
        standard_output(longestWords, *longestCount);
    } else {
        standard_output(matchingWords, wordCount);
    } 
    return longestWords;
}

// Checks for invalid arguments, non-alpha chars and arg length
int check_arg_errors(int argc, char** argv) {

    // Error messages
    char invalidCommand[] = "Usage: unjumble [-alpha|-len|-longest] "
            "[-include letter] letters [dictionary]\n";
    char invalidLettersArg[] = "unjumble: "
            "must supply at least three letters\n";
    char nonAlphaChar[] = "unjumble: "
            "can only unjumble alphabetic characters\n";
    
    // Prints error message to corresponding error
    if (check_invalid_command(argc, argv)) {
        fprintf(stderr, "%s", invalidCommand);
        return 1;
    } else if (check_alphabetic_chars(argc, argv)) {
        fprintf(stderr, "%s", nonAlphaChar);
        return 4;
    } else if (check_letters_arg(argc, argv)) {
        fprintf(stderr, "%s", invalidLettersArg);
        return 3;
    }
    return 0;
}

// Initialises variables in the parameters struct
struct Parameters init_parameters(struct Parameters par, char* defaultDict) {

    // 'alpha', 'len' and 'longest' are assigned 0 by default.
    par.alpha = 0;
    par.len = 0;
    par.longest = 0;
    
    // 'include' is assigned null by default
    par.include = '\0';
    
    // 'letters' and 'filename' are initialised
    // 'filename' stored the default file directory
    par.letters = (char*)malloc(0);
    int filenameLen = strlen(defaultDict) + 2;
    par.filename = (char*)malloc(filenameLen * sizeof(char));
    strcpy(par.filename, defaultDict);
    
    return par;
}

// Helper function for the check_invalid_command function
int check_invalid(int argc, char** argv, int* argCount, int* letterCount, 
        int* dictCount, int check) {

    for (int i = 0; i < argc - 1; i++) {
        if (argv[i + 1][0] == '-') {

            // Returns 1 if letters arg appears before parameters args
            if ((*letterCount) > 0) {
                return 1;
            } 
            
            (*argCount)++;
            if (strcmp(argv[i + 1], "-alpha") == 0 ||
                    strcmp(argv[i + 1], "-len") == 0 ||
                    strcmp(argv[i + 1], "-longest") == 0 ||
                    strcmp(argv[i + 1], "-include") == 0) {
                check = 0; 

            // Returns 1 if the arg behind '-' is invalid
            } else {
                return 1;
            }

        } else {
            if (strcmp(argv[i], "-include") == 0) {
                ;
            } else if ((*letterCount > 0)) {
                (*dictCount)++;

                // Returns 1 if one or more args are given after dict 
                if ((*dictCount) > 1) {
                    return 1;
                } 
            } else {
                (*letterCount)++;
            }
        }

        // Returns 1 if > 2 '-' arg is given
        if ((*argCount) > 2) {
            return 1;
        }
    }
    return check;
}

// Checks for invalid commands
int check_invalid_command(int argc, char** argv) {
    int check = 0;

    // Returns 1 if the number of args is invalid
    if (argc < 2 || argc > 6) {
        return 1; 
    }

    int argCount = 0, letterCount = 0, dictCount = 0;
    check = check_invalid(argc, argv, &argCount, &letterCount, &dictCount, 
            check);

    // Returns 1 if no letters arg is given
    if (letterCount == 0) {
        return 1; 
    }
    
    int includeArg = 0;
    for (int j = 0; j < argc - 1; j++) {

        if (strcmp(argv[j + 1], "-include") == 0) {
            includeArg = 1;

            if (strlen(argv[j + 2]) != 1) {
                return 1;
            } else if ((argv[j + 2][0] >= 'a' && argv[j + 2][0] <= 'z') ||
                    (argv[j + 2][0] >= 'A' && argv[j + 2][0] <= 'Z')) {
                ;

            // Returns 1 if '-include' is not followed by an alpha char
            } else {
                return 1;
            }
        }
    }

    // Returns 1 if more than 1 parameter arg is given
    if ((argCount > 1 && includeArg == 0) || 
            (argCount > 2 && includeArg == 1)) {
        return 1; 
    }
    return check;
}

// Checks if the dictionary file given is invalid
int check_invalid_dict(char* filename) {
    FILE* words = fopen(filename, "r");

    // Returns 1 if file is not found
    if (words == NULL) {
        return 1;
    }

    fclose(words);
    return 0;
}

// Checks if the letters argument contains less than 3 letters
int check_letters_arg(int argc, char** argv) {
    int argInclude = 0;

    for (int i = 0; i < argc - 1; i++) {

        if (argv[i + 1][0] != '-' && strcmp(argv[i], "-include") == 0) {
            argInclude = 1;
        }

        // Returns 1 if less than 3 chars are given in letters arg
        if (strlen(argv[i + 1]) < 3 && argInclude == 0) {
            return 1;
        } 
        argInclude = 0;
    }
    return 0;
}

// Checks for non-alphabetic characters in the letters arguments
int check_alphabetic_chars(int argc, char** argv) {
    int argCount = 0, argInclude = 0, argLetters = 0;

    for (int i = 0; i < argc - 1; i++) {

        if (strcmp(argv[i + 1], "-include") == 0) {
            argInclude = 1;
        } else if ((argInclude = 0 && argCount == 1) ||
                (argInclude = 1 && argCount == 2)) {
            break;
        }

        if (argv[i + 1][0] != '-') {
            argCount++;

            if (argLetters == 1) {
                break;
            }

            argLetters++;
            for (int j = 0; argv[i + 1][j] != '\0'; j++) {

                if ((argv[i + 1][j] >= 'a' && argv[i + 1][j] <= 'z') ||
                        (argv[i + 1][j] >= 'A' && argv[i + 1][j] <= 'Z')) {
                    ;

                // Returns 1 if non alpha chars are given in letters arg
                } else {
                    return 1;
                }
            }
        }
    }
    return 0;
}

// Handles the command line arguments specified by the user.
struct Parameters handle_args(int argc, char** argv, struct Parameters par) {
    int lettersCount = 0;

    for (int i = 0; i < argc - 1; i++) {

        if (argv[i + 1][0] == '-') {

            // Assignes a 1 to 'alpha', 'len', or 'longest' if that 
            // corresponding arguments was specified by user
            if (strcmp(argv[i + 1], "-alpha") == 0) {
                par.alpha = 1;
            } else if (strcmp(argv[i + 1], "-len") == 0) {
                par.len = 1;
            } else if (strcmp(argv[i + 1], "-longest") == 0) {
                par.longest = 1;
            }

            // Assignes the specified character to 'include' if the 
            // '-include' arg was specified by user
            if (strcmp(argv[i + 1], "-include") == 0) {
                par.include = argv[i + 2][0];
            }

        // Assigns the letters arg and the specified file name/directory to 
        // 'letters' and 'filename' respectively
        } else if (strcmp(argv[i], "-include") == 0) {
            ;
        } else if (lettersCount > 0) {
            int len = strlen(argv[i + 1]) + 2;
            par.filename = (char*)realloc(par.filename, len * sizeof(char));
            strcpy(par.filename, argv[i + 1]);
        } else {
            int len = strlen(argv[i + 1]) + 2;
            par.letters = (char*)realloc(par.letters, len * sizeof(char));
            strcpy(par.letters, argv[i + 1]);
            lettersCount++;
        }
    }
    return par;
}

// Changes the letters and include argument to all lower case
void handle_letters_arg(char* letters, char* include) {

    // changes all letters in the 'letters' arg to lower case
    for (int i = 0; letters[i] != '\0'; i++) {
        letters[i] = (char)tolower((int)letters[i]);
    }
    strcat(letters, "\n");

    // changes the 'include' char to lower case
    if (*include != '\0') {
        *include = (char)tolower((int)*include);
    }
}

// Opens the dictionary file and reads its contests
char** open_dict_file(char* filename, char** fileWords, int* lineCount) {
    FILE* wordFile = fopen(filename, "r");
    char word[50];
    int copy;

    // Reads the dictionary file line by line
    while (fgets(word, 50, wordFile) != NULL) {
        copy = 1;

        if (str_length(word) < 3) {
            copy = 0;
        } else {
            for (int i = 0; word[i] != '\n'; i++) {
                if (is_not_alpha(word[i])) {
                    copy = 0;
                }
            }
        }

        // Copies the dictionary words into the 'fileWords' array
        if (copy) {
            fileWords = (char**)realloc(fileWords, 
                    (*lineCount) * sizeof(char*));

            fileWords[(*lineCount) - 1] = (char*)malloc(50 * sizeof(char));
            strcpy(fileWords[(*lineCount) - 1], word);
            (*lineCount)++;
        }
    }
    fclose(wordFile);
    return fileWords;
}

// Makes a copy of all the file words in the fileWordsCopy array
char** copy_file_words(char** fileWords, char** fileWordsCopy, 
        int lineCount) {

    // Creates a copy of the 'fileWords' array into 'fileWordsCopy'
    // All words in 'fileWordsCopy' will be changed to lower case
    // All words in 'fileWords' will remain unchanged
    // Only words in 'fileWords' will be printed at the end
    for (int i = 0; i < lineCount - 1; i++) {
        fileWordsCopy = (char**)realloc(fileWordsCopy, 
                (i + 1) * sizeof(char*));

        fileWordsCopy[i] = (char*)malloc(50 * sizeof(char));
        strcpy(fileWordsCopy[i], fileWords[i]);
    }
    return fileWordsCopy;
}

// Changes all of the file words to lower case 
void to_lower_case(char** fileWords, int lineCount) {
    int wordLength;

    // Changed each char in each word to lower case
    for (int i = 0; i < lineCount - 1; i++) {
        wordLength = strlen(fileWords[i]) - 1;
        for (int j = 0; j < wordLength; j++) {
            fileWords[i][j] = (char)tolower((int)fileWords[i][j]);
        }
    }
}

// Compares the words from the dict file to the letters arg
int* compare_words(char** fileWords, char* letters, int* indices, 
        int lineCount, int* wordCount, char include) {
    for (int i = 0; i < lineCount - 1; i++) {

        // Calls the 'compare_characters' func and keeps track of matches 
        // The 'compare_characters' func is where the chars are compared
        if (compare_characters(fileWords[i], letters, include)) {
            (*wordCount)++;

            indices = (int*)realloc(indices, (*wordCount) * sizeof(int));
            indices[(*wordCount) - 1] = i;
        }
    }
    return indices;
}

// Compares the chars from each file words to each char from the letters arg
int compare_characters(char* word, char* letters, char include) {

    int wordLen = strlen(word);
    int letterLen = strlen(letters);
    char* lettersCopy = (char*)malloc((letterLen + 1) * sizeof(char));
    int match, includeMatch = 0;

    strcpy(lettersCopy, letters);
    for (int i = 0; i < wordLen; i++) {
        match = 0;

        // Ignores all non-alpha chars
        if (is_not_alpha(word[i])) {
            match = 2;
        } else {

            // Replaces 'letters' with '\0' for each matching char
            for (int j = 0; j < letterLen; j++) {
                if (word[i] == lettersCopy[j]) {

                    if (match == 0) {
                        lettersCopy[j] = '\0';
                        match = 1;
                    }

                    if (check_include(word[i], include)) {
                        includeMatch |= 1;
                    }
                }
            }
        }
        if (match == 0) {
            free(lettersCopy);
            return 0;
        }
    } 
    if (includeMatch == 0) {
        free(lettersCopy);
        return 0;
    }
    free(lettersCopy);
    return 1;
}

// Checks for non-alphabetical characters in words from the dict file
int is_not_alpha(char letter) {

    if ((letter >= 'a' && letter <= 'z') || 
            (letter >= 'A' && letter <= 'Z')) {
        return 0;
    }

    // Returns 1 if letter is a non-alpha char
    return 1;
}

// Checks that the "-include" character is in all the matching words
int check_include(char letter, char include) {

    if (include == '\0' || include == letter) {
        return 1;
    }

    // Returns 1 if the 'include' char is in the letters arg or if
    // the 'include' arg is not specified
    return 0;
}

// Replaces the array of matching words with the original, unmodified word
void get_matching_words(char** fileWords, char** matchingWords, int* indices, 
        int wordCount) {

    // Replaces each word in 'matchingWords' with the unmodified original
    // version of that words from the 'fileWords' array 
    for (int i = 0; i < wordCount; i++) {
        matchingWords[i] = (char*)malloc(50 * sizeof(char));
        strcpy(matchingWords[i], fileWords[indices[i]]);
    }
}

// Sorts an array of strings corresponding to user specified arguments
void sort_words(char** words, int wordCount, int len) {

    // Sorts in alpha order if len is 0, else sorts in len order
    if (!len) {
        qsort(words, wordCount, sizeof(char*), compare_alpha);
    } else {
        qsort(words, wordCount, sizeof(char*), compare_len);
    }

    // Calls the sort_ascii helper function
    sort_ascii(words, wordCount);
}

// Sorts words in ascii order after '-alpha', '-len' or '-longest'
void sort_ascii(char** words, int wordCount) {

    // Reorders the array of words only if words have the same spelling
    // but not in ascii order
    char temp[50];
    for (int i = 0; i < wordCount - 1; i++) {
        if (!strcasecmp(words[i], words[i + 1]) && 
                strcmp(words[i], words[i + 1]) > 0) {
            strcpy(temp, words[i + 1]);
            strcpy(words[i + 1], words[i]);
            strcpy(words[i], temp);
        }
    }
}

// Helper function for the qsort function, sorts alphabetically
int compare_alpha(const void* wordA, const void* wordB) {
    const char** word1 = (const char**)wordA;
    const char** word2 = (const char**)wordB;
    return strcasecmp(*word1, *word2);
}

// Helper function for the qsort function, sorts descending length
int compare_len(const void* wordA, const void* wordB) {

    const char** word1 = (const char**)wordA;
    const char** word2 = (const char**)wordB;
    int len1 = strlen(*word1);
    int len2 = strlen(*word2);

    if (len1 != len2) {
        return len2 - len1;
    } else {
        return strcasecmp(*word1, *word2);
    }
}

// Gets the number of alphabetical characters in a string
int str_length(char* str) {

    // Increments count only if an alpha char is detected
    int count = 0;
    for (int i = 0; str[i] != '\n'; i++) {
        if ((str[i] >= 'a' && str[i] <= 'z') || 
                (str[i] >= 'A' && str[i] <= 'Z')) {
            count++;
        }
    }
    return count;
}

// Gets the longest string(s) from an array based on the number of alpha chars
char** sort_longest(char** words, char** longest, int wordCount, int* count) {

    // First finds the length of the longest word in the dictionary
    int maxLen = 0;
    for (int i = 0; i < wordCount; i++) {
        if (str_length(words[i]) > maxLen) {
            maxLen = str_length(words[i]);
        }
    }

    // Then appends all of the longest words to the 'longest' array
    for (int i = 0; i < wordCount; i++) {
        if (str_length(words[i]) == maxLen) {
            (*count)++;
            longest = (char**)realloc(longest, (*count) * sizeof(char*));
            longest[(*count) - 1] = (char*)malloc(50 * sizeof(char));
            strcpy(longest[(*count) - 1], words[i]);
        }
    }
    
    // Finally sorts the words in ascii order
    sort_ascii(longest, *count);
    return longest;
}

// Prints array of strings to standard output
void standard_output(char** words, int length) {
    for (int i = 0; i < length; i++) {
        fprintf(stdout, "%s", words[i]);
    }
}

// Frees allocated memory for 2D arrays
void free_alloc_mem(char** words, int length) {
    for (int i = 0; i < length; i++) {
        free(words[i]);
    }
    free(words);
}
