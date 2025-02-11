#include <string.h>     // for strlen
#include <sys/socket.h> // for socket
#include <arpa/inet.h>  // for inet_addr
#include <unistd.h>     // for write
#include <stdio.h>      // for file
#include <pthread.h>    //thread + mutex
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h> // for boolean flag

/*Global variables prepared to be the desired constant in the given project*/
#define INPUT_CHARACTER_LIMIT 100
#define OUTPUT_CHARACTER_LIMIT 200
#define LEVENSHTEIN_LIST_LIMIT 5
#define PORT_NUMBER 60000

/*This buffer size is a size used for the remaining printing operations except for printing the input and output sections.*/
/*If there are missing values ​​in the Levensthein formula, it is due to the buffer, not the algorithm.*/
/*This problem is solved by increasing buffer_size*/
#define BUFFER_SIZE 256

/*The reason for using this kind of structure is to apply the Levensthein formula to the entire dictionary and keep it in an array.*/
typedef struct
{
    char stringName[INPUT_CHARACTER_LIMIT + 1];
    int diff;
} LevInfo;

/*The reason for creating this structure is to be able to send more than one element to the function called threadFunction
(the word itself, the order in which the id words will be written, and to be able to perform socket write operations).*/
typedef struct
{
    char *word;
    int id;
    int socket;
} ThreadData;

/*These are the functions used in the structure of the code. Below the main function, you will find clear explanations of all functions.*/
/*Just above the contents of the functions, you can see what the functions do and what the variables in the contents of these functions do.*/
void freeArray(char **array, int size);
void addString(char ***array, int *size, int *capacity, const char *newString);
void toLowerCase(char *str);
LevInfo *calculateLevenshtein(const char *s1);
LevInfo *TopWords(LevInfo *allWords, int totalWords);
int compareLevInfo(const void *a, const void *b);
char *getInput(int newSocket);
void freeArrayList(char ***array_list, int *sizes, int count);
int isinArray(char **array, int size, const char *word);
char ***SplitbyRepeatedWords(const char *input, const char *delim, int **sizes, int *count);
void *threadFunction(void *arg);
void MakeOutputString(int thread_id, char *word);
int compareStrings(const void *a, const void *b);
void clearScreen(int client_fd);

/*Global variables: The reason they are global is that they are called by more than one function or as an element in more than one function.
These variables are global and are seen in the necessary functions and main.*/
int arraySize = 0;
int arrayCapacity = 2;
char **dict_array = NULL;
char *error_message = NULL;
int turn = 1;
char *Output_String;
int output_offset = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex lock
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;    // Conditional Variable

