#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include <errno.h> 
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <vector>
#include <map>

#define TRUE 1 
#define FALSE 0
#define NUM_PEERS   2
#define MAX_MSG_LEN 200
#define MAX_CMD_LEN 256
#define BASE_PORT   20000
#define MAX_MSG_AMT 1000
#define LOCALHOST   "127.0.0.1"

enum server_status {
    FOLLOWER = 0,
    CANDIDATE,
    LEADER
} server_status;

/*----- Message Types -----*/
enum message_type {
    APPEND_ENTRIES=0,
    REQUEST_VOTE,
    VOTE,
    ACK,
    FORWARD,
    COMMIT
} message_type;

typedef struct entry {
    char msg[MAX_MSG_LEN];
    uint16_t term;
    uint16_t message_id;
} entry;

typedef struct message {
    enum message_type type;
    uint16_t message_len;
    uint16_t from; //sender of the message, by server_id
    uint16_t term; 
    char msg[MAX_MSG_LEN];

    uint16_t message_id;    

    // AppendEntries
    uint16_t prevLogIndex;
    uint16_t prevLogTerm;
    uint16_t leaderCommit;
    bool success;
} message;

int createProxySocket(int proxyPort);
int createPeerSocket(int peerPort);