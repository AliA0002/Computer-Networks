
#include "Socket.h"

/**
 * @brief Open, configure, and bind socket.
 */
int socket_init(int port = 0) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("Error opening stream socket");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("Error setting socket option 'Reuse'");
        return -1;
    }
    struct sockaddr_in addr;
    makeSockAddr(&addr, port);

    if (bind(fd, (sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Error binding stream socket");
        return -1;
    }
    return fd;
}

int socket_recv(int fd, void *buf, size_t max_len) {
    int len = recv(fd, buf, max_len, 0);
    if (len == -1) {
        perror("Error receiving data");
        return -1;
    }
    return len;
}

int socket_recv_all(int fd, void *buf, size_t max_len) {
    int len = 0;
    while (len < max_len) {
        int received = socket_recv(fd, (char *)buf + len, max_len - len);
        if (received == -1) {
            return -1;
        }
        len += received;
    }
    return len;
}

int socket_send(int fd, const void *buf, size_t max_len) {
    int len = send(fd, buf, max_len, 0 | MSG_NOSIGNAL);
    if (len == -1) {
        // perror("Error sending data");
        return -1;
    }
    return len;
}

int socket_send_all(int fd, const void *buf, size_t max_len) {
    int len = 0;
    while (len < max_len) {
        int sent = socket_send(fd, (char *)buf + len, max_len - len);
        if (sent == -1) {
            return -1;
        }
        len += sent;
    }
    return len;
}

int socket_send(int fd, std::string msg) { return socket_send_all(fd, msg.c_str(), msg.length() + 1); }

int socket_listen(int fd, int queue) {
    if (listen(fd, queue) == -1) {
        perror("Error listening on socket");
        return -1;
    }
    return 0;
}

int socket_accept(int fd) {
    int client_fd = accept(fd, nullptr, nullptr);
    if (client_fd == -1) {
        perror("Error accepting connection");
        return -1;
    }
    return client_fd;
}

int socket_close(int fd) {
    if (close(fd) == -1) {
        perror("Error closing socket");
        return -1;
    }
    return 0;
}

int socket_getPort(int fd) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd, (sockaddr *)&addr, &len) == -1) {
        perror("Error getting port of socket");
        return -1;
    }
    // Use ntohs to convert from network byte order to host byte order.
    return ntohs(addr.sin_port);
}

void makeSockAddr(struct sockaddr_in *addr, int port) {
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons(port);
}

host_t::host_t(char *hostname, int port) : hostname(hostname), port(port) {}

int make_sockaddr(struct sockaddr_in *addr, const char *hostname, int port) {
    addr->sin_family = AF_INET;
    struct hostent *host = gethostbyname(hostname);
    if (host == nullptr) {
        fprintf(stderr, "%s: unknown host\n", hostname);
        return -1;
    }
    memcpy(&(addr->sin_addr), host->h_addr, host->h_length);
    addr->sin_port = htons(port);
    return 0;
}

int make_sock(const char *hostname, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(-1);
    }
    struct sockaddr_in serv_addr;
    if (make_sockaddr(&serv_addr, hostname, port) < 0) {
        exit(-1);
    }
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(-1);
    }
    return sockfd;
}
int make_sock(host_t host) { return make_sock(host.hostname, host.port); }

std::string get_ip_addr(int fd) {

    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    if (getpeername(fd, (struct sockaddr *)&addr, &addr_size) < 0) {
        perror("Error getting ip address");
        exit(-1);
    }
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), str, INET_ADDRSTRLEN);

    return std::string(str);
}