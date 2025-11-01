#include "assembly_lifter.h"
#include "assembly_parser.h"
#include "wasm_generator.h"

#include <iostream>
#include <string>

namespace
{
  void printUsage(const char *programName)
  {
    std::cout << "Usage: " << programName << " [--wasm file] [--wast file] <input file>\n";
    std::cout << "  --wasm <file>  Output WebAssembly binary\n";
    std::cout << "  --wast <file>  Output WebAssembly text\n";
    std::cout << "  -h, --help     Show this help\n";
    std::cout << "If output files are not specified, the input file name is used to generate .wasm/.wat.\n";
  }

  std::string deriveOutputName(const std::string &inputFile, const std::string &extension)
  {
    const std::size_t dotPos = inputFile.find_last_of('.');
    const std::string base = (dotPos == std::string::npos) ? inputFile : inputFile.substr(0, dotPos);
    return base + extension;
  }
}

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    printUsage(argv[0]);
    return 1;
  }

  std::string inputFile;
  std::string wasmFile;
  std::string wastFile;

  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help")
    {
      printUsage(argv[0]);
      return 0;
    }
    else if (arg == "--wasm")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "Error: --wasm option requires an output file name\n";
        return 1;
      }
      wasmFile = argv[++i];
    }
    else if (arg == "--wast")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "Error: --wast option requires an output file name\n";
        return 1;
      }
      wastFile = argv[++i];
    }
    else if (!arg.empty() && arg[0] == '-')
    {
      std::cerr << "Error: unknown option: " << arg << "\n";
      printUsage(argv[0]);
      return 1;
    }
    else
    {
      inputFile = arg;
    }
  }

  if (inputFile.empty())
  {
    std::cerr << "Error: input file is not specified\n";
    printUsage(argv[0]);
    return 1;
  }

  if (wasmFile.empty() && wastFile.empty())
  {
    wasmFile = deriveOutputName(inputFile, ".wasm");
    wastFile = deriveOutputName(inputFile, ".wat");
    std::cout << "Output files are not specified, using " << wasmFile << " and " << wastFile << "\n";
  }

  std::cout << "Parsing Assembly file: " << inputFile << "\n";

  asm2wasm::AssemblyParser parser;
  if (!parser.parseFile(inputFile))
  {
    std::cerr << "Parse error: " << parser.getErrorMessage() << "\n";
    return 1;
  }

  asm2wasm::AssemblyLifter lifter;
  if (!lifter.liftToLLVM(parser.getInstructions(), parser.getLabels()))
  {
    std::cerr << "Assembly lifter error: " << lifter.getErrorMessage() << "\n";
    return 1;
  }

  llvm::Module *module = lifter.getModule();
  if (!module)
  {
    std::cerr << "Error: LLVM module cannot be obtained\n";
    return 1;
  }

  asm2wasm::WasmGenerator wasmGenerator;
  if (!wasmGenerator.generateWasm(module))
  {
    std::cerr << "WebAssembly generation error: " << wasmGenerator.getErrorMessage() << "\n";
    return 1;
  }

  if (!wasmFile.empty())
  {
    std::cout << "Outputting WebAssembly binary: " << wasmFile << "\n";
    if (!wasmGenerator.writeWasmToFile(wasmFile))
    {
      std::cerr << "WebAssembly binary output error: " << wasmGenerator.getErrorMessage() << "\n";
      return 1;
    }
  }

  if (!wastFile.empty())
  {
    std::cout << "Outputting WebAssembly text: " << wastFile << "\n";
    if (!wasmGenerator.writeWastToFile(wastFile))
    {
      std::cerr << "WebAssembly text output error: " << wasmGenerator.getErrorMessage() << "\n";
      return 1;
    }
  }

  std::cout << "Generated WebAssembly text:\n";
  std::cout << "----------------------------------------\n";
  std::cout << wasmGenerator.getWastString();
  std::cout << "----------------------------------------\n";
  std::cout << "WebAssembly conversion completed.\n";

  return 0;
}