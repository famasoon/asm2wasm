#include "assembly_lifter.h"
#include "assembly_parser.h"
#include "wasm_generator.h"

#include <iostream>
#include <string>

namespace
{
  void printUsage(const char *programName)
  {
    std::cout << "使用方法: " << programName << " [--wasm ファイル] [--wast ファイル] <入力ファイル>\n";
    std::cout << "  --wasm <ファイル>  WebAssemblyバイナリを出力\n";
    std::cout << "  --wast <ファイル>  WebAssemblyテキストを出力\n";
    std::cout << "  -h, --help        このヘルプを表示\n";
    std::cout << "出力ファイルを指定しない場合、入力ファイル名から .wasm/.wat を自動生成します。\n";
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
        std::cerr << "エラー: --wasm オプションには出力ファイル名が必要です\n";
        return 1;
      }
      wasmFile = argv[++i];
    }
    else if (arg == "--wast")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "エラー: --wast オプションには出力ファイル名が必要です\n";
        return 1;
      }
      wastFile = argv[++i];
    }
    else if (!arg.empty() && arg[0] == '-')
    {
      std::cerr << "エラー: 不明なオプション: " << arg << "\n";
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
    std::cerr << "エラー: 入力ファイルが指定されていません\n";
    printUsage(argv[0]);
    return 1;
  }

  if (wasmFile.empty() && wastFile.empty())
  {
    wasmFile = deriveOutputName(inputFile, ".wasm");
    wastFile = deriveOutputName(inputFile, ".wat");
    std::cout << "出力ファイルが指定されていないため、" << wasmFile << " と " << wastFile << " を使用します。\n";
  }

  std::cout << "Assemblyファイルを解析中: " << inputFile << "\n";

  asm2wasm::AssemblyParser parser;
  if (!parser.parseFile(inputFile))
  {
    std::cerr << "パースエラー: " << parser.getErrorMessage() << "\n";
    return 1;
  }

  asm2wasm::AssemblyLifter lifter;
  if (!lifter.liftToLLVM(parser.getInstructions(), parser.getLabels()))
  {
    std::cerr << "Assemblyリフターエラー: " << lifter.getErrorMessage() << "\n";
    return 1;
  }

  llvm::Module *module = lifter.getModule();
  if (!module)
  {
    std::cerr << "エラー: LLVMモジュールを取得できませんでした\n";
    return 1;
  }

  asm2wasm::WasmGenerator wasmGenerator;
  if (!wasmGenerator.generateWasm(module))
  {
    std::cerr << "WebAssembly生成エラー: " << wasmGenerator.getErrorMessage() << "\n";
    return 1;
  }

  if (!wasmFile.empty())
  {
    std::cout << "WebAssemblyバイナリを出力中: " << wasmFile << "\n";
    if (!wasmGenerator.writeWasmToFile(wasmFile))
    {
      std::cerr << "WebAssemblyバイナリ出力エラー: " << wasmGenerator.getErrorMessage() << "\n";
      return 1;
    }
  }

  if (!wastFile.empty())
  {
    std::cout << "WebAssemblyテキストを出力中: " << wastFile << "\n";
    if (!wasmGenerator.writeWastToFile(wastFile))
    {
      std::cerr << "WebAssemblyテキスト出力エラー: " << wasmGenerator.getErrorMessage() << "\n";
      return 1;
    }
  }

  std::cout << "生成されたWebAssemblyテキスト:\n";
  std::cout << "----------------------------------------\n";
  std::cout << wasmGenerator.getWastString();
  std::cout << "----------------------------------------\n";
  std::cout << "WebAssembly変換が完了しました。\n";

  return 0;
}