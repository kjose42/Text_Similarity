#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>

#ifndef QSIZE
#define QSIZE 8
#endif

//node that stores word frequency (distribution) with the corresponding word
typedef struct WFD
{
  char *word;
  double distribution;
  struct WFD *next;
} wfd_t;

//stores WFD with its corresponding file's name
//stores file's total word count
struct file_node{
	char* name;
	int count;
	wfd_t *WFDNode;
	struct file_node *next;
};

//initializes new WFD
wfd_t *newWFD(char *buf, int i) {
  wfd_t *newnode = (wfd_t *)malloc(sizeof(wfd_t));
  newnode->word = (char *)malloc(i + 1);
  strcpy(newnode->word, buf);
  newnode->distribution = 1;
  newnode->next = NULL;
  return newnode;
}

//WFD calculation
struct file_node *getWFD(char *file) { 
  int inputFd = open(file, O_RDONLY);
  if (inputFd < 0){
	perror(file);
    	return NULL;}
  int i = 0, wcount = 0, cmp, readcount;
  char *buf = (char *)malloc(256);
  char c;
  wfd_t *head = NULL, *current = NULL, *previous = NULL;
  while ( 1 ) {
	//loop goes through each char
	//once a full word has been read either of two things happen:
	//1. if its the first encounter with this word, a WFD is created
	//2. if its not the first encounter, the word's WFD's distribution is incremented
    readcount = read(inputFd, &c, 1);
    if (!(isspace(c)) && readcount ==1 ) {//if character is not a white space character
      switch (c) {
      case 45:
      case 48 ... 57:
      case 97 ... 122:
        buf[i] = c;
        i++;
        break;
      case 65 ... 90:
        buf[i] = c + 32;
        i++;
        break;
      default:
        break;}
    }
    else { 
      if (readcount==1 && i == 0 ){
	continue;}
      buf[i] = 0;
      if ( !(isspace(c) && readcount == 0)){
	wcount++;}
      else{break;}
      if (wcount == 1) {
	head = newWFD(buf, i);}
      else {
        current = head;
        previous = NULL;
        while (current) { 
        	cmp = strcmp(buf, current->word);
          	if (cmp == 0) { 
            		(current->distribution)++;
            		break;
         	}
          	else{	if (cmp < 0) {
            			if (previous == NULL) { 
              				head = newWFD(buf, i);
              				head->next = current;
              				break;
				}
            			else { 
              				previous->next = newWFD(buf, i);
              				previous->next->next = current;
              				break;
            			}
          		}
          		else { 
            			if (current->next == NULL) { 
              				current->next = newWFD(buf, i);
              				current->next->next = NULL;
              				break;}
          		}
	      	}
          	previous = current;
          	current = current->next;
        }
      }
      i = 0;
    }
    if (readcount==0)
      break;
  }
  current = head;
  while (current) {//calculating word's frequency
    	current->distribution = (current->distribution) / wcount;
	assert((current->distribution >= 0) || (current->distribution <= 1));
    	current = current->next;
  }
  free(buf);
	struct file_node *tempNode = malloc(sizeof(struct file_node));
	tempNode->name = file;
	tempNode->WFDNode = head;
	tempNode->count = wcount;
	tempNode->next = 0;
  return tempNode;
}


void freeWFD(wfd_t *head) {//deallocates memory
  wfd_t *current = head;
  while (head) {
    current = head;
    head = head->next;
    free(current->word);
    free(current);
  }
}

double getJSD(wfd_t *wfd1, wfd_t *wfd2) {
	//calculates Jensen-Shannon distance of two files
  double sumKLD1 = 0, sumKLD2 = 0, mean = 0;
  wfd_t *p1 = wfd1, *p2 = wfd2;
  while (p1 || p2) {
    if (p1 != NULL && p2 != NULL && strcmp(p1->word, p2->word) == 0) {
      mean = (p1->distribution + p2->distribution) / 2;
      sumKLD1 = sumKLD1 + p1->distribution * log2(p1->distribution / mean);
      sumKLD2 = sumKLD2 + p2->distribution * log2(p2->distribution / mean);
      p1 = p1->next;
      p2 = p2->next;
    }
    else if ((p1 == NULL) || (p1 != NULL && p2 != NULL && strcmp(p1->word, p2->word) > 0)) {
      sumKLD2 = sumKLD2 + p2->distribution;
      p2 = p2->next;
    }
    else if ((p2 == NULL) || (p1 != NULL && p2 != NULL && strcmp(p1->word, p2->word) < 0)) {
      sumKLD1 = sumKLD1 + p1->distribution;
      p1 = p1->next;
    }
  }
  return sqrt((sumKLD1 / 2) + (sumKLD2 / 2));
}

