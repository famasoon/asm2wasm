#include "assembly_lifter.h"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_ostream.h>

#include <sstream>

namespace asm2wasm
{

  AssemblyLifter::AssemblyLifter()
      : context_(std::make_unique<llvm::LLVMContext>()),
        module_(std::make_unique<llvm::Module>("assembly_module", *context_)),
        builder_(std::make_unique<llvm::IRBuilder<>>(*context_))
  {
    registers_.clear();
    blocks_.clear();
    functions_.clear();
    errorMessage_.clear();
    fallthroughCounter_ = 0;
  }

  bool AssemblyLifter::liftToLLVM(const std::vector<Instruction> &instructions,
                                  const std::map<std::string, size_t> &labels)
  {
    fallthroughCounter_ = 0;
    std::map<std::string, bool> callTargets;
    for (const auto &inst : instructions)
    {
      if (inst.type == InstructionType::CALL && inst.operands.size() == 1 && inst.operands[0].type == OperandType::LABEL)
      {
        callTargets[inst.operands[0].value] = true;
      }
    }

    llvm::FunctionType *funcType = llvm::FunctionType::get(getIntType(), false);
    llvm::Function *currentFunc = nullptr;
    bool isFirstLabel = true;

    for (size_t i = 0; i < instructions.size(); ++i)
    {
      if (!instructions[i].label.empty())
      {
        const std::string &labelName = instructions[i].label;
        if (labelName == "main" || labelName == "start" || callTargets.count(labelName) > 0 || isFirstLabel)
        {
          currentFunc = getOrCreateFunction(labelName);
          if (!currentFunc)
          {
            return false;
          }
          blocks_.clear();
          registers_.clear();
          llvm::BasicBlock *funcEntry = llvm::BasicBlock::Create(*context_, labelName, currentFunc);
          builder_->SetInsertPoint(funcEntry);
          isFirstLabel = false;
        }
        else
        {
          if (!currentFunc)
          {
            currentFunc = getOrCreateFunction("main");
            if (!currentFunc)
            {
              return false;
            }
          }
          llvm::BasicBlock *labelBlock = getOrCreateBlock(labelName);
          if (!labelBlock)
          {
            return false;
          }
          builder_->SetInsertPoint(labelBlock);
        }
      }
      else if (!currentFunc && isFirstLabel)
      {
        currentFunc = getOrCreateFunction("main");
        if (!currentFunc)
        {
          return false;
        }
        llvm::BasicBlock *funcEntry = llvm::BasicBlock::Create(*context_, "main", currentFunc);
        builder_->SetInsertPoint(funcEntry);
        isFirstLabel = false;
      }

      if (!liftInstruction(instructions[i], i))
      {
        return false;
      }
    }

    for (auto &func : *module_)
    {
      if (func.isDeclaration())
        continue;

      for (auto &block : func)
      {
        if (!block.getTerminator())
        {
          llvm::IRBuilder<> tempBuilder(&block);
          if (&block == &func.getEntryBlock())
          {
            llvm::IRBuilder<> entryBuilder(&func.getEntryBlock());
            llvm::Value *eaxReg = nullptr;
            auto regIt = registers_.find("%eax");
            if (regIt != registers_.end())
            {
              eaxReg = regIt->second;
            }
            else
            {
              eaxReg = entryBuilder.CreateAlloca(getIntType(), nullptr, "%eax");
              registers_["%eax"] = eaxReg;
            }
            llvm::Value *eaxValue = tempBuilder.CreateLoad(getIntType(), eaxReg, "eax_val");
            tempBuilder.CreateRet(eaxValue);
          }
          else
          {
            tempBuilder.CreateRet(llvm::ConstantInt::get(getIntType(), 0));
          }
        }
      }
    }

    std::string verifyError;
    llvm::raw_string_ostream verifyStream(verifyError);
    for (auto &func : *module_)
    {
      if (func.isDeclaration())
        continue;
      verifyError.clear();
      llvm::raw_string_ostream vs(verifyError);
      if (llvm::verifyFunction(func, &vs))
      {
        std::string moduleDump;
        llvm::raw_string_ostream dumpStream(moduleDump);
        module_->print(dumpStream, nullptr);
      }
    }

    return true;
  }