int main(int argc, char *argv[])
{
    int socket_desc, new_socket, c;
    struct sockaddr_in server, client;
    char *message;
    char *input;
    int opt = 1; // Option value for SO_REUSEADDR

    // Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1)
    {
        perror("Could not create socket");
        return 1;
    }

    /*The purpose of the code snippet specified in the if condition is to allow the user to use the port again immediately.
    If this code snippet is to be removed, it is necessary to wait for the time determined by the OS.*/
    if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt failed");
        close(socket_desc);
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT_NUMBER); // server will listen to 60000 port

    // Bind
    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("Binding failed");
        close(socket_desc);
        return 1;
    }
    puts("Socket is binded");

    // Listen
    if (listen(socket_desc, 3) < 0) // Start listening
    {
        perror("Listen failed");
        close(socket_desc);
        return 1;
    }
    // Accept and incoming connection
    puts("Waiting for incoming connections...");

    c = sizeof(struct sockaddr_in);
    new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c);
    if (new_socket < 0)
    {
        perror("Accept failed");
        close(socket_desc);
        return 1;
    }

    puts("Connection accepted");
    message = "\n\nHello, this is Text Analysis Server!\n";
    write(new_socket, message, strlen(message));

    while (true)
    {
        arraySize = 0;
        arrayCapacity = 2;
        error_message = NULL;
        turn = 1;
        /*The name of the variable may give a different impression to the reader,
        but the main purpose of this variable is to ensure that the threads enter the mutex operation in order.*/

        /*The dict_array variable basically serves to hold the lines in the text file. It is opened dynamically with malloc.
        This will be seen in the add_String function in the following sections.*/
        dict_array = malloc(arrayCapacity * sizeof(char *));
        if (dict_array == NULL)
        {
            error_message = "\nArray could not be created\n";
            write(new_socket, error_message, strlen(error_message));
            break;
        }

        // Open dictionary file
        FILE *dictionary;
        char line[INPUT_CHARACTER_LIMIT + 1];
        if ((dictionary = fopen("basic_english_2000.txt", "r")) == NULL)
        {
            error_message = "\nThe file could not found\n";
            write(new_socket, error_message, strlen(error_message));
            free(dict_array);
            break;
        }

        // Read dictionary words
        while (fgets(line, sizeof(line), dictionary) != NULL)
        {
            line[strcspn(line, "\r\n")] = 0; // Remove newline character
            toLowerCase(line);
            addString(&dict_array, &arraySize, &arrayCapacity, line);
        }
        fclose(dictionary);
        message = "\nPlease enter your input string:\n";

        write(new_socket, message, strlen(message));
        input = getInput(new_socket);
        /*If the file process is completed successfully and no errors are found, the user enters an input in the next step.
        The input entered by the user cannot be as desired in this code. In certain cases, messages are sent indicating that the input is incorrect.*/
        /*If the length of the char*(string) variable called error_message is more than 0,
        then the user has made one of the error conditions and in this case the code
        completes the process by exiting the while loop and closes both the client and the server.*/
        if (error_message != NULL)
        {
            if (strlen(error_message) > 0)
            {
                write(new_socket, error_message, strlen(error_message));
                free(input);
                break;
            }
        }

        /*If there is no contrary situation in the input phase, the code fragment will go to the end of the while loop and ask the user one last question,
        even if an error occurs in any other case in the remaining designed code
        (output specified in the project document or input cases related to the dictionary, etc.).*/
        const char *delim = " "; // this variable is used to detect spaces.
        int *sizes = NULL;       // List holding array sizes
        int numberofArrays = 0;
        int counter = 1; // this variable will be used later, its main purpose is to determine the order of the threads.

        // Split arrays
        char ***array_list = SplitbyRepeatedWords(input, delim, &sizes, &numberofArrays);

        if (error_message != NULL)
        {
            if (strlen(error_message) > 0)
            {
                write(new_socket, error_message, strlen(error_message));
                free(input);
                break;
            }
        }

        /*If there is no contrary situation after the array process is completed, the user can now be given the answer respectively.*/
        /*The reason why the size of the output string is 2 more than the output_limit is the following.
        snprintf, which will be used in the future, detects the characters in the specified buffer size even if you write as many characters as you want
        (for write operation).For example, if you write 201 in the buffer size,
        snprintf 200 detects at most 200 characters and puts a null terminator in the last character.
        The reason why it is 2 more than Output_Limit is to give +1 error condition (to give 201 character error in this code fragment equal to output_limit 200).*/
        Output_String = (char *)malloc((OUTPUT_CHARACTER_LIMIT + 2) * sizeof(char));

        output_offset = 0; // offset is an integer variable used to print side by side with snprintf

        /*The variable created by running the SplitbyRepeatedWords function will be used in this for loop.Unlike normal thread creation stages,
        an extra for loop is required here.This is because it is not known exactly how many arrays the array_list variable holds.
        If this variable holds more than one array, a separate loop state is required for each array.
        It may seem a little slower than creating a thread from all elements and making a join,
        but this is explained as much as possible in the example shown above in the SplitbyRepeatedWords code fragment.*/
        for (int i = 0; i < numberofArrays; i++)
        {
            // Create thread array new for each group
            pthread_t *threads = malloc(sizes[i] * sizeof(pthread_t));

            for (int j = 0; j < sizes[i]; j++)
            {
                ThreadData *data = malloc(sizeof(ThreadData));
                data->word = strdup(array_list[i][j]);
                data->id = counter;
                data->socket = new_socket;
                // Create a thread for each word in the group to compare against dict_array
                // threadFunction is a function that each thread goes to and is the part where the algorithm operations and printing are done.
                if (pthread_create(&threads[j], NULL, threadFunction, (void *)data) != 0)
                {
                    printf("Thread creation failed");
                }
                counter++; // The counter variable id is incremented so that the states are different.
            }

            // Wait for all threads in this group to finish
            for (int j = 0; j < sizes[i]; j++)
            {
                pthread_join(threads[j], NULL);
            }

            free(threads); // Free the thread array after the group is processed
        }

        /*After the user sees the Levensthein answers and the dictionary possibilities,
        the input and output answers are written to the screen. If the limit is exceeded,
        an error is printed and the user cannot add the words to the dictionary even if he wants to.
        Otherwise, the new words are written to the dictionary in a sorted manner.*/
        toLowerCase(input);
        write(new_socket, "\nINPUT: ", strlen("\nINPUT: "));
        write(new_socket, input, strlen(input));
        if (strlen(Output_String) > OUTPUT_CHARACTER_LIMIT)
        {
            error_message = "\nError: Ouput exceeds OUTPUT_CHARACTER_LIMIT characters.";
            write(new_socket, error_message, strlen(error_message));
        }
        else
        {
            write(new_socket, "\nOUTPUT: ", strlen("\nOUTPUT: "));
            write(new_socket, Output_String, strlen(Output_String));

            qsort(dict_array, arraySize, sizeof(char *), compareStrings);

            dictionary = fopen("basic_english_2000.txt", "w");

            if (dictionary == NULL)
            {
                error_message = "\nThe file could not found\n";
                write(new_socket, error_message, strlen(error_message));
                free(dict_array);
                break;
            }
            for (int j = 0; j < arraySize; j++)
            {
                fprintf(dictionary, "%s\n", dict_array[j]);
            }
            fclose(dictionary);
        }
        free(Output_String);
        freeArrayList(array_list, sizes, numberofArrays);
        freeArray(dict_array, arraySize);

        /*The user can choose whether or not to enter another input.*/
        message = "\n\nWould you like to enter another input?(y|Y):";
        write(new_socket, message, strlen(message));
        input = getInput(new_socket);
        toLowerCase(input);
        if (strcmp(input, "y") != 0)
        {
            message = "\nNo other input will be received";
            write(new_socket, message, strlen(message));
            free(input);
            break;
        }
        free(input);
        sleep(1);
        clearScreen(new_socket);
    }
    message = "\n\nThank you for using Text Analysis Server! Good Bye!\n\n";
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    printf("%s\n", "The user's work is done");
    write(new_socket, message, strlen(message));

    close(new_socket);
    close(socket_desc);

    return 0;
}

