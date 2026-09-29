#include <array>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <print>
#include <stdexcept>
#include <thread>
#include <vector>

#include "NetworkHelper.h"

static bool serveClient(int clientSockfd) {
    bool success{ true };
    while (true) {
        std::array<std::byte, 1024> buf{};
        ssize_t bytesReceived = recv(clientSockfd, buf.data(), buf.size(), 0);
        if (bytesReceived <= 0) {
            if (bytesReceived < 0)
                success = false;
            break;
        }
        // TODO: Decode
    }
    close(clientSockfd);
    return success;
}

static int createListeningSocket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        throw std::runtime_error("socket creation failed");

    int opt{ 1 };
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind failed");

    listen(sockfd, 5);
    return sockfd;
}

int main () {
    int mdSockfd{ createListeningSocket(MD_PORT) };
    int oeSockfd{ createListeningSocket(OE_PORT) };

    std::thread mdThread([mdSockfd] {
        sockaddr_in clientAddr{};
        socklen_t len{ sizeof(clientAddr) };
        int clientSockfd{ accept(mdSockfd, reinterpret_cast<sockaddr*>(&clientAddr), &len) };
        if (clientSockfd < 0)
            throw std::runtime_error("accept failed");
        serveClient(clientSockfd);
    });

    std::thread oeThread([oeSockfd] {
        sockaddr_in clientAddr{};
        socklen_t len{ sizeof(clientAddr) };
        int clientSockfd{ accept(oeSockfd, reinterpret_cast<sockaddr*>(&clientAddr), &len) };
        if (clientSockfd < 0)
            throw std::runtime_error("accept failed");
        serveClient(clientSockfd);
    });

    mdThread.join();
    oeThread.join();

    close(mdSockfd);
    close(oeSockfd);
}
