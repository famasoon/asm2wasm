# asm2wasm

## Overview

asm2wasm is a tool that converts Assembly code to WebAssembly. It takes assembly instructions and generates WebAssembly binary (.wasm) and text format (.wat) files.

## Features

### Supported Instructions

- **Arithmetic Operations**: `ADD`, `SUB`, `MUL`, `DIV`
- **Data Movement**: `MOV`
- **Comparison**: `CMP`
- **Control Flow**:
  - Unconditional jumps: `JMP`
  - Conditional jumps: `JE`, `JNE`, `JL`, `JG`, `JLE`, `JGE`
- **Function Calls**: `CALL`, `RET`
- **Stack Operations**: `PUSH`, `POP`
- **Labels**: Function entry points and jump targets

### Supported Operands

- **Registers**: `%eax`, `%ebx`, `%ecx`, `%edx`, `%esi`, etc.
- **Immediate Values**: Numeric constants (e.g., `10`, `20`)
- **Memory Access**:
  - Direct addressing: `(%esi)`
  - Offset addressing: `(%esi+4)`, `(%esi+8)`, etc.
  - Indexed addressing: `(%esi+%ebx*4)` for array indexing
- **Labels**: Function names and jump targets

### Key Capabilities

- **Register Management**: All registers are treated as local variables in WebAssembly
- **Memory Operations**: Full support for reading from and writing to memory addresses
- **Conditional Branching**: Flag-based conditional jumps using comparison results
- **Function Calls**: Support for function definitions and recursive calls
- **Loop Structures**: While loops and iterative algorithms
- **Multi-function Programs**: Multiple function definitions in a single assembly file

## Requirements

- C++17 compiler
- CMake 3.15 or higher
- LLVM library (configured and installed)

## Building

1. Ensure LLVM is installed and configured on your system
2. Run the build script:

```bash
./build.sh
```

Or build manually:

```bash
mkdir build
cd build
cmake ..
make
```

The executable `asm2wasm` will be generated in the `build/` directory.

## Usage

### Basic Usage

```bash
./build/asm2wasm input.asm
```

This generates `input.wasm` and `input.wat` files in the same directory as the input file.

### Specify Output Files

```bash
./build/asm2wasm --wasm output.wasm --wast output.wat input.asm
```

### Options

- `--wasm <file>`: Specify the output WebAssembly binary file
- `--wast <file>`: Specify the output WebAssembly text file (.wat)
- `-h, --help`: Show usage information

## Architecture

The conversion process consists of three main stages:

1. **Assembly Parsing** (`AssemblyParser`):
   - Parses assembly source code
   - Identifies instructions, operands, and labels
   - Builds an internal representation of the program

2. **LLVM IR Generation** (`AssemblyLifter`):
   - Converts assembly instructions to LLVM Intermediate Representation
   - Manages registers as alloca instructions
   - Handles control flow with LLVM basic blocks
   - Performs flag-based conditional branching

3. **WebAssembly Generation** (`WasmGenerator`):
   - Converts LLVM IR to WebAssembly format
   - Maps LLVM instructions to WebAssembly opcodes
   - Generates both binary (.wasm) and text (.wat) formats

## Example Programs

The `examples/` directory contains several example programs:

- **simple_add.asm**: Basic arithmetic operations
- **conditional_jump.asm**: Conditional branching example
- **loop_example.asm**: Loop structure implementation
- **function_calls.asm**: Function calls and recursion (factorial)
- **memory_operations.asm**: Basic memory read/write operations
- **memory_advanced.asm**: Advanced memory operations with array indexing
- **fibonacci.asm**: Recursive Fibonacci sequence implementation
- **advanced_arithmetic.asm**: Complex arithmetic operations

## Example: Simple Addition

**Input (simple_add.asm)**:
```assembly
start:
    mov %eax, 10
    mov %ebx, 20
    add %eax, %ebx
    ret
```

**Output**: Generates WebAssembly that computes 10 + 20 = 30 and returns the result.

## Implementation Details

### Register Handling

All registers are implemented as WebAssembly local variables. Each register is allocated memory space, and operations are translated to load/store operations on these locals.

### Memory Management

Memory addresses are calculated dynamically:
- Base register + offset: `(%esi+4)`
- Base register + scaled index: `(%esi+%ebx*4)`
- Direct addressing: `(%esi)`

### Control Flow

Conditional jumps are implemented using WebAssembly's `br_if` instruction:
- Comparison operations (`CMP`) set flag registers
- Conditional jumps check these flags and branch accordingly
- Unconditional jumps use the `br` instruction

### Function Calls

Functions are identified by labels:
- Labels that are called with `CALL` instruction are treated as functions
- The `main` label is always treated as the entry point
- The first label encountered is also treated as a function entry point

### Return Values

The `RET` instruction returns the value stored in the `%eax` register by default.

## License

This project uses LLVM, which is licensed under the Apache 2.0 License with LLVM Exceptions.
