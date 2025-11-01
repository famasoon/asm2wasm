#pragma once

#include "assembly_parser.h"

#include <string>
#include <vector>
#include <map>
#include <memory>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace asm2wasm
{
  class AssemblyLifter
  {
  public:
    AssemblyLifter();
    ~AssemblyLifter() = default;

    bool liftToLLVM(const std::vector<Instruction> &instructions, const std::map<std::string, size_t> &labels);

    llvm::Module *getModule() const { return module_.get(); }

    const std::string &getErrorMessage() const { return errorMessage_; }

  private:
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;
    std::map<std::string, llvm::Value *> registers_;
    std::map<std::string, llvm::BasicBlock *> blocks_;
    std::map<std::string, llvm::Function *> functions_;
    std::string errorMessage_;
    size_t fallthroughCounter_;

    llvm::Value *getOrCreateRegister(const std::string &regName);
    llvm::Value *getOperandValue(const Operand &operand);

    bool liftInstruction(const Instruction &Instruction, size_t index);
    bool liftArithmeticInstruction(const Instruction &Instruction);
    bool liftMoveInstruction(const Instruction &instruction);
    bool liftCompareInstruction(const Instruction &instruction);
    bool liftJumpInstruction(const Instruction &instruction);
    bool liftCallInstruction(const Instruction &instruction);
    bool liftReturnInstruction(const Instruction &instruction);
    bool liftStackInstruction(const Instruction &instruction);

    llvm::BasicBlock *getOrCreateBlock(const std::string &labelName);

    llvm::Function *getOrCreateFunction(const std::string &funcName);

    llvm::Type *getIntType() const;

    llvm::Type *getPtrType() const;

    llvm::Value *calculateMemoryAddress(const Operand &operand);

    llvm::Value *getFlagRegister(const std::string &flagName);
    void setFlagRegister(const std::string &flagName, llvm::Value *value);
  };
}