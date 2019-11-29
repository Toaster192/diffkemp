//===------------ ModuleComparator.cpp - Comparing LLVM modules -----------===//
//
//       SimpLL - Program simplifier for analysis of semantic difference      //
//
// This file is published under Apache 2.0 license. See LICENSE for details.
// Author: Viktor Malik, vmalik@redhat.com
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains definitions of methods of the ModuleComparator class that
/// can be used for syntactical comparison of two LLVM modules.
///
//===----------------------------------------------------------------------===//

#include "ModuleComparator.h"
#include "Config.h"
#include "DifferentialFunctionComparator.h"
#include "Utils.h"
#include "passes/FieldAccessFunctionGenerator.h"
#include "passes/FunctionAbstractionsGenerator.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>

/// Syntactical comparison of functions.
/// Function declarations are equal if they have the same name.
/// Functions with body are compared using custom FunctionComparator that
/// is designed for comparing functions between different modules.
void ModuleComparator::compareFunctions(Function *FirstFun,
                                        Function *SecondFun) {
    DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                    dbgs() << getDebugIndent() << "Comparing "
                           << FirstFun->getName() << " and "
                           << SecondFun->getName() << "\n";
                    increaseDebugIndentLevel());
    ComparedFuns.emplace(std::make_pair(FirstFun, SecondFun),
                         Result(FirstFun, SecondFun));

    // Comparing function declarations (function without bodies).
    if (FirstFun->isDeclaration() || SecondFun->isDeclaration()) {
        // Drop suffixes of function names. This is necessary in order to
        // successfully compare an original void-returning function with one
        // generated by RemoveUnusedReturnValuesPass, which will have a number
        // suffix.
        auto FirstFunName = FirstFun->getName().str();
        if (hasSuffix(FirstFunName))
            FirstFunName = dropSuffix(FirstFunName);
        auto SecondFunName = SecondFun->getName().str();
        if (hasSuffix(SecondFunName))
            SecondFunName = dropSuffix(SecondFunName);

        if (controlFlowOnly) {
            // If checking control flow only, it suffices that one of the
            // functions is a declaration to treat them equal.
            if (FirstFunName == SecondFunName)
                ComparedFuns.at({FirstFun, SecondFun}).kind = Result::EQUAL;
            else
                ComparedFuns.at({FirstFun, SecondFun}).kind = Result::NOT_EQUAL;
        } else {
            if (FirstFun->isDeclaration() && SecondFun->isDeclaration()
                && FirstFunName == SecondFunName)
                ComparedFuns.at({FirstFun, SecondFun}).kind = Result::EQUAL;
            else if (FirstFunName != SecondFunName)
                ComparedFuns.at({FirstFun, SecondFun}).kind = Result::NOT_EQUAL;
            else {
                // One function has a body, the second one does not; add the
                // missing definition
                if (FirstFun->isDeclaration())
                    this->MissingDefs.push_back({FirstFun, nullptr});
                else if (SecondFun->isDeclaration())
                    this->MissingDefs.push_back({nullptr, SecondFun});
            }
        }

        DEBUG_WITH_TYPE(
                DEBUG_SIMPLL, decreaseDebugIndentLevel();
                if (ComparedFuns.at({FirstFun, SecondFun}).kind
                    == Result::EQUAL) {
                    dbgs() << getDebugIndent() << "Declarations with matching "
                           << "names, assuming they are equal\n";
                } else if (ComparedFuns.at({FirstFun, SecondFun}).kind
                           == Result::NOT_EQUAL) {
                    dbgs() << getDebugIndent()
                           << "Declarations without matching "
                           << "names, assuming they are not equal\n";
                });

        return;
    }

    // Comparing functions with bodies using custom FunctionComparator.
    DifferentialFunctionComparator fComp(
            FirstFun, SecondFun, controlFlowOnly, showAsmDiffs, DI, this);
    int result = fComp.compare();

    DEBUG_WITH_TYPE(DEBUG_SIMPLL, decreaseDebugIndentLevel());
    if (result == 0) {
        DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                        dbgs() << getDebugIndent() << "Functions are equal\n");
        ComparedFuns.at({FirstFun, SecondFun}).kind = Result::EQUAL;
    } else {
        DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                        dbgs() << getDebugIndent()
                               << "Functions are not equal\n");
        ComparedFuns.at({FirstFun, SecondFun}).kind = Result::NOT_EQUAL;
        while (tryInline.first || tryInline.second) {
            DEBUG_WITH_TYPE(DEBUG_SIMPLL, increaseDebugIndentLevel());

            // Try to inline the problematic function calls
            CallInst *inlineFirst = findCallInst(tryInline.first, FirstFun);
            CallInst *inlineSecond = findCallInst(tryInline.second, SecondFun);

            ConstFunPair missingDefs;
            bool inlined = false;
            Function *InlinedFunFirst =
                    !inlineFirst
                            ? nullptr
                            : getCalledFunction(inlineFirst->getCalledValue());
            Function *InlinedFunSecond =
                    !inlineSecond
                            ? nullptr
                            : getCalledFunction(inlineSecond->getCalledValue());
            // Here it is good to make some changes to the variables.
            // If we have two functions to inline and one of them is a field
            // access abstraction, postponing the inlining of the abstraction
            // to the point all other functions are inlined is useful for
            // structure type difference detection (because it relies on them).
            if (inlineFirst && inlineSecond
                && isSimpllFieldAccessAbstraction(InlinedFunFirst)
                && !isSimpllFieldAccessAbstraction(InlinedFunSecond))
                inlineFirst = nullptr;
            if (inlineFirst && inlineSecond
                && isSimpllFieldAccessAbstraction(InlinedFunSecond)
                && !isSimpllFieldAccessAbstraction(InlinedFunFirst))
                inlineSecond = nullptr;
            // If the called function is a declaration, add it to missingDefs.
            // Otherwise, inline the call and simplify the function.
            // The above is done for the first and the second call to inline.
            if (inlineFirst) {
                const Function *toInline =
                        getCalledFunction(inlineFirst->getCalledValue());
                DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                                dbgs() << getDebugIndent() << "Try to inline "
                                       << toInline->getName() << " in first\n");
                if (toInline->isDeclaration()) {
                    DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                                    dbgs() << getDebugIndent()
                                           << "Missing definition\n");
                    if (!toInline->isIntrinsic()
                        && !isSimpllAbstraction(toInline))
                        missingDefs.first = toInline;
                } else {
                    InlineFunctionInfo ifi;
                    if (InlineFunction(inlineFirst, ifi, nullptr, false))
                        inlined = true;
                }
            }
            if (inlineSecond) {
                const Function *toInline =
                        getCalledFunction(inlineSecond->getCalledValue());
                DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                                dbgs() << getDebugIndent() << "Try to inline "
                                       << toInline->getName()
                                       << " in second\n");
                if (toInline->isDeclaration()) {
                    DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                                    dbgs() << getDebugIndent()
                                           << "Missing definition\n");
                    if (!toInline->isIntrinsic()
                        && !isSimpllAbstraction(toInline))
                        missingDefs.second = toInline;
                } else {
                    InlineFunctionInfo ifi;
                    if (InlineFunction(inlineSecond, ifi, nullptr, false))
                        inlined = true;
                }
            }
            // If some function to be inlined does not have a declaration,
            // store it into MissingDefs (will be reported at the end).
            if (missingDefs.first || missingDefs.second) {
                MissingDefs.push_back(missingDefs);
            }
            tryInline = {nullptr, nullptr};
            // If nothing was inlined, do not continue
            if (!inlined) {
                DEBUG_WITH_TYPE(DEBUG_SIMPLL, decreaseDebugIndentLevel());
                break;
            }
            simplifyFunction(FirstFun);
            simplifyFunction(SecondFun);
            // Reset the function diff result
            ComparedFuns.at({FirstFun, SecondFun}).kind = Result::UNKNOWN;
            // Re-run the comparison
            DifferentialFunctionComparator fCompSecond(FirstFun,
                                                       SecondFun,
                                                       controlFlowOnly,
                                                       showAsmDiffs,
                                                       DI,
                                                       this);
            result = fCompSecond.compare();
            // If the functions are equal after the inlining, we do not want to
            // report the called functions as unequal in case they are compared
            // as such alone - only the equivalence inside the compared function
            // matters here.
            if (!result)
                ComparedFuns.erase({InlinedFunFirst, InlinedFunSecond});

            DEBUG_WITH_TYPE(DEBUG_SIMPLL, decreaseDebugIndentLevel());
            if (result == 0) {
                DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                                dbgs() << getDebugIndent() << "After inlining, "
                                       << "the functions are equal\n");
                ComparedFuns.at({FirstFun, SecondFun}).kind = Result::EQUAL;
            } else {
                DEBUG_WITH_TYPE(DEBUG_SIMPLL,
                                dbgs() << getDebugIndent() << "After inlining, "
                                       << "the functions are not equal\n");
                ComparedFuns.at({FirstFun, SecondFun}).kind = Result::NOT_EQUAL;
            }
        }
    }
}
