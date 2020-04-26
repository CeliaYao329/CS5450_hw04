#include "process.h"
FILE *output;
int NUM_SERVER;

char *timestamp() {
    // time_t ltime; /* calendar time */
    // ltime=time(NULL); /* get current cal time */
    // sprintf(time_str, "%s", asctime(localtime(&ltime)));
    char time_str[50];
    int time_len = 0, n;
    struct tm *tm_info;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);
    time_len += strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    time_len += snprintf(time_str + strlen(time_str), sizeof(time_str) - time_len, ".%04ld ", tv.tv_usec / 1000);
    return time_str;
}

class Node {
public:
    // Communication variables
    uint16_t serverID;
    int proxyPort, peerPort;
    int proxy_socket, peer_socket;
    char *fwd_buffers[MAX_MSG_AMT];

    // Persistent state
    enum server_status status = FOLLOWER;
    uint16_t currentTerm = 0;     // latest term server has seen (initialized to 0 on first boot, increases monotonically)
    int16_t votedFor = -1;        // candidateId that received vote in current term (or -1 if none)
    entry log[MAX_MSG_AMT] = {0}; // log entries; each entry contains command for state machine, and term when entry was received by leader (first index is 1)
    int16_t curLeader = -1;

    //Volatile state
    uint16_t commitIndex; // index of highest log entry known to be committed (initialized to 0, increases monotonically)
    uint16_t lastApplied;

    //Volatile state for leader
    std::vector<uint16_t> nextIndex;
    std::vector<uint16_t> matchIndex;

    //Volatile state for candidate;
    int vote;

    Node(uint16_t _serverID, int _proxyPort);
    void apply();
    void runElection();
    void sendHeartbeats();
    void follower_handler(message *msg);
    void candidate_handler(message *msg);
    void leader_handler(message *msg);

    int sendMsg(int socket, message *m, int peerPort) {
        struct sockaddr_in peerAddr;
        memset((char *)&peerAddr, 0, sizeof(peerAddr));
        peerAddr.sin_family = AF_INET;
        peerAddr.sin_addr.s_addr = INADDR_ANY;
        peerAddr.sin_port = htons(peerPort);
        int valsend = sendto(socket, m, sizeof(struct message), 0, (struct sockaddr *)&peerAddr, sizeof(peerAddr));
        if (valsend > -1) {
            // printf("Success: Sent.\n", valsend, peerPort);
        } else {
            printf("ERROR: sent failed.\n");
        }
        fflush(output);
        return valsend;
    }
};

Node::Node(uint16_t _serverID, int _proxyPort) : serverID(_serverID), proxyPort(_proxyPort) {
    printf("%d server node constructed\n", serverID);
    peerPort = BASE_PORT + serverID;
}

void Node::apply() {
    // TODO: If commitIndex > lastApplied: increment lastApplied, apply log[lastApplied] to state machine
}

void Node::runElection() {
    curLeader = -1;
    currentTerm++;
    char *time_str = timestamp();
    printf("%s - running election\n", time_str);

    vote = 1;
    votedFor = serverID;
    status = CANDIDATE;
    message *voteReqMsg = (message *)calloc(1, sizeof(message));
    voteReqMsg->type = REQUEST_VOTE;
    voteReqMsg->message_len = 0;
    voteReqMsg->term = currentTerm;
    voteReqMsg->from = serverID;

    for (int i = 0; i < NUM_SERVER; i++) {
        if (i != serverID)
            sendMsg(peer_socket, voteReqMsg, BASE_PORT + i % NUM_SERVER);
    }
}

void Node::sendHeartbeats() {
    message *heartBeat = (message *)calloc(1, sizeof(message));
    heartBeat->type = APPEND_ENTRIES;
    heartBeat->message_len = 0;
    heartBeat->term = currentTerm;
    heartBeat->from = serverID;
    for (int i = 0; i < NUM_SERVER; i++) {
        if (i != serverID)
            sendMsg(peer_socket, heartBeat, BASE_PORT + i % NUM_SERVER);
    }
}

void Node::follower_handler(message *msg) {
    switch (msg->type) {
    case APPEND_ENTRIES:
        if (msg->term >= currentTerm) {
            currentTerm = msg->term;
            curLeader = msg->from;
        }
        // TODO: handle msg
        break;
    case REQUEST_VOTE:
        // responde to candidate
        char *time_str = timestamp();
        printf("%s - get recvQuest from %d, myterm %d, questTerm %d\n", time_str, msg->from, currentTerm, msg->term);
        curLeader = -1;
        if (msg->term > currentTerm) {
            currentTerm = msg->term;
            printf("%s - vote for %d\n", time_str, msg->from);
            message *vote_msg = (message *)calloc(1, sizeof(message));
            vote_msg->type = VOTE;
            vote_msg->from = serverID;
            vote_msg->term = currentTerm;
            votedFor = msg->from;
            sendMsg(peer_socket, vote_msg, BASE_PORT + msg->from);
        }
        break;
    }
}

