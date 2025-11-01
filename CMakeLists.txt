cmake_minimum_required(VERSION 3.15)
project(AsmToWasm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVMの設定
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# LLVMのインクルードディレクトリとライブラリを追加
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# プロジェクトのソースファイル
set(SOURCES
    src/main.cpp
    src/assembly_parser.cpp
    src/wasm_generator.cpp
    src/assembly_lifter.cpp
)

# ヘッダーファイル
set(HEADERS
    include/assembly_parser.h
    include/wasm_generator.h
    include/assembly_lifter.h
)

# 実行ファイルを作成
add_executable(asmtowasm ${SOURCES} ${HEADERS})

# LLVMライブラリをリンク
llvm_map_components_to_libnames(llvm_libs support core)
target_link_libraries(asmtowasm ${llvm_libs})

# コンパイラフラグの設定
target_compile_options(asmtowasm PRIVATE ${LLVM_CXX_FLAGS})

# インクルードディレクトリの設定
target_include_directories(asmtowasm PRIVATE include)