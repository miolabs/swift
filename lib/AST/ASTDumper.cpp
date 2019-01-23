//===--- ASTDumper.cpp - Swift Language AST Dumper ------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file implements dumping for the Swift ASTs.
//
//===----------------------------------------------------------------------===//

#include <string>
#include <regex>
#include <unordered_map>
#include <map>
#include <iostream>
#include <vector>
#include "swift/AST/ASTContext.h"
#include "swift/AST/ASTPrinter.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/ForeignErrorConvention.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/TypeVisitor.h"
#include "swift/Basic/Defer.h"
#include "swift/Basic/QuotedString.h"
#include "swift/Basic/STLExtras.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;

//the `^` is for unwrapping inout expressions; not sure how feasible in the long run
const std::unordered_map<std::string, std::string> REPLACEMENTS = {
  {"Swift.(file).String.count", "#L.length"},
  {"Swift.(file).print(_:separator:terminator:)", "console.log(#AA)"},
  {"Swift.(file).String.+=", "#PRENOL(^#A0 + #A1)#ISASS"},
  {"Swift.(file).Int.==", "#PRENOL(#A0 == #A1)"},
  {"Swift.(file).Int.!=", "#PRENOL(#A0 != #A1)"},
  {"Swift.(file).BinaryInteger.!=", "#PRENOL(#A0 != #A1)"},
  {"Swift.(file).Int.>", "#PRENOL(#A0 > #A1)"},
  {"Swift.(file).Int.<", "#PRENOL(#A0 < #A1)"},
  {"Swift.(file).Int.>=", "#PRENOL(#A0 >= #A1)"},
  {"Swift.(file).Int.<=", "#PRENOL(#A0 <= #A1)"},
  {"Swift.(file).String.==", "#PRENOL(#A0 == #A1)"},
  {"Swift.(file).String.!=", "#PRENOL(#A0 != #A1)"},
  {"Swift.(file).String.>", "#PRENOL(#A0 > #A1)"},
  {"Swift.(file).String.<", "#PRENOL(#A0 < #A1)"},
  {"Swift.(file).String.>=", "#PRENOL(#A0 >= #A1)"},
  {"Swift.(file).String.<=", "#PRENOL(#A0 <= #A1)"},
  {"Swift.(file).Character.==", "#PRENOL(#A0 == #A1)"},
  {"Swift.(file).Character.!=", "#PRENOL(#A0 != #A1)"},
  {"Swift.(file).Character.>", "#PRENOL(#A0 > #A1)"},
  {"Swift.(file).Character.<", "#PRENOL(#A0 < #A1)"},
  {"Swift.(file).Character.>=", "#PRENOL(#A0 >= #A1)"},
  {"Swift.(file).Character.<=", "#PRENOL(#A0 <= #A1)"},
  {"Swift.(file).FloatingPoint.==", "#PRENOL(#A0 == #A1)"},
  {"Swift.(file).Equatable.!=", "#PRENOL(#A0 != #A1)"},
  {"Swift.(file).FloatingPoint.>", "#PRENOL(#A0 > #A1)"},
  {"Swift.(file).FloatingPoint.<", "#PRENOL(#A0 < #A1)"},
  {"Swift.(file).FloatingPoint.>=", "#PRENOL(#A0 >= #A1)"},
  {"Swift.(file).FloatingPoint.<=", "#PRENOL(#A0 <= #A1)"},
  {"Swift.(file).Double.+", "#PRENOL(#A0 + #A1)"},
  {"Swift.(file).Double.-", "#PRENOL(#A0 - #A1)"},
  {"Swift.(file).Double.*", "#PRENOL(#A0 * #A1)"},
  {"Swift.(file).Double./", "#PRENOL(#A0 / #A1)"},
  {"Swift.(file).Int.+", "#PRENOL(#A0 + #A1)"},
  {"Swift.(file).Int.-", "#PRENOL(#A0 - #A1)"},
  {"Swift.(file).Int.*", "#PRENOL(#A0 * #A1)"},
  {"Swift.(file).Int./", "#PRENOL((#A0 / #A1) | 0)"},
  {"Swift.(file).Int.%", "#PRENOL(#A0 % #A1)"},
  {"Swift.(file).Int.+=", "#PRENOL(^#A0 + #A1)#ISASS"},
  {"Swift.(file).Int.-=", "#PRENOL(^#A0 - #A1)#ISASS"},
  {"Swift.(file).Int.*=", "#PRENOL(^#A0 * #A1)#ISASS"},
  {"Swift.(file).Int./=", "#PRENOL((^#A0 / #A1) | 0)#ISASS"},
  {"Swift.(file).Int.%=", "#PRENOL(^#A0 % #A1)#ISASS"},
  {"Swift.(file).String.+", "#PRENOL(#A0 + #A1)"},
  {"Swift.(file).SignedNumeric.-", "(-(#AA))#NOL"},
  {"Swift.(file).Dictionary.subscript(_:)", "#L.get(#AA)"},
  {"Swift.(file).Dictionary.subscript(_:)#ASS", "#L.setConditional(#AA, #ASS)"},
  {"Swift.(file).Dictionary.count", "#L.size"},
  {"Swift.(file).Array.subscript(_:)", "#L[#AA]"},
  {"Swift.(file).Array.subscript(_:)#ASS", "#L.setConditional(#AA, #ASS)"},
  {"Swift.(file).Array.count", "#L.length"},
  {"Swift.(file).Array.+", "#PRENOL#A0.concat(#A1)"},
  {"Swift.(file).Array.+=", "#PRENOL^#A0.pushMany(#A1)"},
  {"Swift.(file).Array.append", "#L.push(#AA)"},
  {"Swift.(file).Array.append(contentsOf:)", "#L.pushMany(#AA)"},
  {"Swift.(file).Array.insert(_:at:)", "#L.splice(#A1, 0, #A0)"},
  {"Swift.(file).RangeReplaceableCollection.insert(contentsOf:at:)", "#L.pushManyAt(#AA)"},
  {"Swift.(file).Array.remove(at:)", "#L.splice(#AA, 1)"},
  {"Swift.(file).BidirectionalCollection.joined(separator:)", "#L.join(#AA)"},
  {"Swift.(file).Array.init()", "new Array()"},
  {"Swift.(file).Array.init(repeating:count:)", "new Array(#A1).fill(#A0)"},
  {"Swift.(file).ArrayProtocol.filter", "#L.filter(#AA)"},
  {"Swift.(file).Collection.makeIterator()", "#L.makeIterator(#AA)"},
  {"Swift.(file).Dictionary.makeIterator()", "#L.makeIterator(#AA)"},
  {"Swift.(file).Sequence.reduce", "#L.reduce(#A1, #A0)"},
  {"Swift.(file).MutableCollection.sort(by:)", "#L.sortBool(#AA)"},
  {"Swift.(file).Collection.map", "#L.map(#AA)"},
  {"Swift.(file).Set.insert", "#L.add(#AA)"},
  {"Swift.(file).Set.count", "#L.size"},
  {"Swift.(file).Set.init()", "new Set()"},
  {"Swift.(file).Set.init(_:)", "new Set(#AA)"},
  {"Swift.(file).Comparable....", "#PRENOLnew ClosedRange(#A0, #A1)"},
  {"Swift.(file).Comparable...<", "#PRENOLnew Range(#A0, #A1)"},
  {"Swift.(file).Optional.none", "null#NOL"},
  {"Swift.(file).??", "_.nilCoalescing(#A0, #A1)"},
  {"Swift.(file).Bool.!", "(!(#AA))#NOL"},
  {"Swift.(file).Bool.||", "#PRENOL(#A0 || #A1)"},
  {"Swift.(file).Bool.&&", "#PRENOL(#A0 && #A1)"},
  {"Swift.(file).Bool.==", "#PRENOL(#A0 == #A1)"},
  {"Swift.(file).Bool.!=", "#PRENOL(#A0 != #A1)"},
  {"Swift.(file).Double.init(_:)", "parseFloat(#AA)"},
  {"Swift.(file).~=", "#PRENOL(#A0 == #A1)"},
  {"Swift.(file).RangeExpression.~=", "#PRENOL(#A0).includes(#A1)"},
  {"Swift.(file)._findStringSwitchCase(cases:string:)", "#A0.indexOf(#A1)"}
};
const std::unordered_map<std::string, bool> REPLACEMENTS_CLONE_STRUCT = {
  {"Swift.(file).Int", false},
  {"Swift.(file).String", false},
  {"Swift.(file).Double", false},
  {"Swift.(file).Bool", false}
};
const std::unordered_map<std::string, std::string> REPLACEMENTS_TYPE = {
  {"Swift.(file).Int", "number"},
  {"Swift.(file).Double", "number"},
  {"Swift.(file).String", "string"},
  {"Swift.(file).Bool", "boolean"},
  {"Swift.(file).Dictionary", "Map"}
};

Expr *lAssignmentExpr;
Expr *functionArgsCall;

std::vector<std::string> optionalCondition = {};

std::unordered_map<std::string, std::string> functionUniqueNames = {};
std::unordered_map<std::string, int> functionOverloadedCounts = {};

std::unordered_map<std::string, std::string> nameReplacements = {};

std::string regex_escape(std::string replacement) {
  return std::regex_replace(replacement, std::regex("\\$"), "$$$$");
}

std::string matchNameReplacement(std::vector<unsigned> indexes) {
  std::string nameReplacement = "$match";
  for(auto index : indexes) nameReplacement += "[" + std::to_string(index) + "]";
  return nameReplacement;
}

std::string dumpToStr(Expr *E) {
  std::string str;
  llvm::raw_string_ostream stream(str);
  E->dump(stream);
  return stream.str();
}
std::string getMemberIdentifier(ValueDecl *D) {
  std::string str;
  llvm::raw_string_ostream stream(str);
  D->dumpRef(stream);
  //std::cout << "/*" << stream.str() << "*/";
  return stream.str();
}
std::string getReplacement(ValueDecl *D, ConcreteDeclRef DR = nullptr, bool isAss = false) {
  std::string memberIdentifier = getMemberIdentifier(D);
  if(memberIdentifier == "Swift.(file).Equatable.!=" && DR && DR.isSpecialized()) {
    if(auto elo = DR.getSubstitutions().getReplacementTypes()[0]->getEnumOrBoundGenericEnum()) {
      return "#PRENOL(#A0.rawValue != #A1.rawValue)";
    }
  }
  if(!strncmp(D->getBaseName().userFacingName().data(), "__derived_enum_equals", 100)) {
    return "#PRENOL(#A0.rawValue == #A1.rawValue)";
  }
  if(isAss && REPLACEMENTS.count(memberIdentifier + "#ASS")) {
    return REPLACEMENTS.at(memberIdentifier + "#ASS");
  }
  if(REPLACEMENTS.count(memberIdentifier)) {
    return REPLACEMENTS.at(memberIdentifier);
  }
  return "";
}

Expr *skipWrapperExpressions(Expr *E) {
  while (true) {
    if(auto *tupleShuffleExpr = dyn_cast<TupleShuffleExpr>(E)) {
      E = tupleShuffleExpr->getSubExpr();
    }
    else if(auto *openExistentialExpr = dyn_cast<OpenExistentialExpr>(E)) {
      E = openExistentialExpr->getSubExpr();
    }
    else break;
  }
  return E;
}

std::string handleLAssignment(Expr *lExpr, std::string rExpr) {
  lAssignmentExpr = skipWrapperExpressions(lExpr);
  std::string setStr = dumpToStr(lExpr);
  if(setStr.find("#ASS") == std::string::npos) {
    setStr += " = #ASS";
  }
  return std::regex_replace(setStr, std::regex("#ASS"), regex_escape(rExpr));
}
std::string handleRAssignment(Expr *rExpr, std::string baseStr) {
  bool cloneStruct = false;
  if (rExpr->getType()->isExistentialType()) {
    cloneStruct = true;
  }
  else if (auto *structDecl = rExpr->getType()->getStructOrBoundGenericStruct()) {
    bool isInitializer = false;
    if (auto *callExpr = dyn_cast<CallExpr>(rExpr)) {
      if (auto *constructorRefCallExpr = dyn_cast<ConstructorRefCallExpr>(callExpr->getFn())) {
        isInitializer = true;
      }
    }
    else if (auto *dictionaryExpr = dyn_cast<DictionaryExpr>(rExpr)) {
      isInitializer = true;
    }
    else if (auto *arrayExpr = dyn_cast<ArrayExpr>(rExpr)) {
      isInitializer = true;
    }
    cloneStruct = !isInitializer && !REPLACEMENTS_CLONE_STRUCT.count(getMemberIdentifier(structDecl));
  }
  if(cloneStruct) {
    baseStr = "_.cloneStruct(" + baseStr + ")";
  }
  return baseStr;
}

std::string getFunctionName(ValueDecl *D) {
  std::string uniqueIdentifier = getMemberIdentifier(D);
  std::string userFacingName = D->getBaseName().userFacingName();
  //TODO won't work when overriding native functions
  //if(uniqueIdentifier.find("Swift.(file).") == 0) return userFacingName;
  if(!functionUniqueNames.count(uniqueIdentifier)) {
    if(D->isOperator()) {
      std::string stringifiedOp = "OP";
      for(unsigned long i = 0; i < userFacingName.size(); i++) {
        stringifiedOp += "_" + std::to_string(int(userFacingName[i]));
      }
      userFacingName = stringifiedOp;
    }
    functionUniqueNames[uniqueIdentifier] = userFacingName;

    std::string overloadIdentifier;
    //TODO can't just use current context; need to use context of base super class [e.g. initializers.swift]
    //DeclContext: Decl getAsDecl() bool isTypeContext() ClassDecl *getSelfClassDecl()
    /*llvm::raw_string_ostream stream(overloadIdentifier);
    printContext(stream, D->getDeclContext());
    stream.flush();
    overloadIdentifier += ".";*/
    overloadIdentifier += userFacingName;
    if(functionOverloadedCounts.count(overloadIdentifier)) {
      functionOverloadedCounts[overloadIdentifier] += 1;
      functionUniqueNames[uniqueIdentifier] += std::to_string(functionOverloadedCounts[overloadIdentifier]);
    }
    else {
      functionOverloadedCounts[overloadIdentifier] = 0;
    }
  }
  return functionUniqueNames[uniqueIdentifier];
}
std::string getName(ValueDecl *D, unsigned long satisfiedProtocolRequirementI = 0) {
  while (auto *overriden = D->getOverriddenDecl()) {
    D = overriden;
  }
  auto satisfiedProtocolRequirements = D->getSatisfiedProtocolRequirements();
  if(satisfiedProtocolRequirementI > 0 && satisfiedProtocolRequirementI >= satisfiedProtocolRequirements.size()) {
    return "!NO_DUPLICATE";
  }
  if(!satisfiedProtocolRequirements.empty()) {
    D = D->getSatisfiedProtocolRequirements()[satisfiedProtocolRequirementI];
  }
  
  std::string name;
  
  if(auto *functionDecl = dyn_cast<AbstractFunctionDecl>(D)) {
    name = getFunctionName(functionDecl);
  }
  else if(auto *subscriptDecl = dyn_cast<SubscriptDecl>(D)) {
    name = getFunctionName(subscriptDecl);
  }
  else {
    name = D->getBaseName().userFacingName();
  }
  
  if(nameReplacements.count(name)) {
    return nameReplacements[name];
  }
  
  return name;
}

std::string getType(Type T) {
  if(auto *metatypeType = dyn_cast<AnyMetatypeType>(T.getPointer())) {
    //that's to display e.g. `Double` instead of `Double.Type` when referring to the class itself
    T = metatypeType->getInstanceType();
  }

  if(auto *nominalDecl = T->getNominalOrBoundGenericNominal()) {
    std::string memberIdentifier = getMemberIdentifier(nominalDecl);
    if(REPLACEMENTS_TYPE.count(memberIdentifier)) {
      return REPLACEMENTS_TYPE.at(memberIdentifier);
    }
  }
  
  std::string str;
  llvm::raw_string_ostream stream(str);
  T->print(stream);
  return stream.str();
}

Expr *skipInOutExpr(Expr *E) {
  if (auto *inOutExpr = dyn_cast<InOutExpr>(E)) {
    return inOutExpr->getSubExpr();
  }
  return E;
}

unsigned tempValI = 0;
struct TempValInfo { std::string name; std::string expr; };
TempValInfo getTempVal(std::string init = "") {
  std::string name = "_.tmp" + std::to_string(tempValI);
  std::string expr = "(" + name + " = " + init + ")";
  tempValI++;
  return TempValInfo{name, expr};
}

std::unordered_map<OpaqueValueExpr*, Expr*> opaqueValueReplacements;

struct TerminalColor {
  llvm::raw_ostream::Colors Color;
  bool Bold;
};

#define DEF_COLOR(NAME, COLOR, BOLD) \
static const TerminalColor NAME##Color = { llvm::raw_ostream::COLOR, BOLD };

DEF_COLOR(Func, YELLOW, false)
DEF_COLOR(Range, YELLOW, false)
DEF_COLOR(AccessLevel, YELLOW, false)
DEF_COLOR(ASTNode, YELLOW, true)
DEF_COLOR(Parameter, YELLOW, false)
DEF_COLOR(Extension, MAGENTA, false)
DEF_COLOR(Pattern, RED, true)
DEF_COLOR(Override, RED, false)
DEF_COLOR(Stmt, RED, true)
DEF_COLOR(Captures, RED, false)
DEF_COLOR(Arguments, RED, false)
DEF_COLOR(TypeRepr, GREEN, false)
DEF_COLOR(LiteralValue, GREEN, false)
DEF_COLOR(Decl, GREEN, true)
DEF_COLOR(Parenthesis, BLUE, false)
DEF_COLOR(Type, BLUE, false)
DEF_COLOR(Discriminator, BLUE, false)
DEF_COLOR(InterfaceType, GREEN, false)
DEF_COLOR(Identifier, GREEN, false)
DEF_COLOR(Expr, MAGENTA, true)
DEF_COLOR(ExprModifier, CYAN, false)
DEF_COLOR(DeclModifier, CYAN, false)
DEF_COLOR(ClosureModifier, CYAN, false)
DEF_COLOR(TypeField, CYAN, false)
DEF_COLOR(Location, CYAN, false)

#undef DEF_COLOR

namespace {
  /// RAII object that prints with the given color, if color is supported on the
  /// given stream.
  class PrintWithColorRAII {
    raw_ostream &OS;
    bool ShowColors;

  public:
    PrintWithColorRAII(raw_ostream &os, TerminalColor color)
    : OS(os), ShowColors(false)
    {
      ShowColors = os.has_colors();

      if (ShowColors)
        OS.changeColor(color.Color, color.Bold);
    }

    ~PrintWithColorRAII() {
      if (ShowColors) {
        OS.resetColor();
      }
    }

    raw_ostream &getOS() const { return OS; }

    template<typename T>
    friend raw_ostream &operator<<(PrintWithColorRAII &&printer,
                                   const T &value){
      printer.OS << value;
      return printer.OS;
    }

  };
} // end anonymous namespace

//===----------------------------------------------------------------------===//
//  Generic param list printing.
//===----------------------------------------------------------------------===//

void RequirementRepr::dump() const {
  print(llvm::errs());
  llvm::errs() << "\n";
}

void RequirementRepr::printImpl(ASTPrinter &out, bool AsWritten) const {
  auto printTy = [&](const TypeLoc &TyLoc) {
    if (AsWritten && TyLoc.getTypeRepr()) {
      TyLoc.getTypeRepr()->print(out, PrintOptions());
    } else {
      TyLoc.getType().print(out, PrintOptions());
    }
  };

  auto printLayoutConstraint =
      [&](const LayoutConstraintLoc &LayoutConstraintLoc) {
        LayoutConstraintLoc.getLayoutConstraint()->print(out, PrintOptions());
      };

  switch (getKind()) {
  case RequirementReprKind::LayoutConstraint:
    printTy(getSubjectLoc());
    out << " : ";
    printLayoutConstraint(getLayoutConstraintLoc());
    break;

  case RequirementReprKind::TypeConstraint:
    printTy(getSubjectLoc());
    out << " : ";
    printTy(getConstraintLoc());
    break;

  case RequirementReprKind::SameType:
    printTy(getFirstTypeLoc());
    out << " == ";
    printTy(getSecondTypeLoc());
    break;
  }
}