void Node::candidate_handler(message *msg) {
    curLeader = -1; // CANDIDATE's current leader is always -1
    if (msg->term > currentTerm) {
        vote = 0;
        status = FOLLOWER;
        votedFor = -1;
        follower_handler(msg);
        return;
    }
    if (msg->type == VOTE && msg->term==currentTerm) {
        vote++;
        char *time_str = timestamp();
        printf("%s - recv vote from %d, total votes: %d\n", time_str, msg->from, vote);
        if (vote > (NUM_SERVER / 2)) {
            status = LEADER;
            curLeader = serverID;
            sendHeartbeats();
            time_str = timestamp();
            printf("%s - became leader\n", time_str, vote);
            fflush(output);
        }
    }
        
}

void Node::leader_handler(message *msg) {
    // TODO: if term higher than me, step down
    if (msg->term <= currentTerm) {
        return;
    }
    currentTerm = msg->term;
    status = FOLLOWER;
    vote = 0;
    votedFor = -1;
    char *time_str = timestamp();
    if (msg->type == REQUEST_VOTE) {
        printf("%s - vote for %d\n", time_str, msg->from);
        message *vote_msg = (message *)calloc(1, sizeof(message));
        vote_msg->type = VOTE;
        vote_msg->from = serverID;
        vote_msg->term = currentTerm;
        votedFor = msg->from;
        sendMsg(peer_socket, vote_msg, BASE_PORT + msg->from);
    } else {
        curLeader = msg->from;
    }
}

FILE *redir(char *fileName) {
    return freopen(fileName, "w", stdout);
}

int createProxySocket(int proxyPort) {
    int opt = TRUE;
    int socket_fd;
    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("ERROR: Creating master socket failed\n");
        printf("%s\n", strerror(errno));
        fflush(output);
        exit(EXIT_FAILURE);
    }

    // set master socket to allow multiple connections
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        printf("ERROR: setsockopt failed\n");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(proxyPort);

    //bind the socket to specified port
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("Error: bind proxy_socket to pocket %d\n", proxyPort);
        printf("%s\n", strerror(errno));
        exit(-1);
    }

    //try to specify maximum of 3 pending connections for the master socket
    if (listen(socket_fd, 10) < 0) {
        printf("Error: listen of proxy_socket \n");
        exit(EXIT_FAILURE);
    }

    return socket_fd;
}

