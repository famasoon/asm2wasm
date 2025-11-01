#include "wasm_generator.h"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_ostream.h>

#include <fstream>
#include <sstream>
#include <algorithm>

namespace asm2wasm
{

  WasmGenerator::WasmGenerator()
  {
    wasmModule_ = WasmModule();
    functionMap_.clear();
    localMap_.clear();
    errorMessage_.clear();
  }

  bool WasmGenerator::generateWasm(llvm::Module *module)
  {
    if (!module)
    {
      errorMessage_ = "Invalid LLVM module";
      return false;
    }

    functionMap_.clear();
    localMap_.clear();

    uint32_t funcIndex = 0;
    for (auto &func : *module)
    {
      if (!func.isDeclaration())
      {
        functionMap_[&func] = funcIndex++;
      }
    }

    for (auto &func : *module)
    {
      if (!func.isDeclaration())
      {
        if (!convertFunction(&func))
        {
          return false;
        }
      }
    }

    return true;
  }

  bool WasmGenerator::writeWasmToFile(const std::string &filename)
  {
    std::vector<uint8_t> binary = generateBinary();

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
      errorMessage_ = "Failed to open file: " + filename;
      return false;
    }

    file.write(reinterpret_cast<const char *>(binary.data()), binary.size());
    file.close();

