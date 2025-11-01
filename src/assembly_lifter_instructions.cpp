#include "assembly_lifter.h"

#include <llvm/IR/Instructions.h>

namespace asm2wasm
{

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
      llvm::Value *memAddr = calculateMemoryAddress(instruction.operands[0]);
      llvm::Value *memPtr = builder_->CreateIntToPtr(memAddr, getPtrType(), "mem_ptr");
      if (instruction.operands[1].type == OperandType::REGISTER)
      {
        llvm::Value *reg = getOrCreateRegister(instruction.operands[1].value);
        llvm::Value *regValue = builder_->CreateLoad(getIntType(), reg, "reg_val");
        builder_->CreateStore(regValue, memPtr);
      }
      else if (instruction.operands[1].type == OperandType::IMMEDIATE)
      {
        llvm::Value *immValue = getOperandValue(instruction.operands[1]);
        builder_->CreateStore(immValue, memPtr);
      }
      else
      {
        errorMessage_ = "Source must be a register or immediate for memory destination MOV instruction";
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

    llvm::Value *callResult = builder_->CreateCall(func);
    llvm::Value *eaxReg = getOrCreateRegister("%eax");
    builder_->CreateStore(callResult, eaxReg);
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

}
