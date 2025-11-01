cmake_minimum_required(VERSION 3.15)
project(asm2wasm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVMの設定
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

set(SOURCES
    src/main.cpp
    src/assembly_parser.cpp
    src/wasm_generator.cpp
    src/assembly_lifter_core.cpp
    src/assembly_lifter_instructions.cpp
    src/assembly_lifter_control_flow.cpp
    src/assembly_lifter_memory.cpp
)

set(HEADERS
    include/assembly_parser.h
    include/wasm_generator.h
    include/assembly_lifter.h
)

add_executable(asm2wasm ${SOURCES} ${HEADERS})

llvm_map_components_to_libnames(llvm_libs support core instcombine scalaropts transformutils)
target_link_libraries(asm2wasm ${llvm_libs})

target_compile_options(asm2wasm PRIVATE ${LLVM_CXX_FLAGS})

target_include_directories(asm2wasm PRIVATE include)