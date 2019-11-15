//===------------- ModuleComparator.h - Comparing LLVM modules ------------===//
//
//       SimpLL - Program simplifier for analysis of semantic difference      //
//
// This file is published under Apache 2.0 license. See LICENSE for details.
// Author: Viktor Malik, vmalik@redhat.com
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the ModuleComparator class that can be
/// used for syntactical comparison of two LLVM modules.
///
//===----------------------------------------------------------------------===//

#ifndef DIFFKEMP_SIMPLL_MODULECOMPARATOR_H
#define DIFFKEMP_SIMPLL_MODULECOMPARATOR_H

#include "DebugInfo.h"
#include "Utils.h"
#include "passes/StructureDebugInfoAnalysis.h"
#include "passes/StructureSizeAnalysis.h"
#include <llvm/IR/Module.h>
#include <set>

using namespace llvm;

/// Generic type for non-function differences.
struct NonFunctionDifference {
    /// Discriminator for LLVM-style RTTI (dyn_cast<> et al.)
    enum DiffKind { SynDiff, TypeDiff };
    /// Name of the object.
    std::string name;
    /// Stacks containing the differing objects and all other objects affected
    /// by the difference (for both modules).
    CallStack StackL, StackR;
    /// The function in which the difference was found
    std::string function;
    NonFunctionDifference(DiffKind Kind) : Kind(Kind){};
    NonFunctionDifference(std::string name,
                          CallStack StackL,
                          CallStack StackR,
                          std::string function,
                          DiffKind Kind)
            : name(name), StackL(StackL), StackR(StackR), function(function),
              Kind(Kind) {}
    DiffKind getKind() const { return Kind; }

  private:
    const DiffKind Kind;
};

/// Syntactic difference between objects that cannot be found in the original
/// source files.
/// Note: this can be either a macro difference or inline assembly difference.
struct SyntaxDifference : public NonFunctionDifference {
    /// The difference.
    std::string BodyL, BodyR;
    SyntaxDifference() : NonFunctionDifference(SynDiff){};
    SyntaxDifference(std::string name,
                     std::string BodyL,
                     std::string BodyR,
                     CallStack StackL,
                     CallStack StackR,
                     std::string function)
            : NonFunctionDifference(name, StackL, StackR, function, SynDiff),
              BodyL(BodyL), BodyR(BodyR) {}
    static bool classof(const NonFunctionDifference *Diff) {
        return Diff->getKind() == SynDiff;
    }

  private:
    DiffKind kind;
};

/// Represents a difference between structure types (the actual diff is
/// generated by diffkemp in a way similar to functions diffs).
struct TypeDifference : public NonFunctionDifference {
    /// The files and lines where the type definitions are (for both modules).
    std::string FileL, FileR;
    int LineL, LineR;
    TypeDifference() : NonFunctionDifference(TypeDiff) {}
    static bool classof(const NonFunctionDifference *Diff) {
        return Diff->getKind() == TypeDiff;
    }

  private:
    DiffKind kind;
};

class ModuleComparator {
    Module &First;
    Module &Second;
    bool controlFlowOnly, showAsmDiffs;

  public:
    /// Possible results of syntactical function comparison.
    enum Result { EQUAL, NOT_EQUAL, UNKNOWN };
    /// Storing results of function comparisons.
    std::map<FunPair, Result> ComparedFuns;
    /// Storing results from macro, asm and type comparisons.
    std::vector<std::unique_ptr<NonFunctionDifference>> DifferingObjects;
    /// Storing covered functions names.
    /// Note: currently only from inlining.
    std::set<std::string> CoveredFuns;
    // Structure size to structure name map.
    StructureSizeAnalysis::Result &StructSizeMapL;
    StructureSizeAnalysis::Result &StructSizeMapR;
    // Structure name to structure debug info map.
    StructureDebugInfoAnalysis::Result &StructDIMapL;
    StructureDebugInfoAnalysis::Result &StructDIMapR;
    // Counter of assembly diffs
    int asmDifferenceCounter = 0;

    std::vector<ConstFunPair> MissingDefs;

    /// DebugInfo class storing results from analysing debug information
    const DebugInfo *DI;

    ModuleComparator(Module &First,
                     Module &Second,
                     bool controlFlowOnly,
                     bool showAsmDiffs,
                     const DebugInfo *DI,
                     StructureSizeAnalysis::Result &StructSizeMapL,
                     StructureSizeAnalysis::Result &StructSizeMapR,
                     StructureDebugInfoAnalysis::Result &StructDIMapL,
                     StructureDebugInfoAnalysis::Result &StructDIMapR)
            : First(First), Second(Second), controlFlowOnly(controlFlowOnly),
              showAsmDiffs(showAsmDiffs), DI(DI),
              StructSizeMapL(StructSizeMapL), StructSizeMapR(StructSizeMapR),
              StructDIMapL(StructDIMapL), StructDIMapR(StructDIMapR) {}

    /// Syntactically compare two functions.
    /// The result of the comparison is stored into the ComparedFuns map.
    void compareFunctions(Function *FirstFun, Function *SecondFun);

    /// Pointer to a function that is called just by one of the compared
    /// functions and needs to be inlined.
    std::pair<const CallInst *, const CallInst *> tryInline = {nullptr,
                                                               nullptr};
};

#endif // DIFFKEMP_SIMPLL_MODULECOMPARATOR_H
