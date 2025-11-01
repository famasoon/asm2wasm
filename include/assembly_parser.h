#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace asm2wasm
{
  enum class InstructionType
  {
    ADD,
    SUB,
    MUL,
    DIV,
    MOV,
    CMP,
    JMP,
    JE,
    JNE,
    JL,
    JG,
    JLE,
    JGE,
    CALL,
    RET,
    PUSH,
    POP,
    LABEL,
    UNKNOWN,
  };

  enum class OperandType
  {
    REGISTER,
    IMMEDIATE,
    MEMORY,
    LABEL
  };

  struct Operand
  {
    OperandType type;
    std::string value;

    Operand(OperandType t, const std::string &v) : type(t), value(v) {}
  };

  struct Instruction
  {
    InstructionType type;
    std::vector<Operand> operands;
    std::string label;

    Instruction(InstructionType t) : type(t) {}
  };

  class AssemblyParser
  {
  public:
    AssemblyParser();
    ~AssemblyParser() = default;

    bool parseFile(const std::string &filename);

    bool parseString(const std::string &assemblyCode);

    const std::vector<Instruction> &getInstructions() const { return instructions_; }

    const std::map<std::string, size_t> &getLabels() const { return labels_; }

    const std::string &getErrorMessage() const { return errorMessage_; }

  private:
    std::vector<Instruction> instructions_;
    std::map<std::string, size_t> labels_;
    std::string errorMessage_;

    InstructionType parseInstructionType(const std::string &instruction);

    Operand parseOperand(const std::string &operand);

    bool parseLine(const std::string &line);

    std::string trim(const std::string &str);

    std::string removeComments(const std::string &line);
  };
}