void RequirementRepr::print(raw_ostream &out) const {
  StreamPrinter printer(out);
  printImpl(printer, /*AsWritten=*/true);
}
void RequirementRepr::print(ASTPrinter &out) const {
  printImpl(out, /*AsWritten=*/true);
}

void GenericParamList::print(llvm::raw_ostream &OS) {
  OS << '<';
  interleave(*this,
             [&](const GenericTypeParamDecl *P) {
               OS << P->getName();
               if (!P->getInherited().empty()) {
                 OS << " : ";
                 P->getInherited()[0].getType().print(OS);
               }
             },
             [&] { OS << ", "; });

  if (!getRequirements().empty()) {
    OS << " where ";
    interleave(getRequirements(),
               [&](const RequirementRepr &req) {
                 req.print(OS);
               },
               [&] { OS << ", "; });
  }
  OS << '>';
}

void GenericParamList::dump() {
  print(llvm::errs());
  llvm::errs() << '\n';
}

static void printGenericParameters(raw_ostream &OS, GenericParamList *Params) {
  if (!Params)
    return;
  OS << ' ';
  Params->print(OS);
}


static StringRef
getSILFunctionTypeRepresentationString(SILFunctionType::Representation value) {
  switch (value) {
  case SILFunctionType::Representation::Thick: return "thick";
  case SILFunctionType::Representation::Block: return "block";
  case SILFunctionType::Representation::CFunctionPointer: return "c";
  case SILFunctionType::Representation::Thin: return "thin";
  case SILFunctionType::Representation::Method: return "method";
  case SILFunctionType::Representation::ObjCMethod: return "objc_method";
  case SILFunctionType::Representation::WitnessMethod: return "witness_method";
  case SILFunctionType::Representation::Closure: return "closure";
  }

  llvm_unreachable("Unhandled SILFunctionTypeRepresentation in switch.");
}

StringRef swift::getReadImplKindName(ReadImplKind kind) {
  switch (kind) {
  case ReadImplKind::Stored:
    return "stored";
  case ReadImplKind::Inherited:
    return "inherited";
  case ReadImplKind::Get:
    return "getter";
  case ReadImplKind::Address:
    return "addressor";
  case ReadImplKind::Read:
    return "read_coroutine";
  }
  llvm_unreachable("bad kind");
}

StringRef swift::getWriteImplKindName(WriteImplKind kind) {
  switch (kind) {
  case WriteImplKind::Immutable:
    return "immutable";
  case WriteImplKind::Stored:
    return "stored";
  case WriteImplKind::StoredWithObservers:
    return "stored_with_observers";
  case WriteImplKind::InheritedWithObservers:
    return "inherited_with_observers";
  case WriteImplKind::Set:
    return "setter";
  case WriteImplKind::MutableAddress:
    return "mutable_addressor";
  case WriteImplKind::Modify:
    return "modify_coroutine";
  }
  llvm_unreachable("bad kind");
}

StringRef swift::getReadWriteImplKindName(ReadWriteImplKind kind) {
  switch (kind) {
  case ReadWriteImplKind::Immutable:
    return "immutable";
  case ReadWriteImplKind::Stored:
    return "stored";
  case ReadWriteImplKind::MutableAddress:
    return "mutable_addressor";
  case ReadWriteImplKind::MaterializeToTemporary:
    return "materialize_to_temporary";
  case ReadWriteImplKind::Modify:
    return "modify_coroutine";
  }
  llvm_unreachable("bad kind");
}

static StringRef getImportKindString(ImportKind value) {
  switch (value) {
  case ImportKind::Module: return "module";
  case ImportKind::Type: return "type";
  case ImportKind::Struct: return "struct";
  case ImportKind::Class: return "class";
  case ImportKind::Enum: return "enum";
  case ImportKind::Protocol: return "protocol";
  case ImportKind::Var: return "var";
  case ImportKind::Func: return "func";
  }
  
  llvm_unreachable("Unhandled ImportKind in switch.");
}

static StringRef
getForeignErrorConventionKindString(ForeignErrorConvention::Kind value) {
  switch (value) {
  case ForeignErrorConvention::ZeroResult: return "ZeroResult";
  case ForeignErrorConvention::NonZeroResult: return "NonZeroResult";
  case ForeignErrorConvention::ZeroPreservedResult: return "ZeroPreservedResult";
  case ForeignErrorConvention::NilResult: return "NilResult";
  case ForeignErrorConvention::NonNilError: return "NonNilError";
  }

  llvm_unreachable("Unhandled ForeignErrorConvention in switch.");
}
static StringRef getDefaultArgumentKindString(DefaultArgumentKind value) {
  switch (value) {
    case DefaultArgumentKind::None: return "none";
    case DefaultArgumentKind::Column: return "#column";
    case DefaultArgumentKind::DSOHandle: return "#dsohandle";
    case DefaultArgumentKind::File: return "#file";
    case DefaultArgumentKind::Function: return "#function";
    case DefaultArgumentKind::Inherited: return "inherited";
    case DefaultArgumentKind::Line: return "#line";
    case DefaultArgumentKind::NilLiteral: return "nil";
    case DefaultArgumentKind::EmptyArray: return "[]";
    case DefaultArgumentKind::EmptyDictionary: return "[:]";
    case DefaultArgumentKind::Normal: return "normal";
  }

  llvm_unreachable("Unhandled DefaultArgumentKind in switch.");
}
static StringRef getAccessorKindString(AccessorKind value) {
  switch (value) {
#define ACCESSOR(ID)
#define SINGLETON_ACCESSOR(ID, KEYWORD) \
  case AccessorKind::ID: return #KEYWORD;
#include "swift/AST/AccessorKinds.def"
  }

  llvm_unreachable("Unhandled AccessorKind in switch.");
}
static StringRef
getMagicIdentifierLiteralExprKindString(MagicIdentifierLiteralExpr::Kind value) {
  switch (value) {
    case MagicIdentifierLiteralExpr::File: return "#file";
    case MagicIdentifierLiteralExpr::Function: return "#function";
    case MagicIdentifierLiteralExpr::Line: return "#line";
    case MagicIdentifierLiteralExpr::Column: return "#column";
    case MagicIdentifierLiteralExpr::DSOHandle: return "#dsohandle";
  }

  llvm_unreachable("Unhandled MagicIdentifierLiteralExpr in switch.");
}
static StringRef
getObjCSelectorExprKindString(ObjCSelectorExpr::ObjCSelectorKind value) {
  switch (value) {
    case ObjCSelectorExpr::Method: return "method";
    case ObjCSelectorExpr::Getter: return "getter";
    case ObjCSelectorExpr::Setter: return "setter";
  }

  llvm_unreachable("Unhandled ObjCSelectorExpr in switch.");
}
static StringRef getAccessSemanticsString(AccessSemantics value) {
  switch (value) {
    case AccessSemantics::Ordinary: return "ordinary";
    case AccessSemantics::DirectToStorage: return "direct_to_storage";
    case AccessSemantics::DirectToImplementation: return "direct_to_impl";
  }

  llvm_unreachable("Unhandled AccessSemantics in switch.");
}
static StringRef getMetatypeRepresentationString(MetatypeRepresentation value) {
  switch (value) {
    case MetatypeRepresentation::Thin: return "thin";
    case MetatypeRepresentation::Thick: return "thick";
    case MetatypeRepresentation::ObjC: return "@objc";
  }

  llvm_unreachable("Unhandled MetatypeRepresentation in switch.");
}
static StringRef
getStringLiteralExprEncodingString(StringLiteralExpr::Encoding value) {
  switch (value) {
    case StringLiteralExpr::UTF8: return "utf8";
    case StringLiteralExpr::UTF16: return "utf16";
    case StringLiteralExpr::OneUnicodeScalar: return "unicodeScalar";
  }

  llvm_unreachable("Unhandled StringLiteral in switch.");
}
static StringRef getCtorInitializerKindString(CtorInitializerKind value) {
  switch (value) {
    case CtorInitializerKind::Designated: return "designated";
    case CtorInitializerKind::Convenience: return "convenience";
    case CtorInitializerKind::ConvenienceFactory: return "convenience_factory";
    case CtorInitializerKind::Factory: return "factory";
  }

  llvm_unreachable("Unhandled CtorInitializerKind in switch.");
}
static StringRef getOptionalTypeKindString(OptionalTypeKind value) {
  switch (value) {
    case OTK_None: return "none";
    case OTK_Optional: return "Optional";
    case OTK_ImplicitlyUnwrappedOptional: return "ImplicitlyUnwrappedOptional";
  }

  llvm_unreachable("Unhandled OptionalTypeKind in switch.");
}
static StringRef getAssociativityString(Associativity value) {
  switch (value) {
    case Associativity::None: return "none";
    case Associativity::Left: return "left";
    case Associativity::Right: return "right";
  }

  llvm_unreachable("Unhandled Associativity in switch.");
}

//===----------------------------------------------------------------------===//
//  Decl printing.
//===----------------------------------------------------------------------===//

// Print a name.
static void printName(raw_ostream &os, DeclName name) {
  if (!name)
    os << "<anonymous>";
  else
    os << name;
}

namespace {
  class PrintPattern : public PatternVisitor<PrintPattern> {
  public:
    raw_ostream &OS;
    unsigned Indent;

    explicit PrintPattern(raw_ostream &os, unsigned indent = 0)
      : OS(os), Indent(indent) { }

    void printRec(Decl *D) { D->dump(OS, Indent + 2); }
    void printRec(Expr *E) { E->dump(OS, Indent + 2); }
    void printRec(Stmt *S, const ASTContext &Ctx) { S->dump(OS, &Ctx, Indent + 2); }
    void printRec(TypeRepr *T);
    void printRec(const Pattern *P) {
      PrintPattern(OS, Indent+2).visit(const_cast<Pattern *>(P));
    }

    raw_ostream &printCommon(Pattern *P, const char *Name) {
      OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      PrintWithColorRAII(OS, PatternColor) << Name;

      if (P->isImplicit())
        PrintWithColorRAII(OS, ExprModifierColor) << " implicit";

      if (P->hasType()) {
        PrintWithColorRAII(OS, TypeColor) << " type='";
        P->getType().print(PrintWithColorRAII(OS, TypeColor).getOS());
        PrintWithColorRAII(OS, TypeColor) << "'";
      }
      return OS;
    }

