#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <regex.h>
#include <sys/stat.h>
#include <pthread.h>
#include "threadRingBuffer.h"

#define PORT 1516
#define MAXLINE 13108 // 13.108 Kb
#define MAX_UDP_BUFFER 10000 // 2k packages
#define MAX_IP_BUFFER 12
#define TRUE 1
#define FALSE 0
#define REG_IPTEMPLATE "^[0-9]{1,3}.[0-9]{1,3}.[0-9]{1,3}.[0-9]{1,3}$"
#define MAX_LOCAL_HOST_BUFFER 9
#define LOCAL_HOST "127.0.0.1"

int PDATA_SIZE = 0;
int PACKETSPERSEC = 0;
_Bool RUNNING = TRUE;
struct ThreadRingBuffer *t_ringBuffer;

enum log_event {BAD_PACKAGE = 1, LOST_PACKAGE, IDLE, INIT, STOP};

typedef struct EUDPHeader {
    unsigned int eudp_sequence;
    unsigned short int eudp_len;
} EUDPHeader;

typedef struct IPHeader {
	unsigned char iph_verlen;
	unsigned char iph_tos;
	unsigned short int iph_length;
	unsigned short int iph_id;
	unsigned short int iph_offset;
	unsigned char iph_ttl;
	unsigned char iph_protocol;
	unsigned short int iph_xsum;
	unsigned long int iph_src;
	unsigned long int iph_dest;
} IPHeader;

typedef struct UDPHeader {
	unsigned short int uh_sport;
	unsigned short int uh_dport;
	unsigned short int uh_len;
	unsigned short int uh_check;
} UDPHeader;

typedef struct Result {
    int numDgrams;
    unsigned long long int numUnits;
    int avgNumWrongPos;
    float wrongUnitsPercent;
    float lostPackagesPercent;
    float badPackagesPercent;
    int numLostPackages;
    int numBadPackages;
    struct CounterErrors *ce;
} Result;

typedef struct PackageMonitorEntity {
    int numPackages;
    int seconds;
    float downloadSpeed;
    _Bool headerIsNotCreated;
} PackageMonitorEntity;

typedef struct MonitorThreadArgs {
    int trigger;
    void (*func)(struct PackageMonitorEntity *);
    struct PackageMonitorEntity *pm;
} ThreadArgs;

typedef struct CounterErrors {
    int size;
    int *counter_buff;
} CounterErrors;

void initServerSocket (int *sock, struct sockaddr_in servaddr, struct timeval timeout);
void getAvgWrongPos (struct Result **res);
_Bool getData (int sock, char *buffer, struct sockaddr_in cliaddr, int *numDgrams);
float getPercentWrongUnits (struct Result **res);
float getLostPackagesPercent (struct Result **res);
float getBadPackagesPercent (struct Result **res);
void printMonitorPackagesInfo (struct PackageMonitorEntity *pm);
void *clockTimer (void *args);
void *handlePackageData (void *args);
void getResultToText (struct Result *res);
void getIPFileInfo ();
void saveInLogFile (enum log_event event);
int checkLostPackage (char* buffer, int *prevValSeq);
_Bool checkBadPackage (char* buffer, struct Result **res);
int cfileexists(const char* filename);
void createConfigurateFile ();