  llvm::Value *AssemblyLifter::getOrCreateRegister(const std::string &regName)
  {
    auto it = registers_.find(regName);
    if (it != registers_.end())
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
      auto funcIt = functions_.find("main");
      if (funcIt != functions_.end())
      {
        currentFunc = funcIt->second;
      }
      else
      {
        currentFunc = getOrCreateFunction("main");
      }
      if (!currentFunc || currentFunc->empty())
      {
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context_, "entry", currentFunc);
        builder_->SetInsertPoint(entry);
      }
      else
      {
        builder_->SetInsertPoint(&currentFunc->front());
      }
    }

    if (currentFunc && !currentFunc->empty())
    {
      llvm::BasicBlock *entryBlock = &currentFunc->front();
      llvm::IRBuilder<> entryBuilder(entryBlock);

      if (!entryBlock->empty())
      {
        llvm::Instruction *firstInst = &entryBlock->front();
        entryBuilder.SetInsertPoint(firstInst);
      }

      llvm::Value *reg = entryBuilder.CreateAlloca(getIntType(), nullptr, regName);
      registers_[regName] = reg;

      if (currentBlock)
      {
        builder_->SetInsertPoint(currentBlock);
      }
      return reg;
    }

    llvm::Value *reg = builder_->CreateAlloca(getIntType(), nullptr, regName);
    registers_[regName] = reg;
    return reg;
  }

  llvm::Value *AssemblyLifter::getOperandValue(const Operand &operand)
  {
    switch (operand.type)
    {
    case OperandType::REGISTER:
    {
      llvm::Value *reg = getOrCreateRegister(operand.value);
      return builder_->CreateLoad(getIntType(), reg, operand.value + "_val");
    }
    case OperandType::IMMEDIATE:
    {
      int value = std::stoi(operand.value);
      return llvm::ConstantInt::get(getIntType(), value);
    }
    case OperandType::MEMORY:
    {
      return calculateMemoryAddress(operand);
    }
    case OperandType::LABEL:
    {
      return getOrCreateBlock(operand.value);
    }
    default:
      return nullptr;
    }
  }

  bool AssemblyLifter::liftInstruction(const Instruction &instruction, size_t index)
  {
    switch (instruction.type)
    {
    case InstructionType::ADD:
    case InstructionType::SUB:
    case InstructionType::MUL:
    case InstructionType::DIV:
      return liftArithmeticInstruction(instruction);
    case InstructionType::MOV:
      return liftMoveInstruction(instruction);
    case InstructionType::CMP:
      return liftCompareInstruction(instruction);
    case InstructionType::JMP:
    case InstructionType::JE:
    case InstructionType::JNE:
    case InstructionType::JL:
    case InstructionType::JG:
    case InstructionType::JLE:
    case InstructionType::JGE:
      return liftJumpInstruction(instruction);
    case InstructionType::CALL:
      return liftCallInstruction(instruction);
    case InstructionType::RET:
      return liftReturnInstruction(instruction);
    case InstructionType::PUSH:
    case InstructionType::POP:
      return liftStackInstruction(instruction);
    case InstructionType::LABEL:
      return true;
    default:
      errorMessage_ = "Unsupported instruction type";
      return false;
    }
  }

  llvm::Type *AssemblyLifter::getIntType() const
  {
    return llvm::Type::getInt32Ty(*context_);
  }

  llvm::Type *AssemblyLifter::getPtrType() const
  {
    return llvm::PointerType::get(getIntType(), 0);
  }

  llvm::Value *AssemblyLifter::getFlagRegister(const std::string &flagName)
  {
    return getOrCreateRegister("FLAG_" + flagName);
  }

  void AssemblyLifter::setFlagRegister(const std::string &flagName, llvm::Value *value)
  {
    llvm::Value *flagReg = getFlagRegister(flagName);
    builder_->CreateStore(value, flagReg);
  }

}