    return true;
  }

  bool WasmGenerator::writeWastToFile(const std::string &filename)
  {
    std::string wast = generateWast();

    std::ofstream file(filename);
    if (!file.is_open())
    {
      errorMessage_ = "Failed to open file: " + filename;
      return false;
    }

    file << wast;
    file.close();

    return true;
  }

  std::string WasmGenerator::getWastString() const
  {
    return generateWast();
  }

  WasmType WasmGenerator::convertLLVMType(llvm::Type *type)
  {
    if (type->isIntegerTy(32))
    {
      return WasmType::I32;
    }
    else if (type->isIntegerTy(64))
    {
      return WasmType::I64;
    }
    else if (type->isFloatTy())
    {
      return WasmType::F32;
    }
    else if (type->isDoubleTy())
    {
      return WasmType::F64;
    }
    else if (type->isVoidTy())
    {
      return WasmType::VOID;
    }

    return WasmType::I32;
  }

  bool WasmGenerator::convertFunction(llvm::Function *func)
  {
    localMap_.clear();
    blockMap_.clear();
    blockIndices_.clear();
    currentFunction_ = func;

    WasmFunction wasmFunc(func->getName().str());

    localMap_.clear();

    uint32_t localIndex = 0;
    for (auto &arg : func->args())
    {
      wasmFunc.params.push_back(convertLLVMType(arg.getType()));
    }

    wasmFunc.returnType = convertLLVMType(func->getReturnType());
    for (auto &block : *func)
    {
      for (auto &inst : block)
      {
        if (llvm::isa<llvm::AllocaInst>(inst))
        {
          assignLocalIndex(&inst, WasmType::I32, wasmFunc);
        }
      }
    }

    uint32_t blockIndex = 0;
    for (auto &block : *func)
    {
      blockIndices_[&block] = blockIndex++;
    }

    for (auto &block : *func)
    {
      for (auto &inst : block)
      {
        if (llvm::isa<llvm::BinaryOperator>(inst) ||
            llvm::isa<llvm::CmpInst>(inst) ||
            llvm::isa<llvm::ZExtInst>(inst) ||
            llvm::isa<llvm::IntToPtrInst>(inst) ||
            llvm::isa<llvm::PtrToIntInst>(inst) ||
            llvm::isa<llvm::BitCastInst>(inst) ||
            llvm::isa<llvm::PHINode>(inst))
        {
          WasmType resultType = convertLLVMType(inst.getType());
          assignLocalIndex(&inst, resultType, wasmFunc);
        }
        else if (!inst.getType()->isVoidTy())
        {
          WasmType localType = convertLLVMType(inst.getType());
          if (localType != WasmType::VOID)
          {
            wasmFunc.locals.push_back(localType);
            localMap_[&inst] = localIndex++;
          }
        }
      }
    }

    std::vector<llvm::BasicBlock *> blockOrder;
    for (auto &block : *func)
    {
      blockOrder.push_back(&block);
    }

    std::map<llvm::BasicBlock *, size_t> blockPositions;
    for (size_t i = 0; i < blockOrder.size(); ++i)
    {
      blockPositions[blockOrder[i]] = i;
    }

    for (auto &block : *func)
    {
      size_t currentPos = blockPositions[&block];
      for (auto &inst : block)
      {
        if (llvm::isa<llvm::BranchInst>(inst))
        {
          llvm::BranchInst *branch = llvm::cast<llvm::BranchInst>(&inst);
          if (branch->isUnconditional())
          {
            llvm::BasicBlock *target = branch->getSuccessor(0);
            size_t targetPos = blockPositions[target];
            if (targetPos > currentPos + 1)
            {
              uint32_t depth = 0;
              for (size_t i = currentPos + 1; i < targetPos; ++i)
              {
                depth++;
              }
              blockMap_[&block] = depth;
            }
          }
        }
      }
      if (!convertBasicBlock(&block, wasmFunc))
      {
        return false;
      }
    }

    wasmModule_.functions.push_back(wasmFunc);
    wasmModule_.functionIndices[func->getName().str()] = wasmModule_.functions.size() - 1;

    return true;
  }

  bool WasmGenerator::convertBasicBlock(llvm::BasicBlock *block, WasmFunction &wasmFunc)
  {
    for (auto &inst : *block)
    {
      if (!convertInstruction(&inst, wasmFunc))
      {
        return false;
      }
    }
    return true;
  }

  bool WasmGenerator::convertInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    if (llvm::isa<llvm::BinaryOperator>(inst))
    {
      return convertArithmeticInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::CmpInst>(inst))
    {
      return convertCompareInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::BranchInst>(inst))
    {
      return convertBranchInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::CallInst>(inst))
    {
      return convertCallInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::ReturnInst>(inst))
    {
      return convertReturnInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::LoadInst>(inst) || llvm::isa<llvm::StoreInst>(inst))
    {
      return convertMemoryInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::AllocaInst>(inst))
    {
      return true;
    }
    else if (llvm::isa<llvm::IntToPtrInst>(inst))
    {
      return convertIntToPtrInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::PtrToIntInst>(inst))
    {
      return convertPtrToIntInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::BitCastInst>(inst))
    {
      return convertBitCastInstruction(inst, wasmFunc);
    }
    else if (llvm::isa<llvm::ZExtInst>(inst))
    {
      llvm::ZExtInst *zext = llvm::cast<llvm::ZExtInst>(inst);
      llvm::Value *op = zext->getOperand(0);

      if (llvm::isa<llvm::CmpInst>(op))
      {
        if (!convertCompareInstruction(llvm::cast<llvm::Instruction>(op), wasmFunc))
        {
          return false;
        }
      }
      else if (llvm::isa<llvm::ConstantInt>(op))
      {
        auto *constInt = llvm::cast<llvm::ConstantInt>(op);
        instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
      }
      else if (llvm::isa<llvm::LoadInst>(op))
      {
        auto *load = llvm::cast<llvm::LoadInst>(op);
        uint32_t localIdx = getLocalIndex(load->getPointerOperand());
        instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
      }
      else if (llvm::isa<llvm::PHINode>(op) || llvm::isa<llvm::Instruction>(op))
      {
        uint32_t localIdx = getLocalIndex(op);
        instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
      }
      else
      {
        errorMessage_ = "Unsupported ZExt operand";
        return false;
      }

      uint32_t resultIdx = assignLocalIndex(inst, convertLLVMType(inst->getType()), wasmFunc);
      instructions.push_back(WasmInstruction(WasmOpcode::SET_LOCAL, resultIdx));
      return true;
    }
    else if (llvm::isa<llvm::PHINode>(inst))
    {
      return convertPhiInstruction(inst, wasmFunc);
    }

    errorMessage_ = "Unsupported LLVM instruction: " + std::string(inst->getOpcodeName());
    return false;
  }

  bool WasmGenerator::convertArithmeticInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    llvm::BinaryOperator *binOp = llvm::cast<llvm::BinaryOperator>(inst);

    llvm::Value *lhs = binOp->getOperand(0);
    llvm::Value *rhs = binOp->getOperand(1);
    if (llvm::isa<llvm::ConstantInt>(lhs))
    {
      llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(lhs);
      instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
    }
    else if (llvm::isa<llvm::LoadInst>(lhs))
    {
      llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(lhs);
      uint32_t localIdx = getLocalIndex(load->getPointerOperand());
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }
    else if (llvm::isa<llvm::Instruction>(lhs))
    {
      uint32_t localIdx = getLocalIndex(lhs);
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }

    if (llvm::isa<llvm::ConstantInt>(rhs))
    {
      llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(rhs);
      instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
    }
    else if (llvm::isa<llvm::LoadInst>(rhs))
    {
      llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(rhs);
      uint32_t localIdx = getLocalIndex(load->getPointerOperand());
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }
    else if (llvm::isa<llvm::Instruction>(rhs))
    {
      uint32_t localIdx = getLocalIndex(rhs);
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }

    switch (binOp->getOpcode())
    {
    case llvm::Instruction::Add:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_ADD));
      break;
    case llvm::Instruction::Sub:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_SUB));
      break;
    case llvm::Instruction::Mul:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_MUL));
      break;
    case llvm::Instruction::SDiv:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_DIV_S));
      break;
    case llvm::Instruction::UDiv:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_DIV_U));
      break;
    default:
      errorMessage_ = "Unsupported arithmetic operation: " + std::string(binOp->getOpcodeName());
      return false;
    }

    uint32_t resultIdx = assignLocalIndex(inst, convertLLVMType(inst->getType()), wasmFunc);
    instructions.push_back(WasmInstruction(WasmOpcode::SET_LOCAL, resultIdx));

    return true;
  }

  bool WasmGenerator::convertCompareInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    llvm::CmpInst *cmpInst = llvm::cast<llvm::CmpInst>(inst);

    llvm::Value *lhs = cmpInst->getOperand(0);
    llvm::Value *rhs = cmpInst->getOperand(1);
    if (llvm::isa<llvm::ConstantInt>(lhs))
    {
      llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(lhs);
      instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
    }
    else if (llvm::isa<llvm::LoadInst>(lhs))
    {
      llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(lhs);
      uint32_t localIdx = getLocalIndex(load->getPointerOperand());
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }

    if (llvm::isa<llvm::ConstantInt>(rhs))
    {
      llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(rhs);
      instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
    }
    else if (llvm::isa<llvm::LoadInst>(rhs))
    {
      llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(rhs);
      uint32_t localIdx = getLocalIndex(load->getPointerOperand());
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }

    switch (cmpInst->getPredicate())
    {
    case llvm::CmpInst::ICMP_EQ:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_EQ));
      break;
    case llvm::CmpInst::ICMP_NE:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_NE));
      break;
    case llvm::CmpInst::ICMP_SLT:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_LT_S));
      break;
    case llvm::CmpInst::ICMP_ULT:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_LT_U));
      break;
    case llvm::CmpInst::ICMP_SGT:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_GT_S));
      break;
    case llvm::CmpInst::ICMP_UGT:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_GT_U));
      break;
    case llvm::CmpInst::ICMP_SLE:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_LE_S));
      break;
    case llvm::CmpInst::ICMP_ULE:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_LE_U));
      break;
    case llvm::CmpInst::ICMP_SGE:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_GE_S));
      break;
    case llvm::CmpInst::ICMP_UGE:
      instructions.push_back(WasmInstruction(WasmOpcode::I32_GE_U));
      break;
    default:
      errorMessage_ = "Unsupported comparison operation";
      return false;
    }

    return true;
  }

  bool WasmGenerator::convertBranchInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    llvm::BranchInst *branchInst = llvm::cast<llvm::BranchInst>(inst);

    if (branchInst->isUnconditional())
    {
      llvm::BasicBlock *target = branchInst->getSuccessor(0);
      auto it = blockMap_.find(branchInst->getParent());
      if (it != blockMap_.end())
      {
        instructions.push_back(WasmInstruction(WasmOpcode::BR, it->second));
      }
      return true;
    }
    else
    {
      llvm::Value *condition = branchInst->getCondition();
      llvm::BasicBlock *trueTarget = branchInst->getSuccessor(0);
      llvm::BasicBlock *falseTarget = branchInst->getSuccessor(1);

      if (llvm::isa<llvm::ICmpInst>(condition))
      {
        llvm::ICmpInst *icmp = llvm::cast<llvm::ICmpInst>(condition);
        llvm::Value *left = icmp->getOperand(0);
        llvm::Value *right = icmp->getOperand(1);

        if (llvm::isa<llvm::LoadInst>(left))
        {
          llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(left);
          uint32_t localIdx = getLocalIndex(load->getPointerOperand());
          instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
        }
        else if (llvm::isa<llvm::ConstantInt>(left))
        {
          auto *constInt = llvm::cast<llvm::ConstantInt>(left);
          instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
        }

        if (llvm::isa<llvm::LoadInst>(right))
        {
          llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(right);
          uint32_t localIdx = getLocalIndex(load->getPointerOperand());
          instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
        }
        else if (llvm::isa<llvm::ConstantInt>(right))
        {
          auto *constInt = llvm::cast<llvm::ConstantInt>(right);
          instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
        }

        switch (icmp->getPredicate())
        {
        case llvm::CmpInst::ICMP_EQ:
          instructions.push_back(WasmInstruction(WasmOpcode::I32_EQ));
          break;
        case llvm::CmpInst::ICMP_NE:
          instructions.push_back(WasmInstruction(WasmOpcode::I32_NE));
          break;
        case llvm::CmpInst::ICMP_SLT:
          instructions.push_back(WasmInstruction(WasmOpcode::I32_LT_S));
          break;
        case llvm::CmpInst::ICMP_SGT:
          instructions.push_back(WasmInstruction(WasmOpcode::I32_GT_S));
          break;
        case llvm::CmpInst::ICMP_SLE:
          instructions.push_back(WasmInstruction(WasmOpcode::I32_LE_S));
          break;
        case llvm::CmpInst::ICMP_SGE:
          instructions.push_back(WasmInstruction(WasmOpcode::I32_GE_S));
          break;
        default:
          instructions.push_back(WasmInstruction(WasmOpcode::I32_EQ));
          break;
        }
      }
      else if (llvm::isa<llvm::LoadInst>(condition))
      {
        llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(condition);
        uint32_t localIdx = getLocalIndex(load->getPointerOperand());
        instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
        instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, 0));
        instructions.push_back(WasmInstruction(WasmOpcode::I32_NE));
      }
      else if (llvm::isa<llvm::ZExtInst>(condition))
      {
        llvm::ZExtInst *zext = llvm::cast<llvm::ZExtInst>(condition);
        llvm::Value *src = zext->getOperand(0);
        if (llvm::isa<llvm::LoadInst>(src))
        {
          llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(src);
          uint32_t localIdx = getLocalIndex(load->getPointerOperand());
          instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
        }
        else
        {
          uint32_t localIdx = getLocalIndex(src);
          instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
        }
        instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, 0));
        instructions.push_back(WasmInstruction(WasmOpcode::I32_NE));
      }

      std::vector<llvm::BasicBlock *> blockOrder;
      for (auto &block : *currentFunction_)
      {
        blockOrder.push_back(&block);
      }

      std::map<llvm::BasicBlock *, size_t> blockPositions;
      for (size_t i = 0; i < blockOrder.size(); ++i)
      {
        blockPositions[blockOrder[i]] = i;
      }

      size_t currentPos = blockPositions[branchInst->getParent()];
      size_t truePos = blockPositions[trueTarget];
      size_t falsePos = blockPositions[falseTarget];

      if (falsePos == currentPos + 1)
      {
        uint32_t depth = 0;
        if (truePos > currentPos + 1)
        {
          for (size_t i = currentPos + 1; i < truePos; ++i)
          {
            depth++;
          }
        }
        instructions.push_back(WasmInstruction(WasmOpcode::BR_IF, depth));
      }
      else
      {
        if (truePos == currentPos + 1)
        {
          instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, 0));
          instructions.push_back(WasmInstruction(WasmOpcode::I32_EQ));
          uint32_t depth = 0;
          if (falsePos > currentPos + 1)
          {
            for (size_t i = currentPos + 1; i < falsePos; ++i)
            {
              depth++;
            }
          }
          instructions.push_back(WasmInstruction(WasmOpcode::BR_IF, depth));
        }
        else
        {
          instructions.push_back(WasmInstruction(WasmOpcode::BR_IF, 0));
        }
      }
    }

    return true;
  }

  bool WasmGenerator::convertCallInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    llvm::CallInst *callInst = llvm::cast<llvm::CallInst>(inst);

    for (auto &arg : callInst->args())
    {
      if (llvm::isa<llvm::ConstantInt>(arg))
      {
        llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(arg);
        instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
      }
    }

    llvm::Function *calledFunc = callInst->getCalledFunction();
    if (calledFunc)
    {
      auto it = functionMap_.find(calledFunc);
      if (it != functionMap_.end())
      {
        instructions.push_back(WasmInstruction(WasmOpcode::CALL, it->second));
      }
    }

    return true;
  }

  bool WasmGenerator::convertReturnInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    llvm::ReturnInst *retInst = llvm::cast<llvm::ReturnInst>(inst);

    if (retInst->getNumOperands() > 0)
    {
      llvm::Value *retValue = retInst->getOperand(0);
      if (llvm::isa<llvm::ConstantInt>(retValue))
      {
        llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(retValue);
        instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
      }
    }

    instructions.push_back(WasmInstruction(WasmOpcode::RETURN));
    return true;
  }

  bool WasmGenerator::convertMemoryInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    if (llvm::isa<llvm::LoadInst>(inst))
    {
      llvm::LoadInst *loadInst = llvm::cast<llvm::LoadInst>(inst);

      llvm::Value *ptrOperand = loadInst->getPointerOperand();

      if (llvm::isa<llvm::IntToPtrInst>(ptrOperand))
      {
        llvm::IntToPtrInst *intToPtr = llvm::cast<llvm::IntToPtrInst>(ptrOperand);
        llvm::Value *addrOperand = intToPtr->getOperand(0);

        if (llvm::isa<llvm::BinaryOperator>(addrOperand))
        {
          llvm::BinaryOperator *binOp = llvm::cast<llvm::BinaryOperator>(addrOperand);
          convertArithmeticInstruction(binOp, wasmFunc);
        }
        else if (llvm::isa<llvm::ConstantInt>(addrOperand))
        {
          llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(addrOperand);
          instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
        }
        else if (llvm::isa<llvm::LoadInst>(addrOperand))
        {
          llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(addrOperand);
          uint32_t localIdx = getLocalIndex(load->getPointerOperand());
          instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
        }
      }
      else
      {
        uint32_t localIdx = getLocalIndex(ptrOperand);
        instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
      }

      instructions.push_back(WasmInstruction(WasmOpcode::I32_LOAD));
    }
    else if (llvm::isa<llvm::StoreInst>(inst))
    {
      llvm::StoreInst *storeInst = llvm::cast<llvm::StoreInst>(inst);

      llvm::Value *ptrOperand = storeInst->getPointerOperand();

      if (llvm::isa<llvm::IntToPtrInst>(ptrOperand))
      {
        llvm::IntToPtrInst *intToPtr = llvm::cast<llvm::IntToPtrInst>(ptrOperand);
        llvm::Value *addrOperand = intToPtr->getOperand(0);

        if (llvm::isa<llvm::BinaryOperator>(addrOperand))
        {
          llvm::BinaryOperator *binOp = llvm::cast<llvm::BinaryOperator>(addrOperand);
          convertArithmeticInstruction(binOp, wasmFunc);
        }
        else if (llvm::isa<llvm::ConstantInt>(addrOperand))
        {
          llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(addrOperand);
          instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
        }
        else if (llvm::isa<llvm::LoadInst>(addrOperand))
        {
          llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(addrOperand);
          uint32_t localIdx = getLocalIndex(load->getPointerOperand());
          instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
        }
      }
      else
      {
        uint32_t localIdx = getLocalIndex(ptrOperand);
        instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
      }

      llvm::Value *value = storeInst->getValueOperand();
      if (llvm::isa<llvm::ConstantInt>(value))
      {
        llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(value);
        instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
      }
      else if (llvm::isa<llvm::LoadInst>(value))
      {
        llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(value);
        uint32_t valLocalIdx = getLocalIndex(load->getPointerOperand());
        instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, valLocalIdx));
      }
      else if (llvm::isa<llvm::ZExtInst>(value))
      {
        uint32_t valLocalIdx = getLocalIndex(value);
        instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, valLocalIdx));
      }
      else if (llvm::isa<llvm::Instruction>(value))
      {
        uint32_t valLocalIdx = getLocalIndex(value);
        instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, valLocalIdx));
      }

      instructions.push_back(WasmInstruction(WasmOpcode::I32_STORE));
    }

    return true;
  }

  uint32_t WasmGenerator::assignLocalIndex(llvm::Value *value, WasmType type, WasmFunction &wasmFunc)
  {
    if (!value)
    {
      return 0;
    }

    auto it = localMap_.find(value);
    if (it != localMap_.end())
    {
      return it->second;
    }

    uint32_t index = static_cast<uint32_t>(wasmFunc.params.size() + wasmFunc.locals.size());
    wasmFunc.locals.push_back(type);
    localMap_[value] = index;

    return index;
  }

  uint32_t WasmGenerator::getLocalIndex(llvm::Value *value)
  {
    auto it = localMap_.find(value);
    if (it != localMap_.end())
    {
      return it->second;
    }

    return 0;
  }

  bool WasmGenerator::convertPhiInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;
    llvm::PHINode *phi = llvm::cast<llvm::PHINode>(inst);

    uint32_t resultIdx = getLocalIndex(phi);

    instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, resultIdx));
    return true;
  }

  std::vector<uint8_t> WasmGenerator::generateBinary() const
  {
    std::vector<uint8_t> binary;

    binary.push_back(0x00);
    binary.push_back(0x61);
    binary.push_back(0x73);
    binary.push_back(0x6D);

    binary.push_back(0x01);
    binary.push_back(0x00);
    binary.push_back(0x00);
    binary.push_back(0x00);

    binary.push_back(0x03);
    binary.push_back(0x01);
    binary.push_back(static_cast<uint8_t>(wasmModule_.functions.size()));

    binary.push_back(0x0A);
    binary.push_back(0x01);
    binary.push_back(static_cast<uint8_t>(wasmModule_.functions.size()));

    for (const auto &func : wasmModule_.functions)
    {
      binary.push_back(0x01);
      binary.push_back(0x00);
    }

    return binary;
  }

  std::string WasmGenerator::generateWast() const
  {
    std::ostringstream wast;

    wast << "(module\n";

    wast << "  (memory " << wasmModule_.memorySize;
    if (wasmModule_.memoryMaxSize > 0)
    {
      wast << " " << wasmModule_.memoryMaxSize;
    }
    wast << ")\n";

    for (const auto &func : wasmModule_.functions)
    {
      wast << generateFunctionWast(func) << "\n";
    }

    wast << ")\n";

    return wast.str();
  }

  std::string WasmGenerator::generateFunctionWast(const WasmFunction &func) const
  {
    std::ostringstream wast;

    wast << "  (func $" << func.name;

    for (size_t i = 0; i < func.params.size(); ++i)
    {
      wast << " (param $" << i << " " << getWasmTypeString(func.params[i]) << ")";
    }

    if (func.returnType != WasmType::VOID)
    {
      wast << " (result " << getWasmTypeString(func.returnType) << ")";
    }

    for (size_t i = 0; i < func.locals.size(); ++i)
    {
      wast << " (local $" << (func.params.size() + i) << " " << getWasmTypeString(func.locals[i]) << ")";
    }

    wast << "\n";

    for (const auto &inst : func.instructions)
    {
      wast << "    " << generateInstructionWast(inst) << "\n";
    }

    wast << "  )";

    return wast.str();
  }

  std::string WasmGenerator::generateInstructionWast(const WasmInstruction &inst) const
  {
    std::ostringstream wast;

    wast << getWasmOpcodeString(inst.opcode);

    for (uint64_t operand : inst.operands)
    {
      wast << " " << operand;
    }

    return wast.str();
  }

  std::string WasmGenerator::getWasmTypeString(WasmType type) const
  {
    switch (type)
    {
    case WasmType::I32:
      return "i32";
    case WasmType::I64:
      return "i64";
    case WasmType::F32:
      return "f32";
    case WasmType::F64:
      return "f64";
    case WasmType::VOID:
      return "void";
    default:
      return "i32";
    }
  }

  std::string WasmGenerator::getWasmOpcodeString(WasmOpcode opcode) const
  {
    switch (opcode)
    {
    case WasmOpcode::I32_CONST:
      return "i32.const";
    case WasmOpcode::I32_ADD:
      return "i32.add";
    case WasmOpcode::I32_SUB:
      return "i32.sub";
    case WasmOpcode::I32_MUL:
      return "i32.mul";
    case WasmOpcode::I32_DIV_S:
      return "i32.div_s";
    case WasmOpcode::I32_DIV_U:
      return "i32.div_u";
    case WasmOpcode::I32_EQ:
      return "i32.eq";
    case WasmOpcode::I32_NE:
      return "i32.ne";
    case WasmOpcode::I32_LT_S:
      return "i32.lt_s";
    case WasmOpcode::I32_LT_U:
      return "i32.lt_u";
    case WasmOpcode::I32_GT_S:
      return "i32.gt_s";
    case WasmOpcode::I32_GT_U:
      return "i32.gt_u";
    case WasmOpcode::I32_LE_S:
      return "i32.le_s";
    case WasmOpcode::I32_LE_U:
      return "i32.le_u";
    case WasmOpcode::I32_GE_S:
      return "i32.ge_s";
    case WasmOpcode::I32_GE_U:
      return "i32.ge_u";
    case WasmOpcode::GET_LOCAL:
      return "local.get";
    case WasmOpcode::SET_LOCAL:
      return "local.set";
    case WasmOpcode::CALL:
      return "call";
    case WasmOpcode::RETURN:
      return "return";
    case WasmOpcode::BR:
      return "br";
    case WasmOpcode::BR_IF:
      return "br_if";
    case WasmOpcode::I32_LOAD:
      return "i32.load";
    case WasmOpcode::I32_STORE:
      return "i32.store";
    default:
      return "unknown";
    }
  }

  bool WasmGenerator::convertIntToPtrInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    llvm::IntToPtrInst *intToPtr = llvm::cast<llvm::IntToPtrInst>(inst);

    llvm::Value *operand = intToPtr->getOperand(0);

    if (llvm::isa<llvm::ConstantInt>(operand))
    {
      llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(operand);
      instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
    }
    else if (llvm::isa<llvm::LoadInst>(operand))
    {
      llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(operand);
      uint32_t localIdx = getLocalIndex(load->getPointerOperand());
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }
    else if (llvm::isa<llvm::BinaryOperator>(operand))
    {
      llvm::BinaryOperator *binOp = llvm::cast<llvm::BinaryOperator>(operand);
      convertArithmeticInstruction(binOp, wasmFunc);
    }

    return true;
  }

  bool WasmGenerator::convertPtrToIntInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    llvm::PtrToIntInst *ptrToInt = llvm::cast<llvm::PtrToIntInst>(inst);

    llvm::Value *operand = ptrToInt->getOperand(0);

    if (llvm::isa<llvm::ConstantInt>(operand))
    {
      llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(operand);
      instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
    }
    else if (llvm::isa<llvm::LoadInst>(operand))
    {
      llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(operand);
      uint32_t localIdx = getLocalIndex(load->getPointerOperand());
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }

    return true;
  }

  bool WasmGenerator::convertBitCastInstruction(llvm::Instruction *inst, WasmFunction &wasmFunc)
  {
    auto &instructions = wasmFunc.instructions;

    llvm::BitCastInst *bitCast = llvm::cast<llvm::BitCastInst>(inst);

    llvm::Value *operand = bitCast->getOperand(0);

    if (llvm::isa<llvm::ConstantInt>(operand))
    {
      llvm::ConstantInt *constInt = llvm::cast<llvm::ConstantInt>(operand);
      instructions.push_back(WasmInstruction(WasmOpcode::I32_CONST, constInt->getZExtValue()));
    }
    else if (llvm::isa<llvm::LoadInst>(operand))
    {
      llvm::LoadInst *load = llvm::cast<llvm::LoadInst>(operand);
      uint32_t localIdx = getLocalIndex(load->getPointerOperand());
      instructions.push_back(WasmInstruction(WasmOpcode::GET_LOCAL, localIdx));
    }

    return true;
  }

}