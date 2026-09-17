cmake_minimum_required(VERSION 4.3)
project(VolumeAwareExecutionEngine)

set(CMAKE_CXX_STANDARD 23)

add_executable(VolumeAwareExecutionEngine main.cpp
        src/data/GCMD_1/FrameHeader.h
        src/data/GCMD_1/Quote.h
        src/data/GCMD_1/Trade.h
        src/data/GCMD_1/Heartbeat.h
        src/data/GCMD_1/SessionControl.h
        src/Parser.cpp
        src/Parser.h
        src/data/GCOE_1/NewOrder.h
        src/data/GCOE_1/ExecReport.h
        src/serialization/Encoder.cpp
        src/serialization/Encoder.h)
