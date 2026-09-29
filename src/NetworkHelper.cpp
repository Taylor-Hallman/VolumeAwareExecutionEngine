#include "NetworkHelper.h"

#include <stdexcept>
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
