#include "NetworkHelper.h"

#include <stdexcept>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

void sendAll(int sockfd, std::span<const std::byte> data) {
    auto totalSent{ 0uz };

    while (totalSent < data.size()) {
        ssize_t bytesSent{ send(sockfd, data.data() + totalSent, data.size() - totalSent, 0) };
        if (bytesSent < 0)
            throw std::runtime_error(std::string("send failed: ") + std::to_string(errno));
        totalSent += bytesSent;
    }
}

int createSocket(const char* hostAddr, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        throw std::runtime_error(std::string("socket creation failed: ") + std::to_string(errno));
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, hostAddr, &serverAddr.sin_addr) <= 0)
        throw std::runtime_error(std::string("invalid address: ") + std::to_string(errno));

    if (connect(sockfd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0)
        throw std::runtime_error(std::string("connect failed: ") + std::to_string(errno));

    return sockfd;
}