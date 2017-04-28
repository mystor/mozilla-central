/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GetterAddrefsChecker.h"
#include "CustomMatchers.h"

void GetterAddrefsChecker::registerMatchers(MatchFinder* AstMatcher) {
  AstMatcher->addMatcher(callExpr().bind("call"), this);
}

void GetterAddrefsChecker::check(
    const MatchFinder::MatchResult &Result) {
  auto Error = "This outparameter of type %0 must either be passed getter_AddRefs or a forwarded parameter.";
  auto Note = "The result will be AddRefed by the getter. This call may leak.";

  auto Call = Result.Nodes.getNodeAs<CallExpr>("call");
  auto Callee = Call->getDirectCallee();

  // We don't want to report errors which would be detected by calling templated
  // functions, as they probably don't know that their outparameter is an XPCOM
  // outparameter.
  if (Callee && Callee->getTemplatedKind() != FunctionDecl::TK_NonTemplate) {
    return;
  }

  auto NumArgs = Call->getNumArgs();
  auto NumParams = Callee ? Callee->getNumParams() : 0;
  for (size_t Idx = 0; Idx < NumArgs; ++Idx) {
    // Check if the parameter is marked as non-outparam.
    auto Parm = Idx < NumParams ? Callee->getParamDecl(Idx) : nullptr;
    if (Parm && (hasCustomAnnotation(Parm, "moz_non_outparam") ||
                 hasCustomAnnotation(Parm, "moz_does_not_addref"))) {
      continue;
    }
    // If the parameter is being passed by reference, then it isn't an outparam.
    if (Parm && Parm->getType()->isReferenceType()) {
      continue;
    }

    auto Arg = Call->getArg(Idx);

    // We need at least a T**
    QualType ArgTy = Arg->getType();
    if (ArgTy.isNull() || !ArgTy->isPointerType() || ArgTy.isConstQualified()) {
      continue;
    }
    // T*
    QualType Pointee = ArgTy->getPointeeType();
    if (Pointee.isNull() || !Pointee->isPointerType() || Pointee.isConstQualified()) {
      continue;
    }
    // T
    QualType Target = Pointee->getPointeeType();
    if (Target.isNull() || !Target->getAsCXXRecordDecl() ||
        !isClassRefCounted(Target->getAsCXXRecordDecl())) {
      continue;
    }

    // Ignore Explicit Casts in addition to trivials.
    while (true) {
      auto *NewArg = IgnoreTrivials(Arg);
      if (NewArg != Arg) {
        Arg = NewArg;
      } else  if (auto *Ece = dyn_cast_or_null<ExplicitCastExpr>(Arg)) {
        Arg = Ece->getSubExpr();
      } else {
        break;
      }
    }

    // If we're looking at an operator T** on a getter_AddRefs type or an
    // outparam forwarded from our own call, we're OK - otherwise we should
    // report an error.
    if (auto MemberCall = dyn_cast<CXXMemberCallExpr>(Arg)) {
      auto D = MemberCall->getRecordDecl();
      if (D && hasCustomAnnotation(D, "moz_getter_addrefs_type")) {
        continue;
      }
    } else if (auto DeclRef = dyn_cast<DeclRefExpr>(Arg)) {
      auto D = DeclRef->getDecl();
      if (D && isa<ParmVarDecl>(D)) {
        continue;
      }
    } else if (isa<CXXNullPtrLiteralExpr>(Arg)) {
      continue;
    } else if (isa<CXXDefaultArgExpr>(Arg)) {
      continue;
    }

    // Normally we want to reject &someVariable, but we make an exeception for
    // static variables, as they have different lifetime semantics. In addition,
    // especially in older code, there is a pattern of using static pointers
    // which are manually refcounted, and changing all of them to use
    // StaticRefPtr would be tedious.
    auto AddrOf = dyn_cast<UnaryOperator>(Arg);
    if (AddrOf && AddrOf->getOpcode() == UO_AddrOf) {
      auto Target = dyn_cast<DeclRefExpr>(IgnoreTrivials(AddrOf->getSubExpr()));
      auto Decl = Target
        ? dyn_cast_or_null<VarDecl>(Target->getDecl())
        : nullptr;
      if (Decl && Decl->getStorageDuration() == SD_Static) {
        continue;
      }
    }

    diag(Arg->getLocStart(), Error, DiagnosticIDs::Error) << Arg->getType();
    diag(Arg->getLocStart(), Note, DiagnosticIDs::Note);
  }
}