// Thread Function Informations

/*This is the most important part of the code. This is the desired function during the thread creation phase.
First, regardless of the number of threads sent, Levenshtein values ​​are calculated first without entering
the mutex structure and this value is thrown into an array. Then, the mutex structure is used.
The mutex structure allows only and only 1 thread to enter during the specified intervals (lock, unlock).
Unlike this event, the condition state is provided with the global variable turn and the id sent to the function.
Thus, the threads are allowed to enter the critical region state according to their formation order.*/

/*The response information of the elements entered in order is printed in a way that it will be the same as the given document.
The snprintf structure is very important here. Whether or not each word written by the user is in the dictionary is done by looking
at the first element of the array formed by the Levensthein function. As stated above, if the user has entered the input correctly,
no matter what happens, the user is asked again if they enter a wrong word (do you want to add it to the dictionary).
And according to the specified conditions, it is either added to the dictionary or not.*/

/*While adding or not adding to the Dictionary,
the user's current output status is created at the same time and sent to the function called MakeOutputString.*/
void *threadFunction(void *arg)
{
    ThreadData *data = (ThreadData *)arg;
    LevInfo *result = calculateLevenshtein(data->word);
    int thread_id = data->id;
    int socket = data->socket;
    pthread_mutex_lock(&mutex);
    while (turn != thread_id)
    {
        pthread_cond_wait(&cond, &mutex); // Thread waits in queue
    }
    // sleep(1);
    // The buffer required to print the specified text outside the output string.
    char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
    int offset = 0;
    char *input = NULL;

    snprintf(buffer, BUFFER_SIZE, "\nWORD %02d: %s\n", thread_id, data->word);
    write(socket, buffer, strlen(buffer));

    offset += snprintf(buffer + offset, BUFFER_SIZE - offset, "MATCHES: ");
    for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT; i++)
    {
        // Print each element (word and diff)
        offset += snprintf(buffer + offset, BUFFER_SIZE - offset, "%s (%d)", result[i].stringName, result[i].diff);

        // Let's add a comma before the next element, but not after the last element
        if (i < LEVENSHTEIN_LIST_LIMIT - 1)
        {
            offset += snprintf(buffer + offset, BUFFER_SIZE - offset, ", ");
        }
    }
    offset += snprintf(buffer + offset, BUFFER_SIZE - offset, "\n");
    write(socket, buffer, strlen(buffer));

    // This status has been reset because the status of the threads could not be fully estimated.
    offset = 0;

    // check dictionary situation
    /*If the word is in the dictionary, it is written directly to the output as it is.
    If the word is not in the dictionary, there are 2 situations. Requesting and not requesting the addition.
    If the user adds the word, the word is added to the dictionary and written to the output string as it is.
    If the user does not want it, the closest word to it is added to the output string.*/
    // The process continues until y\n or empty response is received.
    if (result[0].diff == 0)
    {
        snprintf(buffer, 132, "WORD %s is present in dictionary\n", data->word);
        write(socket, buffer, strlen(buffer));
        MakeOutputString(thread_id, data->word);
    }
    else
    {
        snprintf(buffer, 136, "WORD %s is not present in dictionary\n", data->word);
        write(socket, buffer, strlen(buffer));
        snprintf(buffer, 51, "Do you want to add this word to dictionary? (y/N):");
        write(socket, buffer, strlen(buffer));
        input = getInput(socket);
        bool flag = true;
        toLowerCase(input);
        if (strlen(input) == 0 || strcmp(input, "n") == 0)
        {
            MakeOutputString(thread_id, result[0].stringName);
        }
        else if (strcmp(input, "y") == 0)
        {
            MakeOutputString(thread_id, data->word);
            addString(&dict_array, &arraySize, &arrayCapacity, data->word);
        }
        else
        {
            while (flag == true)
            {
                snprintf(buffer, 38, "Wrong Input Please Enter Again (y/N):");
                write(socket, buffer, strlen(buffer));
                input = getInput(socket);
                toLowerCase(input);
                if (strlen(input) == 0 || strcmp(input, "n") == 0)
                {
                    flag = false;
                    MakeOutputString(thread_id, result[0].stringName);
                }
                else if (strcmp(input, "y") == 0)
                {
                    flag = false;
                    MakeOutputString(thread_id, data->word);
                    addString(&dict_array, &arraySize, &arrayCapacity, data->word);
                }
            }
        }
    }
    free(input);
    free(result);
    free(buffer);
    turn++; // Move to next thread
    pthread_cond_broadcast(&cond);

    pthread_mutex_unlock(&mutex);
}

