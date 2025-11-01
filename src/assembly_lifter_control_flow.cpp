#include "assembly_lifter.h"

#include <llvm/IR/Instructions.h>

namespace asm2wasm
{

  bool AssemblyLifter::liftJumpInstruction(const Instruction &instruction)
  {
    if (instruction.operands.size() != 1)
    {
      errorMessage_ = "Jump instruction requires 1 operand";
      return false;
    }

    llvm::BasicBlock *targetBlock = getOrCreateBlock(instruction.operands[0].value);
    if (!targetBlock)
    {
      errorMessage_ = "Jump target label not found: " + instruction.operands[0].value;
      return false;
    }

    switch (instruction.type)
    {
    case InstructionType::JMP:
      builder_->CreateBr(targetBlock);
      {
        llvm::Function *currentFunc = builder_->GetInsertBlock()->getParent();
        llvm::BasicBlock *nextBlock = llvm::BasicBlock::Create(*context_, "cont", currentFunc);
        builder_->SetInsertPoint(nextBlock);
      }
      break;
    case InstructionType::JE:
    case InstructionType::JNE:
    case InstructionType::JL:
    case InstructionType::JG:
    case InstructionType::JLE:
    case InstructionType::JGE:
    {
      auto getFlagCond = [&](const char *name, bool branchWhenNonZero) -> llvm::Value *
      {
        llvm::Value *reg = getFlagRegister(name);
        llvm::Value *val = builder_->CreateLoad(getIntType(), reg, std::string(name) + "_val");
        return branchWhenNonZero
                   ? builder_->CreateICmpNE(val, llvm::ConstantInt::get(getIntType(), 0), std::string(name) + "_nz")
                   : builder_->CreateICmpEQ(val, llvm::ConstantInt::get(getIntType(), 0), std::string(name) + "_z");
      };

      llvm::Function *currentFunc = builder_->GetInsertBlock()->getParent();
      std::string fallthroughName = "fallthrough_" + std::to_string(fallthroughCounter_++);
      llvm::BasicBlock *fallthrough = llvm::BasicBlock::Create(*context_, fallthroughName, currentFunc);

      switch (instruction.type)
      {
      case InstructionType::JE:
        builder_->CreateCondBr(getFlagCond("ZF", true), targetBlock, fallthrough);
        break;
      case InstructionType::JNE:
        builder_->CreateCondBr(getFlagCond("ZF", false), fallthrough, targetBlock);
        break;
      case InstructionType::JL:
        builder_->CreateCondBr(getFlagCond("LT", true), targetBlock, fallthrough);
        break;
      case InstructionType::JG:
        builder_->CreateCondBr(getFlagCond("GT", true), targetBlock, fallthrough);
        break;
      case InstructionType::JLE:
        builder_->CreateCondBr(getFlagCond("LE", true), targetBlock, fallthrough);
        break;
      case InstructionType::JGE:
        builder_->CreateCondBr(getFlagCond("GE", true), targetBlock, fallthrough);
        break;
      default:
        builder_->CreateBr(targetBlock);
        break;
      }

      builder_->SetInsertPoint(fallthrough);
      break;
    }
    default:
      return false;
    }

    return true;
  }

  llvm::BasicBlock *AssemblyLifter::getOrCreateBlock(const std::string &labelName)
  {
    auto it = blocks_.find(labelName);
    if (it != blocks_.end())
    {
      return it->second;
    }

    llvm::BasicBlock *currentBlock = builder_->GetInsertBlock();
    llvm::Function *currentFunc = nullptr;

    if (currentBlock)
    {
      currentFunc = currentBlock->getParent();
    }
    else
    {
      currentFunc = getOrCreateFunction("main");
      if (!currentFunc)
      {
        return nullptr;
      }
    }

    llvm::BasicBlock *block = llvm::BasicBlock::Create(*context_, labelName, currentFunc);
    blocks_[labelName] = block;
    return block;
  }

  llvm::Function *AssemblyLifter::getOrCreateFunction(const std::string &funcName)
  {
    auto it = functions_.find(funcName);
    if (it != functions_.end())
    {
      return it->second;
    }

    llvm::FunctionType *funcType = llvm::FunctionType::get(getIntType(), false);
    llvm::Function *func = llvm::Function::Create(funcType,
                                                  llvm::Function::ExternalLinkage,
                                                  funcName,
                                                  *module_);
    functions_[funcName] = func;
    return func;
  }

}
