//===--- RemoveUnusedReturnValuesPass.h - Transforming functions to void --===//
//
//       SimpLL - Program simplifier for analysis of semantic difference      //
//
// This file is published under Apache 2.0 license. See LICENSE for details.
// Author: Tomas Glozar, tglozar@gmail.com
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the RemoveUnusedReturnValues pass.
///
//===----------------------------------------------------------------------===//

#include "RemoveUnusedReturnValuesPass.h"
#include "Utils.h"
#include <llvm/IR/Instructions.h>

PreservedAnalyses RemoveUnusedReturnValuesPass::run(
        Module &Mod,
        ModuleAnalysisManager &mam,
        Function *Main) {

    // These attributes are invalid for void functions
    Attribute::AttrKind badAttributes[] = {
            Attribute::AttrKind::ByVal,
            Attribute::AttrKind::InAlloca,
            Attribute::AttrKind::Nest,
            Attribute::AttrKind::NoAlias,
            Attribute::AttrKind::NoCapture,
            Attribute::AttrKind::NonNull,
            Attribute::AttrKind::ReadNone,
            Attribute::AttrKind::ReadOnly,
            Attribute::AttrKind::SExt,
            Attribute::AttrKind::StructRet,
            Attribute::AttrKind::ZExt,
            Attribute::AttrKind::Dereferenceable,
            Attribute::AttrKind::DereferenceableOrNull
    };

    // Old functions ought to be deleted after iteration
    std::vector<Function *> functionsToDelete;

    for (Function &Fun : Mod) {
        if (Fun.getIntrinsicID() != llvm::Intrinsic::not_intrinsic)
            continue;

        if (Fun.getLinkage() != GlobalValue::LinkageTypes::ExternalLinkage)
            continue;

        if (Fun.getReturnType()->isVoidTy())
            continue;

        if (Main && !callsTransitively(*Main, Fun))
            continue;

        bool can_replace = true;
#ifdef DEBUG
        errs() << "Changing function: " << Fun.getName() << " to void\n";
#endif
        for (Use &U : Fun.uses()) {
            // Figure out whether the return value is used after each call

            if (auto CI = dyn_cast<CallInst>(U.getUser())) {
                if (CI->getCalledFunction() != &Fun)
                    // Different function is called, Fun is an argument
                    can_replace = false;
#ifdef DEBUG
                CI->print(errs(), false);
                errs() << "\n";
                for (Use &UU : CI->uses()) {
                    errs() << "  ";
                    UU.getUser()->print(errs(),
                                        false);
                    errs() << "\n";
                }
#endif
                if (!CI->use_empty())
                    // The return value is actually used
                    can_replace = false;
            } else if (auto II = dyn_cast<InvokeInst>(U.getUser())) {
                if (II->getCalledFunction() != &Fun)
                    // Different function is called, Fun is an argument
                    can_replace = false;
#ifdef DEBUG
                II->print(errs(), false);
                errs() << "\n";
                for (Use &UU : II->uses()) {
                    errs() << "  ";
                    UU.getUser()->print(errs(),
                                        false);
                    errs() << "\n";
                }
#endif
                if (!II->use_empty())
                    // The return value is actually used
                    can_replace = false;
            } else
                // The function is used somewhere as an argument, therefore
                // it ought not to be replaced
                can_replace = false;
        }

        if (can_replace) {
            // Create the header of the new function
            std::vector<Type *> FAT_New(Fun.getFunctionType()->param_begin(),
                                        Fun.getFunctionType()->param_end());
            FunctionType *FT_New = FunctionType::get(
                    Type::getVoidTy(Fun.getContext()),
                    FAT_New, Fun.isVarArg());
            Function *Fun_New = Function::Create(FT_New,
                                                 Fun.getLinkage(),
                                                 Fun.getName(),
                                                 Fun.getParent());

            // Copy the attributes from the old function and delete the ones
            // related to the return value
            Fun_New->copyAttributesFrom(&Fun);
            for (Attribute::AttrKind AK : badAttributes) {
                Fun_New->removeAttribute(AttributeList::ReturnIndex, AK);
                Fun_New->removeAttribute(AttributeList::FunctionIndex, AK);
            }
            Fun_New->takeName(&Fun);

            // Set the names of all arguments of the new function
            for (Function::arg_iterator AI = Fun.arg_begin(),
                         AE = Fun.arg_end(), NAI = Fun_New->arg_begin();
                 AI != AE;
                 ++AI, ++NAI) {
                NAI->takeName(AI);
            }

            // Copy the function body (currently not used, because function with
            // a body are ignored)
            Fun_New->getBasicBlockList().splice(Fun_New->begin(),
                                                Fun.getBasicBlockList());

            // Replace return instructions on ends of basic blocks with ret void
            // (currently not used because function with a body are ignored)
            for (BasicBlock &B : *Fun_New)
                if (dyn_cast<ReturnInst>(B.getTerminator())) {
                    B.getInstList().pop_back();
                    ReturnInst *Term_New = ReturnInst::Create(
                            B.getContext());
                    B.getInstList().push_back(Term_New);
                }

            // Replace all uses of the old arguments
            for (Function::arg_iterator I = Fun.arg_begin(),
                         E = Fun.arg_end(), NI = Fun_New->arg_begin();
                 I != E;
                 ++I, ++NI) {
                I->replaceAllUsesWith(NI);
            }

            // For call or invoke instructions a new instruction has to be
            // created and the old one replaced
            for (Use &U : Fun.uses()) {
                if (CallInst *CI = dyn_cast<CallInst>(U.getUser())) {
                    // First copy all arguments to an array
                    // and create the new instruction
                    std::vector<Value *> Args;

                    for (Value *A : CI->arg_operands()) {
                        Args.push_back(A);
                    }

                    ArrayRef<Value *> Args_AR(Args);

                    // Insert the new instruction next to the old one
                    CallInst *CI_New = CallInst::Create(Fun_New, Args_AR, "",
                                                        CI);

                    // Copy additional properties
                    CI_New->setAttributes(CI->getAttributes());
                    for (Attribute::AttrKind AK : badAttributes) {
                        // Remove incompatibile attributes
                        CI_New->removeAttribute(
                                AttributeList::ReturnIndex, AK);
                        CI_New->removeAttribute(
                                AttributeList::FunctionIndex, AK);
                    }
                    CI_New->setCallingConv(CI->getCallingConv());
                    if (CI->isTailCall())
                        CI_New->setTailCall();
#ifdef DEBUG
                    errs() << "Replacing :" << *CI << " with " << *CI_New;
                    errs() << "\n";
#endif
                    // Erase the old instruction
                    CI->eraseFromParent();
                } else if (InvokeInst *II = dyn_cast<InvokeInst>(U.getUser())) {
                    // First copy all arguments to an array and create
                    // the new instruction
                    std::vector<Value *> Args;

                    for (Value *A : II->arg_operands()) {
                        Args.push_back(A);
                    }

                    ArrayRef<Value *> Args_AR(Args);

                    // Insert the new instruction next to the old one
                    InvokeInst *II_New = InvokeInst::Create(
                            Fun_New,
                            II->getNormalDest(),
                            II->getUnwindDest(),
                            Args_AR, "", II);

                    // Copy additional properties
                    II_New->setAttributes(II->getAttributes());
                    for (Attribute::AttrKind AK : badAttributes) {
                        // Remove incompatibile attributes
                        II_New->removeAttribute(
                                AttributeList::ReturnIndex, AK);
                        II_New->removeAttribute(
                                AttributeList::FunctionIndex, AK);
                    }
                    II_New->setCallingConv(II->getCallingConv());
#ifdef DEBUG
                    errs() << "Replacing :" << *II << " with " << *II_New;
                    errs() << "\n";
#endif
                    // Erase the old instruction
                    II->eraseFromParent();
                }
            }
#ifdef DEBUG
            Fun_New->print(errs());
            errs() << "\n";
#endif
            // Delete function after iteration
            functionsToDelete.push_back(&Fun);
        }
    }

    // Delete replaced functions
    for (Function *F : functionsToDelete)
        F->removeFromParent();

    return PreservedAnalyses();
}