struct qNode{//queue node
	char* name;
	struct qNode *next;
};

struct aNode{
	//node for calculating JSD
	//count = total word count of both file1 & file2
	//for the analysis thread
	double JSD;
	char* file1;
	char* file2;
	int count;
};

typedef struct{//queue that contains directories
	struct qNode *head;
	unsigned count;
	unsigned waitingCount;
	unsigned runningCount;
	pthread_mutex_t lock;
	pthread_cond_t readReady;
	pthread_cond_t writeReady;
}dQueue;

typedef struct{//queue that contains file names
	char* data[QSIZE];
	unsigned count;
	unsigned head;
	unsigned runningCount;
	pthread_mutex_t lock;
	pthread_cond_t readReady;
	pthread_cond_t writeReady;
}fQueue;

typedef struct{//queue that contains WFDs
	struct file_node *head;
	unsigned count;
	unsigned runningCount;
	pthread_mutex_t lock;
	pthread_cond_t readReady;
	pthread_cond_t writeReady;
}WFDRepos;

int dirInit(dQueue *Q){//initialize directory queue
	Q->head = 0;
	Q->count = 0;
	Q->waitingCount = 0;
	Q->runningCount = 0;
	pthread_mutex_init(&Q->lock, NULL);
	pthread_cond_init(&Q->readReady, NULL);
	pthread_cond_init(&Q->writeReady, NULL);
	return 0;
}

int fileInit(fQueue *Q){//initialize file queue
	Q->head = 0;
	Q->count = 0;
	Q->runningCount = 0;
	pthread_mutex_init(&Q->lock, NULL);
	pthread_cond_init(&Q->readReady, NULL);
	pthread_cond_init(&Q->writeReady, NULL);
	return 0;
}

int WFDRInit(WFDRepos *Q){//initialize WFD queue
	Q->head = 0;
	Q->count = 0;
	Q->runningCount = 0;
	pthread_mutex_init(&Q->lock, NULL);
	pthread_cond_init(&Q->readReady, NULL);
	pthread_cond_init(&Q->writeReady, NULL);
	return 0;
}

