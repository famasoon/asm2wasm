#include "assembly_parser.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace asm2wasm
{
  AssemblyParser::AssemblyParser()
  {
    instructions_.clear();
    labels_.clear();
    errorMessage_.clear();
  };

  bool AssemblyParser::parseFile(const std::string &filename)
  {
    std::ifstream file(filename);
    if (!file.is_open())
    {
      errorMessage_ = "Failed to open file: " + filename;
      return false;
    }

    std::string line;
    size_t lineNumber = 0;

    while (std::getline(file, line))
    {
      lineNumber++;
      if (!parseLine(line))
      {
        errorMessage_ = "Error at line " + std::to_string(lineNumber) + ": " + getErrorMessage();
        return false;
      }
    }

    file.close();
    return true;
  }

  bool AssemblyParser::parseString(const std::string &assemblyCode)
  {
    std::istringstream stream(assemblyCode);
    std::string line;
    size_t lineNumber = 0;

    while (std::getline(stream, line))
    {
      lineNumber++;
      if (!parseLine(line))
      {
        errorMessage_ = "Error at line " + std::to_string(lineNumber) + ": " + getErrorMessage();
        return false;
      }
    }

    return true;
  }

  bool AssemblyParser::parseLine(const std::string &line)
  {
    std::string cleanLine = removeComments(line);

    cleanLine = trim(cleanLine);

    if (cleanLine.empty())
    {
      return true;
    }

    std::istringstream iss(cleanLine);
    std::string token;
    std::vector<std::string> tokens;

    while (iss >> token)
    {
      tokens.push_back(token);
    }

    if (tokens.empty())
    {
      return true;
    }

    std::string firstToken = tokens[0];
    if (firstToken.back() == ':')
    {
      std::string labelName = firstToken.substr(0, firstToken.length() - 1);
      labels_[labelName] = instructions_.size();

      if (tokens.size() > 1)
      {
        InstructionType type = parseInstructionType(tokens[1]);
        if (type == InstructionType::UNKNOWN)
        {
          errorMessage_ = "Unknown instruction: " + tokens[1];
          return false;
        }

        Instruction inst(type);
        inst.label = labelName;

        for (size_t i = 2; i < tokens.size(); ++i)
        {
          inst.operands.push_back(parseOperand(tokens[i]));
        }

        instructions_.push_back(inst);
      }
      else
      {
        Instruction labelInst(InstructionType::LABEL);
        labelInst.label = labelName;
        instructions_.push_back(labelInst);
      }
    }
    else
    {
      InstructionType type = parseInstructionType(firstToken);
      if (type == InstructionType::UNKNOWN)
      {
        errorMessage_ = "Unknown instruction: " + firstToken;
        return false;
      }

      Instruction inst(type);

      for (size_t i = 1; i < tokens.size(); ++i)
      {
        inst.operands.push_back(parseOperand(tokens[i]));
      }

      instructions_.push_back(inst);
    }

    return true;
  }

  InstructionType AssemblyParser::parseInstructionType(const std::string &instruction)
  {
    std::string upper = instruction;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    if (upper == "ADD")
      return InstructionType::ADD;
    if (upper == "SUB")
      return InstructionType::SUB;
    if (upper == "MUL")
      return InstructionType::MUL;
    if (upper == "DIV")
      return InstructionType::DIV;
    if (upper == "MOV")
      return InstructionType::MOV;
    if (upper == "CMP")
      return InstructionType::CMP;
    if (upper == "JMP")
      return InstructionType::JMP;
    if (upper == "JE")
      return InstructionType::JE;
    if (upper == "JZ") // alias of JE
      return InstructionType::JE;
    if (upper == "JNE")
      return InstructionType::JNE;
    if (upper == "JNZ") // alias of JNE
      return InstructionType::JNE;
    if (upper == "JL")
      return InstructionType::JL;
    if (upper == "JG")
      return InstructionType::JG;
    if (upper == "JLE")
      return InstructionType::JLE;
    if (upper == "JGE")
      return InstructionType::JGE;
    if (upper == "CALL")
      return InstructionType::CALL;
    if (upper == "RET")
      return InstructionType::RET;
    if (upper == "PUSH")
      return InstructionType::PUSH;
    if (upper == "POP")
      return InstructionType::POP;

    return InstructionType::UNKNOWN;
  }

  Operand AssemblyParser::parseOperand(const std::string &operand)
  {
    std::string trimmed = trim(operand);

    if (!trimmed.empty() && trimmed.back() == ',')
    {
      trimmed = trimmed.substr(0, trimmed.length() - 1);
    }

    if (trimmed.length() >= 2 && trimmed[0] == '%')
    {
      return Operand(OperandType::REGISTER, trimmed);
    }

    if (trimmed.length() >= 3 && trimmed[0] == '(' && trimmed.back() == ')')
    {
      return Operand(OperandType::MEMORY, trimmed);
    }

    if (std::all_of(trimmed.begin(), trimmed.end(), [](char c)
                    { return std::isdigit(c) || c == '-' || c == '+'; }))
    {
      return Operand(OperandType::IMMEDIATE, trimmed);
    }

    return Operand(OperandType::LABEL, trimmed);
  }

  std::string AssemblyParser::trim(const std::string &str)
  {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos)
    {
      return "";
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, last - first + 1);
  }

  std::string AssemblyParser::removeComments(const std::string &line)
  {
    size_t commentPos = line.find('#');
    if (commentPos != std::string::npos)
    {
      return line.substr(0, commentPos);
    }
    return line;
  }
}