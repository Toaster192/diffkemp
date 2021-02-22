//===--- PatternFunctionComparator.h - Code pattern instruction matcher ---===//
//
//       SimpLL - Program simplifier for analysis of semantic difference      //
//
// This file is published under Apache 2.0 license. See LICENSE for details.
// Author: Petr Silling, psilling@redhat.com
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the LLVM code pattern matcher. The
/// pattern matcher is a comparator extension of the LLVM FunctionComparator
/// tailored to difference pattern comparison.
///
//===----------------------------------------------------------------------===//

#ifndef DIFFKEMP_SIMPLL_PATTERNFUNCTIONCOMPARATOR_H
#define DIFFKEMP_SIMPLL_PATTERNFUNCTIONCOMPARATOR_H

#include "FunctionComparator.h"
#include "PatternSet.h"
#include <unordered_map>
#include <vector>

using namespace llvm;

/// Extension of LLVM FunctionComparator which compares a difference pattern
/// against its corresponding module function. Compared functions are expected
/// to lie in different modules.
class PatternFunctionComparator : protected FunctionComparator {
  public:
    /// Pattern instructions matched to their respective module replacement
    /// instructions. Pattern instructions are used as keys.
    mutable InstructionMap InstMatchMap;

    PatternFunctionComparator(const Function *ModFun,
                              const Function *PatFun,
                              const Pattern *ParentPattern)
            : FunctionComparator(ModFun, PatFun, nullptr),
              IsNewSide(ParentPattern->NewPattern == PatFun),
              ParentPattern(ParentPattern){};

    /// Compare the module function and the difference pattern from the starting
    /// module instruction.
    int compare() override;

    /// Set the starting module instruction.
    void setStartInstruction(const Instruction *StartModInst);

  protected:
    /// Clear all result structures to prepare for a new comparison.
    void beginCompare();

    /// Compare a module function instruction with a pattern instruction along
    /// with their operands.
    int cmpOperationsWithOperands(const Instruction *L,
                                  const Instruction *R) const;

    /// Compare a module function basic block with a pattern basic block.
    int cmpBasicBlocks(const BasicBlock *BBL,
                       const BasicBlock *BBR) const override;

    /// Compare global values by their names, because their indexes are not
    /// expected to be the same.
    int cmpGlobalValues(GlobalValue *L, GlobalValue *R) const override;

  private:
    /// Whether the comparator has been created for the new side of the pattern.
    const bool IsNewSide;
    /// The pattern which should be used during comparison.
    const Pattern *ParentPattern;
    /// The staring instruction of the compared module function.
    mutable const Instruction *StartInst;

    /// Position the basic block instruction iterator forward to the given
    /// starting instruction.
    void jumpToStartInst(BasicBlock::const_iterator &BBIterator,
                         const Instruction *Start) const;
};

#endif // DIFFKEMP_SIMPLL_PATTERNFUNCTIONCOMPARATOR_H
