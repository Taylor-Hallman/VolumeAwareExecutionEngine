#pragma once
#include <span>

void sendAll(int sockfd, std::span<const std::byte> data);