    void visitParenPattern(ParenPattern *P) {
      printCommon(P, "pattern_paren") << '\n';
      printRec(P->getSubPattern());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitTuplePattern(TuplePattern *P) {
      printCommon(P, "pattern_tuple");

      OS << " names=";
      interleave(P->getElements(),
                 [&](const TuplePatternElt &elt) {
                   auto name = elt.getLabel();
                   OS << (name.empty() ? "''" : name.str());
                 },
                 [&] { OS << ","; });

      for (auto &elt : P->getElements()) {
        OS << '\n';
        printRec(elt.getPattern());
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitNamedPattern(NamedPattern *P) {
      printCommon(P, "pattern_named");
      PrintWithColorRAII(OS, IdentifierColor) << " '" << P->getNameStr() << "'";
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitAnyPattern(AnyPattern *P) {
      /*printCommon(P, "pattern_any");
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }
    void visitTypedPattern(TypedPattern *P) {
      printCommon(P, "pattern_typed") << '\n';
      printRec(P->getSubPattern());
      if (P->getTypeLoc().getTypeRepr()) {
        OS << '\n';
        printRec(P->getTypeLoc().getTypeRepr());
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitIsPattern(IsPattern *P) {
      printCommon(P, "pattern_is")
        << ' ' << getCheckedCastKindName(P->getCastKind()) << ' ';
      P->getCastTypeLoc().getType().print(OS);
      if (auto sub = P->getSubPattern()) {
        OS << '\n';
        printRec(sub);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitExprPattern(ExprPattern *P) {
      /*printCommon(P, "pattern_expr");
      OS << '\n';*/
      if (auto m = P->getMatchExpr())
        printRec(m);
      else
        printRec(P->getSubExpr());
      /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }
    void visitVarPattern(VarPattern *P) {
      printCommon(P, P->isLet() ? "pattern_let" : "pattern_var");
      OS << '\n';
      printRec(P->getSubPattern());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitEnumElementPattern(EnumElementPattern *P) {
      printCommon(P, "pattern_enum_element");
      OS << ' ';
      P->getParentType().getType().print(
        PrintWithColorRAII(OS, TypeColor).getOS());
      PrintWithColorRAII(OS, IdentifierColor) << '.' << P->getName();
      if (P->hasSubPattern()) {
        OS << '\n';
        printRec(P->getSubPattern());
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitOptionalSomePattern(OptionalSomePattern *P) {
      printCommon(P, "pattern_optional_some");
      OS << '\n';
      printRec(P->getSubPattern());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitBoolPattern(BoolPattern *P) {
      printCommon(P, "pattern_bool");
      OS << (P->getValue() ? " true)" : " false)");
    }

  };

  /// PrintDecl - Visitor implementation of Decl::print.
  class PrintDecl : public DeclVisitor<PrintDecl> {
  public:
    raw_ostream &OS;
    unsigned Indent;

    explicit PrintDecl(raw_ostream &os, unsigned indent = 0)
      : OS(os), Indent(indent) { }
    
    void printRec(Decl *D) { PrintDecl(OS, Indent + 2).visit(D); }
    void printRec(Expr *E) { E->dump(OS, Indent+2); }
    void printRec(Stmt *S, const ASTContext &Ctx) { S->dump(OS, &Ctx, Indent+2); }
    void printRec(Stmt *S, const ASTContext &Ctx, raw_ostream &os) { S->dump(os, &Ctx, Indent+2); }
    void printRec(Pattern *P) { PrintPattern(OS, Indent+2).visit(P); }
    void printRec(TypeRepr *T);

    // Print a field with a value.
    template<typename T>
    raw_ostream &printField(StringRef name, const T &value) {
      OS << " ";
      PrintWithColorRAII(OS, TypeFieldColor) << name;
      OS << "=" << value;
      return OS;
    }

    void printCommon(Decl *D, const char *Name,
                     TerminalColor Color = DeclColor) {
      OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      PrintWithColorRAII(OS, Color) << Name;

      if (D->isImplicit())
        PrintWithColorRAII(OS, DeclModifierColor) << " implicit";

      auto R = D->getSourceRange();
      if (R.isValid()) {
        PrintWithColorRAII(OS, RangeColor) << " range=";
        R.print(PrintWithColorRAII(OS, RangeColor).getOS(),
                D->getASTContext().SourceMgr, /*PrintText=*/false);
      }

      if (D->TrailingSemiLoc.isValid())
        PrintWithColorRAII(OS, DeclModifierColor) << " trailing_semi";
    }

    void printInherited(ArrayRef<TypeLoc> Inherited) {
      if (Inherited.empty())
        return;
      OS << " inherits: ";
      interleave(Inherited, [&](TypeLoc Super) { Super.getType().print(OS); },
                 [&] { OS << ", "; });
    }

    void visitImportDecl(ImportDecl *ID) {
      printCommon(ID, "import_decl");

      if (ID->isExported())
        OS << " exported";

      if (ID->getImportKind() != ImportKind::Module)
        OS << " kind=" << getImportKindString(ID->getImportKind());

      OS << " '";
      interleave(ID->getFullAccessPath(),
                 [&](const ImportDecl::AccessPathElement &Elem) {
                   OS << Elem.first;
                 },
                 [&] { OS << '.'; });
      OS << "')";
    }

    void visitExtensionDecl(ExtensionDecl *ED) {
      printCommon(ED, "extension_decl", ExtensionColor);
      OS << ' ';
      ED->getExtendedType().print(OS);
      printInherited(ED->getInherited());
      for (Decl *Member : ED->getMembers()) {
        OS << '\n';
        printRec(Member);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void printDeclName(const ValueDecl *D) {
      if (D->getFullName()) {
        PrintWithColorRAII(OS, IdentifierColor)
          << '\"' << D->getFullName() << '\"';
      } else {
        PrintWithColorRAII(OS, IdentifierColor)
          << "'anonname=" << (const void*)D << '\'';
      }
    }

    void visitTypeAliasDecl(TypeAliasDecl *TAD) {
      //we should be ignoring typealiases for associatedtypes, i.e. within class that conforms to protocol
      //but explicitly defined typealiases are fine I guess
      /*printCommon(TAD, "typealias");
      PrintWithColorRAII(OS, TypeColor) << " type='";
      if (TAD->getUnderlyingTypeLoc().getType()) {
        PrintWithColorRAII(OS, TypeColor)
          << TAD->getUnderlyingTypeLoc().getType().getString();
      } else {
        PrintWithColorRAII(OS, TypeColor) << "<<<unresolved>>>";
      }
      printInherited(TAD->getInherited());
      OS << "')";*/
    }

    void printAbstractTypeParamCommon(AbstractTypeParamDecl *decl,
                                      const char *name) {
      printCommon(decl, name);
      if (decl->getDeclContext()->getGenericEnvironmentOfContext()) {
        if (auto superclassTy = decl->getSuperclass()) {
          OS << " superclass='" << superclassTy->getString() << "'";
        }
      }
    }

    void visitGenericTypeParamDecl(GenericTypeParamDecl *decl) {
      printAbstractTypeParamCommon(decl, "generic_type_param");
      OS << " depth=" << decl->getDepth() << " index=" << decl->getIndex();
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitAssociatedTypeDecl(AssociatedTypeDecl *decl) {
      /*printAbstractTypeParamCommon(decl, "associated_type_decl");
      if (auto defaultDef = decl->getDefaultDefinitionType()) {
        OS << " default=";
        defaultDef.print(OS);
      }
      if (auto whereClause = decl->getTrailingWhereClause()) {
        OS << " where requirements: ";
        interleave(whereClause->getRequirements(),
                   [&](const RequirementRepr &req) { req.print(OS); },
                   [&] { OS << ", "; });
      }
      if (decl->overriddenDeclsComputed()) {
        OS << " overridden=";
        interleave(decl->getOverriddenDecls(),
                   [&](AssociatedTypeDecl *overridden) {
                     OS << overridden->getProtocol()->getName();
                   }, [&]() {
                     OS << ", ";
                   });
      }

      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void visitProtocolDecl(ProtocolDecl *PD) {
      /*printCommon(PD, "protocol");

      OS << " requirement signature=";
      if (PD->isRequirementSignatureComputed()) {
        OS << GenericSignature::get({PD->getProtocolSelfType()} ,
                                    PD->getRequirementSignature())
                ->getAsString();
      } else {
        OS << "<null>";
      }
      printInherited(PD->getInherited());
      if (auto whereClause = PD->getTrailingWhereClause()) {
        OS << " where requirements: ";
        interleave(whereClause->getRequirements(),
                   [&](const RequirementRepr &req) { req.print(OS); },
                   [&] { OS << ", "; });
      }

      for (auto VD : PD->getMembers()) {
        OS << '\n';
        printRec(VD);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      
      visitAnyStructDecl(PD, "protocol");
    }

    void printCommon(ValueDecl *VD, const char *Name,
                     TerminalColor Color = DeclColor) {
      printCommon((Decl*)VD, Name, Color);

      OS << ' ';
      printDeclName(VD);
      if (auto *AFD = dyn_cast<AbstractFunctionDecl>(VD))
        printGenericParameters(OS, AFD->getGenericParams());
      if (auto *GTD = dyn_cast<GenericTypeDecl>(VD))
        printGenericParameters(OS, GTD->getGenericParams());

      if (auto *var = dyn_cast<VarDecl>(VD)) {
        PrintWithColorRAII(OS, TypeColor) << " type='";
        if (var->hasType())
          var->getType().print(PrintWithColorRAII(OS, TypeColor).getOS());
        else
          PrintWithColorRAII(OS, TypeColor) << "<null type>";
        PrintWithColorRAII(OS, TypeColor) << "'";
      }

      if (VD->hasInterfaceType()) {
        PrintWithColorRAII(OS, InterfaceTypeColor) << " interface type='";
        VD->getInterfaceType()->print(
            PrintWithColorRAII(OS, InterfaceTypeColor).getOS());
        PrintWithColorRAII(OS, InterfaceTypeColor) << "'";
      }

      if (VD->hasAccess()) {
        PrintWithColorRAII(OS, AccessLevelColor) << " access="
          << getAccessLevelSpelling(VD->getFormalAccess());
      }

      if (VD->overriddenDeclsComputed()) {
        auto overridden = VD->getOverriddenDecls();
        if (!overridden.empty()) {
          PrintWithColorRAII(OS, OverrideColor) << " override=";
          interleave(overridden,
                     [&](ValueDecl *overridden) {
                       overridden->dumpRef(
                                PrintWithColorRAII(OS, OverrideColor).getOS());
                     }, [&]() {
                       OS << ", ";
                     });
        }
      }

      if (VD->isFinal())
        OS << " final";
      if (VD->isObjC())
        OS << " @objc";
      if (VD->isDynamic())
        OS << " dynamic";
      if (auto *attr =
              VD->getAttrs().getAttribute<DynamicReplacementAttr>()) {
        OS << " @_dynamicReplacement(for: \"";
        OS << attr->getReplacedFunctionName();
        OS << "\")";
      }
    }

    void printCommon(NominalTypeDecl *NTD, const char *Name,
                     TerminalColor Color = DeclColor) {
      printCommon((ValueDecl *)NTD, Name, Color);

      if (NTD->hasInterfaceType()) {
        if (NTD->isResilient())
          OS << " resilient";
        else
          OS << " non-resilient";
      }
    }

    void visitSourceFile(const SourceFile &SF) {
      /*OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      PrintWithColorRAII(OS, ASTNodeColor) << "source_file ";
      PrintWithColorRAII(OS, LocationColor) << '\"' << SF.getFilename() << '\"';*/
      
      for (Decl *D : SF.Decls) {
        if (D->isImplicit())
          continue;

        OS << '\n';
        printRec(D);
      }
      /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void visitVarDecl(VarDecl *VD) {
      /*printCommon(VD, "var_decl");
      if (VD->isStatic())
        PrintWithColorRAII(OS, DeclModifierColor) << " type";
      if (VD->isLet())
        PrintWithColorRAII(OS, DeclModifierColor) << " let";
      if (VD->hasNonPatternBindingInit())
        PrintWithColorRAII(OS, DeclModifierColor) << " non_pattern_init";
      if (VD->getAttrs().hasAttribute<LazyAttr>())
        PrintWithColorRAII(OS, DeclModifierColor) << " lazy";
      printStorageImpl(VD);
      printAccessors(VD);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void printStorageImpl(AbstractStorageDecl *D) {
      auto impl = D->getImplInfo();
      PrintWithColorRAII(OS, DeclModifierColor)
        << " readImpl="
        << getReadImplKindName(impl.getReadImpl());
      if (!impl.supportsMutation()) {
        PrintWithColorRAII(OS, DeclModifierColor)
          << " immutable";
      } else {
        PrintWithColorRAII(OS, DeclModifierColor)
          << " writeImpl="
          << getWriteImplKindName(impl.getWriteImpl());
        PrintWithColorRAII(OS, DeclModifierColor)
          << " readWriteImpl="
          << getReadWriteImplKindName(impl.getReadWriteImpl());
      }
    }

    void printAccessors(AbstractStorageDecl *D) {
      for (auto accessor : D->getAllAccessors()) {
        OS << "\n";
        printRec(accessor);
      }
    }

    void visitParamDecl(ParamDecl *PD) {
      printParameter(PD, OS);
    }

    void visitEnumCaseDecl(EnumCaseDecl *ECD) {
      /*printCommon(ECD, "enum_case_decl");
      for (EnumElementDecl *D : ECD->getElements()) {
        OS << '\n';
        printRec(D);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void visitEnumDecl(EnumDecl *ED) {
      /*printCommon(ED, "enum_decl");
      printInherited(ED->getInherited());
      for (Decl *D : ED->getMembers()) {
        OS << '\n';
        printRec(D);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      visitAnyStructDecl(ED, "enum");
    }

    void visitEnumElementDecl(EnumElementDecl *EED) {
      /*printCommon(EED, "enum_element_decl");
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      OS << "\nstatic " << EED->getName() << " = ";
      if(EED->hasAssociatedValues()) OS << "function() {return ";
      OS << "{rawValue: ";
      if(EED->hasRawValueExpr()) {
        OS << dumpToStr(EED->getRawValueExpr());
      }
      else {
        OS << '"' << EED->getName() << '"';
      }
      if(EED->hasAssociatedValues()) OS << ", ...arguments";
      OS << "}";
      if(EED->hasAssociatedValues()) OS << "}";
    }
    
    void visitAnyStructDecl(NominalTypeDecl *D, std::string kind) {
      std::string name = kind == "protocol" ? "interface" : "class";
      OS << name << " " << getName(D) << "";
      
      if(kind == "protocol") {
        bool wasAssociatedType = false;
        for (Decl *subD : D->getMembers()) {
          if(auto *associatedTypeDecl = dyn_cast<AssociatedTypeDecl>(subD)) {
            if(!wasAssociatedType) {
              OS << "<";
              wasAssociatedType = true;
            }
            else {
              OS << ", ";
            }
            OS << associatedTypeDecl->getFullName();
          }
        }
        if(wasAssociatedType) OS << ">";
      }
      else {
        printGenericParameters(OS, D->getGenericParams());
      }

      auto Inherited = D->getInherited();
      bool wasClass = false, wasProtocol = false;
      if(!Inherited.empty() && kind != "enum") {
        for(auto Super : Inherited) {
          bool isProtocol = false;
          if (auto *protocol = dyn_cast<ProtocolType>(Super.getType().getPointer())) { isProtocol = true; }
          if ((isProtocol ? wasProtocol : wasClass)) {
            OS << ", ";
          }
          else if(isProtocol) {
            OS << (kind == "protocol" ? " extends " : " implements ");
            wasProtocol = true;
          }
          else {
            OS << " extends ";
            wasClass = true;
          }
          OS << getType(Super.getType());
        }
      }
      
      OS << "{";
      
      if(kind == "struct") {
        OS << "\n$struct = true";
      }

      for (Decl *subD : D->getMembers()) {
        OS << '\n';
        printRec(subD);
      }
      
      if(kind != "protocol") {
        OS << "\nconstructor(signature: string, ...params: any[]) {";
        if(wasClass) {
          OS << "\nsuper(null)";
        }
        bool first = true;
        for (Decl *subD : D->getMembers()) {
          if (auto *constructor = dyn_cast<ConstructorDecl>(subD)) {
            OS << "\n";
            if (first) first = false;
            else OS << "\nelse ";
            OS << "if(signature === '" << getName(constructor) << "') this." << getName(constructor) << ".apply(this, params)";
          }
        }
        OS << "\nthis.$initialized = true";
        OS << "\n}";
      }

      OS << "\n}";
    }

    void visitStructDecl(StructDecl *SD) {
      /*printCommon(SD, "struct_decl");
      printInherited(SD->getInherited());
      for (Decl *D : SD->getMembers()) {
        OS << '\n';
        printRec(D);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      visitAnyStructDecl(SD, "struct");
    }

    void visitClassDecl(ClassDecl *CD) {
      /*printCommon(CD, "class_decl");
      if (CD->getAttrs().hasAttribute<StaticInitializeObjCMetadataAttr>())
        OS << " @_staticInitializeObjCMetadata";
      printInherited(CD->getInherited());
      for (Decl *D : CD->getMembers()) {
        OS << '\n';
        printRec(D);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      visitAnyStructDecl(CD, "class");
    }
    
    using FlattenedPattern = std::map<std::vector<unsigned>, const Pattern*>;
    FlattenedPattern flattenPattern(const Pattern *P) {
      
      FlattenedPattern result;
      std::vector<unsigned> access;
      
      walkPattern(P, result, access);
      
      return result;
    }
    void walkPattern(const Pattern *P, FlattenedPattern &info, const std::vector<unsigned> &access) {
      
      if(auto *tuplePattern = dyn_cast<TuplePattern>(P)) {
        unsigned i = 0;
        for (auto elt : tuplePattern->getElements()) {
          std::vector<unsigned> elAccess(access);
          elAccess.push_back(i++);
          walkPattern(elt.getPattern(), info, elAccess);
        }
      }
      else if(auto *wrapped = dyn_cast<IsPattern>(P)) {
        walkPattern(wrapped->getSubPattern(), info, access);
      }
      else if(auto *wrapped = dyn_cast<ParenPattern>(P)) {
        walkPattern(wrapped->getSubPattern(), info, access);
      }
      else if(auto *wrapped = dyn_cast<TypedPattern>(P)) {
        walkPattern(wrapped->getSubPattern(), info, access);
      }
      else if(auto *wrapped = dyn_cast<VarPattern>(P)) {
        walkPattern(wrapped->getSubPattern(), info, access);
      }
      else if(auto *wrapped = dyn_cast<EnumElementPattern>(P)) {
        info[access] = P;
        if(wrapped->hasSubPattern()) {
          //if enum has only 1 associated value and not multiple,
          //EnumElementPattern's child will be ParenPattern instead of TuplePattern
          //so we're trying to mimick TuplePattern in that case
          std::vector<unsigned> elAccess(access);
          if(auto *wrappedChild = dyn_cast<TuplePattern>(wrapped->getSubPattern())){}
          else elAccess.push_back(0);
          walkPattern(wrapped->getSubPattern(), info, elAccess);
        }
      }
      else if(auto *wrapped = dyn_cast<OptionalSomePattern>(P)) {
        walkPattern(wrapped->getSubPattern(), info, access);
      }
      else {
        info[access] = P;
      }
    }
    
    struct SinglePatternBinding {
      std::string varPrefix = "";
      std::string varName = "";
      std::unordered_map<std::string, std::string> accessorBodies = {};
      VarDecl *varDecl = nullptr;
      std::string tupleInit = "";
      std::vector<std::string> varNames = {};
    };
    SinglePatternBinding singlePatternBinding(FlattenedPattern &flattened) {
      
      SinglePatternBinding info;
      
      for(auto const& node : flattened) {
        if(auto *namedPattern = dyn_cast<NamedPattern>(node.second)) {
          auto *VD = namedPattern->getDecl();
          if(!info.varDecl) {
            info.varDecl = VD;
            if(!VD->getDeclContext()->isTypeContext()) {
              info.varPrefix += VD->isLet() ? "const " : "let ";
            }
            else {
              if(VD->isStatic()) info.varPrefix += "static ";
              if(VD->isLet()) info.varPrefix += "readonly ";
            }
            
            if(node.first.size()) {
              info.varName += "$tuple";
            }
            else {
              info.varName = VD->getNameStr();
            }
          }
          
          if(!VD->getDeclContext()->getSelfProtocolDecl()) {
            for (auto accessor : VD->getAllAccessors()) {
              //swift autogenerates getter/setters for regular vars; no need to display them
              if(accessor->isImplicit()) continue;
              
              std::string accessorType = getAccessorKindString(accessor->getAccessorKind());
              std::string bodyStr = printFuncSignature(accessor->getParameters(), accessor->getGenericParams()) + printFuncBody(accessor);
              
              info.accessorBodies[accessorType] = bodyStr;
            }
          }
          
          info.varNames.push_back(VD->getNameStr());
          
          if(node.first.size()) {
            info.tupleInit += ", ";
            info.tupleInit += VD->getNameStr();
            info.tupleInit += " = $tuple";
            std::string indexes = "";
            for(auto index : node.first) {
              indexes += "[" + std::to_string(index) + "]";
              info.tupleInit += " && $tuple" + indexes;
            }
          }
        }
      }
      
      return info;
    }

    void visitPatternBindingDecl(PatternBindingDecl *PBD) {
      /*printCommon(PBD, "pattern_binding_decl");

      for (auto entry : PBD->getPatternList()) {
        OS << '\n';
        printRec(entry.getPattern());
        if (entry.getInit()) {
          OS << '\n';
          printRec(entry.getInit());
        }
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      
      for (auto entry : PBD->getPatternList()) {
        
        auto flattened = flattenPattern(entry.getPattern());
        auto info = singlePatternBinding(flattened);
        
        bool isOverriden = false;
        if (info.varDecl->getDeclContext()->isTypeContext()) {
          if (auto *overriden = entry.getPattern()->getSingleVar()->getOverriddenDecl()) {
            isOverriden = true;
          }
        }
        
        if(!isOverriden || entry.getInit()) {
          OS << "\n" << info.varPrefix << info.varName;
          if(info.varDecl->getDeclContext()->isTypeContext() && !info.varDecl->getDeclContext()->getSelfProtocolDecl()) {
            OS << "$internal";
          }
          if(entry.getInit()) {
            OS << " = " << handleRAssignment(entry.getInit(), dumpToStr(entry.getInit()));
          }
        }
        
        if(info.varDecl->getDeclContext()->isTypeContext() && !info.varDecl->getDeclContext()->getSelfProtocolDecl()) {
          std::string internalGetVar = "this." + info.varName + "$internal";
          std::string internalSetVar = "this." + info.varName + "$internal = $newValue";
          if(isOverriden) {
            internalGetVar = "super." + info.varName + "$get()";
            internalSetVar = "super." + info.varName + "$set($newValue)";
          }
          
          OS << "\n" << info.varPrefix << info.varName << "$get";
          if(info.accessorBodies.count("get")) {
            OS << info.accessorBodies["get"];
          }
          else {
            OS << "() { return " << internalGetVar << " }";
          }
          OS << "\n" << info.varPrefix << "get " << info.varName << "() { return this." << info.varName << "$get() }";

          if(info.accessorBodies.count("set") || !info.accessorBodies.count("get")) {
            OS << "\n" << info.varPrefix << info.varName << "$set";
            if(info.accessorBodies.count("set")) {
              OS << info.accessorBodies["set"];
            }
            else {
              OS << "($newValue) {";
              if(info.accessorBodies.count("willSet")) OS << "\nfunction $willSet" << info.accessorBodies["willSet"];
              if(info.accessorBodies.count("didSet")) OS << "\nfunction $didSet" << info.accessorBodies["didSet"];
              OS << "\nlet $oldValue = " << internalGetVar;
              if(info.accessorBodies.count("willSet")) OS << "\nif(this.$initialized) $willSet.call(this, $newValue)";
              OS << "\n" << internalSetVar;
              if(info.accessorBodies.count("didSet")) OS << "\nif(this.$initialized) $didSet.call(this, $oldValue)";
              OS << "\n}";
            }
            OS << "\n" << info.varPrefix << "set " << info.varName << "($newValue) { this." << info.varName << "$set($newValue) }\n";
          }
        }
        
        OS << info.tupleInit;
        
        OS << ";\n";
      }
    }
    
    void visitSubscriptDecl(SubscriptDecl *SD) {
      /*printCommon(SD, "subscript_decl");
      printStorageImpl(SD);
      printAccessors(SD);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      for (auto accessor : SD->getAllAccessors()) {
        //swift generates implicit `_modify` accessors, not sure what that's about
        if(accessor->isImplicit()) continue;
        std::string accessorType = getAccessorKindString(accessor->getAccessorKind());
        
        OS << printAbstractFunc(accessor, SD, "$" + accessorType);
      }
    }

    void printCommonAFD(AbstractFunctionDecl *D, const char *Type) {
      printCommon(D, Type, FuncColor);
      if (!D->getCaptureInfo().isTrivial()) {
        OS << " ";
        D->getCaptureInfo().print(OS);
      }

      if (auto fec = D->getForeignErrorConvention()) {
        OS << " foreign_error=";
        OS << getForeignErrorConventionKindString(fec->getKind());
        bool wantResultType = (
          fec->getKind() == ForeignErrorConvention::ZeroResult ||
          fec->getKind() == ForeignErrorConvention::NonZeroResult);

        OS << ((fec->isErrorOwned() == ForeignErrorConvention::IsOwned)
                ? ",owned"
                : ",unowned");
        OS << ",param=" << llvm::utostr(fec->getErrorParameterIndex());
        OS << ",paramtype=" << fec->getErrorParameterType().getString();
        if (wantResultType)
          OS << ",resulttype=" << fec->getResultType().getString();
      }
    }

    void printParameter(const ParamDecl *P, raw_ostream &OS) {
      /*OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      PrintWithColorRAII(OS, ParameterColor) << "parameter ";
      printDeclName(P);
      if (!P->getArgumentName().empty())
        PrintWithColorRAII(OS, IdentifierColor)
          << " apiName=" << P->getArgumentName();

      if (P->hasType()) {
        PrintWithColorRAII(OS, TypeColor) << " type='";
        P->getType().print(PrintWithColorRAII(OS, TypeColor).getOS());
        PrintWithColorRAII(OS, TypeColor) << "'";
      }

      if (P->hasInterfaceType()) {
        PrintWithColorRAII(OS, InterfaceTypeColor) << " interface type='";
        P->getInterfaceType().print(
            PrintWithColorRAII(OS, InterfaceTypeColor).getOS());
        PrintWithColorRAII(OS, InterfaceTypeColor) << "'";
      }

      switch (P->getSpecifier()) {
      case VarDecl::Specifier::Let:
        break;
      case VarDecl::Specifier::Var:
        OS << " mutable";
        break;
      case VarDecl::Specifier::InOut:
        OS << " inout";
        break;
      case VarDecl::Specifier::Shared:
        OS << " shared";
        break;
      case VarDecl::Specifier::Owned:
        OS << " owned";
        break;
      }

      if (P->isVariadic())
        OS << " variadic";

      if (P->isAutoClosure())
        OS << " autoclosure";

      if (P->getDefaultArgumentKind() != DefaultArgumentKind::None)
        printField("default_arg",
                   getDefaultArgumentKindString(P->getDefaultArgumentKind()));

      if (auto init = P->getDefaultValue()) {
        OS << " expression=\n";
        printRec(init);
      }

      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      
      OS << P->getFullName();
      
      if(P->isInOut()) {
        OS << "$inout";
      }
      
      if(P->hasType()) {
        OS << ": " << getType(P->getType());
      }
      else if(P->hasInterfaceType()) {
        //not sure what that's about
        OS << ": " << getType(P->getInterfaceType());
      }
      
      if (auto init = P->getDefaultValue()) {
        OS << " = ";
        init->dump(OS);
      }
    }

    void printParameterList(const ParameterList *params, const ASTContext *ctx = nullptr) {
      OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      PrintWithColorRAII(OS, ParameterColor) << "parameter_list";
      Indent += 2;
      for (auto P : *params) {
        OS << '\n';
        printParameter(P, OS);
      }

      if (!ctx && params->size() != 0 && params->get(0))
        ctx = &params->get(0)->getASTContext();

      if (ctx) {
        auto R = params->getSourceRange();
        if (R.isValid()) {
          PrintWithColorRAII(OS, RangeColor) << " range=";
          R.print(PrintWithColorRAII(OS, RangeColor).getOS(),
                  ctx->SourceMgr, /*PrintText=*/false);
        }
      }

      PrintWithColorRAII(OS, ParenthesisColor) << ')';
      Indent -= 2;
    }

    void printAbstractFunctionDecl(AbstractFunctionDecl *D) {
      Indent += 2;
      if (auto *P = D->getImplicitSelfDecl()) {
        OS << '\n';
        printParameter(P, OS);
      }

      OS << '\n';
      printParameterList(D->getParameters(), &D->getASTContext());
      Indent -= 2;

      if (auto FD = dyn_cast<FuncDecl>(D)) {
        if (FD->getBodyResultTypeLoc().getTypeRepr()) {
          OS << '\n';
          Indent += 2;
          OS.indent(Indent);
          PrintWithColorRAII(OS, ParenthesisColor) << '(';
          OS << "result\n";
          printRec(FD->getBodyResultTypeLoc().getTypeRepr());
          PrintWithColorRAII(OS, ParenthesisColor) << ')';
          Indent -= 2;
        }
      }
      if (auto Body = D->getBody(/*canSynthesize=*/false)) {
        OS << '\n';
        printRec(Body, D->getASTContext());
      }
    }

    void printCommonFD(FuncDecl *FD, const char *type) {
      printCommonAFD(FD, type);
      if (FD->isStatic())
        OS << " type";
    }
    
    std::string printFuncSignature(ParameterList *params, GenericParamList *genericParams) {
      
      std::string signature = "";
      std::string genericStr;
      llvm::raw_string_ostream genericStream(genericStr);
      printGenericParameters(genericStream, genericParams);
      signature += genericStream.str();

      signature += "(";
      if(params) {
        bool first = true;
        for (auto P : *params) {
          if(first) first = false;
          else signature += ", ";
          std::string parameterStr;
          llvm::raw_string_ostream parameterStream(parameterStr);
          printParameter(P, parameterStream);
          signature += parameterStream.str();
        }
      }
      signature += ")";

      return signature;
    }
    
    std::string printFuncBody(AbstractFunctionDecl *FD) {
      
      std::string body = "";
      
      if (FD -> isMemberwiseInitializer()) {
        body += "{";
        for (auto P : *FD->getParameters()) {
          body += '\n';
          
          //manually creating member_ref_expr (self.`P->getFullName()`)
          //so that we can then use it in handleLAssignment
          VarDecl *storedVar = dyn_cast<VarDecl>(FD->getDeclContext()->getSelfStructDecl()->lookupDirect(P->getFullName())[0]);
          ASTContext &C = FD->getASTContext();
          auto *selfDecl = FD->getImplicitSelfDecl();
          auto selfRef = new (C) DeclRefExpr(selfDecl, DeclNameLoc(), /*implicit*/true);
          selfRef->setType(selfDecl->getType());
          auto storedRef = new (C) MemberRefExpr(selfRef, SourceLoc(), storedVar,
                                                 DeclNameLoc(), /*Implicit=*/true,
                                                 AccessSemantics::DirectToStorage);
          storedRef->setType(storedVar->getInterfaceType());
          
          body += handleLAssignment(storedRef, getName(P));
        }
        body += "\n}";
      }
      else if (auto Body = FD->getBody(/*canSynthesize=*/false)) {
        std::string bodyStr;
        llvm::raw_string_ostream bodyStream(bodyStr);
        printRec(Body, FD->getASTContext(), bodyStream);
        body += "{\n";
        body += generateInOutPrefix(FD);
        body += bodyStream.str();
        body += generateInOutSuffix(FD);
        body += "\n}";
      }
      
      return body;
    }
    
    std::string generateInOutPrefix(AbstractFunctionDecl *FD) {
      std::string result;
      if (auto params = FD->getParameters()) {
        for (auto P : *params) {
          if(P->isInOut()) {
            std::string paramName = P->getBaseName().userFacingName().data();
            result += "\nlet " + paramName + " = " + paramName + "$inout.get()";
          }
        }
      }
      if(result.length()) result += "\nconst $result = (() => {";
      return result;
    }
    std::string generateInOutSuffix(AbstractFunctionDecl *FD) {
      std::string result;
      if (auto params = FD->getParameters()) {
        for (auto P : *params) {
          if(P->isInOut()) {
            std::string paramName = P->getBaseName().userFacingName().data();
            result += "\n" + paramName + "$inout.set(" + paramName + ")";
          }
        }
      }
      if(result.length()) result = "})()" + result + "\nreturn $result";
      return result;
    }

    std::string printAbstractFunc(AbstractFunctionDecl *FD, ValueDecl *NameD = nullptr, std::string suffix = "") {
      
      //TODO bodge; remove when we've implemented overriding native functions (including operators)
      if(!strncmp(FD->getBaseName().userFacingName().data(), "__derived_enum_equals", 100)) return "";
      
      if(!NameD) NameD = FD;
      std::string str = "";
      
      std::string functionPrefix = "";
      if(!FD->getDeclContext()->isTypeContext()) {
        functionPrefix += "function ";
      }
      else {
        if(FD->isStatic()) functionPrefix += "static ";
      }
      str += functionPrefix;
      
      std::string functionName = getName(NameD) + suffix;
      str += functionName;
      
      std::string signature = printFuncSignature(FD->getParameters(), FD->getGenericParams());
      str += signature;

      str += printFuncBody(FD);
      
      unsigned long i = 1;
      while(true) {
        std::string duplicateName = getName(NameD, i);
        if(duplicateName == "!NO_DUPLICATE") break;
        str += "\n" + functionPrefix + duplicateName + suffix + signature;
        str += "{\nthis." + functionName + ".apply(this,arguments)\n}";
        i++;
      }
      
      return str;
    }

    void visitFuncDecl(FuncDecl *FD) {
      /*printCommonFD(FD, "func_decl");
      printAbstractFunctionDecl(FD);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      OS << printAbstractFunc(FD);
      
    }

    void visitAccessorDecl(AccessorDecl *AD) {
      /*printCommonFD(AD, "accessor_decl");
      OS << " " << getAccessorKindString(AD->getAccessorKind());
      OS << "_for=" << AD->getStorage()->getFullName();
      printAbstractFunctionDecl(AD);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void visitConstructorDecl(ConstructorDecl *CD) {
      /*printCommonAFD(CD, "constructor_decl");
      if (CD->isRequired())
        PrintWithColorRAII(OS, DeclModifierColor) << " required";
      PrintWithColorRAII(OS, DeclModifierColor) << " "
        << getCtorInitializerKindString(CD->getInitKind());
      if (CD->getFailability() != OTK_None)
        PrintWithColorRAII(OS, DeclModifierColor) << " failable="
          << getOptionalTypeKindString(CD->getFailability());
      printAbstractFunctionDecl(CD);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      OS << printAbstractFunc(CD);
    }

    void visitDestructorDecl(DestructorDecl *DD) {
      //destructors not supported :(
      /*printCommonAFD(DD, "destructor_decl");
      printAbstractFunctionDecl(DD);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void visitTopLevelCodeDecl(TopLevelCodeDecl *TLCD) {
      /*printCommon(TLCD, "top_level_code_decl");*/
      if (TLCD->getBody()) {
        OS << "\n";
        printRec(TLCD->getBody(), static_cast<Decl *>(TLCD)->getASTContext());
      }
      /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }
    
    void printASTNodes(const ArrayRef<ASTNode> &Elements, const ASTContext &Ctx, StringRef Name) {
      OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << "(";
      PrintWithColorRAII(OS, ASTNodeColor) << Name;
      for (auto Elt : Elements) {
        OS << '\n';
        if (auto *SubExpr = Elt.dyn_cast<Expr*>())
          printRec(SubExpr);
        else if (auto *SubStmt = Elt.dyn_cast<Stmt*>())
          printRec(SubStmt, Ctx);
        else
          printRec(Elt.get<Decl*>());
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitIfConfigDecl(IfConfigDecl *ICD) {
      printCommon(ICD, "if_config_decl");
      Indent += 2;
      for (auto &Clause : ICD->getClauses()) {
        OS << '\n';
        OS.indent(Indent);
        PrintWithColorRAII(OS, StmtColor) << (Clause.Cond ? "#if:" : "#else:");
        if (Clause.isActive)
          PrintWithColorRAII(OS, DeclModifierColor) << " active";
        if (Clause.Cond) {
          OS << "\n";
          printRec(Clause.Cond);
        }

        OS << '\n';
        Indent += 2;
        printASTNodes(Clause.Elements, ICD->getASTContext(), "elements");
        Indent -= 2;
      }

      Indent -= 2;
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitPoundDiagnosticDecl(PoundDiagnosticDecl *PDD) {
      printCommon(PDD, "pound_diagnostic_decl");
      auto kind = PDD->isError() ? "error" : "warning";
      OS << " kind=" << kind << "\n";
      Indent += 2;
      printRec(PDD->getMessage());
      Indent -= 2;
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitPrecedenceGroupDecl(PrecedenceGroupDecl *PGD) {
      printCommon(PGD, "precedence_group_decl ");
      OS << PGD->getName() << "\n";

      OS.indent(Indent+2);
      OS << "associativity "
         << getAssociativityString(PGD->getAssociativity()) << "\n";

      OS.indent(Indent+2);
      OS << "assignment " << (PGD->isAssignment() ? "true" : "false");

      auto printRelations =
          [&](StringRef label, ArrayRef<PrecedenceGroupDecl::Relation> rels) {
        if (rels.empty()) return;
        OS << '\n';
        OS.indent(Indent+2);
        OS << label << ' ' << rels[0].Name;
        for (auto &rel : rels.slice(1))
          OS << ", " << rel.Name;
      };
      printRelations("higherThan", PGD->getHigherThan());
      printRelations("lowerThan", PGD->getLowerThan());

      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void printOperatorIdentifiers(OperatorDecl *OD) {
      auto identifiers = OD->getIdentifiers();
      for (auto index : indices(identifiers)) {
        OS.indent(Indent + 2);
        OS << "identifier #" << index << " " << identifiers[index];
        if (index != identifiers.size() - 1)
          OS << "\n";
      }
    }

    void visitInfixOperatorDecl(InfixOperatorDecl *IOD) {
      /*printCommon(IOD, "infix_operator_decl");
      OS << " " << IOD->getName();
      if (!IOD->getIdentifiers().empty()) {
        OS << "\n";
        printOperatorIdentifiers(IOD);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void visitPrefixOperatorDecl(PrefixOperatorDecl *POD) {
      /*printCommon(POD, "prefix_operator_decl");
      OS << " " << POD->getName();
      if (!POD->getIdentifiers().empty()) {
        OS << "\n";
        printOperatorIdentifiers(POD);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void visitPostfixOperatorDecl(PostfixOperatorDecl *POD) {
      /*printCommon(POD, "postfix_operator_decl");
      OS << " " << POD->getName();
      if (!POD->getIdentifiers().empty()) {
        OS << "\n";
        printOperatorIdentifiers(POD);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

    void visitModuleDecl(ModuleDecl *MD) {
      printCommon(MD, "module");
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitMissingMemberDecl(MissingMemberDecl *MMD) {
      printCommon(MMD, "missing_member_decl ");
      PrintWithColorRAII(OS, IdentifierColor)
          << '\"' << MMD->getFullName() << '\"';
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
  };
} // end anonymous namespace

void ParameterList::dump() const {
  dump(llvm::errs(), 0);
}

void ParameterList::dump(raw_ostream &OS, unsigned Indent) const {
  llvm::Optional<llvm::SaveAndRestore<bool>> X;
  
  // Make sure to print type variables if we can get to ASTContext.
  if (size() != 0 && get(0)) {
    auto &ctx = get(0)->getASTContext();
    X.emplace(llvm::SaveAndRestore<bool>(ctx.LangOpts.DebugConstraintSolver,
                                         true));
  }
  
  PrintDecl(OS, Indent).printParameterList(this);
  llvm::errs() << '\n';
}



void Decl::dump() const {
  dump(llvm::errs(), 0);
}

void Decl::dump(const char *filename) const {
  std::error_code ec;
  llvm::raw_fd_ostream stream(filename, ec, llvm::sys::fs::FA_Read |
                              llvm::sys::fs::FA_Write);
  // In assert builds, we blow up. Otherwise, we just return.
  assert(!ec && "Failed to open file for dumping?!");
  if (ec)
    return;
  dump(stream, 0);
}

void Decl::dump(raw_ostream &OS, unsigned Indent) const {
  // Make sure to print type variables.
  llvm::SaveAndRestore<bool> X(getASTContext().LangOpts.DebugConstraintSolver,
                               true);
  PrintDecl(OS, Indent).visit(const_cast<Decl *>(this));
  OS << '\n';
}

/// Print the given declaration context (with its parents).
void swift::printContext(raw_ostream &os, DeclContext *dc) {
  if (auto parent = dc->getParent()) {
    printContext(os, parent);
    os << '.';
  }

  switch (dc->getContextKind()) {
  case DeclContextKind::Module:
    printName(os, cast<ModuleDecl>(dc)->getName());
    break;

  case DeclContextKind::FileUnit:
    // FIXME: print the file's basename?
    os << "(file)";
    break;

  case DeclContextKind::SerializedLocal:
    os << "local context";
    break;

  case DeclContextKind::AbstractClosureExpr: {
    auto *ACE = cast<AbstractClosureExpr>(dc);
    if (isa<ClosureExpr>(ACE)) {
      PrintWithColorRAII(os, DiscriminatorColor)
        << "explicit closure discriminator=";
    }
    if (isa<AutoClosureExpr>(ACE)) {
      PrintWithColorRAII(os, DiscriminatorColor)
        << "autoclosure discriminator=";
    }
    PrintWithColorRAII(os, DiscriminatorColor) << ACE->getDiscriminator();
    break;
  }

  case DeclContextKind::GenericTypeDecl:
    printName(os, cast<GenericTypeDecl>(dc)->getName());
    break;

  case DeclContextKind::ExtensionDecl:
    if (auto extendedNominal = cast<ExtensionDecl>(dc)->getExtendedNominal()) {
      printName(os, extendedNominal->getName());
    }
    //os << " extension";
    break;

  case DeclContextKind::Initializer:
    switch (cast<Initializer>(dc)->getInitializerKind()) {
    case InitializerKind::PatternBinding:
      os << "pattern binding initializer";
      break;
    case InitializerKind::DefaultArgument:
      os << "default argument initializer";
      break;
    }
    break;

  case DeclContextKind::TopLevelCodeDecl:
    os << "top-level code";
    break;

  case DeclContextKind::AbstractFunctionDecl:
    printName(os, cast<AbstractFunctionDecl>(dc)->getFullName());
    break;

  case DeclContextKind::SubscriptDecl:
    printName(os, cast<SubscriptDecl>(dc)->getFullName());
    break;
  }
}

std::string ValueDecl::printRef() const {
  std::string result;
  llvm::raw_string_ostream os(result);
  dumpRef(os);
  return os.str();
}

void ValueDecl::dumpRef(raw_ostream &os) const {
  // Print the context.
  printContext(os, getDeclContext());
  os << ".";

  // Print name.
  getFullName().printPretty(os);

  // Print location.
  auto &srcMgr = getASTContext().SourceMgr;
  if (getLoc().isValid()) {
    os << '@';
    getLoc().print(os, srcMgr);
  }
}

void LLVM_ATTRIBUTE_USED ValueDecl::dumpRef() const {
  dumpRef(llvm::errs());
}

void SourceFile::dump() const {
  dump(llvm::errs());
}

void SourceFile::dump(llvm::raw_ostream &OS) const {
  llvm::SaveAndRestore<bool> X(getASTContext().LangOpts.DebugConstraintSolver,
                               true);
  PrintDecl(OS).visitSourceFile(*this);
  llvm::errs() << '\n';
}

void Pattern::dump() const {
  PrintPattern(llvm::errs()).visit(const_cast<Pattern*>(this));
  llvm::errs() << '\n';
}

//===----------------------------------------------------------------------===//
// Printing for Stmt and all subclasses.
//===----------------------------------------------------------------------===//

namespace {
/// PrintStmt - Visitor implementation of Stmt::dump.
class PrintStmt : public StmtVisitor<PrintStmt> {
public:
  raw_ostream &OS;
  const ASTContext *Ctx;
  unsigned Indent;

  PrintStmt(raw_ostream &os, const ASTContext *ctx, unsigned indent)
    : OS(os), Ctx(ctx), Indent(indent) {
  }

  void printRec(Stmt *S) {
    Indent += 2;
    if (S)
      visit(S);
    else
      OS.indent(Indent) << "(**NULL STATEMENT**)";
    Indent -= 2;
  }

  void printRec(Decl *D) { D->dump(OS, Indent + 2); }
  void printRec(Expr *E) { E->dump(OS, Indent + 2); }
  void printRec(const Pattern *P) {
    PrintPattern(OS, Indent+2).visit(const_cast<Pattern *>(P));
  }

  void printRec(StmtConditionElement C) {
    switch (C.getKind()) {
    case StmtConditionElement::CK_Boolean:
      return printRec(C.getBoolean());
    case StmtConditionElement::CK_PatternBinding:
      Indent += 2;
      OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      PrintWithColorRAII(OS, PatternColor) << "pattern\n";

      printRec(C.getPattern());
      OS << "\n";
      printRec(C.getInitializer());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
      Indent -= 2;
      break;
    case StmtConditionElement::CK_Availability:
      Indent += 2;
      OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      OS << "#available\n";
      for (auto *Query : C.getAvailability()->getQueries()) {
        OS << '\n';
        switch (Query->getKind()) {
        case AvailabilitySpecKind::PlatformVersionConstraint:
          cast<PlatformVersionConstraintAvailabilitySpec>(Query)->print(OS, Indent + 2);
          break;
        case AvailabilitySpecKind::LanguageVersionConstraint:
          cast<LanguageVersionConstraintAvailabilitySpec>(Query)->print(OS, Indent + 2);
          break;
        case AvailabilitySpecKind::OtherPlatform:
          cast<OtherPlatformAvailabilitySpec>(Query)->print(OS, Indent + 2);
          break;
        }
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ")";
      Indent -= 2;
      break;
    }
  }

  raw_ostream &printCommon(Stmt *S, const char *Name) {
    OS.indent(Indent);
    PrintWithColorRAII(OS, ParenthesisColor) << '(';
    PrintWithColorRAII(OS, StmtColor) << Name;

    if (S->isImplicit())
      OS << " implicit";

    if (Ctx) {
      auto R = S->getSourceRange();
      if (R.isValid()) {
        PrintWithColorRAII(OS, RangeColor) << " range=";
        R.print(PrintWithColorRAII(OS, RangeColor).getOS(),
                Ctx->SourceMgr, /*PrintText=*/false);
      }
    }

    if (S->TrailingSemiLoc.isValid())
      OS << " trailing_semi";

    return OS;
  }

  void visitBraceStmt(BraceStmt *S) {
    /*printCommon(S, "brace_stmt");*/
    printASTNodes(S->getElements());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }

  void printASTNodes(const ArrayRef<ASTNode> &Elements) {
    for (auto Elt : Elements) {
      OS << '\n';
      if (auto *SubExpr = Elt.dyn_cast<Expr*>())
        printRec(SubExpr);
      else if (auto *SubStmt = Elt.dyn_cast<Stmt*>())
        printRec(SubStmt);
      else
        printRec(Elt.get<Decl*>());
      OS << ";";
    }
  }

  void visitReturnStmt(ReturnStmt *S) {
    /*printCommon(S, "return_stmt");
    if (S->hasResult()) {
      OS << '\n';
      printRec(S->getResult());
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << "return ";
    if (S->hasResult()) {
      printRec(S->getResult());
    }
  }

  void visitYieldStmt(YieldStmt *S) {
    printCommon(S, "yield_stmt");
    for (auto yield : S->getYields()) {
      OS << '\n';
      printRec(yield);
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitDeferStmt(DeferStmt *S) {
    printCommon(S, "defer_stmt") << '\n';
    printRec(S->getTempDecl());
    OS << '\n';
    printRec(S->getCallExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  struct ConditionAndInitializerStr { std::string conditionStr; std::string initializerStr; };
  
  ConditionAndInitializerStr getIfLet(Pattern *P, Expr *initExpr) {
    std::string conditionStr = "";
    std::string initializerStr = "";
    
    auto flattened = PrintDecl(OS).flattenPattern(P);
    auto info = PrintDecl(OS).singlePatternBinding(flattened);
    initializerStr += info.varPrefix + info.varName;
    if(initializerStr.length()) initializerStr += " = ";
    initializerStr += handleRAssignment(initExpr, dumpToStr(initExpr));
    initializerStr += info.tupleInit;
    
    for(auto varName: info.varNames) {
      if (conditionStr.length()) conditionStr += " && ";
      conditionStr += "(" + varName + " != null)";
    }
    
    return ConditionAndInitializerStr{ conditionStr, initializerStr };
  }
  
  ConditionAndInitializerStr getConditionAndInitializerStr(StmtCondition conditions) {
    
    std::string conditionStr = "";
    std::string initializerStr = "";
    
    for (auto elt : conditions) {
      if (auto condition = elt.getBooleanOrNull()) {
        if (conditionStr.length()) conditionStr += " && ";
        conditionStr += "(" + dumpToStr(condition) + ")";
      }
      else if (auto pattern = elt.getPatternOrNull()) {
        auto ifLet = getIfLet(pattern, elt.getInitializer());
        if (conditionStr.length()) conditionStr += " && ";
        conditionStr += ifLet.conditionStr;
        initializerStr += ifLet.initializerStr;
      }
    }
    
    return ConditionAndInitializerStr{ conditionStr, initializerStr };
  }
  
  void visitIfStmt(IfStmt *S) {
    /*printCommon(S, "if_stmt") << '\n';
    for (auto elt : S->getCond())
      printRec(elt);
    OS << '\n';
    printRec(S->getThenStmt());
    if (S->getElseStmt()) {
      OS << '\n';
      printRec(S->getElseStmt());
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    auto conditionAndInitializerStr = getConditionAndInitializerStr(S->getCond());
    
    OS << "\n{";
    OS << conditionAndInitializerStr.initializerStr;
    OS << "\nif(" << conditionAndInitializerStr.conditionStr << ") {\n";
    
    printRec(S->getThenStmt());
    
    OS << "\n}";
    
    if (S->getElseStmt()) {
      OS << "\nelse {";
      printRec(S->getElseStmt());
      OS << "\n}";
    }
    OS << "\n}";
  }

  void visitGuardStmt(GuardStmt *S) {
    /*printCommon(S, "guard_stmt") << '\n';
    for (auto elt : S->getCond())
      printRec(elt);
    OS << '\n';
    printRec(S->getBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    auto conditionAndInitializerStr = getConditionAndInitializerStr(S->getCond());
    
    OS << "\n{";
    OS << conditionAndInitializerStr.initializerStr;
    OS << "\nif(!(" << conditionAndInitializerStr.conditionStr << ")) {\n";
    printRec(S->getBody());
    OS << "\n}";
    OS << "\n}";
  }

  void visitDoStmt(DoStmt *S) {
    printCommon(S, "do_stmt") << '\n';
    printRec(S->getBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitWhileStmt(WhileStmt *S) {
    /*printCommon(S, "while_stmt") << '\n';
    for (auto elt : S->getCond())
      printRec(elt);
    OS << '\n';
    printRec(S->getBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    auto conditionAndInitializerStr = getConditionAndInitializerStr(S->getCond());
    
    OS << "while(" << conditionAndInitializerStr.conditionStr << ") {\n";
    printRec(S->getBody());
    OS << "\n}";
  }

  void visitRepeatWhileStmt(RepeatWhileStmt *S) {
    /*printCommon(S, "repeat_while_stmt") << '\n';
    printRec(S->getBody());
    OS << '\n';
    printRec(S->getCond());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    std::string conditionStr = dumpToStr(S->getCond());
    
    OS << "do {\n";
    printRec(S->getBody());
    OS << "\n} while(" << conditionStr << ")";
  }
  void visitForEachStmt(ForEachStmt *S) {
    /*printCommon(S, "for_each_stmt") << '\n';
    printRec(S->getPattern());
    OS << '\n';
    if (S->getWhere()) {
      Indent += 2;
      OS.indent(Indent) << "(where\n";
      printRec(S->getWhere());
      OS << ")\n";
      Indent -= 2;
    }
    printRec(S->getPattern());
    OS << '\n';
    printRec(S->getSequence());
    OS << '\n';
    if (S->getIterator()) {
      printRec(S->getIterator());
      OS << '\n';
    }
    if (S->getIteratorNext()) {
      printRec(S->getIteratorNext());
      OS << '\n';
    }
    printRec(S->getBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << "\n{";
    
    if(S->getIterator()) {
      printRec(S->getIterator());
    }
    
    OS << "\nwhile(true) {\n";
    
    auto ifLet = getIfLet(S->getPattern(), S->getIteratorNext());
    
    if(!ifLet.conditionStr.length()) {
      //when using '_' in the for loop
      OS << ";\nif(" << ifLet.initializerStr << " == null) break;\n";
    }
    else {
      OS << ifLet.initializerStr;
      OS << ";\nif(!(" + ifLet.conditionStr + ")) break;\n";
    }

    if(S->getWhere()) {
      OS << "\nif(!(" << dumpToStr(S->getWhere()) << ")) break;";
    }
    
    printRec(S->getBody());
    
    OS << "\n}";
    OS << "\n}";
  }
  void visitBreakStmt(BreakStmt *S) {
    /*printCommon(S, "break_stmt");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << "break";
  }
  void visitContinueStmt(ContinueStmt *S) {
    /*printCommon(S, "continue_stmt");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << "continue";
  }
  void visitFallthroughStmt(FallthroughStmt *S) {
    //we're ignoring fallthrough statement; it's supposed to be handled by switch itself
    /*printCommon(S, "fallthrough_stmt");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  
  CaseStmt *getCase(const ASTNode &caseNode) {
    Stmt *AS = caseNode.get<Stmt*>();
    auto *S = dyn_cast<CaseStmt>(AS);
    return S;
  }
  void printSwitchConditions(CaseStmt *S) {
    OS << "(";
    bool first = true;
    for (const auto &LabelItem : S->getCaseLabelItems()) {
      if (!first) OS << " || ";
      if (auto *CasePattern = LabelItem.getPattern()) {
        bool first2 = true;
        for(auto const& node : PrintDecl(OS).flattenPattern(CasePattern)) {
          if(auto *exprPattern = dyn_cast<ExprPattern>(node.second)) {
            nameReplacements["$match"] = matchNameReplacement(node.first);
            if(first2) first2 = false;
            else OS << " && ";
            OS << "(";
            printRec(exprPattern);
            OS << ")";
            first = false;
            nameReplacements = {};
          }
          else if(auto *enumElementPattern = dyn_cast<EnumElementPattern>(node.second)) {
            OS << "$match.rawValue == ";
            OS << getType(enumElementPattern->getParentType().getType()) << '.' << enumElementPattern->getName();
            if(enumElementPattern->getElementDecl()->hasAssociatedValues()) OS << "()";
            OS << ".rawValue";
            first = false;
          }
        }
      }
      if (auto *Guard = LabelItem.getGuardExpr()) {
        if (auto *CasePattern = LabelItem.getPattern()) {
          for(auto const& node : PrintDecl(OS).flattenPattern(CasePattern)) {
            if(auto *namedPattern = dyn_cast<NamedPattern>(node.second)) {
              nameReplacements[getName(namedPattern->getSingleVar())] = matchNameReplacement(node.first);
            }
          }
        }
        if(!first) OS << ") && (";
        Guard->dump(OS, Indent+4);
        first = false;
        nameReplacements = {};
      }
    }
    if(first) OS << "true";
    OS << ")";
  }
  void printSwitchDeclarations(CaseStmt *S) {
    for (const auto &LabelItem : S->getCaseLabelItems()) {
      if (auto *CasePattern = LabelItem.getPattern()) {
        for(auto const& node : PrintDecl(OS).flattenPattern(CasePattern)) {
          if(auto *namedPattern = dyn_cast<NamedPattern>(node.second)) {
            OS << "\nconst " << getName(namedPattern->getSingleVar()) << " = " << matchNameReplacement(node.first);
          }
        }
      }
    }
  }
  bool hasFallThrough(CaseStmt *S) {
    if(auto *body = dyn_cast<BraceStmt>(S->getBody())) {
      if(body->getElements().size()) {
        if(auto *anyStmt = body->getElements().back().dyn_cast<Stmt*>()) {
          if(auto *fallthroughStmt = dyn_cast<FallthroughStmt>(anyStmt)) {
            return true;
          }
        }
      }
    }
    return false;
  }
  
  void visitSwitchStmt(SwitchStmt *S) {
    /*printCommon(S, "switch_stmt") << '\n';
    printRec(S->getSubjectExpr());
    for (auto N : S->getRawCases()) {
      OS << '\n';
      if (N.is<Stmt*>())
        printRec(N.get<Stmt*>());
      else
        printRec(N.get<Decl*>());
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << "const $match = ";
    printRec(S->getSubjectExpr());

    auto switchCases = S->getRawCases();
    for(unsigned i = 0; i < switchCases.size(); i++) {
      OS << '\n';
      if(i > 0) OS << "else ";
      unsigned j = 0;
      for(; j < switchCases.size() - i; j++) {
        //if the i+j'th case doesn't have fallthrough, break
        if(!hasFallThrough(getCase(switchCases[i + j]))) break;
      }
      OS << "if((";
      for(unsigned k = i; k <= i + j; k++) {
        if(k > i) OS << ") || (";
        printSwitchConditions(getCase(switchCases[k]));
      }
      OS << ")) {";
      for(unsigned k = i; k <= i + j; k++) {
        if(k < i + j) {
          OS << "if((";
          for(unsigned l = i; l <= k; l++) {
            if(l > i) OS << ") || (";
            printSwitchConditions(getCase(switchCases[l]));
          }
          OS << ")) {";
        }
        printSwitchDeclarations(getCase(switchCases[k]));
        printRec(getCase(switchCases[k])->getBody());
        if(k < i + j) OS << "\n}";
      }
      OS << "\n}";
      i += j;
    }
  }
  void visitCaseStmt(CaseStmt *S) {
    /*printCommon(S, "case_stmt");
    if (S->hasUnknownAttr())
      OS << " @unknown";
    for (const auto &LabelItem : S->getCaseLabelItems()) {
      OS << '\n';
      OS.indent(Indent + 2);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      PrintWithColorRAII(OS, StmtColor) << "case_label_item";
      if (LabelItem.isDefault())
        OS << " default";
      if (auto *CasePattern = LabelItem.getPattern()) {
        OS << '\n';
        printRec(CasePattern);
      }
      if (auto *Guard = LabelItem.getGuardExpr()) {
        OS << '\n';
        Guard->dump(OS, Indent+4);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    OS << '\n';
    printRec(S->getBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitFailStmt(FailStmt *S) {
    /*printCommon(S, "fail_stmt");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << "return (this.$failed = true)";
  }

  void visitThrowStmt(ThrowStmt *S) {
    printCommon(S, "throw_stmt") << '\n';
    printRec(S->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitPoundAssertStmt(PoundAssertStmt *S) {
    printCommon(S, "pound_assert");
    OS << " message=" << QuotedString(S->getMessage()) << "\n";
    printRec(S->getCondition());
    OS << ")";
  }

  void visitDoCatchStmt(DoCatchStmt *S) {
    printCommon(S, "do_catch_stmt") << '\n';
    printRec(S->getBody());
    OS << '\n';
    Indent += 2;
    visitCatches(S->getCatches());
    Indent -= 2;
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitCatches(ArrayRef<CatchStmt*> clauses) {
    for (auto clause : clauses) {
      visitCatchStmt(clause);
    }
  }
  void visitCatchStmt(CatchStmt *clause) {
    printCommon(clause, "catch") << '\n';
    printRec(clause->getErrorPattern());
    if (auto guard = clause->getGuardExpr()) {
      OS << '\n';
      printRec(guard);
    }
    OS << '\n';
    printRec(clause->getBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
};

} // end anonymous namespace

void Stmt::dump() const {
  dump(llvm::errs());
  llvm::errs() << '\n';
}

void Stmt::dump(raw_ostream &OS, const ASTContext *Ctx, unsigned Indent) const {
  PrintStmt(OS, Ctx, Indent).visit(const_cast<Stmt*>(this));
}

//===----------------------------------------------------------------------===//
// Printing for Expr and all subclasses.
//===----------------------------------------------------------------------===//

namespace {
/// PrintExpr - Visitor implementation of Expr::dump.
class PrintExpr : public ExprVisitor<PrintExpr> {
public:
  raw_ostream &OS;
  llvm::function_ref<Type(const Expr *)> GetTypeOfExpr;
  llvm::function_ref<Type(const TypeLoc &)> GetTypeOfTypeLoc;
  unsigned Indent;

  PrintExpr(raw_ostream &os,
            llvm::function_ref<Type(const Expr *)> getTypeOfExpr,
            llvm::function_ref<Type(const TypeLoc &)> getTypeOfTypeLoc,
            unsigned indent)
      : OS(os), GetTypeOfExpr(getTypeOfExpr),
        GetTypeOfTypeLoc(getTypeOfTypeLoc), Indent(indent) {}

  void printRec(Expr *E) {
    Indent += 2;
    if (E)
      visit(E);
    else
      OS.indent(Indent) << "(**NULL EXPRESSION**)";
    Indent -= 2;
  }

  void printRecLabeled(Expr *E, StringRef label) {
    Indent += 2;
    OS.indent(Indent);
    PrintWithColorRAII(OS, ParenthesisColor) << '(';
    PrintWithColorRAII(OS, ExprColor) << label;
    OS << '\n';
    printRec(E);
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
    Indent -= 2;
  }

  /// FIXME: This should use ExprWalker to print children.

  void printRec(Decl *D) { D->dump(OS, Indent + 2); }
  void printRec(Stmt *S, const ASTContext &Ctx) { S->dump(OS, &Ctx, Indent + 2); }
  void printRec(const Pattern *P) {
    PrintPattern(OS, Indent+2).visit(const_cast<Pattern *>(P));
  }
  void printRec(TypeRepr *T);
  void printRec(ProtocolConformanceRef conf) {
    conf.dump(OS, Indent + 2);
  }

  void printDeclRef(ConcreteDeclRef declRef) {
    declRef.dump(PrintWithColorRAII(OS, DeclColor).getOS());
  }

  raw_ostream &printCommon(Expr *E, const char *C) {
    OS.indent(Indent);
    PrintWithColorRAII(OS, ParenthesisColor) << '(';
    PrintWithColorRAII(OS, ExprColor) << C;

    if (E->isImplicit())
      PrintWithColorRAII(OS, ExprModifierColor) << " implicit";
    PrintWithColorRAII(OS, TypeColor) << " type='" << GetTypeOfExpr(E) << '\'';

    // If we have a source range and an ASTContext, print the source range.
    if (auto Ty = GetTypeOfExpr(E)) {
      auto &Ctx = Ty->getASTContext();
      auto L = E->getLoc();
      if (L.isValid()) {
        PrintWithColorRAII(OS, LocationColor) << " location=";
        L.print(PrintWithColorRAII(OS, LocationColor).getOS(), Ctx.SourceMgr);
      }

      auto R = E->getSourceRange();
      if (R.isValid()) {
        PrintWithColorRAII(OS, RangeColor) << " range=";
        R.print(PrintWithColorRAII(OS, RangeColor).getOS(),
                Ctx.SourceMgr, /*PrintText=*/false);
      }
    }

    if (E->TrailingSemiLoc.isValid())
      OS << " trailing_semi";

    return OS;
  }
  
  void printSemanticExpr(Expr * semanticExpr) {
    if (semanticExpr == nullptr) {
      return;
    }
    
    OS << '\n';
    printRecLabeled(semanticExpr, "semantic_expr");
  }

  void visitErrorExpr(ErrorExpr *E) {
    printCommon(E, "error_expr");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitCodeCompletionExpr(CodeCompletionExpr *E) {
    printCommon(E, "code_completion_expr");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitNilLiteralExpr(NilLiteralExpr *E) {
    printCommon(E, "nil_literal_expr");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitIntegerLiteralExpr(IntegerLiteralExpr *E) {
    /*printCommon(E, "integer_literal_expr");
    if (E->isNegative())
      PrintWithColorRAII(OS, LiteralValueColor) << " negative";
    PrintWithColorRAII(OS, LiteralValueColor) << " value=";
    Type T = GetTypeOfExpr(E);
    if (T.isNull() || !T->is<BuiltinIntegerType>())
      PrintWithColorRAII(OS, LiteralValueColor) << E->getDigitsText();
    else
      PrintWithColorRAII(OS, LiteralValueColor) << E->getValue();
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << E->getValue();
  }
  void visitFloatLiteralExpr(FloatLiteralExpr *E) {
    /*printCommon(E, "float_literal_expr");
    PrintWithColorRAII(OS, LiteralValueColor)
      << " value=" << E->getDigitsText();
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << E->getDigitsText();
  }

  void visitBooleanLiteralExpr(BooleanLiteralExpr *E) {
    /*printCommon(E, "boolean_literal_expr");
    PrintWithColorRAII(OS, LiteralValueColor)
      << " value=" << (E->getValue() ? "true" : "false");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << (E->getValue() ? "true" : "false");
  }

  void visitStringLiteralExpr(StringLiteralExpr *E) {
    /*printCommon(E, "string_literal_expr");
    PrintWithColorRAII(OS, LiteralValueColor) << " encoding="
      << getStringLiteralExprEncodingString(E->getEncoding())
      << " value=" << QuotedString(E->getValue())
      << " builtin_initializer=";
    E->getBuiltinInitializer().dump(
      PrintWithColorRAII(OS, LiteralValueColor).getOS());
    PrintWithColorRAII(OS, LiteralValueColor) << " initializer=";
    E->getInitializer().dump(PrintWithColorRAII(OS, LiteralValueColor).getOS());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << QuotedString(E->getValue());
  }
  void visitInterpolatedStringLiteralExpr(InterpolatedStringLiteralExpr *E) {
    /*printCommon(E, "interpolated_string_literal_expr");
    PrintWithColorRAII(OS, LiteralValueColor) << " literal_capacity="
      << E->getLiteralCapacity() << " interpolation_count="
      << E->getInterpolationCount() << '\n';
    printRec(E->getAppendingExpr());
    printSemanticExpr(E->getSemanticExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << "((";
    bool isFirst = true;
    E->forEachSegment(E->getAppendingExpr()->getVar()->getDeclContext()->getASTContext(),
        [&](bool isInterpolation, CallExpr *segment) -> void {
        if(isFirst) isFirst = false;
        else OS << ") + (";
        printRec(segment->getArg());
    });
    OS << "))";
  }
  void visitMagicIdentifierLiteralExpr(MagicIdentifierLiteralExpr *E) {
    printCommon(E, "magic_identifier_literal_expr")
      << " kind=" << getMagicIdentifierLiteralExprKindString(E->getKind());

    if (E->isString()) {
      OS << " encoding="
         << getStringLiteralExprEncodingString(E->getStringEncoding())
         << " builtin_initializer=";
      E->getBuiltinInitializer().dump(OS);
      OS << " initializer=";
      E->getInitializer().dump(OS);
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitObjectLiteralExpr(ObjectLiteralExpr *E) {
    printCommon(E, "object_literal")
      << " kind='" << E->getLiteralKindPlainName() << "'";
    printArgumentLabels(E->getArgumentLabels());
    OS << "\n";
    printRec(E->getArg());
    printSemanticExpr(E->getSemanticExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitDiscardAssignmentExpr(DiscardAssignmentExpr *E) {
    printCommon(E, "discard_assignment_expr");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitDeclRefExpr(DeclRefExpr *E) {
    /*printCommon(E, "declref_expr");
    PrintWithColorRAII(OS, DeclColor) << " decl=";
    printDeclRef(E->getDeclRef());
    if (E->getAccessSemantics() != AccessSemantics::Ordinary)
      PrintWithColorRAII(OS, AccessLevelColor)
        << " " << getAccessSemanticsString(E->getAccessSemantics());
    PrintWithColorRAII(OS, ExprModifierColor)
      << " function_ref=" << getFunctionRefKindStr(E->getFunctionRefKind());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    std::string string;
    std::string replacement = getReplacement(E->getDecl(), E->getDeclRef());
    if(replacement.length()) {
      string = replacement;
    }
    else {
      string = getName(E->getDecl());
    }
    
    bool isSelf = false;
    if (auto *var = dyn_cast<VarDecl>(E->getDecl())) {
      isSelf = var->isSelfParameter();
    }
    if(isSelf) {
      if(lAssignmentExpr == E) {
        string = "Object.assign(this, #ASS)";
      }
      else {
        string = "this";
      }
    }
    
    OS << string;
  }
  void visitSuperRefExpr(SuperRefExpr *E) {
    /*printCommon(E, "super_ref_expr");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << "super";
  }

  void visitTypeExpr(TypeExpr *E) {
    /*printCommon(E, "type_expr");
    PrintWithColorRAII(OS, TypeReprColor) << " typerepr='";
    if (E->getTypeRepr())
      E->getTypeRepr()->print(PrintWithColorRAII(OS, TypeReprColor).getOS());
    else
      PrintWithColorRAII(OS, TypeReprColor) << "<<NULL>>";
    PrintWithColorRAII(OS, TypeReprColor) << "'";
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << getType(GetTypeOfExpr(E));
  }

  void visitOtherConstructorDeclRefExpr(OtherConstructorDeclRefExpr *E) {
    /*printCommon(E, "other_constructor_ref_expr");
    PrintWithColorRAII(OS, DeclColor) << " decl=";
    printDeclRef(E->getDeclRef());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << getName(E->getDecl());
  }
  void visitOverloadedDeclRefExpr(OverloadedDeclRefExpr *E) {
    printCommon(E, "overloaded_decl_ref_expr");
    PrintWithColorRAII(OS, IdentifierColor) << " name="
      << E->getDecls()[0]->getBaseName();
    PrintWithColorRAII(OS, ExprModifierColor)
      << " number_of_decls=" << E->getDecls().size()
      << " function_ref=" << getFunctionRefKindStr(E->getFunctionRefKind())
      << " decls=[\n";
    interleave(E->getDecls(),
               [&](ValueDecl *D) {
                 OS.indent(Indent + 2);
                 D->dumpRef(PrintWithColorRAII(OS, DeclModifierColor).getOS());
               },
               [&] { PrintWithColorRAII(OS, DeclModifierColor) << ",\n"; });
    PrintWithColorRAII(OS, ExprModifierColor) << "]";
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitUnresolvedDeclRefExpr(UnresolvedDeclRefExpr *E) {
    printCommon(E, "unresolved_decl_ref_expr");
    PrintWithColorRAII(OS, IdentifierColor) << " name=" << E->getName();
    PrintWithColorRAII(OS, ExprModifierColor)
      << " function_ref=" << getFunctionRefKindStr(E->getFunctionRefKind());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitUnresolvedSpecializeExpr(UnresolvedSpecializeExpr *E) {
    printCommon(E, "unresolved_specialize_expr") << '\n';
    printRec(E->getSubExpr());
    for (TypeLoc T : E->getUnresolvedParams()) {
      OS << '\n';
      printRec(T.getTypeRepr());
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitMemberRefExpr(MemberRefExpr *E) {
    /*printCommon(E, "member_ref_expr");
    PrintWithColorRAII(OS, DeclColor) << " decl=";
    printDeclRef(E->getMember());
    if (E->getAccessSemantics() != AccessSemantics::Ordinary)
      PrintWithColorRAII(OS, AccessLevelColor)
        << " " << getAccessSemanticsString(E->getAccessSemantics());
    if (E->isSuper())
      OS << " super";

    OS << '\n';
    printRec(E->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    std::string rString;
    std::string replacement = getReplacement(E->getMember().getDecl(), E->getMember());
    if(replacement.length()) {
      rString = replacement;
    }
    else {
      rString = getName(E->getMember().getDecl());
      bool isSuper = false;
      if(auto *superRefExpr = dyn_cast<SuperRefExpr>(skipInOutExpr(E->getBase()))) {
        isSuper = true;
      }
      if(isSuper) {
        if(lAssignmentExpr == E) {
          rString += "$set(#ASS)";
        }
        else {
          rString += "$get()";
        }
      }
    }
    
    if(rString.find("#L") == std::string::npos) rString = "#L." + rString;
    
    OS << std::regex_replace(rString, std::regex("\\^?#L"), regex_escape(dumpToStr(skipInOutExpr(E->getBase()))));
  }
  void visitDynamicMemberRefExpr(DynamicMemberRefExpr *E) {
    printCommon(E, "dynamic_member_ref_expr");
    PrintWithColorRAII(OS, DeclColor) << " decl=";
    E->getMember().dump(OS);
    OS << '\n';
    printRec(E->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
    printCommon(E, "unresolved_member_expr")
      << " name='" << E->getName() << "'";
    printArgumentLabels(E->getArgumentLabels());
    if (E->getArgument()) {
      OS << '\n';
      printRec(E->getArgument());
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitDotSelfExpr(DotSelfExpr *E) {
    printCommon(E, "dot_self_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitParenExpr(ParenExpr *E) {
    /*printCommon(E, "paren_expr");
    if (E->hasTrailingClosure())
      OS << " trailing-closure";
    OS << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitTupleExpr(TupleExpr *E) {
    /*printCommon(E, "tuple_expr");
    if (E->hasTrailingClosure())
      OS << " trailing-closure";

    if (E->hasElementNames()) {
      PrintWithColorRAII(OS, IdentifierColor) << " names=";

      interleave(E->getElementNames(),
                 [&](Identifier name) {
                   PrintWithColorRAII(OS, IdentifierColor)
                     << (name.empty()?"''":name.str());
                 },
                 [&] { PrintWithColorRAII(OS, IdentifierColor) << ","; });
    }

    for (unsigned i = 0, e = E->getNumElements(); i != e; ++i) {
      OS << '\n';
      if (E->getElement(i))
        printRec(E->getElement(i));
      else
        OS.indent(Indent+2) << "<<tuple element default value>>";
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    bool wrap = !E->isImplicit() && E != functionArgsCall;
    
    if (wrap) OS << "{";
    for (unsigned i = 0, e = E->getNumElements(); i != e; ++i) {
      if (i) OS << ", ";
      if (wrap) {
        OS << std::to_string(i) << ": ";
      }
      OS << dumpToStr(E->getElement(i));
    }
    if (wrap) OS << "}";
  }
  void visitArrayExpr(ArrayExpr *E) {
    /*printCommon(E, "array_expr");
    for (auto elt : E->getElements()) {
      OS << '\n';
      printRec(elt);
    }
    printSemanticExpr(E->getSemanticExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    bool isSet = false;
    if(auto *nominalDecl = GetTypeOfExpr(E)->getNominalOrBoundGenericNominal()) {
      isSet = getMemberIdentifier(nominalDecl) == "Swift.(file).Set";
    }
    
    if(isSet) OS << "new Set(";
    OS << "[";
    bool first = true;
    for (auto elt : E->getElements()) {
      if(first) first = false;
      else OS << ", ";
      printRec(elt);
    }
    OS << "]";
    if(isSet) OS << ")";
  }
  void visitDictionaryExpr(DictionaryExpr *E) {
    /*printCommon(E, "dictionary_expr");
    for (auto elt : E->getElements()) {
      OS << '\n';
      printRec(elt);
    }
    printSemanticExpr(E->getSemanticExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << "new Map([";
    bool first = true;
    for (auto elt : E->getElements()) {
      if(first) first = false;
      else OS << ", ";
      OS << "[";
      printRec(elt);
      OS << "]";
    }
    OS << "])";
  }
  void visitSubscriptExpr(SubscriptExpr *E) {
    /*printCommon(E, "subscript_expr");
    if (E->getAccessSemantics() != AccessSemantics::Ordinary)
      PrintWithColorRAII(OS, AccessLevelColor)
        << " " << getAccessSemanticsString(E->getAccessSemantics());
    if (E->isSuper())
      OS << " super";
    if (E->hasDecl()) {
      PrintWithColorRAII(OS, DeclColor) << " decl=";
      printDeclRef(E->getDecl());
    }
    printArgumentLabels(E->getArgumentLabels());
    OS << '\n';
    printRec(E->getBase());
    OS << '\n';
    printRec(E->getIndex());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    std::string string;
    std::string replacement = getReplacement(E->getDecl().getDecl(), E->getDecl(), lAssignmentExpr == E);
    if(replacement.length()) {
      string = replacement;
    }
    else {
      string = "#L." + getName(E->getMember().getDecl()) + "$";
      if(lAssignmentExpr == E) {
        string += "set(#ASS, #AA)";
      }
      else {
        string += "get(#AA)";
      }
    }
    
    string = std::regex_replace(string, std::regex("\\^?#L"), regex_escape(dumpToStr(skipInOutExpr(E->getBase()))));
    
    functionArgsCall = skipWrapperExpressions(E->getIndex());
    string = std::regex_replace(string, std::regex("\\^?#AA"), regex_escape(dumpToStr(skipInOutExpr(E->getIndex()))));
    
    OS << string;
  }
  void visitKeyPathApplicationExpr(KeyPathApplicationExpr *E) {
    printCommon(E, "keypath_application_expr");
    OS << '\n';
    printRec(E->getBase());
    OS << '\n';
    printRec(E->getKeyPath());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitDynamicSubscriptExpr(DynamicSubscriptExpr *E) {
    printCommon(E, "dynamic_subscript_expr");
    PrintWithColorRAII(OS, DeclColor) << " decl=";
    printDeclRef(E->getMember());
    printArgumentLabels(E->getArgumentLabels());
    OS << '\n';
    printRec(E->getBase());
    OS << '\n';
    printRec(E->getIndex());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitUnresolvedDotExpr(UnresolvedDotExpr *E) {
    printCommon(E, "unresolved_dot_expr")
      << " field '" << E->getName() << "'";
    PrintWithColorRAII(OS, ExprModifierColor)
      << " function_ref=" << getFunctionRefKindStr(E->getFunctionRefKind());
    if (E->getBase()) {
      OS << '\n';
      printRec(E->getBase());
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitTupleElementExpr(TupleElementExpr *E) {
    /*printCommon(E, "tuple_element_expr")
      << " field #" << E->getFieldNumber() << '\n';
    printRec(E->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    printRec(E->getBase());
    
    OS << "[\"" << std::to_string(E->getFieldNumber()) << "\"]";
  }
  void visitTupleShuffleExpr(TupleShuffleExpr *E) {
    /*printCommon(E, "tuple_shuffle_expr");
    switch (E->getTypeImpact()) {
    case TupleShuffleExpr::ScalarToTuple:
      OS << " scalar_to_tuple";
      break;
    case TupleShuffleExpr::TupleToTuple:
      OS << " tuple_to_tuple";
      break;
    case TupleShuffleExpr::TupleToScalar:
      OS << " tuple_to_scalar";
      break;
    }
    OS << " elements=[";
    for (unsigned i = 0, e = E->getElementMapping().size(); i != e; ++i) {
      if (i) OS << ", ";
      OS << E->getElementMapping()[i];
    }
    OS << "]";
    OS << " variadic_sources=[";
    interleave(E->getVariadicArgs(),
               [&](unsigned source) {
                 OS << source;
               },
               [&] { OS << ", "; });
    OS << "]";

    if (auto defaultArgsOwner = E->getDefaultArgsOwner()) {
      OS << " default_args_owner=";
      defaultArgsOwner.dump(OS);
    }

    OS << "\n";*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitUnresolvedTypeConversionExpr(UnresolvedTypeConversionExpr *E) {
    printCommon(E, "unresolvedtype_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitFunctionConversionExpr(FunctionConversionExpr *E) {
    /*printCommon(E, "function_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    std::string string = dumpToStr(E->getSubExpr());
    if(auto *dotSyntaxCallExpr = dyn_cast<DotSyntaxCallExpr>(E->getSubExpr())) {
      if(auto *declRefExpr = dyn_cast<DeclRefExpr>(dotSyntaxCallExpr->getFn())) {
        //an operator closure
        string = string.substr(string.find("#PRENOL") + 7/*"#PRENOL".count()*/);
        string = std::regex_replace(string, std::regex("\\^?#A0"), "a");
        string = std::regex_replace(string, std::regex("\\^?#A1"), "b");
        string = "(a, b) => " + string;
      }
    }
    OS << string;
  }
  void visitCovariantFunctionConversionExpr(CovariantFunctionConversionExpr *E){
    printCommon(E, "covariant_function_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitCovariantReturnConversionExpr(CovariantReturnConversionExpr *E){
    /*printCommon(E, "covariant_return_conversion_expr") << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitImplicitlyUnwrappedFunctionConversionExpr(
      ImplicitlyUnwrappedFunctionConversionExpr *E) {
    printCommon(E, "implicitly_unwrapped_function_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitErasureExpr(ErasureExpr *E) {
    /*printCommon(E, "erasure_expr") << '\n';
    for (auto conf : E->getConformances()) {
      printRec(conf);
      OS << '\n';
    }*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitAnyHashableErasureExpr(AnyHashableErasureExpr *E) {
    printCommon(E, "any_hashable_erasure_expr") << '\n';
    printRec(E->getConformance());
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitConditionalBridgeFromObjCExpr(ConditionalBridgeFromObjCExpr *E) {
    printCommon(E, "conditional_bridge_from_objc_expr") << " conversion=";
    printDeclRef(E->getConversion());
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitBridgeFromObjCExpr(BridgeFromObjCExpr *E) {
    printCommon(E, "bridge_from_objc_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitBridgeToObjCExpr(BridgeToObjCExpr *E) {
    printCommon(E, "bridge_to_objc_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitLoadExpr(LoadExpr *E) {
    //I think we can just ignore this, as it's just a wrapper
    /*printCommon(E, "load_expr") << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitMetatypeConversionExpr(MetatypeConversionExpr *E) {
    printCommon(E, "metatype_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitCollectionUpcastConversionExpr(CollectionUpcastConversionExpr *E) {
    printCommon(E, "collection_upcast_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    if (auto keyConversion = E->getKeyConversion()) {
      OS << '\n';
      printRecLabeled(keyConversion.Conversion, "key_conversion");
    }
    if (auto valueConversion = E->getValueConversion()) {
      OS << '\n';
      printRecLabeled(valueConversion.Conversion, "value_conversion");
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitDerivedToBaseExpr(DerivedToBaseExpr *E) {
    /*printCommon(E, "derived_to_base_expr") << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitArchetypeToSuperExpr(ArchetypeToSuperExpr *E) {
    printCommon(E, "archetype_to_super_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitInjectIntoOptionalExpr(InjectIntoOptionalExpr *E) {
    /*printCommon(E, "inject_into_optional") << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitClassMetatypeToObjectExpr(ClassMetatypeToObjectExpr *E) {
    printCommon(E, "class_metatype_to_object") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitExistentialMetatypeToObjectExpr(ExistentialMetatypeToObjectExpr *E) {
    printCommon(E, "existential_metatype_to_object") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitProtocolMetatypeToObjectExpr(ProtocolMetatypeToObjectExpr *E) {
    printCommon(E, "protocol_metatype_to_object") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitInOutToPointerExpr(InOutToPointerExpr *E) {
    printCommon(E, "inout_to_pointer")
      << (E->isNonAccessing() ? " nonaccessing" : "") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitArrayToPointerExpr(ArrayToPointerExpr *E) {
    printCommon(E, "array_to_pointer")
      << (E->isNonAccessing() ? " nonaccessing" : "") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitStringToPointerExpr(StringToPointerExpr *E) {
    printCommon(E, "string_to_pointer") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitPointerToPointerExpr(PointerToPointerExpr *E) {
    printCommon(E, "pointer_to_pointer") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitForeignObjectConversionExpr(ForeignObjectConversionExpr *E) {
    printCommon(E, "foreign_object_conversion") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitUnevaluatedInstanceExpr(UnevaluatedInstanceExpr *E) {
    printCommon(E, "unevaluated_instance") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitInOutExpr(InOutExpr *E) {
    /*printCommon(E, "inout_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    std::string getStr = dumpToStr(E->getSubExpr());
    
    std::string setStr = handleLAssignment(E->getSubExpr(), handleRAssignment(E->getSubExpr(), "$val"));
    
    OS << "{get: () => " << getStr << ", set: $val => " << setStr << "}";
  }

  void visitVarargExpansionExpr(VarargExpansionExpr *E) {
    printCommon(E, "vararg_expansion_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitForceTryExpr(ForceTryExpr *E) {
    printCommon(E, "force_try_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitOptionalTryExpr(OptionalTryExpr *E) {
    printCommon(E, "optional_try_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitTryExpr(TryExpr *E) {
    printCommon(E, "try_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitSequenceExpr(SequenceExpr *E) {
    printCommon(E, "sequence_expr");
    for (unsigned i = 0, e = E->getNumElements(); i != e; ++i) {
      OS << '\n';
      printRec(E->getElement(i));
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitCaptureListExpr(CaptureListExpr *E) {
    printCommon(E, "capture_list");
    for (auto capture : E->getCaptureList()) {
      OS << '\n';
      Indent += 2;
      printRec(capture.Var);
      printRec(capture.Init);
      Indent -= 2;
    }
    printRec(E->getClosureBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  llvm::raw_ostream &printClosure(AbstractClosureExpr *E, char const *name) {
    printCommon(E, name);
    PrintWithColorRAII(OS, DiscriminatorColor)
      << " discriminator=" << E->getDiscriminator();
    if (!E->getCaptureInfo().isTrivial()) {
      OS << " ";
      E->getCaptureInfo().print(PrintWithColorRAII(OS, CapturesColor).getOS());
    }
    // Printing a function type doesn't indicate whether it's escaping because it doesn't
    // matter in 99% of contexts. AbstractClosureExpr nodes are one of the only exceptions.
    if (auto Ty = GetTypeOfExpr(E))
      if (!Ty->getAs<AnyFunctionType>()->getExtInfo().isNoEscape())
        PrintWithColorRAII(OS, ClosureModifierColor) << " escaping";

    return OS;
  }

  void visitClosureExpr(ClosureExpr *E) {
    /*printClosure(E, "closure_expr");
    if (E->hasSingleExpressionBody())
      PrintWithColorRAII(OS, ClosureModifierColor) << " single-expression";

    if (E->getParameters()) {
      OS << '\n';
      PrintDecl(OS, Indent+2).printParameterList(E->getParameters(), &E->getASTContext());
    }

    OS << '\n';
    if (E->hasSingleExpressionBody()) {
      printRec(E->getSingleExpressionBody());
    } else {
      printRec(E->getBody(), E->getASTContext());
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << "(";
    OS << PrintDecl(OS).printFuncSignature(E->getParameters(), nullptr);
    OS << " => ";
    if (E->hasSingleExpressionBody()) {
      printRec(E->getSingleExpressionBody());
    } else {
      OS << "{ ";
      printRec(E->getBody(), E->getASTContext());
      OS << " }";
    }
    OS << ")";
  }
  void visitAutoClosureExpr(AutoClosureExpr *E) {
    /*printClosure(E, "autoclosure_expr") << '\n';

    if (E->getParameters()) {
      OS << '\n';
      PrintDecl(OS, Indent+2).printParameterList(E->getParameters(), &E->getASTContext());
    }

    OS << '\n';
    printRec(E->getSingleExpressionBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    printRec(E->getSingleExpressionBody());
  }

  void visitDynamicTypeExpr(DynamicTypeExpr *E) {
    printCommon(E, "metatype_expr");
    OS << '\n';
    printRec(E->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitOpaqueValueExpr(OpaqueValueExpr *E) {
    /*printCommon(E, "opaque_value_expr") << " @ " << (void*)E;
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    printRec(opaqueValueReplacements[E]);
  }

  void printArgumentLabels(ArrayRef<Identifier> argLabels) {
    PrintWithColorRAII(OS, ArgumentsColor) << " arg_labels=";
    for (auto label : argLabels) {
      PrintWithColorRAII(OS, ArgumentsColor)
        << (label.empty() ? "_" : label.str()) << ":";
    }
  }

  void printApplyExpr(Expr *lExpr, Expr *rExpr, std::string rName = "#AA", std::string defaultSuffix = "(#AA)") {
    /*printCommon(E, NodeName);
    if (E->isSuper())
      PrintWithColorRAII(OS, ExprModifierColor) << " super";
    if (E->isThrowsSet()) {
      PrintWithColorRAII(OS, ExprModifierColor)
        << (E->throws() ? " throws" : " nothrow");
    }
    if (auto call = dyn_cast<CallExpr>(E))
      printArgumentLabels(call->getArgumentLabels());

    OS << '\n';
    printRec(E->getFn());
    OS << '\n';
    printRec(E->getArg());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    if (auto rTuple = dyn_cast<TupleExpr>(rExpr)) {
      if (rTuple->getNumElements() == 1) {
        if (auto intExpr = dyn_cast<IntegerLiteralExpr>(rTuple->getElement(0))) {
          return printRec(intExpr);
        }
        if (auto floatExpr = dyn_cast<FloatLiteralExpr>(rTuple->getElement(0))) {
          return printRec(floatExpr);
        }
        if (auto boolExpr = dyn_cast<BooleanLiteralExpr>(rTuple->getElement(0))) {
          return printRec(boolExpr);
        }
      }
    }
    
    std::string lString;
    
    if (auto lConstructor = dyn_cast<ConstructorRefCallExpr>(lExpr)) {
      lString = "new " + dumpToStr(lConstructor->getArg());
      if (auto initDeclRef = dyn_cast<DeclRefExpr>(lConstructor->getFn())) {
        if (auto initDecl = dyn_cast<ConstructorDecl>(initDeclRef->getDecl())) {
          std::string replacement = getReplacement(initDecl);
          if(replacement.length()) {
            lString = replacement;
            defaultSuffix = "";
          }
          else {
            defaultSuffix = "('" + getName(initDecl) + "', #AA)";
            if (initDecl->getFailability() != OTK_None) {
              lString = "_.failableInit(" + lString;
              defaultSuffix += ")";
            }
          }
        }
      }
    }
    else {
      lString = dumpToStr(lExpr);
    }
    
    bool isAss = false, isAssSkipInOutExpr = false;
    if(lString.find("#ISASS") != std::string::npos) {
      isAss = true;
      isAssSkipInOutExpr = lString.find("^#A0") != std::string::npos;
      lString = std::regex_replace(lString, std::regex("#ISASS"), "");
    }
    
    if(lString.find("#PRENOL") != std::string::npos) {
      lString = lString.substr(lString.find("#PRENOL") + 7/*"#PRENOL".count()*/);
    }
    
    std::string lrString;
    if(std::regex_search(lString, std::regex("#A[0-9]"))) {
      //if the replacement references specific arguments e.g. #A0 #A1
      //we can't accept the right-hand side as a single string; we need an array of arguments
      TupleExpr *tuple = (TupleExpr*)rExpr;
      lrString = lString;
      for (unsigned i = 0, e = tuple->getNumElements(); i != e; ++i) {
        std::string replacement = dumpToStr(lrString.find("^#A" + std::to_string(i)) != std::string::npos ? skipInOutExpr(tuple->getElement(i)) : tuple->getElement(i));
        lrString = std::regex_replace(lrString, std::regex("\\^?#A" + std::to_string(i)), regex_escape(replacement));
      }
      if(isAss) {
        lrString = handleLAssignment(isAssSkipInOutExpr ? skipInOutExpr(tuple->getElement(0)) : tuple->getElement(0), lrString);
      }
    }
    else {
      functionArgsCall = skipWrapperExpressions(rExpr);
      std::string rString = dumpToStr(rExpr);
      //that's possibly bodgy; if the right-hand side has replacements, we expect it to include an #L
      //we replace the #L with left-hand side there
      if(rString.find("#L") != std::string::npos) {
        lrString = std::regex_replace(rString, std::regex("#L"), regex_escape(lString));
      }
      else if(rString.find("#NOL") != std::string::npos) {
        lrString = std::regex_replace(rString, std::regex("#NOL"), "");
      }
      //otherwise we replace #R in left-hand side; if no #R present, we assume the default .#R or (#AA)
      else {
        if(lString.find(rName) == std::string::npos) lString += defaultSuffix;
        lrString = std::regex_replace(lString, std::regex(rName), regex_escape(rString));
      }
    }
    
    OS << lrString;
  }

  void visitCallExpr(CallExpr *E) {
    printApplyExpr(E->getFn(), E->getArg());
  }
  void visitPrefixUnaryExpr(PrefixUnaryExpr *E) {
    printApplyExpr(E->getFn(), E->getArg());
  }
  void visitPostfixUnaryExpr(PostfixUnaryExpr *E) {
    printApplyExpr(E->getFn(), E->getArg());
  }
  void visitBinaryExpr(BinaryExpr *E) {
    printApplyExpr(E->getFn(), E->getArg());
  }
  void visitDotSyntaxCallExpr(DotSyntaxCallExpr *E) {
    printApplyExpr(skipInOutExpr(E->getArg()), E->getFn(), "#R", ".#R");
  }
  void visitConstructorRefCallExpr(ConstructorRefCallExpr *E) {
    printApplyExpr(E->getFn(), E->getArg());
  }
  void visitDotSyntaxBaseIgnoredExpr(DotSyntaxBaseIgnoredExpr *E) {
    printCommon(E, "dot_syntax_base_ignored") << '\n';
    printRec(E->getLHS());
    OS << '\n';
    printRec(E->getRHS());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void printExplicitCastExpr(ExplicitCastExpr *E, const char *name) {
    /*printCommon(E, name) << ' ';
    if (auto checkedCast = dyn_cast<CheckedCastExpr>(E))
      OS << getCheckedCastKindName(checkedCast->getCastKind()) << ' ';
    OS << "writtenType='";
    GetTypeOfTypeLoc(E->getCastTypeLoc()).print(OS);
    OS << "'\n";
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    printRec(E->getSubExpr());
  }
  void visitForcedCheckedCastExpr(ForcedCheckedCastExpr *E) {
    printExplicitCastExpr(E, "forced_checked_cast_expr");
  }
  void visitConditionalCheckedCastExpr(ConditionalCheckedCastExpr *E) {
    printExplicitCastExpr(E, "conditional_checked_cast_expr");
  }
  void visitIsExpr(IsExpr *E) {
    printExplicitCastExpr(E, "is_subtype_expr");
  }
  void visitCoerceExpr(CoerceExpr *E) {
    printExplicitCastExpr(E, "coerce_expr");
  }
  void visitArrowExpr(ArrowExpr *E) {
    printCommon(E, "arrow") << '\n';
    printRec(E->getArgsExpr());
    OS << '\n';
    printRec(E->getResultExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitRebindSelfInConstructorExpr(RebindSelfInConstructorExpr *E) {
    /*printCommon(E, "rebind_self_in_constructor_expr") << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitIfExpr(IfExpr *E) {
    /*printCommon(E, "if_expr") << '\n';
    printRec(E->getCondExpr());
    OS << '\n';
    printRec(E->getThenExpr());
    OS << '\n';
    printRec(E->getElseExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    OS << "(";
    printRec(E->getCondExpr());
    OS << " ? ";
    printRec(E->getThenExpr());
    OS << " : ";
    printRec(E->getElseExpr());
    OS << ")";
  }
  void visitAssignExpr(AssignExpr *E) {
    /*printCommon(E, "assign_expr") << '\n';
    printRec(E->getDest());
    OS << '\n';
    printRec(E->getSrc());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    std::string rStr = dumpToStr(E->getSrc());
    
    OS << handleLAssignment(E->getDest(), handleRAssignment(E->getSrc(), rStr));
  }
  void visitEnumIsCaseExpr(EnumIsCaseExpr *E) {
    printCommon(E, "enum_is_case_expr") << ' ' <<
      E->getEnumElement()->getName() << "\n";
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitUnresolvedPatternExpr(UnresolvedPatternExpr *E) {
    printCommon(E, "unresolved_pattern_expr") << '\n';
    printRec(E->getSubPattern());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitBindOptionalExpr(BindOptionalExpr *E) {
    /*printCommon(E, "bind_optional_expr")
      << " depth=" << E->getDepth() << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    auto tempVal = getTempVal(dumpToStr(E->getSubExpr()));
    std::string condition = "(" + tempVal.expr + " != null)";
    
    optionalCondition[optionalCondition.size() - 1] = optionalCondition.back().length() ? optionalCondition.back() + " && " + condition : condition;
    
    OS << tempVal.name;
  }
  void visitOptionalEvaluationExpr(OptionalEvaluationExpr *E) {
    /*printCommon(E, "optional_evaluation_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    optionalCondition.push_back("");
    std::string expr = dumpToStr(E->getSubExpr());
    
    /*bool isAssign = false;
    if (auto *subExpr = dyn_cast<InjectIntoOptionalExpr>(E->getSubExpr())) {
      if (auto *subSubExpr = dyn_cast<AssignExpr>(E->getSubExpr())) {
        isAssign = true;
      }
    }
    if(isAssign) {
      OS << "if(" << optionalCondition << ") {" << expr << "}";
    }
    else {*/
      OS << "((" << optionalCondition.back() << ") ? (" << expr << ") : null)";
    /*}*/
    optionalCondition.pop_back();
  }
  void visitForceValueExpr(ForceValueExpr *E) {
    //printCommon(E, "force_value_expr") << '\n';
    printRec(E->getSubExpr());
    //PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitOpenExistentialExpr(OpenExistentialExpr *E) {
    /*printCommon(E, "open_existential_expr") << '\n';
    printRec(E->getOpaqueValue());
    OS << '\n';
    printRec(E->getExistentialValue());
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    opaqueValueReplacements[E->getOpaqueValue()] = E->getExistentialValue();
    printRec(E->getSubExpr());
  }
  void visitMakeTemporarilyEscapableExpr(MakeTemporarilyEscapableExpr *E) {
    printCommon(E, "make_temporarily_escapable_expr") << '\n';
    printRec(E->getOpaqueValue());
    OS << '\n';
    printRec(E->getNonescapingClosureValue());
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitEditorPlaceholderExpr(EditorPlaceholderExpr *E) {
    printCommon(E, "editor_placeholder_expr") << '\n';
    auto *TyR = E->getTypeLoc().getTypeRepr();
    auto *ExpTyR = E->getTypeForExpansion();
    if (TyR)
      printRec(TyR);
    if (ExpTyR && ExpTyR != TyR) {
      OS << '\n';
      printRec(ExpTyR);
    }
    printSemanticExpr(E->getSemanticExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitLazyInitializerExpr(LazyInitializerExpr *E) {
    printCommon(E, "lazy_initializer_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitObjCSelectorExpr(ObjCSelectorExpr *E) {
    printCommon(E, "objc_selector_expr");
    OS << " kind=" << getObjCSelectorExprKindString(E->getSelectorKind());
    PrintWithColorRAII(OS, DeclColor) << " decl=";
    printDeclRef(E->getMethod());
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitKeyPathExpr(KeyPathExpr *E) {
    printCommon(E, "keypath_expr");
    if (E->isObjC())
      OS << " objc";
    for (auto &component : E->getComponents()) {
      OS << '\n';
      OS.indent(Indent + 2);
      OS << "(component=";
      switch (component.getKind()) {
      case KeyPathExpr::Component::Kind::Invalid:
        OS << "invalid ";
        break;

      case KeyPathExpr::Component::Kind::OptionalChain:
        OS << "optional_chain ";
        break;
        
      case KeyPathExpr::Component::Kind::OptionalForce:
        OS << "optional_force ";
        break;
        
      case KeyPathExpr::Component::Kind::OptionalWrap:
        OS << "optional_wrap ";
        break;
        
      case KeyPathExpr::Component::Kind::Property:
        OS << "property ";
        printDeclRef(component.getDeclRef());
        OS << " ";
        break;
      
      case KeyPathExpr::Component::Kind::Subscript:
        OS << "subscript ";
        printDeclRef(component.getDeclRef());
        OS << '\n';
        component.getIndexExpr()->dump(OS, Indent + 4);
        OS.indent(Indent + 4);
        break;
      
      case KeyPathExpr::Component::Kind::UnresolvedProperty:
        OS << "unresolved_property ";
        component.getUnresolvedDeclName().print(OS);
        OS << " ";
        break;
        
      case KeyPathExpr::Component::Kind::UnresolvedSubscript:
        OS << "unresolved_subscript";
        OS << '\n';
        component.getIndexExpr()->dump(OS, Indent + 4);
        OS.indent(Indent + 4);
        break;
      case KeyPathExpr::Component::Kind::Identity:
        OS << "identity";
        OS << '\n';
        break;
      }
      OS << "type=";
      component.getComponentType().print(OS);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    if (auto stringLiteral = E->getObjCStringLiteralExpr()) {
      OS << '\n';
      printRec(stringLiteral);
    }
    if (!E->isObjC()) {
      OS << "\n";
      if (auto root = E->getParsedRoot()) {
        printRec(root);
      } else {
        OS.indent(Indent + 2) << "<<null>>";
      }
      OS << "\n";
      if (auto path = E->getParsedPath()) {
        printRec(path);
      } else {
        OS.indent(Indent + 2) << "<<null>>";
      }
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitKeyPathDotExpr(KeyPathDotExpr *E) {
    printCommon(E, "key_path_dot_expr");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitTapExpr(TapExpr *E) {
    printCommon(E, "tap_expr");
    PrintWithColorRAII(OS, DeclColor) << " var=";
    printDeclRef(E->getVar());
    OS << '\n';

    printRec(E->getSubExpr());
    OS << '\n';

    printRec(E->getBody(), E->getVar()->getDeclContext()->getASTContext());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
};

} // end anonymous namespace

void Expr::dump() const {
  dump(llvm::errs());
  llvm::errs() << "\n";
}

void Expr::dump(raw_ostream &OS,
                 llvm::function_ref<Type(const Expr *)> getTypeOfExpr,
                 llvm::function_ref<Type(const TypeLoc &)> getTypeOfTypeLoc,
                 unsigned Indent) const {
  PrintExpr(OS, getTypeOfExpr, getTypeOfTypeLoc, Indent)
      .visit(const_cast<Expr *>(this));
}

void Expr::dump(raw_ostream &OS, unsigned Indent) const {
  auto getTypeOfExpr = [](const Expr *E) -> Type { return E->getType(); };
  auto getTypeOfTypeLoc = [](const TypeLoc &TL) -> Type {
    return TL.getType();
  };
  dump(OS, getTypeOfExpr, getTypeOfTypeLoc, Indent);
}

void Expr::print(ASTPrinter &Printer, const PrintOptions &Opts) const {
  // FIXME: Fully use the ASTPrinter.
  llvm::SmallString<128> Str;
  llvm::raw_svector_ostream OS(Str);
  dump(OS);
  Printer << OS.str();
}

//===----------------------------------------------------------------------===//
// Printing for TypeRepr and all subclasses.
//===----------------------------------------------------------------------===//

namespace {
class PrintTypeRepr : public TypeReprVisitor<PrintTypeRepr> {
public:
  raw_ostream &OS;
  unsigned Indent;

  PrintTypeRepr(raw_ostream &os, unsigned indent)
    : OS(os), Indent(indent) { }

  void printRec(Decl *D) { D->dump(OS, Indent + 2); }
  void printRec(Expr *E) { E->dump(OS, Indent + 2); }
  void printRec(TypeRepr *T) { PrintTypeRepr(OS, Indent + 2).visit(T); }

  raw_ostream &printCommon(const char *Name) {
    OS.indent(Indent);
    PrintWithColorRAII(OS, ParenthesisColor) << '(';
    PrintWithColorRAII(OS, TypeReprColor) << Name;
    return OS;
  }

  void visitErrorTypeRepr(ErrorTypeRepr *T) {
    printCommon("type_error");
  }

  void visitAttributedTypeRepr(AttributedTypeRepr *T) {
    printCommon("type_attributed") << " attrs=";
    T->printAttrs(OS);
    OS << '\n';
    printRec(T->getTypeRepr());
  }

  void visitIdentTypeRepr(IdentTypeRepr *T) {
    printCommon("type_ident");
    Indent += 2;
    for (auto comp : T->getComponentRange()) {
      OS << '\n';
      printCommon("component");
      PrintWithColorRAII(OS, IdentifierColor)
        << " id='" << comp->getIdentifier() << '\'';
      OS << " bind=";
      if (comp->isBound())
        comp->getBoundDecl()->dumpRef(OS);
      else OS << "none";
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
      if (auto GenIdT = dyn_cast<GenericIdentTypeRepr>(comp)) {
        for (auto genArg : GenIdT->getGenericArgs()) {
          OS << '\n';
          printRec(genArg);
        }
      }
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
    Indent -= 2;
  }

  void visitFunctionTypeRepr(FunctionTypeRepr *T) {
    printCommon("type_function");
    OS << '\n'; printRec(T->getArgsTypeRepr());
    if (T->throws())
      OS << " throws ";
    OS << '\n'; printRec(T->getResultTypeRepr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitArrayTypeRepr(ArrayTypeRepr *T) {
    printCommon("type_array") << '\n';
    printRec(T->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitDictionaryTypeRepr(DictionaryTypeRepr *T) {
    printCommon("type_dictionary") << '\n';
    printRec(T->getKey());
    OS << '\n';
    printRec(T->getValue());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitTupleTypeRepr(TupleTypeRepr *T) {
    printCommon("type_tuple");

    if (T->hasElementNames()) {
      OS << " names=";
      for (unsigned i = 0, end = T->getNumElements(); i != end; ++i) {
        if (i) OS << ",";
        auto name = T->getElementName(i);
        if (T->isNamedParameter(i))
          OS << (name.empty() ? "_" : "_ " + name.str());
        else
          OS << (name.empty() ? "''" : name.str());
      }
    }

    for (auto elem : T->getElements()) {
      OS << '\n';
      printRec(elem.Type);
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitCompositionTypeRepr(CompositionTypeRepr *T) {
    printCommon("type_composite");
    for (auto elem : T->getTypes()) {
      OS << '\n';
      printRec(elem);
    }
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitMetatypeTypeRepr(MetatypeTypeRepr *T) {
    printCommon("type_metatype") << '\n';
    printRec(T->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitProtocolTypeRepr(ProtocolTypeRepr *T) {
    printCommon("type_protocol") << '\n';
    printRec(T->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitInOutTypeRepr(InOutTypeRepr *T) {
    printCommon("type_inout") << '\n';
    printRec(T->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  
  void visitSharedTypeRepr(SharedTypeRepr *T) {
    printCommon("type_shared") << '\n';
    printRec(T->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }

  void visitOwnedTypeRepr(OwnedTypeRepr *T) {
    printCommon("type_owned") << '\n';
    printRec(T->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
};

} // end anonymous namespace

void PrintDecl::printRec(TypeRepr *T) {
  PrintTypeRepr(OS, Indent+2).visit(T);
}

void PrintExpr::printRec(TypeRepr *T) {
  PrintTypeRepr(OS, Indent+2).visit(T);
}

void PrintPattern::printRec(TypeRepr *T) {
  PrintTypeRepr(OS, Indent+2).visit(T);
}

void TypeRepr::dump() const {
  PrintTypeRepr(llvm::errs(), 0).visit(const_cast<TypeRepr*>(this));
  llvm::errs() << '\n';
}

// Recursive helpers to avoid infinite recursion for recursive protocol
// conformances.
static void dumpProtocolConformanceRec(
    const ProtocolConformance *conformance, llvm::raw_ostream &out,
    unsigned indent,
    llvm::SmallPtrSetImpl<const ProtocolConformance *> &visited);

static void dumpSubstitutionMapRec(
    SubstitutionMap map, llvm::raw_ostream &out,
    SubstitutionMap::DumpStyle style, unsigned indent,
    llvm::SmallPtrSetImpl<const ProtocolConformance *> &visited);

static void dumpProtocolConformanceRefRec(
    const ProtocolConformanceRef conformance, llvm::raw_ostream &out,
    unsigned indent,
    llvm::SmallPtrSetImpl<const ProtocolConformance *> &visited) {
  if (conformance.isInvalid()) {
    out.indent(indent) << "(invalid_conformance)";
  } else if (conformance.isConcrete()) {
    dumpProtocolConformanceRec(conformance.getConcrete(), out, indent, visited);
  } else {
    out.indent(indent) << "(abstract_conformance protocol="
                       << conformance.getAbstract()->getName();
    PrintWithColorRAII(out, ParenthesisColor) << ')';
  }
}

static void dumpProtocolConformanceRec(
    const ProtocolConformance *conformance, llvm::raw_ostream &out,
    unsigned indent,
    llvm::SmallPtrSetImpl<const ProtocolConformance *> &visited) {
  // A recursive conformance shouldn't have its contents printed, or there's
  // infinite recursion. (This also avoids printing things that occur multiple
  // times in a conformance hierarchy.)
  auto shouldPrintDetails = visited.insert(conformance).second;

  auto printCommon = [&](StringRef kind) {
    out.indent(indent);
    PrintWithColorRAII(out, ParenthesisColor) << '(';
    out << kind << "_conformance type=" << conformance->getType()
        << " protocol=" << conformance->getProtocol()->getName();

    if (!shouldPrintDetails)
      out << " (details printed above)";
  };

  switch (conformance->getKind()) {
  case ProtocolConformanceKind::Normal: {
    auto normal = cast<NormalProtocolConformance>(conformance);

    printCommon("normal");
    if (!shouldPrintDetails)
      break;

    // Maybe print information about the conforming context?
    if (normal->isLazilyLoaded()) {
      out << " lazy";
    } else {
      normal->forEachTypeWitness(
          nullptr,
          [&](const AssociatedTypeDecl *req, Type ty,
              const TypeDecl *) -> bool {
            out << '\n';
            out.indent(indent + 2);
            PrintWithColorRAII(out, ParenthesisColor) << '(';
            out << "assoc_type req=" << req->getName() << " type=";
            PrintWithColorRAII(out, TypeColor) << Type(ty->getDesugaredType());
            PrintWithColorRAII(out, ParenthesisColor) << ')';
            return false;
          });
      normal->forEachValueWitness(nullptr, [&](const ValueDecl *req,
                                               Witness witness) {
        out << '\n';
        out.indent(indent + 2);
        PrintWithColorRAII(out, ParenthesisColor) << '(';
        out << "value req=" << req->getFullName() << " witness=";
        if (!witness) {
          out << "(none)";
        } else if (witness.getDecl() == req) {
          out << "(dynamic)";
        } else {
          witness.getDecl()->dumpRef(out);
        }
        PrintWithColorRAII(out, ParenthesisColor) << ')';
      });

      for (auto sigConf : normal->getSignatureConformances()) {
        out << '\n';
        dumpProtocolConformanceRefRec(sigConf, out, indent + 2, visited);
      }
    }

    if (auto condReqs = normal->getConditionalRequirementsIfAvailableOrCached(
            /*computeIfPossible=*/false)) {
      for (auto requirement : *condReqs) {
        out << '\n';
        out.indent(indent + 2);
        requirement.dump(out);
      }
    } else {
      out << '\n';
      out.indent(indent + 2);
      out << "(conditional requirements unable to be computed)";
    }
    break;
  }

  case ProtocolConformanceKind::Self: {
    printCommon("self");
    break;
  }

  case ProtocolConformanceKind::Inherited: {
    auto conf = cast<InheritedProtocolConformance>(conformance);
    printCommon("inherited");
    if (!shouldPrintDetails)
      break;

    out << '\n';
    dumpProtocolConformanceRec(conf->getInheritedConformance(), out, indent + 2,
                               visited);
    break;
  }

  case ProtocolConformanceKind::Specialized: {
    auto conf = cast<SpecializedProtocolConformance>(conformance);
    printCommon("specialized");
    if (!shouldPrintDetails)
      break;

    out << '\n';
    dumpSubstitutionMapRec(conf->getSubstitutionMap(), out,
                           SubstitutionMap::DumpStyle::Full, indent + 2,
                           visited);
    out << '\n';
    if (auto condReqs = conf->getConditionalRequirementsIfAvailableOrCached(
            /*computeIfPossible=*/false)) {
      for (auto subReq : *condReqs) {
        out.indent(indent + 2);
        subReq.dump(out);
        out << '\n';
      }
    } else {
      out.indent(indent + 2);
      out << "(conditional requirements unable to be computed)\n";
    }
    dumpProtocolConformanceRec(conf->getGenericConformance(), out, indent + 2,
                               visited);
    break;
  }
  }

  PrintWithColorRAII(out, ParenthesisColor) << ')';
}

static void dumpSubstitutionMapRec(
    SubstitutionMap map, llvm::raw_ostream &out,
    SubstitutionMap::DumpStyle style, unsigned indent,
    llvm::SmallPtrSetImpl<const ProtocolConformance *> &visited) {
  auto *genericSig = map.getGenericSignature();
  out.indent(indent);

  auto printParen = [&](char p) {
    PrintWithColorRAII(out, ParenthesisColor) << p;
  };
  printParen('(');
  SWIFT_DEFER { printParen(')'); };
  out << "substitution_map generic_signature=";
  if (genericSig == nullptr) {
    out << "<nullptr>";
    return;
  }

  genericSig->print(out);
  auto genericParams = genericSig->getGenericParams();
  auto replacementTypes =
      static_cast<const SubstitutionMap &>(map).getReplacementTypesBuffer();
  for (unsigned i : indices(genericParams)) {
    if (style == SubstitutionMap::DumpStyle::Minimal) {
      out << " ";
    } else {
      out << "\n";
      out.indent(indent + 2);
    }
    printParen('(');
    out << "substitution ";
    genericParams[i]->print(out);
    out << " -> ";
    if (replacementTypes[i])
      replacementTypes[i]->print(out);
    else
      out << "<<unresolved concrete type>>";
    printParen(')');
  }
  // A minimal dump doesn't need the details about the conformances, a lot of
  // that info can be inferred from the signature.
  if (style == SubstitutionMap::DumpStyle::Minimal)
    return;

  auto conformances = map.getConformances();
  for (const auto &req : genericSig->getRequirements()) {
    if (req.getKind() != RequirementKind::Conformance)
      continue;

    out << "\n";
    out.indent(indent + 2);
    printParen('(');
    out << "conformance type=";
    req.getFirstType()->print(out);
    out << "\n";
    dumpProtocolConformanceRefRec(conformances.front(), out, indent + 4,
                                  visited);

    printParen(')');
    conformances = conformances.slice(1);
  }
}

void ProtocolConformanceRef::dump() const {
  dump(llvm::errs());
  llvm::errs() << '\n';
}

void ProtocolConformanceRef::dump(llvm::raw_ostream &out,
                                  unsigned indent) const {
  llvm::SmallPtrSet<const ProtocolConformance *, 8> visited;
  dumpProtocolConformanceRefRec(*this, out, indent, visited);
}
void ProtocolConformance::dump() const {
  auto &out = llvm::errs();
  dump(out);
  out << '\n';
}

void ProtocolConformance::dump(llvm::raw_ostream &out, unsigned indent) const {
  llvm::SmallPtrSet<const ProtocolConformance *, 8> visited;
  dumpProtocolConformanceRec(this, out, indent, visited);
}

void SubstitutionMap::dump(llvm::raw_ostream &out, DumpStyle style,
                           unsigned indent) const {
  llvm::SmallPtrSet<const ProtocolConformance *, 8> visited;
  dumpSubstitutionMapRec(*this, out, style, indent, visited);
}

void SubstitutionMap::dump() const {
  dump(llvm::errs());
  llvm::errs() << "\n";
}

//===----------------------------------------------------------------------===//
// Dumping for Types.
//===----------------------------------------------------------------------===//

namespace {
  class PrintType : public TypeVisitor<PrintType, void, StringRef> {
    raw_ostream &OS;
    unsigned Indent;

    raw_ostream &printCommon(StringRef label, StringRef name) {
      OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      if (!label.empty()) {
        PrintWithColorRAII(OS, TypeFieldColor) << label;
        OS << "=";
      }

      PrintWithColorRAII(OS, TypeColor) << name;
      return OS;
    }

    // Print a single flag.
    raw_ostream &printFlag(StringRef name) {
      PrintWithColorRAII(OS, TypeFieldColor) << " " << name;
      return OS;
    }

    // Print a single flag if it is set.
    raw_ostream &printFlag(bool isSet, StringRef name) {
      if (isSet)
        printFlag(name);

      return OS;
    }

    // Print a field with a value.
    template<typename T>
    raw_ostream &printField(StringRef name, const T &value) {
      OS << " ";
      PrintWithColorRAII(OS, TypeFieldColor) << name;
      OS << "=" << value;
      return OS;
    }

    void dumpParameterFlags(ParameterTypeFlags paramFlags) {
      printFlag(paramFlags.isVariadic(), "vararg");
      printFlag(paramFlags.isAutoClosure(), "autoclosure");
      printFlag(paramFlags.isEscaping(), "escaping");
      switch (paramFlags.getValueOwnership()) {
      case ValueOwnership::Default: break;
      case ValueOwnership::Owned: printFlag("owned"); break;
      case ValueOwnership::Shared: printFlag("shared"); break;
      case ValueOwnership::InOut: printFlag("inout"); break;
      }
    }

  public:
    PrintType(raw_ostream &os, unsigned indent) : OS(os), Indent(indent) { }

    void printRec(Type type) {
      printRec("", type);
    }

    void printRec(StringRef label, Type type) {
      OS << "\n";

      if (type.isNull())
        OS << "<<null>>";
      else {
        Indent += 2;
        visit(type, label);
        Indent -=2;
      }
    }

#define TRIVIAL_TYPE_PRINTER(Class,Name)                        \
    void visit##Class##Type(Class##Type *T, StringRef label) {  \
      printCommon(label, #Name "_type") << ")";              \
    }

    void visitErrorType(ErrorType *T, StringRef label) {
      printCommon(label, "error_type");
      if (auto originalType = T->getOriginalType())
        printRec("original_type", originalType);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    TRIVIAL_TYPE_PRINTER(Unresolved, unresolved)

    void visitBuiltinIntegerType(BuiltinIntegerType *T, StringRef label) {
      printCommon(label, "builtin_integer_type");
      if (T->isFixedWidth())
        printField("bit_width", T->getFixedWidth());
      else
        printFlag("word_sized");
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitBuiltinFloatType(BuiltinFloatType *T, StringRef label) {
      printCommon(label, "builtin_float_type");
      printField("bit_width", T->getBitWidth());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    TRIVIAL_TYPE_PRINTER(BuiltinIntegerLiteral, builtin_integer_literal)
    TRIVIAL_TYPE_PRINTER(BuiltinRawPointer, builtin_raw_pointer)
    TRIVIAL_TYPE_PRINTER(BuiltinNativeObject, builtin_native_object)
    TRIVIAL_TYPE_PRINTER(BuiltinBridgeObject, builtin_bridge_object)
    TRIVIAL_TYPE_PRINTER(BuiltinUnknownObject, builtin_unknown_object)
    TRIVIAL_TYPE_PRINTER(BuiltinUnsafeValueBuffer, builtin_unsafe_value_buffer)
    TRIVIAL_TYPE_PRINTER(SILToken, sil_token)

    void visitBuiltinVectorType(BuiltinVectorType *T, StringRef label) {
      printCommon(label, "builtin_vector_type");
      printField("num_elements", T->getNumElements());
      printRec(T->getElementType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitNameAliasType(NameAliasType *T, StringRef label) {
      printCommon(label, "name_alias_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());

      for (auto arg : T->getInnermostGenericArgs())
        printRec(arg);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitParenType(ParenType *T, StringRef label) {
      printCommon(label, "paren_type");
      dumpParameterFlags(T->getParameterFlags());
      printRec(T->getUnderlyingType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitTupleType(TupleType *T, StringRef label) {
      printCommon(label, "tuple_type");
      printField("num_elements", T->getNumElements());
      Indent += 2;
      for (const auto &elt : T->getElements()) {
        OS << "\n";
        OS.indent(Indent) << "(";
        PrintWithColorRAII(OS, TypeFieldColor) << "tuple_type_elt";
        if (elt.hasName())
          printField("name", elt.getName().str());
        dumpParameterFlags(elt.getParameterFlags());
        printRec(elt.getType());
        OS << ")";
      }
      Indent -= 2;
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

#define REF_STORAGE(Name, name, ...) \
    void visit##Name##StorageType(Name##StorageType *T, StringRef label) { \
      printCommon(label, #name "_storage_type"); \
      printRec(T->getReferentType()); \
      PrintWithColorRAII(OS, ParenthesisColor) << ')'; \
    }
#include "swift/AST/ReferenceStorage.def"

    void visitEnumType(EnumType *T, StringRef label) {
      printCommon(label, "enum_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitStructType(StructType *T, StringRef label) {
      printCommon(label, "struct_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitClassType(ClassType *T, StringRef label) {
      printCommon(label, "class_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitProtocolType(ProtocolType *T, StringRef label) {
      printCommon(label, "protocol_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitMetatypeType(MetatypeType *T, StringRef label) {
      printCommon(label, "metatype_type");
      if (T->hasRepresentation())
        OS << " " << getMetatypeRepresentationString(T->getRepresentation());
      printRec(T->getInstanceType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitExistentialMetatypeType(ExistentialMetatypeType *T,
                                      StringRef label) {
      printCommon(label, "existential_metatype_type");
      if (T->hasRepresentation())
        OS << " " << getMetatypeRepresentationString(T->getRepresentation());
      printRec(T->getInstanceType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitModuleType(ModuleType *T, StringRef label) {
      printCommon(label, "module_type");
      printField("module", T->getModule()->getName());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitDynamicSelfType(DynamicSelfType *T, StringRef label) {
      printCommon(label, "dynamic_self_type");
      printRec(T->getSelfType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    
    void printArchetypeCommon(ArchetypeType *T,
                              StringRef className,
                              StringRef label) {
      printCommon(label, className);
      printField("address", static_cast<void *>(T));
      printFlag(T->requiresClass(), "class");
      if (auto layout = T->getLayoutConstraint()) {
        OS << " layout=";
        layout->print(OS);
      }
      for (auto proto : T->getConformsTo())
        printField("conforms_to", proto->printRef());
      if (auto superclass = T->getSuperclass())
        printRec("superclass", superclass);
    }
    
    void printArchetypeNestedTypes(ArchetypeType *T) {
      Indent += 2;
      for (auto nestedType : T->getKnownNestedTypes()) {
        OS << "\n";
        OS.indent(Indent) << "(";
        PrintWithColorRAII(OS, TypeFieldColor) << "nested_type";
        OS << "=";
        OS << nestedType.first.str() << " ";
        if (!nestedType.second) {
          PrintWithColorRAII(OS, TypeColor) << "<<unresolved>>";
        } else {
          PrintWithColorRAII(OS, TypeColor);
          OS << "=" << nestedType.second.getString();
        }
        OS << ")";
      }
      Indent -= 2;
    }

    void visitPrimaryArchetypeType(PrimaryArchetypeType *T, StringRef label) {
      printArchetypeCommon(T, "primary_archetype_type", label);
      printField("name", T->getFullName());
      OS << "\n";
      auto genericEnv = T->getGenericEnvironment();
      if (auto owningDC = genericEnv->getOwningDeclContext()) {
        owningDC->printContext(OS, Indent + 2);
      }
      printArchetypeNestedTypes(T);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitNestedArchetypeType(NestedArchetypeType *T, StringRef label) {
      printArchetypeCommon(T, "nested_archetype_type", label);
      printField("name", T->getFullName());
      printField("parent", T->getParent());
      printField("assoc_type", T->getAssocType()->printRef());
      printArchetypeNestedTypes(T);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }
    void visitOpenedArchetypeType(OpenedArchetypeType *T, StringRef label) {
      printArchetypeCommon(T, "opened_archetype_type", label);
      printRec("opened_existential", T->getOpenedExistentialType());
      printField("opened_existential_id", T->getOpenedExistentialID());
      printArchetypeNestedTypes(T);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitGenericTypeParamType(GenericTypeParamType *T, StringRef label) {
      printCommon(label, "generic_type_param_type");
      printField("depth", T->getDepth());
      printField("index", T->getIndex());
      if (auto decl = T->getDecl())
        printField("decl", decl->printRef());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitDependentMemberType(DependentMemberType *T, StringRef label) {
      printCommon(label, "dependent_member_type");
      if (auto assocType = T->getAssocType()) {
        printField("assoc_type", assocType->printRef());
      } else {
        printField("name", T->getName().str());
      }
      printRec("base", T->getBase());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void printAnyFunctionParams(ArrayRef<AnyFunctionType::Param> params,
                                StringRef label) {
      printCommon(label, "function_params");
      printField("num_params", params.size());
      Indent += 2;
      for (const auto &param : params) {
        OS << "\n";
        OS.indent(Indent) << "(";
        PrintWithColorRAII(OS, TypeFieldColor) << "param";
        if (param.hasLabel())
          printField("name", param.getLabel().str());
        dumpParameterFlags(param.getParameterFlags());
        printRec(param.getPlainType());
        OS << ")";
      }
      Indent -= 2;
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void printAnyFunctionTypeCommon(AnyFunctionType *T, StringRef label,
                                    StringRef name) {
      printCommon(label, name);
      SILFunctionType::Representation representation =
        T->getExtInfo().getSILRepresentation();

      if (representation != SILFunctionType::Representation::Thick)
        printField("representation",
                   getSILFunctionTypeRepresentationString(representation));

      printFlag(!T->isNoEscape(), "escaping");
      printFlag(T->throws(), "throws");

      OS << "\n";
      Indent += 2;
      printAnyFunctionParams(T->getParams(), "input");
      Indent -=2;
      printRec("output", T->getResult());
    }

    void visitFunctionType(FunctionType *T, StringRef label) {
      printAnyFunctionTypeCommon(T, label, "function_type");
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitGenericFunctionType(GenericFunctionType *T, StringRef label) {
      printAnyFunctionTypeCommon(T, label, "generic_function_type");
      // FIXME: generic signature dumping needs improvement
      OS << "\n";
      OS.indent(Indent + 2) << "(";
      printField("generic_sig", T->getGenericSignature()->getAsString());
      OS << ")";
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitSILFunctionType(SILFunctionType *T, StringRef label) {
      printCommon(label, "sil_function_type");
      // FIXME: Print the structure of the type.
      printField("type", T->getString());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitSILBlockStorageType(SILBlockStorageType *T, StringRef label) {
      printCommon(label, "sil_block_storage_type");
      printRec(T->getCaptureType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitSILBoxType(SILBoxType *T, StringRef label) {
      printCommon(label, "sil_box_type");
      // FIXME: Print the structure of the type.
      printField("type", T->getString());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitArraySliceType(ArraySliceType *T, StringRef label) {
      printCommon(label, "array_slice_type");
      printRec(T->getBaseType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitOptionalType(OptionalType *T, StringRef label) {
      printCommon(label, "optional_type");
      printRec(T->getBaseType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitDictionaryType(DictionaryType *T, StringRef label) {
      printCommon(label, "dictionary_type");
      printRec("key", T->getKeyType());
      printRec("value", T->getValueType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitProtocolCompositionType(ProtocolCompositionType *T,
                                      StringRef label) {
      printCommon(label, "protocol_composition_type");
      if (T->hasExplicitAnyObject())
        OS << " any_object";
      for (auto proto : T->getMembers()) {
        printRec(proto);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitLValueType(LValueType *T, StringRef label) {
      printCommon(label, "lvalue_type");
      printRec(T->getObjectType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitInOutType(InOutType *T, StringRef label) {
      printCommon(label, "inout_type");
      printRec(T->getObjectType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitUnboundGenericType(UnboundGenericType *T, StringRef label) {
      printCommon(label, "unbound_generic_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitBoundGenericClassType(BoundGenericClassType *T, StringRef label) {
      printCommon(label, "bound_generic_class_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitBoundGenericStructType(BoundGenericStructType *T,
                                     StringRef label) {
      printCommon(label, "bound_generic_struct_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitBoundGenericEnumType(BoundGenericEnumType *T, StringRef label) {
      printCommon(label, "bound_generic_enum_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitTypeVariableType(TypeVariableType *T, StringRef label) {
      printCommon(label, "type_variable_type");
      printField("id", T->getID());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

#undef TRIVIAL_TYPE_PRINTER
  };
} // end anonymous namespace

void Type::dump() const {
  // Make sure to print type variables.
  dump(llvm::errs());
}

void Type::dump(raw_ostream &os, unsigned indent) const {
  // Make sure to print type variables.
  llvm::SaveAndRestore<bool> X(getPointer()->getASTContext().LangOpts.
                               DebugConstraintSolver, true);
  PrintType(os, indent).visit(*this, "");
  os << "\n";
}

void TypeBase::dump() const {
  // Make sure to print type variables.
  Type(const_cast<TypeBase *>(this)).dump();
}

void TypeBase::dump(raw_ostream &os, unsigned indent) const {
  auto &ctx = const_cast<TypeBase*>(this)->getASTContext();
  
  // Make sure to print type variables.
  llvm::SaveAndRestore<bool> X(ctx.LangOpts.DebugConstraintSolver, true);
  Type(const_cast<TypeBase *>(this)).dump(os, indent);
}

void GenericEnvironment::dump(raw_ostream &os) const {
  os << "Generic environment:\n";
  for (auto gp : getGenericParams()) {
    gp->dump(os);
    mapTypeIntoContext(gp)->dump(os);
  }
  os << "Generic parameters:\n";
  for (auto paramTy : getGenericParams())
    paramTy->dump(os);
}

void GenericEnvironment::dump() const {
  dump(llvm::errs());
}