int createPeerSocket(int peerPort) {
    int socket_fd;
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("ERROR: cannot create peer socket\n");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(peerPort);
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("ERROR: bind to peer port %d failed\n", peerPort);
        printf("%s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return socket_fd;
}

int main(int argc, char *argv[]) {
    // ./process <id> <n> <port>
    if (argc != 4 || atoi(argv[1]) >= atoi(argv[2])) {
        fprintf(stderr, "usage: process <id> <n> <port>\n");
        exit(-1);
    }
    NUM_SERVER = atoi(argv[2]);
    Node node(atoi(argv[1]), atoi(argv[3]));

    // Redirecting stdout
    char log_file[15];
    sprintf(log_file, "%s_output.log", argv[1]);
    output = redir((char *)log_file);

    printf("Start\n");
    fflush(output);

    int max_sd, activity, new_socket;
    socklen_t addrlen;
    struct sockaddr_in proxyAddr;
    struct sockaddr_in peerAddr;
    addrlen = sizeof(struct sockaddr_in);
    char cmd_buffer[MAX_CMD_LEN];

    srand((unsigned)time(0));

    // set of socket descriptors
    fd_set readfds;

    // create proxy socket
    node.proxy_socket = createProxySocket(node.proxyPort);
    printf("Proxy socket set up %d.\n", node.proxy_socket);

    node.peer_socket = createPeerSocket(node.peerPort);
    printf("Peer socket %d bind to port %d \n", node.peer_socket, BASE_PORT + node.serverID);

    while (TRUE) {

        // clear the socket set
        FD_ZERO(&readfds);

        // add socket to set
        FD_SET(node.proxy_socket, &readfds);
        FD_SET(node.peer_socket, &readfds);
        max_sd = node.proxy_socket > node.peer_socket ? node.proxy_socket : node.peer_socket;

        struct timeval timeout;

        if (node.status != LEADER) {
            timeout.tv_sec = 4;
            timeout.tv_usec = rand() % 150 + 150;
        }

        else {
            timeout.tv_sec = 1;
            timeout.tv_usec = 500;
        }

        char *time_str = timestamp();
        printf("%s - term %d\t status:%d \t currLeader: %d\n", time_str, node.currentTerm, node.status, node.curLeader);

        // wait for an activity on one of the sockets
        activity = select(max_sd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0) {
            printf("ERROR: select error\n");
            printf("%s\n", strerror(errno));
        }

        if (activity > 0) {
            // IO operation on proxy socket --> command
            if (FD_ISSET(node.proxy_socket, &readfds)) {
                if (new_socket == 0) {
                    if ((new_socket = accept(node.proxy_socket, (struct sockaddr *)&proxyAddr, (socklen_t *)&addrlen)) < 0) {
                        printf("ERROR: Accept proxy socket\n");
                        fflush(output);
                        exit(EXIT_FAILURE);
                    }
                    node.proxy_socket = new_socket;

                    char *time_str = timestamp();
                    printf("%s - New Proxy socket: %d\n", time_str, node.proxy_socket);
                    fflush(output);
                } else {
                    memset(cmd_buffer, 0, MAX_CMD_LEN);
                    int valread = read(node.proxy_socket, cmd_buffer, sizeof(cmd_buffer));
                    char *time_str = timestamp();
                    printf("%s - cmd: %s\n", time_str, cmd_buffer);
                    //handle the command from proxy

                    // "<id> get chatLog"
                    if (strncmp(cmd_buffer, "get chatLog", strlen("get chatLog")) == 0) {
                        // TODO: return chatLog
                    } else if (strncmp(cmd_buffer, "crash", strlen("crash")) == 0) {
                        printf("Command: GO DIE!\n");
                        fflush(output);
                        close(node.proxy_socket);
                        close(node.peer_socket);
                        exit(0); // let process crash
                        return 0;
                    } else if (strncmp(cmd_buffer, "msg", strlen("msg")) == 0) {
                        char *text_pointer = strchr(cmd_buffer, ' ');
                        text_pointer = strchr(text_pointer + 1, ' ') + 1;

                        message *fwd_msg = (message *)calloc(1, sizeof(message));
                        fwd_msg->type = FORWARD;
                        fwd_msg->message_len = strlen(text_pointer);
                        fwd_msg->from = node.serverID;
                        fwd_msg->term = node.currentTerm;
                        memcpy(fwd_msg->msg, text_pointer, strlen(text_pointer));
                        if (node.curLeader >= 0) {
                            node.sendMsg(node.peer_socket, fwd_msg, BASE_PORT + node.curLeader % NUM_SERVER);
                        } else {
                            char msg_buf[MAX_MSG_LEN];
                            memcpy(msg_buf, text_pointer, strlen(text_pointer));
                            // TODO: add to buffer
                        }
                    }
                }
            }

            // messages from peer servers
            if (FD_ISSET(node.peer_socket, &readfds)) {
                message *msg_buffer = (message *)calloc(1, sizeof(message));
                int valread = recvfrom(node.peer_socket, msg_buffer, sizeof(struct message), 0, (struct sockaddr *)&peerAddr, &addrlen);
                if (valread <= 0) {
                    printf("ERROR: Recv from peer failed\n");
                    fflush(output);
                    continue;
                }
                int peerID = ntohs(peerAddr.sin_port) - BASE_PORT;
                // char *time_str = timestamp();
                // printf("%s - msg from %d, type %d, term %d!\n", time_str, msg_buffer->from, msg_buffer->type, msg_buffer->term);
                switch (node.status) {
                case FOLLOWER:
                    node.follower_handler(msg_buffer);
                    break;
                case CANDIDATE:
                    node.candidate_handler(msg_buffer);
                    break;
                case LEADER:
                    node.leader_handler(msg_buffer);
                    break;
                default:
                    break;
                }
            }
        }

        // handle election timeout
        if (activity == 0) {
            switch (node.status) {
            case FOLLOWER: {
                char *time_str = timestamp();
                printf("%s - Follower Timeout\n", time_str);
                node.runElection();
                break;
            }
            case CANDIDATE: {
                char *time_str = timestamp();
                printf("%s - Candidate Timeout\n", time_str);
                node.runElection();
                break;
            }

            case LEADER: {
                node.sendHeartbeats();
                break;
            }
            }
        }
    }
}