/*The purpose of this function is to create the output properly.
The reason for writing Output_character_limit+2 is stated above. (To provide the max character requirement as min.)*/
void MakeOutputString(int thread_id, char *word)
{
    if (thread_id == 1)
    {
        output_offset += snprintf(Output_String + output_offset, OUTPUT_CHARACTER_LIMIT + 2 - output_offset, "%s", word);
    }
    else
    {
        output_offset += snprintf(Output_String + output_offset, OUTPUT_CHARACTER_LIMIT + 2 - output_offset, " %s", word);
    }
}

/*The purpose of the calculateLevenshtein function is to compare each word entered by the user with every word in the dictionary.
This is achieved through the outer for loop specified in the code snippet. The function calculates the difference between two words (character differences).
For example, if two words are identical, the difference will be 0. On the other hand, for an example like "hello" and "hollow," the difference will be 2.
The reason it returns an array is as follows: first, it compares all the elements in the dictionary with the input and selects the top matches
based on the specified limit. The topWords variable holds up to the limit number of words and their corresponding differences with any word
in the user's input sentence. Returning an array significantly simplifies the process in this code snippet.*/
LevInfo *calculateLevenshtein(const char *s1)
{
    LevInfo *allWords = (LevInfo *)malloc(arraySize * sizeof(LevInfo));
    for (int m = 0; m < arraySize; m++)
    {
        int len1 = strlen(s1);
        int len2 = strlen(dict_array[m]);
        int dp[len1 + 1][len2 + 1];

        for (int i = 0; i <= len1; i++)
            dp[i][0] = i;
        for (int j = 0; j <= len2; j++)
            dp[0][j] = j;

        for (int i = 1; i <= len1; i++)
        {
            for (int j = 1; j <= len2; j++)
            {
                int cost = (s1[i - 1] == dict_array[m][j - 1]) ? 0 : 1;
                dp[i][j] = dp[i - 1][j - 1] + cost;
                if (dp[i - 1][j] + 1 < dp[i][j])
                    dp[i][j] = dp[i - 1][j] + 1;
                if (dp[i][j - 1] + 1 < dp[i][j])
                    dp[i][j] = dp[i][j - 1] + 1;
            }
        }
        // the results are transferred one by one to the array.
        LevInfo result;
        strcpy(result.stringName, dict_array[m]);
        result.diff = dp[len1][len2];
        allWords[m] = result;
    }
    /*The desired situation in the project document is to return the number of words and the differences of those words with
    a certain limit and the closest limit number. so an extra function was used.*/
    LevInfo *final = TopWords(allWords, arraySize);
    return final;
}