int main(int argc, char **argv) {
    int sock = 0;
    char *buffer = (char *) malloc (sizeof(char) * MAXLINE);
    char ip_address[MAX_IP_BUFFER];
    struct sockaddr_in servaddr, cliaddr;

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    memset(&cliaddr, 0, sizeof(cliaddr));
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    createConfigurateFile ();
    getIPFileInfo (ip_address);
    //printf("IP: %s\n", ip_address);
    servaddr.sin_addr.s_addr = inet_addr(ip_address);
    servaddr.sin_port = htons(PORT);

    initServerSocket (&sock, servaddr, timeout);

    // ANALYSIS

    struct PackageMonitorEntity pm;
    pm.numPackages = 0;
    pm.seconds = 0;
    pm.downloadSpeed = 0.0;
    pm.headerIsNotCreated = TRUE;

    struct Result *res = (struct Result *) malloc (sizeof(struct Result));
    res->ce = (struct CounterErrors *) malloc (sizeof(struct CounterErrors));
    res->ce->size = 1;
    res->ce->counter_buff = (int *) malloc (sizeof(int) * res->ce->size);
    res->ce->counter_buff[0] = 0;
    res->avgNumWrongPos = 0;
    res->numBadPackages = 0;
    res->numDgrams = 0;
    res->numLostPackages = 0;
    res->numUnits = 0;
    res->wrongUnitsPercent = 0.0;
    res->badPackagesPercent = 0.0;
    res->lostPackagesPercent = 0.0;

    // init threads
    _Bool threadMonitorIsNotcreated = TRUE;
    _Bool threadHandlerISNotcreated = TRUE;
    pthread_t monitorThread, handlerThread;
    pthread_attr_t monitorAttr, handlerAttr;

    t_ringBuffer = (struct ThreadRingBuffer *) malloc (sizeof(struct ThreadRingBuffer));
    if (initRingBuffer(t_ringBuffer, MAX_UDP_BUFFER, MAXLINE) != TRUE) {
        perror("init Ring buffer is failed!!!");
        exit(EXIT_FAILURE);
    }


    saveInLogFile(INIT);
    printf("Waiting incoming data....\n\n");

    while (1) {
        RUNNING = getData (sock, buffer, cliaddr, &res->numDgrams);
        if (RUNNING) {

            if (threadHandlerISNotcreated) {       // Creating an extra thread to handle data
                pthread_attr_init(&handlerAttr);
                pthread_create(&handlerThread, &handlerAttr, handlePackageData, (void *) res);
                threadHandlerISNotcreated = FALSE;
            }

            if (threadMonitorIsNotcreated) {       // Creating a new thread for RuntimeMonitor
                struct MonitorThreadArgs *thMontArgs = (struct MonitorThreadArgs *) malloc(sizeof(struct MonitorThreadArgs));
                thMontArgs->func = printMonitorPackagesInfo;
                thMontArgs->pm = &pm;
                thMontArgs->trigger = 1;  // 1 sec.
                pthread_attr_init(&monitorAttr);
                pthread_create(&monitorThread, &monitorAttr, clockTimer, (void *) thMontArgs);
                threadMonitorIsNotcreated = FALSE;
            }

        } else {
            getAvgWrongPos (&res);
            res->wrongUnitsPercent = getPercentWrongUnits (&res);
            res->lostPackagesPercent = getLostPackagesPercent (&res);
            res->badPackagesPercent = getBadPackagesPercent (&res);
            pthread_join(handlerThread, NULL); // Close child thread
            break;
        }
    }
    saveInLogFile(STOP);

    pthread_join(monitorThread, NULL); // Close child thread
    getResultToText(res);

    return 0;
}

_Bool getData (int sock, char *buffer, struct sockaddr_in cliaddr, int *numDgrams) {
    int n, len;
    len = sizeof(cliaddr);

    n = recvfrom(sock, buffer, MAXLINE, MSG_WAITALL, ( struct sockaddr *) &cliaddr, &len);
    if (errno == EAGAIN) {
        return 0;
    }

    (*numDgrams)++;
    PACKETSPERSEC++;

    //char *text = (char *) (buffer + sizeof(struct IPHeader) + sizeof(struct UDPHeader) + sizeof(struct EUDPHeader));
    //printf("\ntext: %s\n",text);
    putPackageRingBuffer (t_ringBuffer, buffer);
    //printf("numDgram: %d\n", *numDgrams);

    return 1;
}

float getPercentWrongUnits (struct Result **res) {
    if (res[0]->numDgrams == 0) {
        return 0.0;
    }
    return (1 - ((float)res[0]->numUnits / ((float)res[0]->numDgrams * (float)PDATA_SIZE))) * 100;
}

float getLostPackagesPercent (struct Result **res) {
    if (res[0]->numDgrams == 0) {
        return 0.0;
    }
    return ((float)res[0]->numLostPackages / ((float)res[0]->numDgrams + (float)res[0]->numLostPackages)) * 100;
}

float getBadPackagesPercent (struct Result **res) {
    if (res[0]->numDgrams == 0) {
        return 0.0;
    }
    return ((float)res[0]->numBadPackages / (float)res[0]->numDgrams) * 100;
}

void getResultToText (struct Result *res) {
    printf("\n\n\t\t\t\tStatistic\n\n");
    printf("\nSummary datagrams: %d\n", res->numDgrams);
    printf("Data size: %d\n", PDATA_SIZE);
    printf("Num of lost packages: %d\n", res->numLostPackages);
    printf("Num of bad packages: %d\n", res->numBadPackages);
    printf("Average count of wrong positions of datagrams (of 10):  %d\n", res->avgNumWrongPos);
    printf("Num of units: %llu\n", res->numUnits);
    printf("Percent of wrong units: %f%%\n", res->wrongUnitsPercent);
    printf("Percent of bad packages: %f%%\n", res->badPackagesPercent);
    printf("Percent of lost packages: %f%%\n", res->lostPackagesPercent);
}

