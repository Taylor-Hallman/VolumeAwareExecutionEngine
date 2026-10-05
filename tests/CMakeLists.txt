add_executable(VAEE_tests
        SerializationTests.cpp
        ParserTests.cpp
        NetworkingTests.cpp
        RollingVwapTests.cpp
)

target_link_libraries(VAEE_tests
        PRIVATE
        VAEE_lib
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(VAEE_tests PROPERTIES TIMEOUT 10)