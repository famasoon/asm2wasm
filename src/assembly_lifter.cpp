#include "assembly_lifter.h"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Pass.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Utils.h>

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
  }

  bool AssemblyLifter::liftToLLVM(const std::vector<Instruction> &instructions,
                                  const std::map<std::string, size_t> &labels)
  {
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

    for (size_t i = 0; i < instructions.size(); ++i)
    {
      if (!instructions[i].label.empty())
      {
        const std::string &labelName = instructions[i].label;
        if (labelName == "main" || labelName == "start" || callTargets.count(labelName) > 0)
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

      if (!liftInstruction(instructions[i], i))
      {
        return false;
      }
    }

    if (currentFunc)
    {
      for (auto &block : *currentFunc)
      {
        if (!block.getTerminator())
        {
          llvm::IRBuilder<> tempBuilder(&block);
          tempBuilder.CreateRet(llvm::ConstantInt::get(getIntType(), 0));
        }
      }
    }

    applyOptimizationPasses();

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
        errorMessage_ = "IR verification error: " + verifyError;
        return false;
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

  bool AssemblyLifter::liftArithmeticInstruction(const Instruction &instruction)
  {
    if (instruction.operands.size() < 2)
    {
      errorMessage_ = "Arithmetic instruction requires at least 2 operands";
      return false;
    }

    llvm::Value *left = getOperandValue(instruction.operands[0]);
    llvm::Value *right = getOperandValue(instruction.operands[1]);

    if (!left || !right)
    {
      errorMessage_ = "Failed to parse operands";
      return false;
    }

    llvm::Value *result = nullptr;
    switch (instruction.type)
    {
    case InstructionType::ADD:
      result = builder_->CreateAdd(left, right, "add");
      break;
    case InstructionType::SUB:
      result = builder_->CreateSub(left, right, "sub");
      break;
    case InstructionType::MUL:
      result = builder_->CreateMul(left, right, "mul");
      break;
    case InstructionType::DIV:
      result = builder_->CreateSDiv(left, right, "div");
      break;
    default:
      return false;
    }

    if (instruction.operands[0].type == OperandType::REGISTER)
    {
      llvm::Value *reg = getOrCreateRegister(instruction.operands[0].value);
      builder_->CreateStore(result, reg);
    }

    return true;
  }

  bool AssemblyLifter::liftMoveInstruction(const Instruction &instruction)
  {
    if (instruction.operands.size() != 2)
    {
      errorMessage_ = "MOV instruction requires 2 operands";
      return false;
    }

    llvm::Value *source = getOperandValue(instruction.operands[1]);
    if (!source)
    {
      errorMessage_ = "Failed to parse source operand";
      return false;
    }

    if (instruction.operands[0].type == OperandType::REGISTER)
    {
      llvm::Value *dest = getOrCreateRegister(instruction.operands[0].value);
      builder_->CreateStore(source, dest);
    }
    else if (instruction.operands[0].type == OperandType::MEMORY)
    {
      if (instruction.operands[1].type == OperandType::REGISTER)
      {
        llvm::Value *memAddr = calculateMemoryAddress(instruction.operands[0]);
        llvm::Value *memPtr = builder_->CreateIntToPtr(memAddr, getPtrType(), "mem_ptr");
        llvm::Value *reg = getOrCreateRegister(instruction.operands[1].value);
        llvm::Value *memValue = builder_->CreateLoad(getIntType(), memPtr, "mem_val");
        builder_->CreateStore(memValue, reg);
      }
      else
      {
        errorMessage_ = "Source must be a register for memory access MOV instruction";
        return false;
      }
    }
    else if (instruction.operands[1].type == OperandType::MEMORY)
    {
      if (instruction.operands[0].type == OperandType::REGISTER)
      {
        llvm::Value *reg = getOrCreateRegister(instruction.operands[0].value);
        llvm::Value *regValue = builder_->CreateLoad(getIntType(), reg, "reg_val");
        llvm::Value *memAddr = calculateMemoryAddress(instruction.operands[1]);
        llvm::Value *memPtr = builder_->CreateIntToPtr(memAddr, getPtrType(), "mem_ptr");
        builder_->CreateStore(regValue, memPtr);
      }
      else
      {
        errorMessage_ = "Destination must be a register for memory access MOV instruction";
        return false;
      }
    }
    else
    {
      errorMessage_ = "MOV instruction destination must be a register or memory access";
      return false;
    }

    return true;
  }

  bool AssemblyLifter::liftCompareInstruction(const Instruction &instruction)
  {
    if (instruction.operands.size() != 2)
    {
      errorMessage_ = "CMP instruction requires 2 operands";
      return false;
    }

    llvm::Value *left = getOperandValue(instruction.operands[0]);
    llvm::Value *right = getOperandValue(instruction.operands[1]);

    if (!left || !right)
    {
      errorMessage_ = "Failed to parse CMP instruction operands";
      return false;
    }

    llvm::Value *eq = builder_->CreateICmpEQ(left, right, "cmp_eq");
    llvm::Value *lt = builder_->CreateICmpSLT(left, right, "cmp_lt");
    llvm::Value *gt = builder_->CreateICmpSGT(left, right, "cmp_gt");
    llvm::Value *le = builder_->CreateICmpSLE(left, right, "cmp_le");
    llvm::Value *ge = builder_->CreateICmpSGE(left, right, "cmp_ge");

    setFlagRegister("ZF", builder_->CreateZExt(eq, getIntType(), "zf_int"));
    setFlagRegister("LT", builder_->CreateZExt(lt, getIntType(), "lt_int"));
    setFlagRegister("GT", builder_->CreateZExt(gt, getIntType(), "gt_int"));
    setFlagRegister("LE", builder_->CreateZExt(le, getIntType(), "le_int"));
    setFlagRegister("GE", builder_->CreateZExt(ge, getIntType(), "ge_int"));

    return true;
  }

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
      llvm::BasicBlock *fallthrough = llvm::BasicBlock::Create(*context_, "cont", currentFunc);

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

  bool AssemblyLifter::liftCallInstruction(const Instruction &instruction)
  {
    if (instruction.operands.size() != 1)
    {
      errorMessage_ = "CALL instruction requires 1 operand";
      return false;
    }

    std::string funcName = instruction.operands[0].value;
    llvm::Function *func = getOrCreateFunction(funcName);

    if (!func)
    {
      errorMessage_ = "Function not found: " + funcName;
      return false;
    }

    builder_->CreateCall(func);
    return true;
  }

  bool AssemblyLifter::liftReturnInstruction(const Instruction &instruction)
  {
    if (instruction.operands.empty())
    {
      llvm::Value *eaxReg = getOrCreateRegister("%eax");
      llvm::Value *eaxValue = builder_->CreateLoad(getIntType(), eaxReg, "eax_val");
      builder_->CreateRet(eaxValue);
    }
    else
    {
      llvm::Value *retValue = getOperandValue(instruction.operands[0]);
      if (!retValue)
      {
        errorMessage_ = "Failed to parse RET instruction operand";
        return false;
      }
      builder_->CreateRet(retValue);
    }
    return true;
  }

  bool AssemblyLifter::liftStackInstruction(const Instruction &instruction)
  {
    if (instruction.type == InstructionType::PUSH)
    {
      if (instruction.operands.size() != 1)
      {
        errorMessage_ = "PUSH instruction requires 1 operand";
        return false;
      }

      llvm::Value *value = getOperandValue(instruction.operands[0]);
      if (!value)
      {
        errorMessage_ = "Failed to parse PUSH instruction operand";
        return false;
      }

      llvm::Value *stackPtr = getOrCreateRegister("STACK_PTR");
      llvm::Value *stackValue = builder_->CreateLoad(getIntType(), stackPtr, "stack_ptr_val");

      llvm::Value *newStackPtr = builder_->CreateSub(stackValue, llvm::ConstantInt::get(getIntType(), 4), "new_stack_ptr");
      builder_->CreateStore(newStackPtr, stackPtr);

      llvm::Value *stackAddr = builder_->CreateIntToPtr(newStackPtr, getPtrType(), "stack_addr");
      builder_->CreateStore(value, stackAddr);
    }
    else if (instruction.type == InstructionType::POP)
    {
      if (instruction.operands.size() != 1)
      {
        errorMessage_ = "POP instruction requires 1 operand";
        return false;
      }

      llvm::Value *stackPtr = getOrCreateRegister("STACK_PTR");
      llvm::Value *stackValue = builder_->CreateLoad(getIntType(), stackPtr, "stack_ptr_val");

      llvm::Value *stackAddr = builder_->CreateIntToPtr(stackValue, getPtrType(), "stack_addr");
      llvm::Value *value = builder_->CreateLoad(getIntType(), stackAddr, "stack_val");

      llvm::Value *newStackPtr = builder_->CreateAdd(stackValue, llvm::ConstantInt::get(getIntType(), 4), "new_stack_ptr");
      builder_->CreateStore(newStackPtr, stackPtr);

      if (instruction.operands[0].type == OperandType::REGISTER)
      {
        llvm::Value *reg = getOrCreateRegister(instruction.operands[0].value);
        builder_->CreateStore(value, reg);
      }
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

  llvm::Type *AssemblyLifter::getIntType() const
  {
    return llvm::Type::getInt32Ty(*context_);
  }

  llvm::Type *AssemblyLifter::getPtrType() const
  {
    return llvm::PointerType::get(getIntType(), 0);
  }

  llvm::Value *AssemblyLifter::calculateMemoryAddress(const Operand &operand)
  {
    std::string addr = operand.value.substr(1, operand.value.length() - 2); // ()を除去

    if (addr.find('+') != std::string::npos)
    {
      size_t plusPos = addr.find('+');
      std::string regName = addr.substr(0, plusPos);
      std::string offsetStr = addr.substr(plusPos + 1);

      llvm::Value *reg = getOrCreateRegister(regName);
      llvm::Value *regValue = builder_->CreateLoad(getIntType(), reg, "base_addr");

      int offset = std::stoi(offsetStr);

      if (offset % 4 == 0)
      {
        llvm::Value *alignedOffset = llvm::ConstantInt::get(getIntType(), offset);
        llvm::Value *result = builder_->CreateAdd(regValue, alignedOffset, "aligned_mem_addr");
        return result;
      }
      else
      {
        llvm::Value *offsetValue = llvm::ConstantInt::get(getIntType(), offset);
        llvm::Value *result = builder_->CreateAdd(regValue, offsetValue, "mem_addr");
        return result;
      }
    }
    else if (addr.find('%') != std::string::npos)
    {
      llvm::Value *reg = getOrCreateRegister(addr);
      llvm::Value *regValue = builder_->CreateLoad(getIntType(), reg, "reg_val");
      return regValue;
    }
    else
    {
      int value = std::stoi(addr);
      return llvm::ConstantInt::get(getIntType(), value);
    }
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

  void AssemblyLifter::applyOptimizationPasses()
  {
    if (!module_)
    {
      return;
    }

    llvm::legacy::PassManager passManager;

    passManager.add(llvm::createInstructionCombiningPass());

    passManager.add(llvm::createCFGSimplificationPass());

    passManager.add(llvm::createReassociatePass());

    passManager.add(llvm::createEarlyCSEPass());

    passManager.add(llvm::createInstructionCombiningPass());
    passManager.add(llvm::createCFGSimplificationPass());
    passManager.add(llvm::createDeadCodeEliminationPass());

    passManager.run(*module_);
  }
}