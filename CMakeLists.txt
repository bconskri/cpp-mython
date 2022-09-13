cmake_minimum_required(VERSION 3.19)
project(Mython)

set(CMAKE_CXX_STANDARD 17)

add_executable(Mython mython/main.cpp mython/lexer.h mython/lexer.cpp mython/lexer_test_open.cpp mython/parse.h mython/parse.cpp mython/parse_test.cpp mython/runtime.h
        mython/runtime.cpp mython/runtime_test.cpp mython/statement.h mython/statement.cpp mython/statement_test.cpp)