#pragma once
#include <span>

static constexpr int MD_PORT{ 14200 };
static constexpr int OE_PORT{ 14300 };

void sendAll(int sockfd, std::span<const std::byte> data);