/*The purpose of using the TopWords function is to select the closest words from the entire dictionary based on the specified limit.
The LevInfo array, which contains all the dictionary words, is first sorted using the compare method written for the qsort function.
Then, the top words up to the specified limit are transferred to another array, which is returned.
The original array is then freed to release the allocated memory.*/
LevInfo *TopWords(LevInfo *allWords, int totalWords)
{
    LevInfo *TopLevenshtein = (LevInfo *)malloc(LEVENSHTEIN_LIST_LIMIT * sizeof(LevInfo));
    qsort(allWords, arraySize, sizeof(LevInfo), compareLevInfo);
    for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT; i++)
    {
        TopLevenshtein[i] = allWords[i];
    }
    free(allWords);
    return TopLevenshtein;
}

/*Compare method for TopWords.First,the method is checking for numbers.If the numbers are the same then it is checking for strings.*/
int compareLevInfo(const void *a, const void *b)
{
    const LevInfo *infoA = (const LevInfo *)a;
    const LevInfo *infoB = (const LevInfo *)b;

    // Compare diff values first
    if (infoA->diff < infoB->diff)
    {
        return -1; //
    }
    else if (infoA->diff > infoB->diff)
    {
        return 1;
    }
    else
    {
        // if diff is equal, sort by stringName
        return strcmp(infoA->stringName, infoB->stringName);
    }
}

/*The purpose of the function is to receive input via telnet.
In this function, the input received is not separated into its parts in any way and is taken as is.
Necessary error messages are printed based on the problems specified in the project document related to the input.
Regardless, the return status solves the memory problem with the free placed in the main code.*/
/*The getInput function will also be used in threadFunction in the future,
where the main purpose is to get the desired input from the user rather than returning error messages,
so the error messages are related to the actual string received from the user.*/
char *getInput(int newSocket)
{
    /*the data received from the user is put into the buffer*/
    /*Limit=100+(\r\n)+2 for accept,101 character+\r for reject*/
    char *buffer = malloc(INPUT_CHARACTER_LIMIT + 2);

    int bytes_received;
    /*The recv function returns an integer value, and this integer value is actually related to how many characters were received.*/

    bytes_received = recv(newSocket, buffer, INPUT_CHARACTER_LIMIT + 2, 0);
    if (bytes_received <= 0)
    {
        error_message = "\nError: Client disconnected or recv failed\n";
        return buffer;
    }

    /*In the usage function of telnet, it puts \r\n at the end of the sentence received with the recv function, regardless of the recv function,
    which causes 2 more characters to be received from the typed text. In this case, it affected the size of the buffer mentioned above.
    It was also collected with the strscpn operation mentioned immediately 1 line below,
    if the user enters more than the specified character limit, this limit is made to be detected input_limit+1 in the background,
    regardless of the limit.\0 character is an important criterion in determining the size of the buffer and indicating that the characters are over.*/
    buffer[strcspn(buffer, "\r\n")] = '\0';
    buffer[INPUT_CHARACTER_LIMIT + 1] = '\0';

    if (strlen(buffer) > INPUT_CHARACTER_LIMIT)
    {
        error_message = "\nError: Input exceeds INPUT_CHARACTER_LIMIT characters.\n";

        return buffer;
    }
    if (strlen(buffer) == 0)
    {
        error_message = "\nError: Input is empty.\n";
        return buffer;
    }

    // Invalid
    for (int i = 0; buffer[i] != '\0'; i++)
    {
        /*The reason we accept '-' outside of the alphabet and spaces is that some words combine to create a different meaning.
        Examples: fire-engine, well-known, well-being etc. but If the user writes hello-their-how-are-you or something, it will be perceived as a single word.*/
        if (!isalpha(buffer[i]) && buffer[i] != ' ' && buffer[i] != '-') // Just alphabet characters or spaces or '-'
        {
            error_message = "\nInvalid character is found\n";
            return buffer;
        }
    }
    return buffer;
}