void printMonitorPackagesInfo (struct PackageMonitorEntity *pm) {
    if (pm->headerIsNotCreated) {
        printf("  Seconds  |     Download Speed     |        Bitrate         |      Count Packages\n");
        pm->headerIsNotCreated = FALSE;
    }

    printf("       %ds          %f MB/s          %f Mbit/s       %d\n",
    pm->seconds, pm->downloadSpeed, pm->downloadSpeed * 8, pm->numPackages);
    pm->seconds++;
    PACKETSPERSEC = 0;
}

void *clockTimer (void *args) {
    struct MonitorThreadArgs *thArgs = (struct MonitorThreadArgs *) args;
    time_t start = time(NULL);
    while (RUNNING) {
        time_t difference = time(NULL) - start;
        if (difference >= thArgs->trigger) {                                                                          // To Active timer
            thArgs->pm->numPackages = PACKETSPERSEC;
            thArgs->pm->downloadSpeed = (float)(((float)MAXLINE * (float)PACKETSPERSEC) / 1000 / 1024);
            thArgs->func(thArgs->pm);
            start = time(NULL);
        }
    }

    pthread_exit(0);
}

void *handlePackageData (void *args) {
    struct Result *res = (struct Result *) args;
    char *buffer = (char *) malloc (sizeof(char));
    int prevSequence = 0;

    while (RUNNING || !(pairingIsHappened(t_ringBuffer, READER_WRITTER))) {
        if (getPackageRingBuffer (t_ringBuffer, &buffer) == NON_CRITICAL) {
            res->numLostPackages += checkLostPackage (buffer, &prevSequence);
            res->numBadPackages += (int)checkBadPackage (buffer, &res);
        }
    }

    pthread_exit(0);
}

