#include "assembly_lifter.h"

#include <llvm/IR/Instructions.h>
#include <algorithm>
#include <cctype>

namespace asm2wasm
{

  llvm::Value *AssemblyLifter::calculateMemoryAddress(const Operand &operand)
  {
    std::string addr = operand.value.substr(1, operand.value.length() - 2);

    if (addr.find('+') != std::string::npos)
    {
      size_t plusPos = addr.find('+');
      std::string basePart = addr.substr(0, plusPos);
      std::string offsetPart = addr.substr(plusPos + 1);

      llvm::Value *result = nullptr;

      if (!basePart.empty() && basePart[0] == '%')
      {
        llvm::Value *baseReg = getOrCreateRegister(basePart);
        result = builder_->CreateLoad(getIntType(), baseReg, "base_addr");
      }

      if (offsetPart.find('*') != std::string::npos)
      {
        size_t starPos = offsetPart.find('*');
        std::string indexRegStr = offsetPart.substr(0, starPos);
        std::string scaleStr = offsetPart.substr(starPos + 1);

        if (indexRegStr[0] == '%')
        {
          llvm::Value *indexReg = getOrCreateRegister(indexRegStr);
          llvm::Value *indexValue = builder_->CreateLoad(getIntType(), indexReg, "index_val");

          int scale = std::stoi(scaleStr);
          llvm::Value *scaleValue = llvm::ConstantInt::get(getIntType(), scale);
          llvm::Value *scaledIndex = builder_->CreateMul(indexValue, scaleValue, "scaled_index");

          if (result)
          {
            result = builder_->CreateAdd(result, scaledIndex, "indexed_mem_addr");
          }
          else
          {
            result = scaledIndex;
          }
        }
      }
      else if (std::all_of(offsetPart.begin(), offsetPart.end(), [](char c)
                           { return std::isdigit(c) || c == '-' || c == '+'; }))
      {
        int offset = std::stoi(offsetPart);
        llvm::Value *offsetValue = llvm::ConstantInt::get(getIntType(), offset);

        if (result)
        {
          result = builder_->CreateAdd(result, offsetValue, "offset_mem_addr");
        }
        else
        {
          result = offsetValue;
        }
      }
      else if (offsetPart[0] == '%')
      {
        llvm::Value *offsetReg = getOrCreateRegister(offsetPart);
        llvm::Value *offsetValue = builder_->CreateLoad(getIntType(), offsetReg, "offset_val");

        if (result)
        {
          result = builder_->CreateAdd(result, offsetValue, "reg_offset_mem_addr");
        }
        else
        {
          result = offsetValue;
        }
      }

      if (!result)
      {
        errorMessage_ = "Failed to calculate memory address: " + operand.value;
        return nullptr;
      }

      return result;
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

}