int enqueueD(dQueue *Q, struct qNode *item){//adds to directory queue
	int errorBool = pthread_mutex_lock(&Q->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	struct qNode *tempNode = Q->head;
	Q->head = item;
	Q->head->next = tempNode;
	Q->count = Q->count + 1;
	errorBool = pthread_mutex_unlock(&Q->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	pthread_cond_signal(&Q->readReady);
	return 0;
}

int enqueueWFD(WFDRepos *Q, struct file_node *item){//adds to WFD queue
	int errorBool = pthread_mutex_lock(&Q->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	struct file_node *tempNode = Q->head;
	Q->head = item;
	Q->head->next = tempNode;
	Q->count = Q->count + 1;
	errorBool = pthread_mutex_unlock(&Q->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	pthread_cond_signal(&Q->readReady);
	return 0;
}

void activatefQ(int runningCount, fQueue *fileQueue){
	//once a directory thread finishes,
	//activate file queue threads
	if(runningCount == 0){
		pthread_cond_broadcast(&fileQueue->readReady);
		pthread_cond_broadcast(&fileQueue->writeReady);
		int errorBool = pthread_mutex_unlock(&fileQueue->lock);
		if(errorBool == -1){
			exit(EXIT_FAILURE);}}
}

int dequeueD(dQueue *dQ, fQueue *fQ, struct qNode **item){//dequeue directory queue
	int errorBool = pthread_mutex_lock(&dQ->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	if(dQ->count == 0){
		dQ->waitingCount = dQ->waitingCount - 1;
		if(dQ->waitingCount == 0){
			errorBool = pthread_mutex_unlock(&dQ->lock);
			if(errorBool == -1){
				exit(EXIT_FAILURE);}
			pthread_cond_broadcast(&dQ->readReady);
			dQ->runningCount = dQ->runningCount - 1;
			activatefQ(dQ->runningCount, fQ);
			pthread_exit(NULL);}
		while(!((dQ->waitingCount == 0) || (dQ->count != 0))){
			pthread_cond_wait(&dQ->readReady, &dQ->lock);}
		if(dQ->count == 0){
			dQ->runningCount = dQ->runningCount - 1;
			activatefQ(dQ->runningCount, fQ);
			errorBool = pthread_mutex_unlock(&dQ->lock);
			if(errorBool == -1){
				exit(EXIT_FAILURE);}
			pthread_exit(NULL);}
		else{dQ->waitingCount = dQ->waitingCount + 1;}}
	*item = dQ->head;
	dQ->head = dQ->head->next;
	dQ->count = dQ->count - 1;
	errorBool = pthread_mutex_unlock(&dQ->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	pthread_cond_signal(&dQ->writeReady);
	return 0;
}

int enqueueF(fQueue *fQ, dQueue *dQ, char* item){//add to file queue
	int errorBool = pthread_mutex_lock(&fQ->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	while(fQ->count == QSIZE){
		pthread_cond_wait(&fQ->writeReady, &fQ->lock);}
	unsigned index = fQ->head + fQ->count;
	if(index >= QSIZE){
		index -= QSIZE;}
	fQ->data[index] = item;
	fQ->count = fQ->count + 1;
	pthread_cond_signal(&fQ->readReady);
	errorBool = pthread_mutex_unlock(&fQ->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	return 0;
}


int dequeueF(fQueue *fQ, dQueue *dQ, char **item){//dequeue from file queue
	int errorBool = pthread_mutex_lock(&fQ->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	if(fQ->count == 0){
		while(!((fQ->count != 0) || (dQ->runningCount == 0))){
			pthread_cond_wait(&fQ->readReady, &fQ->lock);}
		if(fQ->count == 0){
			errorBool = pthread_mutex_unlock(&fQ->lock);
			if(errorBool == -1){
				exit(EXIT_FAILURE);}
			pthread_exit(NULL);}}
	*item = fQ->data[fQ->head];
	fQ->head = fQ->head + 1;
	fQ->count = fQ->count - 1;
	if(fQ->head == QSIZE){
		fQ->head = 0;}
	pthread_cond_signal(&fQ->writeReady);
	errorBool = pthread_mutex_unlock(&fQ->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	return 0;
}

struct file_node *dequeueWFDR(WFDRepos *WFDR){//dequeue from WFD queue
	struct file_node *item;
	int errorBool = pthread_mutex_lock(&WFDR->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	item = WFDR->head;
	WFDR->head = WFDR->head->next;
	--WFDR->count;
	errorBool = pthread_mutex_unlock(&WFDR->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	pthread_cond_signal(&WFDR->writeReady);
	return item;
}

typedef struct{//thread arguments
	char *suf; //suffix of file inputs
	fQueue *fQ;
	dQueue *dQ;
	WFDRepos *repos;
	struct aNode **array; //for output
	int error;
}targs;


int isReg(char *name){//checks if string is a file
	struct stat data;
	int err = stat(name, &data);
	if(err == 1){
		perror(name);
		return -1;}
	if(S_ISREG(data.st_mode) == 1){
		return 1;}
	return 0;
}

int isDir(char *name){//checks if string is a directory
	struct stat data;
	int err = stat(name, &data);
	if(err == 1){
		perror(name);
		return -1;}
	if(S_ISDIR(data.st_mode) == 1){
		return 1;}
	return 0;
}

void *aThread(void *A){//Analysis thread (aThread) is a thread for calculating JSD
	targs *args = A;
	WFDRepos *queueCopy = args->repos;
	int count = 0;
	while(queueCopy->count != 0){
		struct file_node *wfdFile1 = dequeueWFDR(queueCopy);
		wfd_t *wfd1 = wfdFile1->WFDNode;
		struct file_node *wfdFile2 = wfdFile1->next;
		while(wfdFile2 != NULL){
			wfd_t *wfd2 = wfdFile2->WFDNode;
			double JSD = getJSD(wfd1, wfd2);
			struct aNode *tempNode = malloc(sizeof(struct aNode));
			tempNode->JSD = JSD;
			int sLength = strlen(wfdFile1->name) + 1;
			tempNode->file1 = malloc(sLength*sizeof(char));
			strcpy(tempNode->file1, wfdFile1->name);
			sLength = strlen(wfdFile2->name) + 1;
			tempNode->file2 = malloc(sLength*sizeof(char));
			strcpy(tempNode->file2, wfdFile2->name);
			tempNode->count = wfdFile1->count + wfdFile2->count;
			args->array[count] = tempNode;
			wfdFile2 = wfdFile2->next;
			count = count + 1;}
		freeWFD(wfdFile1->WFDNode);
		free(wfdFile1->name);
		free(wfdFile1);}
	return NULL;
}

void *dirThread(void *A){
	//goes through a directory and enqueues 2 things
	//1. enqueues the directory's files into the file queue
	//2. enqueues subdirectories into the directory queue
	targs *args = A;
	dQueue *queueCopy = args->dQ;
	while(!((queueCopy->waitingCount == 0) && (queueCopy->count == 0))){
		struct qNode *tempNode;
		dequeueD(queueCopy, args->fQ, &tempNode);
		char* dirString = tempNode->name;
		struct dirent *de;
		DIR *dirp = opendir(dirString);
		if(dirp == NULL){
			args->error = 1;}
		else{while((de = readdir(dirp))){
			char *suffix = args->suf;
			char *checkSuffix = de->d_name+strlen(de->d_name)-strlen(suffix);
				if(strncmp(de->d_name, ".", 1) != 0){
					int changeLength = 2+strlen(de->d_name)+strlen(dirString);
					char *changeName = malloc (sizeof(char)*changeLength);
					strcpy(changeName, dirString);
					strcpy(changeName+strlen(dirString), "/");
					strcpy(changeName+strlen(dirString)+1, de->d_name);
					int checkInput = isReg(changeName);
					if(checkInput == 1){//if file, add to file queue
						if((strncmp(checkSuffix, suffix, strlen(suffix)) == 0) || (strlen(suffix) == 0)){
							enqueueF(args->fQ, queueCopy, changeName);}
						else{free(changeName);}}
					else{checkInput = isDir(changeName);//if directory, add to directory queue
						if(checkInput == 1){
							struct qNode *newNode = malloc(sizeof(struct qNode));
							newNode->name = changeName;
							enqueueD(queueCopy, newNode);}
						else{free(changeName);}}}}}
		closedir(dirp);
		free(dirString);
		free(tempNode);}
	queueCopy->runningCount = queueCopy->runningCount-1;
	fQueue *sideQueue = args->fQ;
	activatefQ(queueCopy->runningCount, sideQueue);
	int errorBool = pthread_mutex_unlock(&queueCopy->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	return NULL;
}

void *fileThread(void *A){//calculates WFDs for a file and adds to the WFD queue
	targs *args = A;
	fQueue *queueCopy = args->fQ;
	while(!((queueCopy->count == 0) && (args->dQ->runningCount == 0))){
		char* name;
		dequeueF(queueCopy, args->dQ, &name);
		struct file_node *tempFileN = getWFD(name);
		if(tempFileN == NULL){
			args->error = 1;}
		else{enqueueWFD(args->repos, tempFileN);}}
	int errorBool = pthread_mutex_unlock(&queueCopy->lock);
	if(errorBool == -1){
		exit(EXIT_FAILURE);}
	return NULL;
}

void quickSort(int start, int end, struct aNode **results){
	//quick sorts the output array
	//the order is descending order based on the total word count of the two files
	int inc1 = 0;
	int inc2 = 0;
	int pivot = 0;
	struct aNode* tempNode;
	if(start < end){
		inc1 = start;
		inc2 = end;
		pivot = start;
		while(inc1 < inc2){
			while((results[inc1]->count <= results[pivot]->count) && (inc1 < end)){
				inc1 = inc1 + 1;}
			while(results[inc2]->count > results[pivot]->count){
				inc2 = inc2 - 1;}
			if(inc1 < inc2){
				tempNode = results[inc1];
				results[inc1] = results[inc2];
				results[inc2] = tempNode;}}
		tempNode = results[pivot];
		results[pivot] = results[inc2];
		results[inc2] = tempNode;
		quickSort(start, inc2-1, results);
		quickSort(inc2+1, end, results);}
}

int main (int argc, char **argv){
	fQueue fileQ;
	fileInit(&fileQ);
	dQueue dirQ;
	dirInit(&dirQ);
	WFDRepos WFDR;
	WFDRInit(&WFDR);
	char* suffix = ".txt";
	int dirNum = 1;
	int fileNum = 1;
	int aNum = 1;
	for(int inc = 1; inc < argc; inc++){//handling optional arguments
		char* input = argv[inc];
		if(strncmp(input, "-a", 2) == 0){
			if(strlen(input) == 2){
				write(2, "Invalid input\n", 14);
				return EXIT_FAILURE;}
			aNum = atoi(input+2);
			if(aNum <= 0){
				write(2, "Invalid input\n", 14);
				return EXIT_FAILURE;}
			continue;}
		if(strncmp(input, "-d", 2) == 0){
			if(strlen(input) == 2){
				write(2, "Invalid input\n", 14);
				return EXIT_FAILURE;}
			dirNum = atoi(input+2);
			if(dirNum <= 0){
				write(2, "Invalid input\n", 14);
				return EXIT_FAILURE;}
			continue;}
		if(strncmp(input, "-f", 2) == 0){
			if(strlen(input) == 2){
				write(2, "Invalid input\n", 14);
				return EXIT_FAILURE;}
			fileNum = atoi(input+2);
			if(fileNum <= 0){
				write(2, "Invalid input\n", 14);
				return EXIT_FAILURE;}
			continue;}
		if(strncmp(input, "-s", 2) == 0){
			if(strlen(input) == 2){
				suffix = "";
				continue;}
			else{suffix = input+2;
				continue;}}
		if(strncmp(input, "-", 1) == 0){
			write(2, "Invalid input\n", 14);
			return EXIT_FAILURE;}}
	fileQ.runningCount = fileNum;
	dirQ.waitingCount = dirNum;
	dirQ.runningCount = dirNum;
	WFDR.runningCount = aNum;
	int threadCount = dirNum+fileNum+aNum;
	pthread_t *tids = malloc(threadCount*sizeof(pthread_t));
	targs *args = malloc(threadCount*sizeof(targs));
	int errorBool = 0;
	for(int inc = 0; inc < fileNum; inc++){//initializing file threads
		args[inc].suf = suffix;
		args[inc].fQ = &fileQ;
		args[inc].dQ = &dirQ;
		args[inc].repos = &WFDR;
		args[inc].error = 0;
		errorBool = pthread_create(&tids[inc], NULL, fileThread, &args[inc]);
		if(errorBool != 0){
			return EXIT_FAILURE;}}
	for(int inc = 1; inc < argc; inc++){
		//enqueueing files & directories (from input) into their respective queues
		int sLength = strlen(argv[inc]) + 1;
		char* input = malloc(sLength*sizeof(char));
		strcpy(input, argv[inc]);
		if(strncmp(input, "-", 1) == 0){
			free(input);
			continue;}
		int checkInput = isReg(input);
		if(checkInput == -1){
			free(input);
			return EXIT_FAILURE;}
		if(checkInput == 1){
			enqueueF(&fileQ, &dirQ, input);}
		checkInput = isDir(input);
		if(checkInput == -1){
			free(input);
			return EXIT_FAILURE;}
		if(checkInput == 1){
			struct qNode *tempNode = malloc(sizeof(struct qNode));
			tempNode->name = input;
			enqueueD(&dirQ, tempNode);}}
	for(int inc = fileNum; inc < dirNum+fileNum; inc++){//initializing directory threads
		args[inc].suf = suffix;
		args[inc].fQ = &fileQ;
		args[inc].dQ = &dirQ;
		errorBool = pthread_create(&tids[inc], NULL, dirThread, &args[inc]);
		if(errorBool != 0){
			return EXIT_FAILURE;}}
	for(int inc = 0; inc < fileNum+dirNum; inc++){
		pthread_join(tids[inc], NULL);}
	for(int inc = 0; inc < fileNum+dirNum; inc++){
		//check if there was any error opening a file/directory
		if(args[inc].error == 1){
			return EXIT_FAILURE;}}
	if(WFDR.count < 2){
		write(2, "Not enough files\n", 17);
		return EXIT_FAILURE;}
	unsigned comparisons = WFDR.count*(WFDR.count-1)/2;
	struct aNode **array = malloc(comparisons*sizeof(struct aNode));
	for(int inc = fileNum + dirNum; inc < threadCount; inc++){
		//initializing analysis threads
		args[inc].repos = &WFDR;
		args[inc].array = array;
		errorBool = pthread_create(&tids[inc], NULL, aThread, &args[inc]);
		if(errorBool != 0){
			return EXIT_FAILURE;}}
	for(int inc = fileNum+dirNum; inc < threadCount; inc++){
		pthread_join(tids[inc], NULL);}
	quickSort(0, comparisons-1, array);
	for(int inc = comparisons-1; inc >= 0; inc--){
		struct aNode *tempNode = array[inc];
		printf("%0.5f %s %s\n", tempNode->JSD, tempNode->file1, tempNode->file2);
		free(tempNode->file1);
		free(tempNode->file2);
		free(tempNode);}
	free(array);
	free(tids);
	free(args);
	return EXIT_SUCCESS;
}