void initServerSocket (int *sock, struct sockaddr_in servaddr, struct timeval timeout) {
    if ((*sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Socket options

    if (setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (bind(*sock, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0 ) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
}

void getAvgWrongPos (struct Result **res) {
    int sum_wpos = 0;
    for (int j = 0; j < res[0]->ce->size; j++) {
        sum_wpos += res[0]->ce->counter_buff[j];
    }

    res[0]->avgNumWrongPos = sum_wpos / res[0]->ce->size;
    printf("\nsize: %d\n", sum_wpos);
    printf("\nsize: %d\n", res[0]->ce->size);
    printf("\navg: %d\n", res[0]->avgNumWrongPos);
}

int checkLostPackage (char *buffer, int *prevValSeq) {
    //printf("\nbuffer in Lost packages: %p\n", buffer);
    struct EUDPHeader *eudphdr = (struct EUDPHeader *) (buffer + sizeof(struct IPHeader) + sizeof(struct UDPHeader));
    //printf("\nseq = %d\n",eudphdr->eudp_sequence);
    int lost_window = eudphdr->eudp_sequence - (*prevValSeq);

    if (lost_window != 1) {
        for (int i = 0; i < lost_window - 1; i++) {
            saveInLogFile (LOST_PACKAGE);
        }
    }

    *prevValSeq = eudphdr->eudp_sequence;
    return lost_window - 1;
}

_Bool checkBadPackage (char* buffer, struct Result **res) {
    _Bool save = TRUE;
    char *text = (char *) (buffer + sizeof(struct IPHeader) + sizeof(struct UDPHeader) + sizeof(struct EUDPHeader));
    PDATA_SIZE = MAXLINE - (sizeof(struct IPHeader) + sizeof(struct UDPHeader) + sizeof(struct EUDPHeader));

    //printf("\ntext: %s\n",text);

    for (int i = 0; i < PDATA_SIZE; i++) {
        if (text[i] != '1') {
            int local_size = res[0]->ce->size;
            res[0]->ce->counter_buff[local_size - 1]++; // ASSERT TEXT VALUES

            if (save) {
                int *temp = NULL;
                temp = (int *) realloc (res[0]->ce->counter_buff, (res[0]->ce->size + 1) * sizeof(int));  // Dynamic allocation of extra memory for counter_buff
                if (temp == NULL) {                                                                       // in case of bad package
                    perror("counters reallocation is failed!!!");
                    exit(EXIT_FAILURE);
                }
                res[0]->ce->size++;
                res[0]->ce->counter_buff = temp;
                res[0]->ce->counter_buff[res[0]->ce->size - 1] = 0;

                saveInLogFile (BAD_PACKAGE);
                save = FALSE;
            }
        } else {
            res[0]->numUnits++;
        }
    }

    //printf("\nnumUnits: %llu\n", *numUnits); // test
    return (!save);
}

void saveInLogFile (enum log_event event) {
    FILE *file;

    if (event == INIT) {
        if (mkdir("/var/log/inspector", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0 && errno != EEXIST) {
            perror("Folder error");
            exit(EXIT_FAILURE);
        }

        file = fopen("/var/log/inspector/events.log", "a");
        if (file == NULL) {
            perror("File error");
            exit(EXIT_FAILURE);
        }

        long int timestamp;
        timestamp = time(NULL);
        fprintf(file, "Start initilization : %s", ctime(&timestamp));
    }

    if (event == STOP) {
        if (mkdir("/var/log/inspector", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0 && errno != EEXIST) {
            perror("Folder error");
            exit(EXIT_FAILURE);
        }

        file = fopen("/var/log/inspector/events.log", "a");
        if (file == NULL) {
            perror("File error");
            exit(EXIT_FAILURE);
        }

        long int timestamp;
        timestamp = time(NULL);
        fprintf(file, "Finish running : %s", ctime(&timestamp));
    }

    else if (event == BAD_PACKAGE) {
        if (mkdir("/var/log/inspector", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0 && errno != EEXIST) {
            perror("Folder error");
            exit(EXIT_FAILURE);
        }

        file = fopen("/var/log/inspector/events.log", "a");
        if (file == NULL) {
            perror("File error");
            exit(EXIT_FAILURE);
        }

        long int timestamp;
        timestamp = time(NULL);
        fprintf(file, "Bad package is detected : %s", ctime(&timestamp));
    }

    else if (event == LOST_PACKAGE) {
        if (mkdir("/var/log/inspector", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0 && errno != EEXIST) {
            perror("Folder error");
            exit(EXIT_FAILURE);
        }

        file = fopen("/var/log/inspector/events.log", "a");
        if (file == NULL) {
            perror("File error");
            exit(EXIT_FAILURE);
        }

        long int timestamp;
        timestamp = time(NULL);
        fprintf(file, "Lost package is detected : %s", ctime(&timestamp));
    }

    fclose(file);
}

int cfileexists(const char* filename) {
    struct stat buffer;
    int exist = stat(filename, &buffer);

    if(exist == 0) {
        return 1;
    } else {
        return 0;
    }
}

void createConfigurateFile () {
    if (!cfileexists("/etc/inspector/ip.conf")) {
        FILE *file;

        if (mkdir("/etc/inspector", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0 && errno != EEXIST) {
            perror("Folder error");
            exit(EXIT_FAILURE);
        }

        file = fopen("/etc/inspector/ip.conf", "a");
        if (file == NULL) {
            perror("File error");
            exit(EXIT_FAILURE);
        }

        fprintf(file, "%s", LOCAL_HOST);
        fclose(file);
    }
}

void getIPFileInfo (char* ip_address) {
    FILE *file;

    file = fopen("/etc/inspector/ip.conf", "r");
    if (file == NULL) {
        perror("File error");
        exit(EXIT_FAILURE);
    }

    char file_buff[MAX_IP_BUFFER] = {0};
    fscanf(file, "%s", file_buff);
    fclose(file);
    //printf("IP: %s\n", file_buff);

    // regular

    regex_t regex;
    int reti;
    reti = regcomp(&regex, REG_IPTEMPLATE, REG_EXTENDED);

    if (reti) {
        perror("Could not compile regex");
        exit(EXIT_FAILURE);
    }

    reti = regexec(&regex, file_buff, 0, NULL, 0);
    regfree(&regex);
    //printf("reti: %d\n", reti);

    if (!reti) {
        for (int i = 0; i < MAX_IP_BUFFER; i++) {
            ip_address[i] = file_buff[i];
        }
    }

    else if (reti == REG_NOMATCH) {
        char *local_host = LOCAL_HOST;
        for (int i = 0; i < MAX_LOCAL_HOST_BUFFER; i++) {
            ip_address[i] = local_host[i];
        }
    } else {
        char msgbuf[100];
        regerror(reti, &regex, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
        exit(EXIT_FAILURE);
    }
}