/*The usage of this function is as follows. First of all, the purpose of the function is to create an array that will fit each text for a text with no specific number of lines.
The array is first opened as 2 elements and then, after each incoming word, if the array is full, it doubles itself with realloc.*/
void addString(char ***array, int *size, int *capacity, const char *newString)
{
    if (*size >= *capacity)
    {
        *capacity *= 2;
        *array = realloc(*array, *capacity * sizeof(char *));
        if (*array == NULL)
        {
            perror("Error reallocating memory");
            exit(EXIT_FAILURE);
        }
    }
    (*array)[*size] = strdup(newString);
    if ((*array)[*size] == NULL)
    {
        perror("Error duplicating string");
        exit(EXIT_FAILURE);
    }
    (*size)++;
}

// Free the memory allocated for the dynamic array
void freeArray(char **array, int size)
{
    for (int i = 0; i < size; i++)
    {
        free(array[i]);
    }
    free(array);
}

// Convert a string to lowercase
void toLowerCase(char *str)
{
    for (int i = 0; str[i]; i++)
    {
        str[i] = tolower(str[i]);
    }
}

// Function to release a list of String array
void freeArrayList(char ***array_list, int *sizes, int count)
{
    for (int i = 0; i < count; i++)
    {
        freeArray(array_list[i], sizes[i]);
    }
    free(array_list);
    free(sizes);
}

// Check if there are words in Array
int isinArray(char **array, int size, const char *word)
{
    for (int i = 0; i < size; i++)
    {
        if (strcmp(array[i], word) == 0)
        {
            return 1; // the word is found
        }
    }
    return 0; // the word is not found
}

/*The main purpose of this function is to separate the received input according to the gaps.
The most important difference from normal separation is that it creates a separate state for repeated words.
Example: hello ege abdullah ege hello hello.
Yes, this example may not make sense.However, we may not know exactly what the user will write and the sentence he will write may contain repetitive words.
ThreadFunction contains the part of adding to the dictionary, which is one of the possibilities.Certain errors may occur in the cases added to the dictionary.
If the user wants to add the first hello, the first hello state should appear in the Levensthein algorithm of the second hello.
This piece of code also does this.The elements in the example are broken down as follows.
first array:hello,ege,abdullah
second array:hello,ege
third array:hello
then the allocated memory is freed with the free functions mentioned in the code fragment.*/
char ***SplitbyRepeatedWords(const char *input, const char *delim, int **sizes, int *count)
{
    char *temp = strdup(input); // Copy input string
    char *token;
    char **current_array = NULL;
    int current_size = 0;

    char ***array_list = NULL; // List holding all arrays
    int array_count = 0;

    // first token
    token = strtok(temp, delim);

    while (token != NULL)
    {
        // If the token already exists in current_array
        toLowerCase(token);
        if (isinArray(current_array, current_size, token))
        {
            // Add the current array to the list
            array_list = realloc(array_list, (array_count + 1) * sizeof(char **));
            *sizes = realloc(*sizes, (array_count + 1) * sizeof(int));
            array_list[array_count] = current_array;
            (*sizes)[array_count] = current_size;
            array_count++;

            // Start a new array
            current_array = NULL;
            current_size = 0;
        }

        // Add token to current_array
        char **temp_array = realloc(current_array, (current_size + 1) * sizeof(char *));
        if (temp_array == NULL)
        {
            error_message = "\nMemory allocation failed\n";
            freeArray(current_array, current_size);
            free(temp);
            freeArrayList(array_list, *sizes, array_count);
            return NULL;
        }
        current_array = temp_array;
        current_array[current_size] = strdup(token);
        current_size++;

        // Next token
        token = strtok(NULL, delim);
    }

    // Add the last remaining array to the list
    if (current_size > 0)
    {
        array_list = realloc(array_list, (array_count + 1) * sizeof(char **));
        *sizes = realloc(*sizes, (array_count + 1) * sizeof(int));
        array_list[array_count] = current_array;
        (*sizes)[array_count] = current_size;
        array_count++;
    }

    free(temp); // Clear copied string
    *count = array_count;
    return array_list;
}

int compareStrings(const void *a, const void *b) // compare two strings according to their ascii code (letter by letter comparison case)
{
    const char *strA = *(const char **)a;
    const char *strB = *(const char **)b;
    return strcmp(strA, strB);
}

/*This is an escape command. The purpose of this command is to clear the terminal when the user wants to enter input once more.*/
void clearScreen(int client_fd)
{
    char clear_cmd[] = "\033[H\033[J";
    send(client_fd, clear_cmd, strlen(clear_cmd), 0);
}
