#pragma once
#include <span>

[[nodiscard]] bool sendAll(int sockfd, std::span<const std::byte> data);
int createSocket(const char* hostAddr, int port);