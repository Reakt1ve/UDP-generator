#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <regex.h>
#include <sys/stat.h>
#include <pthread.h>

#define PORT 1516
#define MAXLINE 13108 // 13.108 Kb
#define MAX_IP_BUFFER 15
#define MAX_LOCAL_HOST_BUFFER 9
#define LOCAL_HOST "127.0.0.1"
#define REG_IPTEMPLATE "^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}$"
#define PACKAGE_WINDOW 90000 // 0.9 ms
#define RUNNING_TIME 6000   // 60 min.
#define TRUE 1
#define FALSE 0

int PACKETSPERSEC = 0;
_Bool RUNNING = TRUE;

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

typedef struct PackageMonitorEntity {
    int numPackages;
    int seconds;
    float uploadSpeed;
    _Bool headerIsNotCreated;
} PackageMonitorEntity;

typedef struct MonitorThreadArgs {
    int trigger;
    void (*func)(struct PackageMonitorEntity *);
    struct PackageMonitorEntity *pm;
} ThreadArgs;

int cfileexists (const char* filename);
void createConfigurateFile ();
void getIPFileInfo (char* ip_address);
void printMonitorPackagesInfo (struct PackageMonitorEntity *pm);
void *clockTimer (void *args);

int main(int argc, char **argv) {
    int sock;
    char buffer[MAXLINE];
    char ip_address[MAX_IP_BUFFER];
    //char *hello = "Hello from client";
    struct sockaddr_in servaddr;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(buffer, 0, MAXLINE);

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    createConfigurateFile ();
    getIPFileInfo (ip_address);
    //printf("IP: %s\n", ip_address);
    servaddr.sin_addr.s_addr = inet_addr(ip_address);

    // DATA SENDING

    struct timespec tw = {0, PACKAGE_WINDOW};
    struct timespec tr;
    int numDgram = 0;

    char *data = buffer + sizeof(struct IPHeader) + sizeof(struct UDPHeader) + sizeof(struct EUDPHeader);
    int meta_size = sizeof(struct IPHeader) + sizeof(struct UDPHeader) + sizeof(struct EUDPHeader);
    for (int i = 0; i < MAXLINE - meta_size; i++) {
        data[i] = '1';
    }

    // IP Layer

    struct IPHeader *iphdr = (struct IPHeader *) buffer;
    iphdr->iph_length = sizeof(struct IPHeader) + sizeof(struct UDPHeader) + sizeof(struct EUDPHeader) + strlen(data);

    // UDP Layer

    struct UDPHeader *udphdr = (struct UDPHeader *) (buffer + sizeof(struct IPHeader));
    udphdr->uh_len = sizeof(struct UDPHeader) + sizeof(EUDPHeader) + strlen(data);

    // EUDP Layer

    struct EUDPHeader *eudp = (struct EUDPHeader *) (buffer + sizeof(struct IPHeader) + sizeof(struct UDPHeader));
    eudp->eudp_len = sizeof(EUDPHeader) + strlen(data);
    eudp->eudp_sequence = 0;

    struct PackageMonitorEntity pm;
    pm.numPackages = 0;
    pm.seconds = 0;
    pm.uploadSpeed = 0.0;
    pm.headerIsNotCreated = TRUE;

    time_t finishRunning = RUNNING_TIME;
    time_t start, finish;  // 60sec.

    _Bool threadIsNotcreated = TRUE;
    pthread_t monitorThread;
    pthread_attr_t monitorAttr;
    start = time(NULL);
    while (RUNNING) {
        if (threadIsNotcreated) {       // Creating a new thread for RuntimeMonitor
                struct MonitorThreadArgs *thArgs = (struct MonitorThreadArgs *) malloc(sizeof(struct MonitorThreadArgs));
                thArgs->func = printMonitorPackagesInfo;
                thArgs->pm = &pm;
                thArgs->trigger = 1;  // 1 sec.
                pthread_attr_init(&monitorAttr);
                pthread_create(&monitorThread, &monitorAttr, clockTimer, (void *) thArgs);
                threadIsNotcreated = FALSE;
        }

        eudp->eudp_sequence++;
        if (sendto(sock, buffer, iphdr->iph_length, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
            perror("SendTo is failed");
            exit(EXIT_FAILURE);
        }

        PACKETSPERSEC++;
        nanosleep(&tw, &tr);

        finish = time(NULL);
        if (finish - start > finishRunning) {
            RUNNING = FALSE;
        }
    }

    pthread_join(monitorThread, NULL); // Close child thread
    close(sock);
    return 0;
}

void printMonitorPackagesInfo (struct PackageMonitorEntity *pm) {
    if (pm->headerIsNotCreated) {
        printf("  Seconds  |     Upload Speed     |        Bitrate         |      Count Packages\n");
        pm->headerIsNotCreated = FALSE;
    }

    printf("       %ds          %f MB/s          %f Mbit/s       %d\n",
    pm->seconds, pm->uploadSpeed, pm->uploadSpeed * 8, pm->numPackages);
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
            thArgs->pm->uploadSpeed = (float)(((float)MAXLINE * (float)PACKETSPERSEC) / 1000 / 1024);
            thArgs->func(thArgs->pm);
            start = time(NULL);
        }
    }

    pthread_exit(0);
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
    printf("IP: %s\n", file_buff);

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
    printf("reti: %d\n", reti);

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
