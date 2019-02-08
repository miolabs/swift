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
#include <iostream>
#include <vector>
#include <list>
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

const bool LIB_GENERATE_MODE = false;
const bool GENERATE_STD_LIB = false;
const bool GENERATE_IMPORTED_MODULE = false;
const std::string LIB_GENERATE_PATH = "/Users/bubulkowanorka/projects/antlr4-visitor/include/";

const std::string ASSIGNMENT_OPERATORS[] = {"+=", "-=", "*=", "/=", "%=", ">>=", "<<=", "&=", "^=", "|=", "&>>=", "&<<="};

const std::string RESERVED_VAR_NAMES[] = {"abstract","else","instanceof","super","switch","break","export","interface","synchronized","byte","extends","let","this","case","false","throw","catch","final","native","throws","finally","new","class","null","true","const","for","package","try","continue","function","private","typeof","debugger","goto","protected","var","default","if","public","delete","implements","return","volatile","do","import","while","in","of","static","with","alert","frames","outerHeight","all","frameRate","outerWidth","anchor","function","packages","anchors","getClass","pageXOffset","area","hasOwnProperty","pageYOffset","hidden","parent","assign","history","parseFloat","blur","image","parseInt","button","images","password","checkbox","Infinity","pkcs11","clearInterval","isFinite","plugin","clearTimeout","isNaN","prompt","clientInformation","isPrototypeOf","propertyIsEnum","close","java","prototype","closed","radio","confirm","reset","constructor","screenX","crypto","screenY","Date","innerHeight","scroll","decodeURI","innerWidth","secure","decodeURIComponent","layer","select","defaultStatus","layers","self","document","length","setInterval","element","link","setTimeout","elements","location","status","embed","Math","embeds","mimeTypes","submit","encodeURI","name","taint","encodeURIComponent","NaN","text","escape","navigate","textarea","eval","navigator","top","event","Number","toString","fileUpload","Object","undefined","focus","offscreenBuffering","unescape","form","open","untaint","forms","opener","valueOf","frame","option","window","onbeforeunload","ondragdrop","onkeyup","onmouseover","onblur","onerror","onload","onmouseup","ondragdrop","onfocus","onmousedown","onreset","onclick","onkeydown","onmousemove","onsubmit","oncontextmenu","onkeypress","onmouseout","onunload"};

const std::unordered_map<std::string, std::string> LIB_BODIES = {
  {"Swift.(file).String.count", "return this.length"},
  {"Swift.(file).print(_:[Any],separator:String,terminator:String)", "console.log(#A0)"},
  {"Swift.(file).Dictionary.subscript(_:Dictionary<Key, Value>.Index)", "return this.get(#AA)"},
  {"Swift.(file).Dictionary.subscript(_:Key)", "return this.get(#AA)"},
  {"Swift.(file).Dictionary.subscript(_:Key)#ASS", "if(#A0 == null) this.delete(#A1)\nelse this.set(#A1, #A0)"},
  {"Swift.(file).Dictionary.count", "return this.size"},
  {"Swift.(file).Dictionary.makeIterator()", "return new SwiftIterator((current) => Array.from(this)[current])"},
  {"Swift.(file).Array.subscript(_:Int)", "return this[#AA]"},
  {"Swift.(file).Array.subscript(_:Int)#ASS", "if(#A0 == null) this.splice(#A1, 1)\nelse this[#A1]=#A0"},
  {"Swift.(file).Array.count", "return this.length"},
  {"Swift.(file).Array.+infix(_:Array<Element>,_:Array<Element>)", "return #A0.concat(#A1)"},
  {"Swift.(file).Array.+=infix(_:Array<Element>,_:Array<Element>)", "#A0.get().appendContentsOf(null, #A1)"},
  {"Swift.(file).Array.append(_:Element)", "this.push(#AA)"},
  {"Swift.(file).Array.append(contentsOf:S)", "this.push.apply(this, #A0)"},
  {"Swift.(file).Array.insert(_:Element,at:Int)", "this.splice(#A1, 0, #A0)"},
  {"Swift.(file).Array.remove(at:Int)", "this.splice(#AA, 1)"},
  {"Swift.(file).Array.init(repeating:Element,count:Int)", "return new Array(#A1).fill(#A0)"},
  {"Swift.(file).Set.insert(_:Element)", "this.add(#AA)"},
  {"Swift.(file).Set.count", "return this.size"},
  {"Swift.(file).RangeReplaceableCollection.insert(contentsOf:C,at:Self.Index)", "this.splice.apply(this, [#A1, 0].concat(#A0))"},
  {"Swift.(file).BidirectionalCollection.joined(separator:String)", "return this.join(#AA)"},
  {"Swift.(file).Collection.makeIterator()", "return new SwiftIterator((current) => this[current])"},
  {"Swift.(file).Sequence.enumerated()", "return this.map((v, i) => [i, v])"},
  {"Swift.(file).Sequence.reduce(_:Result,_:(Result, Self.Element) throws -> Result)", "return this.reduce(#A1.bind(null, null), #A0)"},
  {"Swift.(file)._ArrayProtocol.filter(_:(Self.Element) throws -> Bool)", "return this.filter(#AA.bind(null, null))"},
  {"Swift.(file).Collection.map(_:(Self.Element) throws -> T)", "return this.map(#AA.bind(null, null))"},
  {"Swift.(file).MutableCollection.sort(by:(Self.Element, Self.Element) throws -> Bool)", "return this.sort((a, b) => areInIncreasingOrder(null, a, b) ? -1 : 1)"},
  {"Swift.(file).??infix(_:T?,_:() throws -> T)", "return #A0 != null ? #A0 : #A1()"},
  {"Swift.(file).??infix(_:T?,_:() throws -> T?)", "return #A0 != null ? #A0 : #A1()"},
  {"Swift.(file).~=infix(_:T,_:T)", "return #A0 == #A1"},
  {"Swift.(file).Comparable...<infix(_:Self,_:Self)", "return _create(Range, 'initUncheckedBoundslowerBoundupperBound', null, [minimum, maximum])"},
  {"Swift.(file).Comparable....infix(_:Self,_:Self)", "return _create(ClosedRange, 'initUncheckedBoundslowerBoundupperBound', null, [minimum, maximum])"},
  {"Swift.(file).Range.init(uncheckedBounds:(lower: Bound, upper: Bound))", "this.lowerBound$internal = #AA[0]\nthis.upperBound$internal = #AA[1]"},
  {"Swift.(file).ClosedRange.init(uncheckedBounds:(lower: Bound, upper: Bound))", "this.lowerBound$internal = #AA[0]\nthis.upperBound$internal = #AA[1]"},
  {"Swift.(file).Range.lowerBound", "return this.lowerBound$internal"},
  {"Swift.(file).Range.upperBound", "return this.upperBound$internal"},
  {"Swift.(file).ClosedRange.lowerBound", "return this.lowerBound$internal"},
  {"Swift.(file).ClosedRange.upperBound", "return this.upperBound$internal"},
  {"Swift.(file).RangeExpression.~=infix(_:Self,_:Self.Bound)", "return #A0.contains(null, #A1)"},
  {"Swift.(file).Range.contains(_:Bound)", "return #AA >= this.lowerBound && #AA < this.upperBound"},
  {"Swift.(file).ClosedRange.contains(_:Bound)", "return #AA >= this.lowerBound && #AA <= this.upperBound"},
  {"Swift.(file).Sequence.makeIterator()", "return new SwiftIterator((current) => this.contains(null, current + this.lowerBound) ? current + this.lowerBound : null)"},
  {"Swift.(file).FloatingPoint.init(_:Int)", "return #AA"},
  {"Swift.(file).Array.init()", "return []"},
  {"Swift.(file).Dictionary.init()", "return new Map()"},
  {"Swift.(file).Set.init()", "return new Set()"},
  {"Swift.(file).Set.init(_:Source)", "return new Set(#AA)"},
  {"Swift.(file).Array.init()", "return []"},
  {"Swift.(file).BinaryInteger./infix(_:Self,_:Self)", "return (#A0 / #A1) | 0"},
  {"Swift.(file).BinaryInteger./=infix(_:Self,_:Self)", "lhs$inout.set((lhs$inout.get() / rhs) | 0)"},
  {"Swift.(file).Int8.<<infix(_:Int8,_:Int8)", "let binaryRepr = lhs.toString(2)\nlet result = 0\nfor(let i = 0; i < binaryRepr.length; i++) {\nlet j = i - rhs\nif(binaryRepr[j] !== '1') continue\nresult += j === 0 ? -128 : Math.pow(2, 7 - j)\n}\nreturn result"},
  {"Swift.(file).UInt8.<<infix(_:UInt8,_:UInt8)", "let binaryRepr = lhs.toString(2)\nlet result = 0\nfor(let i = 0; i < binaryRepr.length; i++) {\nlet j = i - rhs\nif(binaryRepr[j] !== '1') continue\nresult += Math.pow(2, 7 - j)\n}\nreturn result"},
  {"Darwin.(file).arc4random_uniform(_:UInt32)", "return (Math.random() * #AA) | 0"},
  {"Darwin.(file).arc4random()", "return arc4random_uniform(null, 4294967296)"},
  {"Swift.(file).UnsignedInteger.init(_:T)", "return #AA"},
  {"Swift.(file).SignedInteger.init(_:T)", "return #AA"},
  {"Swift.(file).FixedWidthInteger.init(_:T)", "return #AA"},
  {"XCTest.(file).XCTAssert(_:() throws -> Bool,_:() -> String,file:StaticString,line:UInt)", "if(!expression()) throw message ? message() : 'assert fail :' + expression"},
  {"XCTest.(file).XCTAssertEqual(_:() throws -> T,_:() throws -> T,_:() -> String,file:StaticString,line:UInt)", "if(expression1() != expression2()) throw message ? message() : 'assert fail :' + expression1"},
  {"XCTest.(file).XCTAssertFalse(_:() throws -> Bool,_:() -> String,file:StaticString,line:UInt)", "if(expression()) throw message ? message() : 'assert fail :' + expression"},
  {"XCTest.(file).XCTAssertGreaterThan(_:() throws -> T,_:() throws -> T,_:() -> String,file:StaticString,line:UInt)", "if(!(expression1() > expression2())) throw message ? message() : 'assert fail :' + expression1"},
  {"XCTest.(file).XCTAssertGreaterThanOrEqual(_:() throws -> T,_:() throws -> T,_:() -> String,file:StaticString,line:UInt)", "if(!(expression1() >= expression2())) throw message ? message() : 'assert fail :' + expression1"},
  {"XCTest.(file).XCTAssertLessThan(_:() throws -> T,_:() throws -> T,_:() -> String,file:StaticString,line:UInt)", "if(!(expression1() < expression2())) throw message ? message() : 'assert fail :' + expression1"},
  {"XCTest.(file).XCTAssertLessThanOrEqual(_:() throws -> T,_:() throws -> T,_:() -> String,file:StaticString,line:UInt)", "if(!(expression1() <= expression2())) throw message ? message() : 'assert fail :' + expression1"},
  {"XCTest.(file).XCTAssertNil(_:() throws -> Any?,_:() -> String,file:StaticString,line:UInt)", "if(expression() != undefined) throw message ? message() : 'assert fail :' + expression"},
  {"XCTest.(file).XCTAssertEqual(_:() throws -> T,_:() throws -> T,_:() -> String,file:StaticString,line:UInt)", "if(expression1() == expression2()) throw message ? message() : 'assert fail :' + expression1"},
  {"XCTest.(file).XCTAssertNoThrow(_:() throws -> T,_:() -> String,file:StaticString,line:UInt)", "try{expression()}catch(e){throw message ? message() : 'assert fail :' + expression}"},
  {"XCTest.(file).XCTAssertNotNil(_:() throws -> Any?,_:() -> String,file:StaticString,line:UInt)", "if(expression() == undefined) throw message ? message() : 'assert fail :' + expression"},
  {"XCTest.(file).XCTAssertThrowsError(_:() throws -> T,_:() -> String,file:StaticString,line:UInt,_:(Error) -> Void)", "try{expression()}catch(e){return}throw message ? message() : 'assert fail :' + expression"},
  {"XCTest.(file).XCTAssertTrue(_:() throws -> Bool,_:() -> String,file:StaticString,line:UInt)", "if(expression() != true) throw message ? message() : 'assert fail :' + expression"},
  {"XCTest.(file).XCTestCase.init()", "if(this.setUp) this.setUp()\nfor(const testFunction in this) {\nif(typeof this[testFunction] !== 'function' || testFunction === 'setUp' || XCTestCase.prototype[testFunction]/*is inherited*/ || testFunction.endsWith('$get') || testFunction.endsWith('$set')) continue\nthis[testFunction]()\n}"}
};

const std::unordered_map<std::string, std::string> LIB_MIXINS = {
  {"Swift.(file).String", "String"},
  {"Swift.(file).Bool", "Boolean"},
  {"Swift.(file).Int", "Number"},
  {"Swift.(file).Double", "Number"},
  {"Swift.(file).Array", "Array"},
  {"Swift.(file).Dictionary", "Map"},
  {"Swift.(file).Set", "Set"}
};

const std::unordered_map<std::string, std::string> LIB_CLONE_STRUCT_FILLS = {
  {"Swift.(file).Dictionary", "($info, obj){obj.forEach((val, prop) => this.set(prop, _cloneStruct(val)))}"}
};

std::unordered_map<std::string, bool> libFunctionOverloadedCounts = {};

const std::unordered_map<std::string, bool> REPLACEMENTS_CLONE_STRUCT = {
  {"Swift.(file).Int", false},
  {"Swift.(file).String", false},
  {"Swift.(file).Double", false},
  {"Swift.(file).Bool", false}
};

Expr *lAssignmentExpr;
Expr *functionArgsCall;

std::vector<std::string> optionalCondition = {};

bool printGenerics = false;

std::unordered_map<std::string, std::string> functionUniqueNames = {
  {"Swift.(file).Sequence.reduce(_:Result,_:(Result, Self.Element) throws -> Result)", "reduceInvertedArguments"},
  {"Swift.(file)._ArrayProtocol.filter(_:(Self.Element) throws -> Bool)", "filterWithInfo"},
  {"Swift.(file).Collection.map(_:(Self.Element) throws -> T)", "mapWithInfo"},
  {"Swift.(file).RandomAccessCollection.subscript(_:Range<Self.Index>)", "subcriptRange"},
  {"Swift.(file).MutableCollection.subscript(_:Range<Self.Index>)", "subcriptRange"},
  {"Swift.(file).BidirectionalCollection.subscript(_:Range<Self.Index>)", "subcriptRange"},
  {"Swift.(file).Collection.subscript(_:Range<Self.Index>)", "subcriptRange"},
  {"Swift.(file).RangeReplaceableCollection.subscript(_:Range<Self.Index>)", "subcriptRange"}
};
std::unordered_map<std::string, int> functionOverloadedCounts = {
  {"zip", 0},{"va_list", 0},{"_withVaList", 0},{"sequence", 0},{"infix_46_124_61", 0},{"infix_46_94_61", 0},{"infix_46_38_61", 0},{"infix_46_94", 0},{"prefix_46_33", 0},{"infix_46_62", 0},{"replacing", 0},{"infix_46_33_61", 0},{"infix_46_61_61", 0},{"unsafeCastElements", 0},{"quickLookObject", 0},{"_superclassIterator", 0},{"_noSuperclassMirror", 0},{"_isLess", 0},{"_suffix", 0},{"_prefix", 0},{"_dropLast", 0},{"_drop", 0},{"__copyContents", 0},{"__copyToContiguousArray", 0},{"_makeIterator", 0},{"_writeBackMutableSlice", 0},{"_isValid", 0},{"_measureCharacterStrideICU", 0},{"transcodedLength", 0},{"_copy", 0},{"trailSurrogate", 0},{"leadSurrogate", 0},{"_decodeSurrogates", 0},{"getVaList", 0},{"_applyMapping", 0},{"escaped", 0},{"_parseMultipleCodeUnits", 0},{"moveInitializeMemory", 0},{"assumingMemoryBound", 0},{"deinitialize", 0},{"width", 0},{"moveAssign", 0},{"moveInitialize", 0},{"deallocate", 0},{"_mergeRuns", 0},{"release", 0},{"takeRetainedValue", 0},{"takeUnretainedValue", 0},{"toOpaque", 0},{"fromOpaque", 0},{"_numUTF16CodeUnits", 0},{"_continuationPayload", 0},{"_decodeScalar", 0},{"_decodeUTF8", 0},{"_isASCII", 0},{"_isSurrogate", 0},{"_isTrailingSurrogate", 0},{"_encode", 0},{"_createThreadLocalStorage", 0},{"_destroyTLS", 0},{"getUBreakIterator", 0},{"getPointer", 0},{"_loadDestroyTLSCounter", 0},{"_destroyBridgedStorage", 0},{"_isValidArrayIndex", 0},{"repairUTF8", 0},{"_isUTF8MultiByteLeading", 0},{"getCString", 0},{"cString", 0},{"_utf8String", 0},{"_fastCStringContents", 0},{"character", 0},{"_isNSString", 0},{"_postRRCAdjust", 0},{"_cString", 0},{"_getCString", 0},{"make", 0},{"appendInterpolation", 0},{"_toUTF16Indices", 0},{"_toUTF16Offsets", 0},{"_toUTF16Offset", 0},{"getSharedUTF8Start", 0},{"getSmallCount", 0},{"largeMortal", 0},{"largeImmortal", 0},{"small", 0},{"_isValidArraySubscript", 0},{"_findStringSwitchCaseWithCache", 0},{"_slowCompare", 0},{"withNFCCodeUnitsIterator", 0},{"_persistCString", 0},{"_foreignOpaqueCharacterStride", 0},{"_opaqueCharacterStride", 0},{"isOnGraphemeClusterBoundary", 0},{"errorCorrectedScalar", 0},{"foreignErrorCorrectedGrapheme", 0},{"foreignErrorCorrectedUTF16CodeUnit", 0},{"fastUTF8Scalar", 0},{"fastUTF8ScalarLength", 0},{"scalarAlign", 0},{"uniqueNativeReplaceSubrange", 0},{"appendInPlace", 0},{"prepareForAppendInPlace", 0},{"_foreignGrow", 0},{"grow", 0},{"copyUTF8", 0},{"withFastUTF8", 0},{"populateBreadcrumbs", 0},{"getBreadcrumbsPtr", 0},{"foreignHasNormalizationBoundary", 0},{"_binaryCompare", 0},{"_lexicographicalCompare", 0},{"_findDiffIdx", 0},{"_stringCompareSlow", 0},{"_toUTF16Index", 0},{"_stringCompareFastUTF8Abnormal", 0},{"_stringCompareFastUTF8", 0},{"_stringCompareInternal", 0},{"_getDescription", 0},{"_bridgeCocoaString", 0},{"_getCocoaStringPointer", 0},{"_cocoaUTF8Pointer", 0},{"_bridgeTagged", 0},{"_stdlib_isOSVersionAtLeast", 0},{"_unsafeAddressOfCocoaStringClass", 0},{"_cocoaCStringUsingEncodingTrampoline", 0},{"_cocoaHashASCIIBytes", 0},{"_cocoaHashString", 0},{"_cocoaStringCompare", 0},{"_cocoaStringSubscript", 0},{"_cocoaStringCopyCharacters", 0},{"_stdlib_binary_CFStringGetCharactersPtr", 0},{"_stdlib_binary_CFStringCreateCopy", 0},{"withMutableCharacters", 0},{"_isScalar", 0},{"_nativeGetIndex", 0},{"_foreignCount", 0},{"_bridgeCocoaArray", 0},{"_foreignSubscript", 0},{"hasSuffix", 0},{"getGlobalRuntimeFunctionCounters", 0},{"_fromCodeUnits", 0},{"_uncheckedFromUTF16", 0},{"_copyUTF16CodeUnits", 0},{"_lowercaseASCII", 0},{"_dictionaryDownCastConditional", 0},{"_loadPartialUnalignedUInt64LE", 0},{"_uppercaseASCII", 0},{"_slowWithCString", 0},{"increment", 0},{"withCString", 0},{"_numUTF8CodeUnits", 0},{"numericCast", 0},{"decodeCString", 0},{"samePosition", 0},{"_step", 0},{"_findNextRun", 0},{"_nativeCopyUTF16CodeUnits", 0},{"_merge", 0},{"_isUnique_native", 0},{"_setDownCastConditional", 0},{"_setDownCastIndirect", 0},{"delete", 0},{"bridgeElements", 0},{"_initializeBridgedElements", 0},{"_advanceIndex", 0},{"_migrateToNative", 0},{"getBreadcrumb", 0},{"_subtracting", 0},{"isStrictSuperset", 0},{"isSuperset", 0},{"word", 0},{"_stdlib_CFSetGetValues", 0},{"_roundingDownToAlignment", 0},{"isSubset", 0},{"reduce", 0},{"enumerated", 0},{"first", 0},{"_isNativePointer", 0},{"forEach", 0},{"_filter", 0},{"infix_46_62_61", 0},{"advanced", 0},{"shuffled", 0},{"_swift_stdlib_atomicLoadInt", 0},{"_measureRuntimeFunctionCountersDiffs", 0},{"isStrictSubset", 0},{"getNumRuntimeFunctionCounters", 0},{"setPerObjectRuntimeFunctionCountersMode", 0},{"autorelease", 0},{"_checkIndex", 0},{"setObjectRuntimeFunctionCounters", 0},{"_arrayDownCastConditionalIndirect", 0},{"_classify", 0},{"_stdlib_NSSet_allObjects", 0},{"setGlobalRuntimeFunctionCounters", 0},{"getObjectRuntimeFunctionCounters", 0},{"getRuntimeFunctionNameToIndex", 0},{"appendedType", 0},{"setGlobalRuntimeFunctionCountersUpdateHandler", 0},{"removeAll", 0},{"getRuntimeFunctionCountersOffsets", 0},{"_swift_class_getSuperclass", 0},{"_uint64ToString", 0},{"hasNormalizationBoundary", 0},{"find", 0},{"compactMap", 0},{"_int64ToString", 0},{"_float80ToString", 0},{"_float64ToStringImpl", 0},{"_copyCollectionToContiguousArray", 0},{"_float32ToStringImpl", 0},{"isLeadSurrogate", 0},{"nextKey", 0},{"_stdlib_atomicLoadARCRef", 0},{"_getErrorDefaultUserInfo", 0},{"_stdlib_atomicInitializeARCRef", 0},{"_is", 0},{"_arrayConditionalCast", 0},{"getChild", 0},{"removeSubrange", 0},{"_contains_", 0},{"_debugPrint", 0},{"_convertInOutToPointerArgument", 0},{"_print", 0},{"lowercased", 0},{"debugPrint", 0},{"_convertConstStringToUTF8PointerArgument", 0},{"_formIndex", 0},{"predecessor", 0},{"_lock", 0},{"infix_63_63", 0},{"_replPrintLiteralString", 0},{"infix_38_38", 0},{"_diagnoseUnexpectedNilOptional", 0},{"_fromUTF8Repairing", 0},{"member", 0},{"fill", 0},{"fastPathFill", 0},{"_getEnumCaseName", 0},{"encodeIfPresent", 0},{"normalizeWithHeapBuffers", 0},{"foreignErrorCorrectedScalar", 0},{"_float32ToString", 0},{"_combine", 0},{"uncheckedElement", 0},{"toIntMax", 0},{"nextValue", 0},{"_decodeOne", 0},{"_getDisplayStyle", 0},{"get", 0},{"swapEntry", 0},{"swapValuesAt", 0},{"validatedBucket", 0},{"copyAndResize", 0},{"formSquareRoot", 0},{"_makeSwiftNSFastEnumerationState", 0},{"uncheckedDestroy", 0},{"_bridgeAnyObjectToAny", 0},{"_appendingKeyPaths", 0},{"uncheckedValue", 0},{"_bufferedScalar", 0},{"randomElement", 0},{"enableRuntimeFunctionCountersUpdates", 0},{"uncheckedKey", 0},{"_opaqueSummary", 0},{"_bridgeFromObjectiveCAdoptingNativeStorageOf", 0},{"invalidateIndices", 0},{"infix_60_61", 0},{"_boundsCheck", 0},{"bridged", 0},{"swap", 0},{"_reverse", 0},{"shuffle", 0},{"_rawPointerToString", 0},{"getObjCClassInstanceExtents", 0},{"_halfStablePartition", 0},{"_roundingUpBaseToAlignment", 0},{"offset", 0},{"alignment", 0},{"initializeMemory", 0},{"size", 0},{"isKnownUniquelyReferenced", 0},{"_internalInvariantValidBufferClass", 0},{"merging", 0},{"destroy", 0},{"_checkValidBufferClass", 0},{"_usesNativeSwiftReferenceCounting", 0},{"isUniqueReference", 0},{"_normalizedHash", 0},{"disableRuntimeFunctionCountersUpdates", 0},{"_isClassOrObjCExistential", 0},{"tryReallocateUniquelyReferenced", 0},{"withUnsafeMutablePointers", 0},{"create", 0},{"withExtendedLifetime", 0},{"updatePreviousComponentAddr", 0},{"_getKeyPathClassAndInstanceSizeFromPattern", 0},{"_resolveRelativeIndirectableAddress", 0},{"roundUpToPointerAlignment", 0},{"map", 0},{"decode", 0},{"copyContents", 0},{"_walkKeyPathPattern", 0},{"_getClassPlaygroundQuickLook", 0},{"_loadRelativeAddress", 0},{"age", 0},{"_internalInvariantFailure", 0},{"_resolveRelativeAddress", 0},{"_nativeIsEqual", 0},{"infix_38_42", 0},{"visitIntermediateComponentType", 0},{"_withUTF8", 0},{"_reserveCapacityAssumingUniqueBuffer", 0},{"visitOptionalWrapComponent", 0},{"hash", 0},{"visitOptionalForceComponent", 0},{"objectAt", 0},{"visitComputedComponent", 0},{"_isContinuation", 0},{"visitStoredComponent", 0},{"_getCharacters", 0},{"_getTypeByMangledNameInEnvironmentOrContext", 0},{"_swift_getKeyPath", 0},{"hasPrefix", 0},{"partition", 0},{"appending", 0},{"_setAtReferenceWritableKeyPath", 0},{"_getAtKeyPath", 0},{"move", 0},{"_getAtAnyKeyPath", 0},{"validateReservedBits", 0},{"keyEnumerator", 0},{"computeIsASCII", 0},{"storesOnlyElementsOfType", 0},{"_pop", 0},{"_scalarAlign", 0},{"clone", 0},{"_projectReadOnly", 0},{"_dump", 0},{"_withNFCCodeUnits", 0},{"checkSizeConsistency", 0},{"_convertConstArrayToPointerArgument", 0},{"_assumeNonNegative", 0},{"withBuffer", 0},{"setGlobalRuntimeFunctionCountersMode", 0},{"withUTF16CodeUnits", 0},{"_unsafePlus", 0},{"_swift_stdlib_atomicFetchOrInt64", 0},{"_uncheckedSetByte", 0},{"_uncheckedGetByte", 0},{"_stdlib_atomicCompareExchangeStrongPtr", 0},{"largeCocoa", 0},{"_debugPreconditionFailure", 0},{"_toUTF16CodeUnit", 0},{"_maskingAdd", 0},{"_lowBits", 0},{"_convertPointerToPointerArgument", 0},{"withUTF8Buffer", 0},{"_fullShiftRight", 0},{"_joined", 0},{"infix_38_43", 0},{"_foreignCopyUTF8", 0},{"_fatalErrorMessage", 0},{"scale", 0},{"_nonMaskingLeftShift", 0},{"validateUTF8", 0},{"prefix_43", 0},{"_nonMaskingLeftShiftGeneric", 0},{"_nonMaskingRightShift", 0},{"intersection", 0},{"withUnsafeBufferOfObjects", 0},{"preconditionFailure", 0},{"_nonMaskingRightShiftGeneric", 0},{"_getRuntimeFunctionNames", 0},{"_hasBinaryProperty", 0},{"dataCorruptedError", 0},{"infix_38_60_60", 0},{"dumpObjectsRuntimeFunctionPointers", 0},{"withUnsafeBytes", 0},{"infix_38_62_62_61", 0},{"truncatingRemainder", 0},{"_foreignDistance", 0},{"uppercased", 0},{"infix_38_62_62", 0},{"makeIterator", 0},{"multipliedReportingOverflow", 0},{"_isLeadingSurrogate", 0},{"prefix_126", 0},{"withBytes", 0},{"object", 0},{"addingReportingOverflow", 0},{"remainderWithOverflow", 0},{"_print_unlocked", 0},{"_isNotOverlong_E0", 0},{"_exp", 0},{"divideWithOverflow", 0},{"multiplyWithOverflow", 0},{"_description", 0},{"_getChild", 0},{"replace", 0},{"signum", 0},{"quotientAndRemainder", 0},{"assign", 0},{"element", 0},{"_createStringTableCache", 0},{"infix_60_60", 0},{"infix_62_62_61", 0},{"_internalInvariant", 0},{"_getAtPartialKeyPath", 0},{"infix_94_61", 0},{"_getElementSlowPath", 0},{"infix_124_61", 0},{"visitHeader", 0},{"infix_124", 0},{"store", 0},{"infix_38_61", 0},{"_memmove", 0},{"squareRoot", 0},{"_getQuickLookObject", 0},{"infix_38", 0},{"_binaryLogarithm", 0},{"dividingFullWidth", 0},{"normalizeFromSource", 0},{"abs", 0},{"_ascii16", 0},{"_adHocPrint_unlocked", 0},{"nextHole", 0},{"_characterStride", 0},{"previousHole", 0},{"_getTypeByMangledNameInEnvironment", 0},{"_getNormalizedType", 0},{"count", 0},{"bucket", 0},{"superEncoder", 0},{"occupiedBucket", 0},{"_hoistableIsNativeTypeChecked", 0},{"checkOccupied", 0},{"_isImpl", 0},{"_dumpPrint_unlocked", 0},{"_isOccupied", 0},{"storeBytes", 0},{"moveEntry", 0},{"initialize", 0},{"_stdlib_NSObject_isEqual", 0},{"_diagnoseUnexpectedEnumCaseValue", 0},{"readLine", 0},{"infix_62_61", 0},{"compress", 0},{"_setUpCast", 0},{"hashSeed", 0},{"_extract", 0},{"_round", 0},{"_rotateLeft", 0},{"_slideTail", 0},{"combine", 0},{"compare", 0},{"_trueAfterDiagnostics", 0},{"_anyHashableDownCastConditionalIndirect", 0},{"_finalizeRuns", 0},{"merge", 0},{"_minimumMergeRunLength", 0},{"_convertToAnyHashable", 0},{"_unsafeMutableBufferPointerCast", 0},{"_isBridgedNonVerbatimToObjectiveC", 0},{"_identityCast", 0},{"pushDest", 0},{"_makeAnyHashableUpcastingToHashableBaseType", 0},{"_componentBodySize", 0},{"_setDownCastConditionalIndirect", 0},{"unimplemented_utf8_32bit", 0},{"_makeAnyHashableUsingDefaultRepresentation", 0},{"Hashable_isEqual_indirect", 0},{"tryFill", 0},{"_convertToAnyHashableIndirect", 0},{"_dropFirst", 0},{"_hashValue", 0},{"infix_38_60_60_61", 0},{"_isNotOverlong_ED", 0},{"_postAppendAdjust", 0},{"removeLast", 0},{"infix_37_61", 0},{"_instantiateKeyPathBuffer", 0},{"subtractingReportingOverflow", 0},{"infix_37", 0},{"_roundSlowPath", 0},{"withUTF8CodeUnits", 0},{"_isspace_clocale", 0},{"withContiguousStorageIfAvailable", 0},{"isTotallyOrdered", 0},{"_transcode", 0},{"visitOptionalChainComponent", 0},{"suffix", 0},{"rounded", 0},{"_stringCompare", 0},{"_fullShiftLeft", 0},{"minimumMagnitude", 0},{"maximum", 0},{"starts", 0},{"minimum", 0},{"_fromUTF16CodeUnit", 0},{"_parseUnsignedASCII", 0},{"addingProduct", 0},{"_convertMutableArrayToPointerArgument", 0},{"_random", 0},{"insertNew", 0},{"_encodeBitsAsWords", 0},{"bridgedKey", 0},{"formRemainder", 0},{"remainder", 0},{"_float64ToString", 0},{"updateValue", 0},{"append", 0},{"infix_42_61", 0},{"_isNotOverlong_F4", 0},{"infix_42", 0},{"downcast", 0},{"infix_45_61", 0},{"_index", 0},{"negate", 0},{"prefix_45", 0},{"_errorInMain", 0},{"_unexpectedError", 0},{"_bridgeErrorToNSError", 0},{"_growArrayCapacity", 0},{"diff", 0},{"prefix_46_46_46", 0},{"_getErrorEmbeddedNSErrorIndirect", 0},{"_dump_unlocked", 0},{"_bridgeAnythingToObjectiveC", 0},{"_reallocObject", 0},{"infix_61_61_61", 0},{"_conditionallyBridgeFromObjectiveC_bridgeable", 0},{"convert", 0},{"_arrayForceCast", 0},{"_unlock", 0},{"_canBeClass", 0},{"_isValidAddress", 0},{"resize", 0},{"_withVerbatimBridgedUnsafeBuffer", 0},{"_dumpSuperclass_unlocked", 0},{"_dictionaryDownCastIndirect", 0},{"take", 0},{"isUniquelyReferencedUnflaggedNative", 0},{"_getUnownedRetainCount", 0},{"isEqual", 0},{"enumerateKeysAndObjects", 0},{"returnsAutoreleased", 0},{"assertionFailure", 0},{"swapAt", 0},{"_getErrorDomainNSString", 0},{"infix_43", 0},{"copy", 0},{"union", 0},{"_maskingSubtract", 0},{"bridgeValues", 0},{"_isReleaseAssertConfiguration", 0},{"superDecoder", 0},{"bridgeKeys", 0},{"nextObject", 0},{"copyMemory", 0},{"isDisjoint", 0},{"infix_94", 0},{"_getTypeName", 0},{"_getDefaultErrorCode", 0},{"_setAtWritableKeyPath", 0},{"fetchAndOr", 0},{"_float80ToStringImpl", 0},{"_stdlib_NSDictionary_allKeys", 0},{"setValue", 0},{"addAndFetch", 0},{"transcoded", 0},{"_customRemoveLast", 0},{"_initStorageHeader", 0},{"postfix_46_46_46", 0},{"value", 0},{"_create", 0},{"fatalError", 0},{"removeValue", 0},{"compactMapValues", 0},{"_getWeakRetainCount", 0},{"_stringForPrintObject", 0},{"stringForPrintObject", 0},{"withUnsafeBufferPointer", 0},{"shouldExpand", 0},{"isMultiple", 0},{"_getErrorEmbeddedNSError", 0},{"isClass", 0},{"allSatisfy", 0},{"getChildStatus", 0},{"relative", 0},{"add", 0},{"finish", 0},{"_mergeTopRuns", 0},{"_compactMap", 0},{"_castOutputBuffer", 0},{"_deallocateUninitializedArray", 0},{"addWithExistingCapacity", 0},{"_log10", 0},{"_foreignIndex", 0},{"_typeByName", 0},{"asObjectIdentifier", 0},{"withUnsafePointer", 0},{"joined", 0},{"_copySequenceToContiguousArray", 0},{"_getTypeByMangledNameUntrusted", 0},{"dropFirst", 0},{"_fatalErrorFlags", 0},{"_dictionaryDownCast", 0},{"_nativeObject", 0},{"firstIndex", 0},{"_withVerbatimBridgedUnsafeBufferImpl", 0},{"canStoreElements", 0},{"_decodeSurrogatePair", 0},{"_int64ToStringImpl", 0},{"_getNonVerbatimBridgingBuffer", 0},{"_uncheckedFromUTF8", 0},{"_getNonVerbatimBridgedCount", 0},{"clamped", 0},{"_isBitwiseTakable", 0},{"symmetricDifference", 0},{"infix_47", 0},{"contiguousStorage", 0},{"_dictionaryDownCastConditionalIndirect", 0},{"prefix_46_46_60", 0},{"infix_61_61", 0},{"remainderReportingOverflow", 0},{"fetchAndAdd", 0},{"mapValues", 0},{"isMutableAndUniquelyReferenced", 0},{"infix_46_46_60", 0},{"infix_46_46_46", 0},{"wordCount", 0},{"_scalarName", 0},{"_getErrorCode", 0},{"prefix", 0},{"_unsafeBufferPointerCast", 0},{"drop", 0},{"_advanceForward", 0},{"_customLastIndexOfEquatableElement", 0},{"_getErrorUserInfoNSDictionary", 0},{"_customIndexOfEquatableElement", 0},{"_value", 0},{"subtract", 0},{"formUnion", 0},{"decodeIfPresent", 0},{"_arrayAppendSequence", 0},{"_cPointerArgs", 0},{"infix_47_61", 0},{"_rawHashValue", 0},{"decodeNil", 0},{"_fromInvalidUTF16", 0},{"_resolveKeyPathMetadataReference", 0},{"infix_38_42_61", 0},{"infix_38_43_61", 0},{"_swift_stdlib_atomicFetchAddInt32", 0},{"isOccupied", 0},{"join", 0},{"encodeConditional", 0},{"_bytesToUInt64", 0},{"encodeNil", 0},{"_makeBridgeObject", 0},{"singleValueContainer", 0},{"infix_46_60_61", 0},{"_setDownCast", 0},{"_withUninitializedString", 0},{"_stdlib_initializeReturnAutoreleased", 0},{"_initializeBridgedValues", 0},{"objectEnumerator", 0},{"requestNativeBuffer", 0},{"elementsEqual", 0},{"_isNotOverlong_F0", 0},{"_initializeBridgedKeys", 0},{"_forceBridgeFromObjectiveC", 0},{"_dictionaryUpCast", 0},{"_forEach", 0},{"mapError", 0},{"flatMap", 0},{"infix_126_61", 0},{"write", 0},{"_isFastAssertConfiguration", 0},{"infix_124_124", 0},{"_debuggerTestingCheckExpect", 0},{"_isOptional", 0},{"infix_60", 0},{"validate", 0},{"_invariantCheck", 0},{"dumpDiff", 0},{"_rint", 0},{"_modifyAtReferenceWritableKeyPath_impl", 0},{"dump", 0},{"_unsafeMinus", 0},{"_nearbyint", 0},{"_log2", 0},{"encoded", 0},{"_copyToContiguousArray", 0},{"_unimplementedInitializer", 0},{"lookup", 0},{"_uint64ToStringImpl", 0},{"contains", 0},{"checkValue", 0},{"capacity", 0},{"_nativeGetOffset", 0},{"_getChildCount", 0},{"_swift_stdlib_atomicFetchAndInt64", 0},{"_exp2", 0},{"unkeyedContainer", 0},{"countByEnumerating", 0},{"_invalidLength", 0},{"getCharacters", 0},{"_forceBridgeFromObjectiveC_bridgeable", 0},{"_isPowerOf2", 0},{"popLast", 0},{"_cos", 0},{"print", 0},{"_isPOD", 0},{"isLess", 0},{"bindMemory", 0},{"_customContainsEquatableElement", 0},{"_isUnique", 0},{"_outlinedMakeUniqueBuffer", 0},{"_makeNativeBridgeObject", 0},{"_hash", 0},{"sorted", 0},{"_bridgeObject", 0},{"_isASCII_cmp", 0},{"update", 0},{"addProduct", 0},{"_copyToNewBuffer", 0},{"_openExistential", 0},{"_getNonTagBits", 0},{"allocate", 0},{"appendLiteral", 0},{"_isNonTaggedObjCPointer", 0},{"ELEMENT_TYPE_OF_SET_VIOLATES_HASHABLE_REQUIREMENTS", 0},{"cast", 0},{"_nonPointerBits", 0},{"_projectMutableAddress", 0},{"_stdlib_binary_CFStringGetLength", 0},{"_bitPattern", 0},{"errorCorrectedCharacter", 0},{"infix_33_61", 0},{"_cocoaPath", 0},{"_class_getInstancePositiveExtentSize", 0},{"_abstract", 0},{"getSwiftClassInstanceExtents", 0},{"infix_33_61_61", 0},{"_uncheckedUnsafeAssume", 0},{"_utf8ScalarLength", 0},{"popFirst", 0},{"uncheckedInsert", 0},{"lexicographicallyPrecedes", 0},{"_onFastPath", 0},{"_roundingUpToAlignment", 0},{"withoutActuallyEscaping", 0},{"_slowPath", 0},{"_parseASCIISlowPath", 0},{"_roundUpImpl", 0},{"_getUnsafePointerToStoredProperties", 0},{"unsafeDowncast", 0},{"reserveCapacity", 0},{"_ensureBidirectional", 0},{"_bridgeAnythingNonVerbatimToObjectiveC", 0},{"printForDebuggerImpl", 0},{"_typeCheck", 0},{"_conditionallyUnreachable", 0},{"_withUnsafeGuaranteedRef", 0},{"_overflowChecked", 0},{"init", 0},{"_next", 0},{"_reinterpretCastToAnyObject", 0},{"_isUniquelyReferenced", 0},{"_makeCollectionDescription", 0},{"key", 0},{"_bridgeToObjectiveCImpl", 0},{"unsafeBitCast", 0},{"passRetained", 0},{"_roundUp", 0},{"_getTypeByMangledNameInContext", 0},{"isUniquelyReferencedNative", 0},{"_getBridgedNonVerbatimObjectiveCType", 0},{"_assertionFailure", 0},{"_isBridgedToObjectiveC", 0},{"_bridgeNonVerbatimFromObjectiveCConditional", 0},{"asObjectAddress", 0},{"toUIntMax", 0},{"_swift_stdlib_atomicFetchXorInt64", 0},{"_makeKeyValuePairDescription", 0},{"_getElementAddress", 0},{"_bridgeNonVerbatimBoxedValue", 0},{"_failEarlyRangeCheck", 0},{"_arrayOutOfPlaceUpdate", 0},{"_memcpy", 0},{"_firstOccupiedBucket", 0},{"passUnretained", 0},{"transcode", 0},{"_key", 0},{"infix_45", 0},{"_checkValidSubscript", 0},{"_unconditionallyBridgeFromObjectiveC", 0},{"maximumMagnitude", 0},{"toggle", 0},{"withUnsafeMutablePointer", 0},{"subtracting", 0},{"getSmallIsASCII", 0},{"load", 0},{"_typeCheckSlowPath", 0},{"intersecting", 0},{"uncheckedRemove", 0},{"_sin", 0},{"Hashable_hashValue_indirect", 0},{"withUnsafeMutablePointerToHeader", 0},{"_adoptStorage", 0},{"prefix_33", 0},{"_delete", 0},{"_getBridgedObjectiveCType", 0},{"stride", 0},{"uncheckedInitialize", 0},{"isTrailSurrogate", 0},{"parseScalar", 0},{"nestedContainer", 0},{"_modifyAtWritableKeyPath_impl", 0},{"uncheckedContains", 0},{"fetchAndAnd", 0},{"_checkSubscript", 0},{"isValid", 0},{"split", 0},{"ensureUnique", 0},{"_hasGraphemeBreakBetween", 0},{"bit", 0},{"_fastPath", 0},{"withUnsafeMutablePointerToElements", 0},{"_getSymbolicMangledNameLength", 0},{"_preconditionFailure", 0},{"replaceSubrange", 0},{"formIntersection", 0},{"isLessThanOrEqualTo", 0},{"_unreachable", 0},{"withMemoryRebound", 0},{"reversed", 0},{"dividedReportingOverflow", 0},{"lastIndex", 0},{"xorAndFetch", 0},{"_bridgeToObjectiveC", 0},{"_resolveKeyPathGenericArgReference", 0},{"max", 0},{"_distance", 0},{"_getObjCTypeEncoding", 0},{"sort", 0},{"removeFirst", 0},{"_swift_isClassOrObjCExistentialType", 0},{"getRuntimeFunctionNames", 0},{"_checkInoutAndNativeTypeCheckedBounds", 0},{"_nullCodeUnitOffset", 0},{"_convert", 0},{"_foreignAppendInPlace", 0},{"_swift_stdlib_atomicFetchXorInt32", 0},{"_expectEnd", 0},{"_swift_stdlib_atomicFetchOrInt", 0},{"formIndex", 0},{"withContiguousMutableStorageIfAvailable", 0},{"_unsafeDowncastToAnyObject", 0},{"ivarCount", 0},{"withUTF8", 0},{"_swift_stdlib_atomicFetchAndInt32", 0},{"_insertionSort", 0},{"_swift_stdlib_atomicFetchAndInt", 0},{"_swift_stdlib_atomicStoreInt", 0},{"_isDebugAssertConfiguration", 0},{"infix_43_61", 0},{"_swift_stdlib_atomicCompareExchangeStrongInt", 0},{"formSymmetricDifference", 0},{"_swift_stdlib_atomicFetchAddInt64", 0},{"withUnsafeMutableBufferPointer", 0},{"_deallocateUninitialized", 0},{"orAndFetch", 0},{"compareExchange", 0},{"_allASCII", 0},{"fetchAndXor", 0},{"withFastCChar", 0},{"_undefined", 0},{"__customContainsEquatableElement", 0},{"infix_38_45", 0},{"_tryToAppendKeyPaths", 0},{"precondition", 0},{"_findStringSwitchCase", 0},{"_getRetainCount", 0},{"last", 0},{"infix_62", 0},{"_unsafeReferenceCast", 0},{"_allocateBufferUninitialized", 0},{"_debugPrecondition", 0},{"_getForeignCodeUnit", 0},{"clear", 0},{"_makeMutableAndUnique", 0},{"_isObjCTaggedPointer", 0},{"_unsafeUnbox", 0},{"_precondition", 0},{"_isDisjoint", 0},{"_swift_stdlib_atomicFetchAddInt", 0},{"_fromASCII", 0},{"overlaps", 0},{"_cocoaGetCStringTrampoline", 0},{"finishWithOriginalCount", 0},{"collectAllReferencesInsideObject", 0},{"addWithOverflow", 0},{"finalize", 0},{"encode", 0},{"_unsafeInsertNew", 0},{"withMutableCapacity", 0},{"type", 0},{"round", 0},{"_parseASCII", 0},{"_checkInoutAndNativeBounds", 0},{"_slowUTF8CString", 0},{"_subtract", 0},{"_unsafeUncheckedDowncast", 0},{"_partitionImpl", 0},{"requestUniqueMutableBackingBuffer", 0},{"_getEmbeddedNSError", 0},{"_collectReferencesInsideObject", 0},{"_forceCreateUniqueMutableBufferImpl", 0},{"mutatingFind", 0},{"retain", 0},{"infix_60_60_61", 0},{"assert", 0},{"determineCodeUnitCapacity", 0},{"bridgedElement", 0},{"getObjects", 0},{"_downCastConditional", 0},{"_allocateUninitialized", 0},{"_branchHint", 0},{"_isEqual", 0},{"reserveCapacityForAppend", 0},{"_toCustomAnyHashable", 0},{"_debugPrint_unlocked", 0},{"_map", 0},{"nestedUnkeyedContainer", 0},{"remove", 0},{"multipliedFullWidth", 0},{"foreignScalarAlign", 0},{"_swift_stdlib_atomicFetchXorInt", 0},{"_writeASCII", 0},{"_tryNormalize", 0},{"_asciiDigit", 0},{"_withUnsafeMutableBufferPointerIfSupported", 0},{"_log", 0},{"_conditionallyBridgeFromObjectiveC", 0},{"_checkSubscript_native", 0},{"_tryFromUTF8", 0},{"container", 0},{"_makeUniqueAndReserveCapacityIfNotUnique", 0},{"infix_38_45_61", 0},{"infix_46_60", 0},{"hashValue", 0},{"asStringRepresentation", 0},{"_appendElementAssumeUniqueAndCapacity", 0},{"_replDebugPrintln", 0},{"isOnUnicodeScalarBoundary", 0},{"_arrayOutOfPlaceReplace", 0},{"_stableSortImpl", 0},{"withUnsafeMutableBytes", 0},{"withNFCCodeUnitsIterator_2", 0},{"formTruncatingRemainder", 0},{"infix_46_38", 0},{"min", 0},{"_collectAllReferencesInsideObjectImpl", 0},{"ensureUniqueNative", 0},{"successor", 0},{"_unbox", 0},{"_makeObjCBridgeObject", 0},{"idealBucket", 0},{"subtractWithOverflow", 0},{"_getSuperclass", 0},{"_insert", 0},{"index", 0},{"copyBytes", 0},{"_typeName", 0},{"_stringCompareWithSmolCheck", 0},{"_isBridgedVerbatimToObjectiveC", 0},{"infix_46_124", 0},{"_fixLifetime", 0},{"insert", 0},{"repeatElement", 0},{"_bridgeNonVerbatimFromObjectiveCToAny", 0},{"random", 0},{"withVaList", 0},{"_swift_bufferAllocate", 0},{"_isStdlibInternalChecksEnabled", 0},{"_getCapacity", 0},{"_getCount", 0},{"reverse", 0},{"next", 0},{"_arrayDownCastIndirect", 0},{"KEY_TYPE_OF_DICTIONARY_VIOLATES_HASHABLE_REQUIREMENTS", 0},{"_fromSubstring", 0},{"_foreignIsWithin", 0},{"_diagnoseUnexpectedEnumCase", 0},{"dropLast", 0},{"_bridgeNonVerbatimFromObjectiveC", 0},{"_forceCreateUniqueMutableBuffer", 0},{"_finalize", 0},{"getElement", 0},{"infix_62_62", 0},{"descendant", 0},{"_swift_stdlib_atomicFetchOrInt32", 0},{"_isTaggedObject", 0},{"_withUnsafeBufferPointerToUTF8", 0},{"filter", 0},{"isContinuation", 0},{"distance", 0},{"flatMapError", 0},{"subscript", 0},{"_findBoundary", 0},{"_copyContents", 0},{"_asCocoaArray", 0},{"isUniquelyReferenced", 0},{"_getElement", 0},{"_autorelease", 0},{"andAndFetch", 0},{"_allocateUninitializedArray", 0}
};

std::unordered_map<std::string, std::string> nameReplacements = {};

std::string regex_escape(std::string replacement) {
  return std::regex_replace(replacement, std::regex("\\$"), "$$$$");
}

std::string afterStruct = "";

std::vector<BraceStmt*> openedBraceStmts = {};
std::vector<std::pair<BraceStmt*, Expr*>> braceStmtsWithDefer = {};

std::string matchNameReplacement(std::string name, std::vector<unsigned> indexes) {
  for(auto index : indexes) name += "[" + std::to_string(index) + "]";
  return name;
}

std::string getOperatorFix(ValueDecl *D) {
  if(D->isOperator()) {
    if(auto *functionDecl = dyn_cast<FuncDecl>(D)) {
      if(auto *op = functionDecl->getOperatorDecl()) {
        switch (op->getKind()) {
          case DeclKind::PrefixOperator:
            return "prefix";
          case DeclKind::PostfixOperator:
            return "postfix";
          case DeclKind::InfixOperator:
            return "infix";
          default:
            llvm_unreachable("unexpected operator kind");
        }
      }
    }
  }
  return "";
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
  
  //D->dumpRef(stream);//we need parameter types as well
  
  printContext(stream, D->getDeclContext());
  stream << ".";
  
  // Print name.
  stream << D->getFullName().getBaseName();
  
  ParameterList *params = nullptr;
  if(auto *functionDecl = dyn_cast<AbstractFunctionDecl>(D)) {
    params = functionDecl->getParameters();
    stream << getOperatorFix(functionDecl);
  }
  else if(auto *subscriptDecl = dyn_cast<SubscriptDecl>(D)) {
    params = subscriptDecl->getIndices();
  }
  if(params) {
    stream << "(";
    bool first = true;
    for(auto *P : *params) {
      if(first) first = false;
      else stream << ",";
      stream << P->getArgumentName();
      if(P->hasType()) {
        stream << ":";
        P->getType()->print(stream);
      }
      else if(P->hasInterfaceType()) {
        stream << ":";
        P->getInterfaceType()->print(stream);
      }
    }
    stream << ")";
  }
  
  auto &srcMgr = D->getASTContext().SourceMgr;
  if (D->getLoc().isValid()) {
    stream << '@';
    D->getLoc().print(stream, srcMgr);
  }
  
  //std::cout << "/*" << stream.str() << "*/";
  return stream.str();
}
std::string getReplacement(ValueDecl *D, ConcreteDeclRef DR = nullptr, bool isAss = false) {
  /*std::string memberIdentifier = getMemberIdentifier(D);
  if(isAss && REPLACEMENTS.count(memberIdentifier + "#ASS")) {
    return REPLACEMENTS.at(memberIdentifier + "#ASS");
  }
  if(REPLACEMENTS.count(memberIdentifier)) {
    return REPLACEMENTS.at(memberIdentifier);
  }*/
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
    baseStr = "_cloneStruct(" + baseStr + ")";
  }
  return baseStr;
}

bool isNative(std::string uniqueIdentifier) {
  return uniqueIdentifier.find("Swift.(file).") == 0 || uniqueIdentifier.find("XCTest.(file).") == 0 || uniqueIdentifier.find("ObjectiveC.(file).") == 0 || uniqueIdentifier.find("Darwin.(file).") == 0 || uniqueIdentifier.find("Foundation.(file).") == 0;
}
std::string getFunctionName(ValueDecl *D) {
  std::string uniqueIdentifier = getMemberIdentifier(D);
  std::string userFacingName = D->getBaseName().userFacingName();
  if(!functionUniqueNames.count(uniqueIdentifier)) {
    if(D->isOperator()) {
      std::string stringifiedOp = getOperatorFix(D);
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

    if(!isNative(uniqueIdentifier)) {
      if(functionOverloadedCounts.count(overloadIdentifier)) {
        functionOverloadedCounts[overloadIdentifier] += 1;
        functionUniqueNames[uniqueIdentifier] += std::to_string(functionOverloadedCounts[overloadIdentifier]);
      }
      else {
        functionOverloadedCounts[overloadIdentifier] = 0;
      }
    }
    else {
      if(LIB_GENERATE_MODE) {
        libFunctionOverloadedCounts[overloadIdentifier] = true;
      }
      ParameterList *params = nullptr;
      if(auto *functionDecl = dyn_cast<AbstractFunctionDecl>(D)) {
        params = functionDecl->getParameters();
      }
      else if(auto *subscriptDecl = dyn_cast<SubscriptDecl>(D)) {
        params = subscriptDecl->getIndices();
      }
      if(params) {
        bool isInit = functionUniqueNames[uniqueIdentifier] == "init";
        for(auto *P : *params) {
          auto argumentId = P->getArgumentName();
          if(!argumentId.empty()) {
            std::string argumentName = argumentId.get();
            if(argumentName != "_" && argumentName.length() > 0) {
              functionUniqueNames[uniqueIdentifier] += std::toupper(argumentName[0]);
              functionUniqueNames[uniqueIdentifier] += argumentName.substr(1);
            }
          }
          if(isInit) {
            std::string str;
            llvm::raw_string_ostream stream(str);
            if(P->hasType()) {
              P->getType()->print(stream);
            }
            else if(P->hasInterfaceType()) {
              P->getInterfaceType()->print(stream);
            }
            else continue;
            functionUniqueNames[uniqueIdentifier] += std::regex_replace(stream.str(), std::regex("[^a-zA-Z0-9_]"), "");
          }
        }
      }
    }
  }
  return functionUniqueNames[uniqueIdentifier];
}
ValueDecl *getDeclRoot(ValueDecl *D, unsigned long satisfiedProtocolRequirementI = 0) {
  while (auto *overriden = D->getOverriddenDecl()) {
    D = overriden;
  }
  auto satisfiedProtocolRequirements = D->getSatisfiedProtocolRequirements();
  if(satisfiedProtocolRequirementI > 0 && satisfiedProtocolRequirementI >= satisfiedProtocolRequirements.size()) {
    return nullptr;
  }
  if(!satisfiedProtocolRequirements.empty()) {
    D = D->getSatisfiedProtocolRequirements()[satisfiedProtocolRequirementI];
  }
  return D;
}
std::string getName(ValueDecl *D, unsigned long satisfiedProtocolRequirementI = 0) {
  D = getDeclRoot(D, satisfiedProtocolRequirementI);
  if(!D) return "!NO_DUPLICATE";
  
  std::string name;
  
  if(auto *functionDecl = dyn_cast<AbstractFunctionDecl>(D)) {
    name = getFunctionName(functionDecl);
  }
  else if(auto *subscriptDecl = dyn_cast<SubscriptDecl>(D)) {
    name = getFunctionName(subscriptDecl);
  }
  else if(D->hasName()) {
    name = D->getBaseName().userFacingName();
  }
  else {
    name = "_";
  }
  
  if(LIB_GENERATE_MODE && LIB_MIXINS.count(getMemberIdentifier(D))) {
    name = "MIO_Mixin_" + name;
  }
  
  if(nameReplacements.count(name)) {
    return nameReplacements[name];
  }
  
  if(std::find(std::begin(RESERVED_VAR_NAMES), std::end(RESERVED_VAR_NAMES), name) != std::end(RESERVED_VAR_NAMES)) {
    name = "_" + name;
  }
  
  return name;
}
std::string getLibBody(ValueDecl *D, bool isAssignment = false) {
  std::string memberIdentifier = getMemberIdentifier(D);
  if(isAssignment) memberIdentifier += "#ASS";
  if(LIB_BODIES.count(memberIdentifier)) return LIB_BODIES.at(memberIdentifier);
  for(unsigned long i = 0; ; i++) {
    auto *declRoot = getDeclRoot(D, i);
    if(!declRoot) break;
    std::string memberIdentifier = getMemberIdentifier(declRoot);
    if(isAssignment) memberIdentifier += "#ASS";
    if(LIB_BODIES.count(memberIdentifier)) return LIB_BODIES.at(memberIdentifier);
  }
  return "";
}

std::string getTypeName(Type T) {
  std::string str;
  llvm::raw_string_ostream stream(str);
  T->dump(stream);
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

struct AllMembers{ std::list<Decl*> members; std::list<Type> inherited; Decl* lastNonExtensionMember; };
void getAllMembers2(NominalTypeDecl *D, AllMembers &result, bool recursive, bool pushMembers = true) {
  std::list<Decl*> members;
  for (auto *M : D->getMembers()) {if(pushMembers)result.members.push_back(M);members.push_back(M);}
  for (auto I : D->getInherited()) result.inherited.push_back(I.getType());
  result.lastNonExtensionMember = result.members.empty() ? nullptr : result.members.back();
  for (auto E : D->getExtensions()) {
    for (auto *M : E->getMembers()) {if(pushMembers)result.members.push_back(M);members.push_back(M);}
    for (auto I : E->getInherited()) result.inherited.push_back(I.getType());
  }
  if(recursive) {
    for(auto *member : members) {
      if(auto *nMember = dyn_cast<NominalTypeDecl>(member)) {
        getAllMembers2(nMember, result, true, false);
      }
    }
  }
}
AllMembers getAllMembers(NominalTypeDecl *D, bool recursive) {
  AllMembers result;
  getAllMembers2(D, result, recursive);
  return result;
}

std::string printGenericParams(GenericParamList *genericParams) {
  if(!genericParams) return "";
  
  std::string result = "";
  result += "<";
  bool first = true;
  for(auto *param : *genericParams) {
    if(first) first = false;
    else result += ", ";
    result += getName(param);
  }
  result += ">";
  
  return result;
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
      /*printCommon(ID, "import_decl");

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
      OS << "')";*/
      
      if(GENERATE_IMPORTED_MODULE) {
        generateLibForModule(ID->getModule());
      }
    }

    void visitExtensionDecl(ExtensionDecl *ED) {
      /*printCommon(ED, "extension_decl", ExtensionColor);
      OS << ' ';
      ED->getExtendedType().print(OS);
      printInherited(ED->getInherited());
      for (Decl *Member : ED->getMembers()) {
        OS << '\n';
        printRec(Member);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
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
      if(LIB_GENERATE_MODE) return;
      
      if(TAD->getDeclContext()->isTypeContext()) {
        OS << "static readonly ";
      }
      else {
        OS << "const ";
      }
      OS << getName(TAD);
      if (TAD->getUnderlyingTypeLoc().getType()) {
        OS << " = " << getTypeName(TAD->getUnderlyingTypeLoc().getType());
      }
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
      
      visitAnyStructDecl("protocol", PD);
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
    
    void generateLibForModule(ModuleDecl *MD) {
      
      //SmallVector<Decl *, 64> topLevelDecls;
      //MD->getTopLevelDecls(topLevelDecls);
      SmallVector<Decl *, 64> displayDecls;
      MD->getDisplayDecls(displayDecls);
      //SmallVector<TypeDecl *, 64> localTypeDecls;
      //MD->getLocalTypeDecls(localTypeDecls);
      
      std::cout << "\n" << MD->getName().get();


      std::error_code OutErrorInfoOrder;
      llvm::raw_fd_ostream orderFile(llvm::StringRef(LIB_GENERATE_PATH + MD->getName().get() + "/inclusionOrder.txt"), OutErrorInfoOrder, llvm::sys::fs::F_None);
      
      //SmallVector<Decl *, 64> topLevelDecls;
      //MD->getTopLevelDecls(topLevelDecls);
      //SmallVectorImpl<swift::TypeDecl *>
      //MD->getLocalTypeDecls(topLevelDecls);
      //MD->getDisplayDecls(topLevelDecls);
      std::unordered_map<ValueDecl*, std::vector<std::string>> allMembers;
      std::list<ValueDecl*> orderedList;
      std::list<ValueDecl*> unorderedList;
      
      for (Decl *D : displayDecls) {
        std::string outName;
        bool isExtension = false;
        if(auto *VD = dyn_cast<ValueDecl>(D)) {
          outName = getName(VD);
          if(auto *NVD = dyn_cast<NominalTypeDecl>(VD)) {
            std::vector<std::string> allMembersStr;
            for(auto member : getAllMembers(NVD, true).inherited) {
              std::string name = getTypeName(member);
              //TODO understand protocol compositions
              if(name == "Codable") {
                allMembersStr.push_back("Decodable");
                allMembersStr.push_back("Encodable");
              }
              else {
                allMembersStr.push_back(name);
              }
            }
            allMembers[NVD] = allMembersStr;
            unorderedList.push_back(NVD);
          }
          /*else if(auto *TAD = dyn_cast<TypeAliasDecl>(VD)) {
            OS << "\n!" << getName(TAD);
            std::vector<std::string> allMembersStr;
            if (TAD->getUnderlyingTypeLoc().getType()) {
              //bodge way to get needed types by matching words in type name
              std::smatch sm;
              OS << "  " << getTypeName(TAD->getUnderlyingTypeLoc().getType());
              std::regex_match (getTypeName(TAD->getUnderlyingTypeLoc().getType()), sm, std::regex("[A-Za-z0-9_]+"));
              for(auto match : sm) {
                OS << "  " << match;
                allMembersStr.push_back(match);
              }
            }
            allMembers[NVD] = allMembersStr;
            unorderedList.push_back(TAD);
          }*/
          else {
            orderFile << "'" << std::regex_replace(outName, std::regex("MIO_Mixin_"), "") << "',";
          }
        }
        else if(auto *ED = dyn_cast<ExtensionDecl>(D)) {
          outName = getTypeName(ED->getExtendedType());
          isExtension = true;
        }
        else continue;
        outName = std::regex_replace(outName, std::regex("MIO_Mixin_"), "");
        std::error_code OutErrorInfo;
        llvm::raw_fd_ostream outFile(llvm::StringRef(LIB_GENERATE_PATH + MD->getName().get() + "/" + outName + ".ts"), OutErrorInfo, isExtension ? llvm::sys::fs::F_Append : llvm::sys::fs::F_None);
        PrintDecl(outFile, Indent + 2).visit(D);
        outFile << "\n\n";
        outFile.close();
      }
      
      while(!unorderedList.empty()) {
        auto i = unorderedList.begin();
        while (i != unorderedList.end()) {
          auto *NVD = *i;
          std::cout << '\n' << getName(NVD);
          bool allPresent = true;
          for(auto inherited : allMembers[NVD]) {
            bool inheritedPresent = true;
            for(auto unordered : unorderedList) {
              if(getName(unordered) == inherited) {inheritedPresent = false;break;}
            }
            if(!inheritedPresent) {allPresent = false;break;}
          }
          if(allPresent) {
            orderedList.push_back(NVD);
            unorderedList.erase(i++);
          }
          else ++i;
        }
      }
      for (auto *D : orderedList) {
        orderFile << "'" << std::regex_replace(getName(D), std::regex("MIO_Mixin_"), "") << "',";
      }
      
      std::error_code OutErrorInfoOverloadedCounts;
      llvm::raw_fd_ostream overloadedCountsFile(llvm::StringRef(LIB_GENERATE_PATH + MD->getName().get() + "/libFunctionOverloadedCounts.txt"), OutErrorInfoOverloadedCounts, llvm::sys::fs::F_None);
      for(auto pair: libFunctionOverloadedCounts) {
        overloadedCountsFile << "{\"" << pair.first << "\", 0},";
      }
      overloadedCountsFile.close();
      orderFile.close();
    }

    void visitSourceFile(const SourceFile &SF) {
      /*OS.indent(Indent);
      PrintWithColorRAII(OS, ParenthesisColor) << '(';
      PrintWithColorRAII(OS, ASTNodeColor) << "source_file ";
      PrintWithColorRAII(OS, LocationColor) << '\"' << SF.getFilename() << '\"';*/
      
      if(GENERATE_STD_LIB) {
        generateLibForModule(SF.getASTContext().getStdlibModule());
      }
      else {
        for (Decl *D : SF.Decls) {
          /*if (D->isImplicit())
           continue;*/
          
          OS << '\n';
          printRec(D);
        }
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
      visitAnyStructDecl("enum", ED);
    }

    void visitEnumElementDecl(EnumElementDecl *EED) {
      /*printCommon(EED, "enum_element_decl");
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      OS << "\nstatic ";
      if(!EED->hasAssociatedValues()) OS << "get ";
      OS << getName(EED) << "() {return ";
      OS << "Object.assign(new " << getTypeName(EED->getParentEnum()->getDeclaredInterfaceType()) << "(), ";
      OS << "{rawValue: ";
      if(EED->hasRawValueExpr()) {
        OS << dumpToStr(EED->getRawValueExpr());
      }
      else {
        OS << '"' << getName(EED) << '"';
      }
      OS << ", ...Array.from(arguments).slice(1)})}";
    }
    
    void printAnyStructSignature(std::string definition, std::string name, NominalTypeDecl *D) {
      if(D->getDeclContext()->isTypeContext()) {
        OS << "static " << name << " = " << definition;
      }
      else {
        OS << definition << " " << name;
      }
    }
    
    void visitAnyStructDecl(std::string kind, NominalTypeDecl *D) {
      
      auto all = getAllMembers(D, false);
      std::list<Decl*> members = all.members;
      std::list<Type> inherited = all.inherited;
      std::list<std::string> implementedProtocols;
      Decl* lastNonExtensionMember = all.lastNonExtensionMember;
      
      std::string name = getName(D);
      std::string nestedName = getTypeName(D->getDeclaredType());
      
      std::string definition = kind == "protocol" ? "interface" : "class";
      printAnyStructSignature(definition, name, D);
      
      if(kind == "protocol") {
        bool wasAssociatedType = false;
        for (Decl *subD : members) {
          if(auto *associatedTypeDecl = dyn_cast<AssociatedTypeDecl>(subD)) {
            if(!wasAssociatedType) {
              OS << "<";
              wasAssociatedType = true;
            }
            else {
              OS << ", ";
            }
            OS << getName(associatedTypeDecl);
          }
        }
        if(wasAssociatedType) OS << ">";
      }
      else {
        OS << printGenericParams(D->getGenericParams());
      }

      bool wasClass = false, wasProtocol = false;
      if(!inherited.empty() && kind != "enum") {
        for(auto Super : inherited) {
          bool isProtocol = Super->isExistentialType();
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
          OS << getTypeName(Super);
          if(isProtocol) implementedProtocols.push_back(getTypeName(Super));
        }
      }
      
      OS << "{";
      
      if(kind == "struct") {
        OS << "\nstatic readonly $struct = true";
      }
      if(LIB_GENERATE_MODE && LIB_MIXINS.count(getMemberIdentifier(D))) {
        OS << "\nstatic readonly $mixin = true";
      }
      if(kind != "protocol") {
        OS << "\nstatic readonly $infoAddress = '" << D->getInnermostDeclContext() << "'";
      }

      if(LIB_GENERATE_MODE && LIB_CLONE_STRUCT_FILLS.count(getMemberIdentifier(D))) {
        OS << "\ncloneStructFill" << LIB_CLONE_STRUCT_FILLS.at(getMemberIdentifier(D));
      }
      
      bool protocolImplementation = false;
      for (Decl *subD : members) {
        OS << '\n';
        bool shouldPrint = !protocolImplementation && kind == "protocol" && (subD == lastNonExtensionMember || !lastNonExtensionMember);
        bool shouldPrintBefore = shouldPrint && !lastNonExtensionMember;
        if(!shouldPrintBefore) printRec(subD);
        if(shouldPrint) {
          OS << "\n}\n";
          printAnyStructSignature("class", name + "$implementation", D);
          OS << "{";
          protocolImplementation = true;
        }
        if(shouldPrintBefore) printRec(subD);
      }
      
      if(kind == "enum") {
        OS << "\nstatic infix_61_61($info, a, b){return a.rawValue == b.rawValue}";
        OS << "\nstatic infix_33_61($info, a, b){return a.rawValue != b.rawValue}";
      }
      
      OS << "\n}";
      
      if(kind != "protocol" || protocolImplementation) {
        for (auto implementedProtocol : implementedProtocols) {
          size_t dotPos = 0;
          afterStruct += "\nif(";
          while((dotPos = implementedProtocol.find(".", dotPos + 1)) != std::string::npos) {
            afterStruct += "typeof " + implementedProtocol.substr(0, dotPos) + " != 'undefined' && ";
          }
          afterStruct += "typeof " + implementedProtocol + "$implementation != 'undefined') _mixin(" + nestedName + (protocolImplementation ? "$implementation" : "") + ", " + implementedProtocol + "$implementation, false)";
        }
      }
      
      if(LIB_GENERATE_MODE && LIB_MIXINS.count(getMemberIdentifier(D))) {
        afterStruct += "\n_mixin(" + LIB_MIXINS.at(getMemberIdentifier(D)) + ", " + nestedName + ", true)";
        std::string notPrefixedName = std::regex_replace(nestedName, std::regex("MIO_Mixin_"), "");
        if(notPrefixedName != LIB_MIXINS.at(getMemberIdentifier(D))) {
          afterStruct += "\nclass " + notPrefixedName + "{}";
          afterStruct += "\n_mixin(" + notPrefixedName + ", " + nestedName + ", true)";
        }
      }
      if(!D->getDeclContext()->isTypeContext()) {
        OS << afterStruct;
        afterStruct = "";
      }
    }

    void visitStructDecl(StructDecl *SD) {
      /*printCommon(SD, "struct_decl");
      printInherited(SD->getInherited());
      for (Decl *D : SD->getMembers()) {
        OS << '\n';
        printRec(D);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      visitAnyStructDecl("struct", SD);
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
      visitAnyStructDecl("class", CD);
    }
    
    using FlattenedPattern = std::vector<std::pair<std::vector<unsigned>, const Pattern*>>;
    FlattenedPattern flattenPattern(const Pattern *P) {
      
      FlattenedPattern result;
      std::vector<unsigned> access;
      
      walkPattern(P, result, access);
      
      return result;
    }
    void walkPattern(const Pattern *P, FlattenedPattern &info, const std::vector<unsigned> &access) {
      
      if(!P) return;
      
      if(auto *tuplePattern = dyn_cast<TuplePattern>(P)) {
        unsigned i = 0;
        for (auto elt : tuplePattern->getElements()) {
          std::vector<unsigned> elAccess(access);
          elAccess.push_back(i++);
          walkPattern(elt.getPattern(), info, elAccess);
        }
      }
      else if(auto *wrapped = dyn_cast<IsPattern>(P)) {
        info.push_back(std::make_pair(access, P));
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
        info.push_back(std::make_pair(access, P));
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
        info.push_back(std::make_pair(access, P));
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
              info.varName = getName(VD);
            }
          }
          
          if(!VD->getDeclContext()->getSelfProtocolDecl()) {
            for (auto accessor : VD->getAllAccessors()) {
              //swift autogenerates getter/setters for regular vars; no need to display them
              if(accessor->isImplicit()) continue;
              
              std::string accessorType = getAccessorKindString(accessor->getAccessorKind());
              std::string bodyStr = printFuncSignature(accessor->getParameters(), accessor->getGenericParams(), nullptr, false) + printFuncBody(accessor);
              
              info.accessorBodies[accessorType] = bodyStr;
            }
          }
          
          info.varNames.push_back(getName(VD));
          
          if(node.first.size()) {
            info.tupleInit += ", ";
            info.tupleInit += getName(VD);
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
        bool withinStruct = info.varDecl && info.varDecl->getDeclContext()->isTypeContext();
        
        if(LIB_GENERATE_MODE) {
          if(info.varName[0] == '_') continue;
          OS << "\n/*" << getMemberIdentifier(info.varDecl) << "*/";
          if(!getLibBody(info.varDecl).length() && (!info.varDecl->getDeclContext()->getSelfProtocolDecl() || info.varDecl->getDeclContext()->getExtendedProtocolDecl())) OS << "/*";
        }
        
        bool isOverriden = false;
        if (withinStruct) {
          if (auto *overriden = entry.getPattern()->getSingleVar()->getOverriddenDecl()) {
            isOverriden = true;
          }
        }
        
        if((!isOverriden || entry.getInit()) && !LIB_GENERATE_MODE) {
          if(info.varDecl) {
            OS << "\n" << info.varPrefix << info.varName;
          }
          else {
            OS << "\n const _";
          }
          if(withinStruct && !info.varDecl->getDeclContext()->getSelfProtocolDecl()) {
            OS << "$internal";
          }
          if(entry.getInit()) {
            OS << " = " << handleRAssignment(entry.getInit(), dumpToStr(entry.getInit()));
          }
        }
        
        if(withinStruct && !info.varDecl->getDeclContext()->getSelfProtocolDecl()) {
          std::string internalGetVar = "this." + info.varName + "$internal";
          std::string internalSetVar = "this." + info.varName + "$internal = $newValue";
          if(isOverriden) {
            internalGetVar = "super." + info.varName + "$get()";
            internalSetVar = "super." + info.varName + "$set($newValue)";
          }
          
          OS << "\n" << info.varPrefix << info.varName << "$get";
          if(LIB_GENERATE_MODE) {
            std::string defaultBody = "return this." + info.varName;
            if(getLibBody(info.varDecl).length()) defaultBody = getLibBody(info.varDecl);
            OS << "() {\n" + defaultBody + "\n}";
          }
          else if(info.accessorBodies.count("get")) {
            OS << info.accessorBodies["get"];
          }
          else {
            OS << "() { return " << internalGetVar << " }";
          }
          OS << "\n" << info.varPrefix << "get " << info.varName << "() { return this." << info.varName << "$get() }";

          if((info.accessorBodies.count("set") || !info.accessorBodies.count("get")) && !LIB_GENERATE_MODE) {
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
        
        if(LIB_GENERATE_MODE && !getLibBody(info.varDecl).length() && (!info.varDecl->getDeclContext()->getSelfProtocolDecl() || info.varDecl->getDeclContext()->getExtendedProtocolDecl())) OS << "*/";
        
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

    void printParameter(ParamDecl *P, raw_ostream &OS) {
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
      
      OS << getName(P);
      
      if(P->isInOut()) {
        OS << "$inout";
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
    
    std::string printFuncSignature(ParameterList *params, GenericParamList *genericParams, DeclContext *context = nullptr, bool printInfo = true) {
      
      std::string signature = "";

      signature += printGenericParams(genericParams);

      signature += "(";
      signature += printFuncParams(params, context, printInfo);
      signature += ")";

      return signature;
    }
    
    std::string printFuncParams(ParameterList *params, DeclContext *context = nullptr, bool printInfo = true) {
      std::string signature = "";
      bool first = true;
      if(printInfo) {
        signature += "$info";
        first = false;
        if(context) {
          std::string str;
          llvm::raw_string_ostream stream(str);
          stream << context;
          signature += stream.str();
        }
      }
      if(params) {
        for (auto P : *params) {
          if(first) first = false;
          else signature += ", ";
          std::string parameterStr;
          llvm::raw_string_ostream parameterStream(parameterStr);
          printParameter(P, parameterStream);
          signature += parameterStream.str();
        }
      }
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
            std::string paramName = getName(P);
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
            std::string paramName = getName(P);
            result += "\n" + paramName + "$inout.set(" + paramName + ")";
          }
        }
      }
      if(result.length()) result = "})()" + result + "\nreturn $result";
      return result;
    }
    
    struct LibGeneratedFuncBody {
      std::string str = "";
    };
    LibGeneratedFuncBody libGenerateFuncBody(AbstractFunctionDecl *FD, ValueDecl *NameD) {
      bool isAssignment = false;
      std::string userFacingName = FD->getBaseName().userFacingName();
      LibGeneratedFuncBody result;
      
      auto *params = FD->getParameters();
      std::vector<std::string> paramRepr;
      if(params) {
        int i = 0;
        for (auto P : *params) {
          paramRepr.push_back("#A" + std::to_string(i++) + (P->isAutoClosure() ? "()" : ""));
        }
      }

      if(auto *accessorDecl = dyn_cast<AccessorDecl>(FD)) {
        if(getAccessorKindString(accessorDecl->getAccessorKind()) == "set") {
          //subscript set
          isAssignment = true;
          result.str = "this[" + paramRepr[1] + "] = " + paramRepr[0];
        }
        else {
          //subscript get
          result.str = "return this[" + paramRepr[0] + "]";
        }
      }
      else if(auto *constructorDecl = dyn_cast<ConstructorDecl>(FD)) {
        //constructor
        result.str = "";
      }
      else if(FD->isOperator()) {
        //operator
        std::string operatorFix = getOperatorFix(FD);
        if(std::find(std::begin(ASSIGNMENT_OPERATORS), std::end(ASSIGNMENT_OPERATORS), userFacingName) != std::end(ASSIGNMENT_OPERATORS)) {
          result.str = paramRepr[0] + ".set(" + paramRepr[0] + ".get() " + userFacingName.substr(0, userFacingName.length() - 1) + " " + paramRepr[1] + ")";
        }
        else if(operatorFix == "prefix") {
          result.str = "return " + userFacingName + paramRepr[0];
        }
        else if(operatorFix == "postfix") {
          result.str = "return " + paramRepr[0] + userFacingName;
        }
        else {
          result.str = "return " + paramRepr[0] + " " + userFacingName + " " + paramRepr[1];
        }
        //js doesn't understand operators starting with &/.
        if((userFacingName[0] == '&' && userFacingName != "&&") || userFacingName == "~=" || userFacingName[0] == '.') {
          result.str = "/*" + result.str + "*/";
        }
      }
      else {
        //regular function
        result.str = "/*return this." + userFacingName + "(#AA)*/";
      }
      std::string libBody = getLibBody(NameD, isAssignment);
      if(libBody.length()) {
        result.str = libBody;
      }
      
      if(result.str.find("#AA") != std::string::npos) {
        result.str = std::regex_replace(result.str, std::regex("#AA"), regex_escape(printFuncParams(params, nullptr, false)));
      }
      else if(params) {
        int i = 0;
        for (auto P : *params) {
          std::string parameterStr;
          llvm::raw_string_ostream parameterStream(parameterStr);
          printParameter(P, parameterStream);
          result.str = std::regex_replace(result.str, std::regex("#A" + std::to_string(i)), regex_escape(parameterStream.str()));
          i++;
        }
      }
      
      return result;
    }

    std::string printAbstractFunc(AbstractFunctionDecl *FD, ValueDecl *NameD = nullptr, std::string suffix = "") {
      
      if(!NameD) NameD = FD;
      std::string str = "";
      
      LibGeneratedFuncBody libGeneratedFuncBody;
      if(LIB_GENERATE_MODE) {
        str += "/*" + getMemberIdentifier(NameD) + "*/\n";
        for(unsigned long i = 0; ; i++) {
          auto *declRoot = getDeclRoot(NameD, i);
          if(!declRoot) break;
          str += "/*" + getMemberIdentifier(declRoot) + "*/\n";
        }
        libGeneratedFuncBody = libGenerateFuncBody(FD, NameD);
      }
      
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
      
      if(LIB_GENERATE_MODE && functionName[0] == '_') {
        return "";
      }
      
      std::string signature = printFuncSignature(FD->getParameters(), FD->getGenericParams(), FD->getInnermostDeclContext());
      str += signature;

      if(LIB_GENERATE_MODE && (!FD->getDeclContext()->getSelfProtocolDecl() || FD->getDeclContext()->getExtendedProtocolDecl())) {
        str += " {\n" + libGeneratedFuncBody.str + "\n}";
      }
      else {
        str += printFuncBody(FD);
      }
      
      std::unordered_map<std::string, bool> duplicateNames;
      duplicateNames[functionName] = true;
      for(unsigned long i = 1; ; i++) {
        std::string duplicateName = getName(NameD, i);
        if(duplicateName == "!NO_DUPLICATE") break;
        if(duplicateNames.count(duplicateName + suffix)) continue;
        duplicateNames[duplicateName + suffix] = true;
        str += "\n" + functionPrefix + duplicateName + suffix + signature;
        str += "{\nthis." + functionName + ".apply(this,arguments)\n}";
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
      //ignoring for now
      /*printCommon(ICD, "if_config_decl");
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
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
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
      //ignoring for now
      /*printCommon(PGD, "precedence_group_decl ");
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

      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
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
    openedBraceStmts.push_back(S);
    printASTNodes(S->getElements());
    
    auto pair = std::begin(braceStmtsWithDefer);
    while (pair != std::end(braceStmtsWithDefer)) {
      if(pair->first != S) {
        ++pair;
        continue;
      }
      OS << "}catch($error){";
      OS << dumpToStr(pair->second);
      OS << ";throw $error}";
      OS << dumpToStr(pair->second);
      pair = braceStmtsWithDefer.erase(pair);
    }
    openedBraceStmts.pop_back();
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
    /*printCommon(S, "defer_stmt") << '\n';
    printRec(S->getTempDecl());
    OS << '\n';
    printRec(S->getCallExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    
    OS << "let $defer = () => {";
    printRec(S->getTempDecl()->getBody());
    OS << "\n}";
    OS << "\ntry {";
    braceStmtsWithDefer.push_back(std::make_pair(openedBraceStmts.back(), S->getCallExpr()));
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
        if(conditionStr.length()) conditionStr += " && ";
        auto ifLet = getIfLet(pattern, elt.getInitializer());
        if(!ifLet.conditionStr.length()) {
          //when using '_'
          conditionStr += ifLet.initializerStr + " != null";
        }
        else {
          conditionStr += ifLet.conditionStr;
          initializerStr += ifLet.initializerStr;
        }
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
  
  bool printSwitchCondition(std::string varName, const Pattern *P, const Expr *Guard) {
    bool first = true;
    OS << "(";
    if (P) {
      if(printPatternCondition(varName, P)) first = false;
    }
    if (Guard) {
      if (P) {
        for(auto const& node : PrintDecl(OS).flattenPattern(P)) {
          if(auto *namedPattern = dyn_cast<NamedPattern>(node.second)) {
            nameReplacements[getName(namedPattern->getSingleVar())] = matchNameReplacement(varName, node.first);
          }
        }
      }
      if(!first) OS << " && ";
      OS << "(";
      Guard->dump(OS, Indent+4);
      OS << ")";
      first = false;
      nameReplacements = {};
    }
    if(first) OS << "true";
    OS << ")";
    return !first;
  }
  bool printPatternCondition(std::string varName, const Pattern *P) {
    bool first = true;
    for(auto const& node : PrintDecl(OS).flattenPattern(P)) {
      if(auto *exprPattern = dyn_cast<ExprPattern>(node.second)) {
        if(first) first = false;
        else OS << " && ";
        nameReplacements[varName] = matchNameReplacement(varName, node.first);
        OS << "(";
        printRec(exprPattern);
        OS << ")";
        nameReplacements = {};
      }
      else if(auto *enumElementPattern = dyn_cast<EnumElementPattern>(node.second)) {
        if(first) first = false;
        else OS << " && ";
        OS << varName << ".rawValue == ";
        OS << getTypeName(enumElementPattern->getParentType().getType()) << '.' << enumElementPattern->getName();
        if(enumElementPattern->getElementDecl()->hasAssociatedValues()) OS << "()";
        OS << ".rawValue";
      }
      else if(auto *isPattern = dyn_cast<IsPattern>(node.second)) {
        if(first) first = false;
        else OS << " && ";
        OS << varName << " instanceof ";
        OS << getTypeName(isPattern->getCastTypeLoc().getType());
      }
    }
    return !first;
  }
  void printPatternDeclarations(std::string varName, const Pattern *P) {
    for(auto const& node : PrintDecl(OS).flattenPattern(P)) {
      if(auto *namedPattern = dyn_cast<NamedPattern>(node.second)) {
        auto declaredName = getName(namedPattern->getSingleVar());
        auto init = matchNameReplacement(varName, node.first);
        if(declaredName == init) continue;
        OS << "\nconst " << declaredName << " = " << init;
      }
    }
  }
  
  CaseStmt *getCase(const ASTNode &caseNode) {
    Stmt *AS = caseNode.get<Stmt*>();
    auto *S = dyn_cast<CaseStmt>(AS);
    return S;
  }
  void printSwitchConditions(CaseStmt *S) {
    bool first = true;
    for (const auto &LabelItem : S->getCaseLabelItems()) {
      if (!first) OS << " || ";
      if(printSwitchCondition("$match", LabelItem.getPattern(), LabelItem.getGuardExpr())) first = false;
    }
  }
  void printSwitchDeclarations(CaseStmt *S) {
    for (const auto &LabelItem : S->getCaseLabelItems()) {
      if (auto *CasePattern = LabelItem.getPattern()) {
        printPatternDeclarations("$match", CasePattern);
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
    /*printCommon(S, "throw_stmt") << '\n';*/
    OS << "throw ";
    printRec(S->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }

  void visitPoundAssertStmt(PoundAssertStmt *S) {
    printCommon(S, "pound_assert");
    OS << " message=" << QuotedString(S->getMessage()) << "\n";
    printRec(S->getCondition());
    OS << ")";
  }

  void visitDoCatchStmt(DoCatchStmt *S) {
    //printCommon(S, "do_catch_stmt") << '\n';
    OS << "try {";
    printRec(S->getBody());
    //OS << '\n';
    //Indent += 2;
    OS << "\n} catch(error) {";
    visitCatches(S->getCatches());
    OS << "\nelse throw error";
    OS << "\n}";
    //Indent -= 2;
    //PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitCatches(ArrayRef<CatchStmt*> clauses) {
    bool first = true;
    for (auto clause : clauses) {
      OS << "\n";
      if(first) first = false;
      else OS << "else ";
      visitCatchStmt(clause);
    }
  }
  void visitCatchStmt(CatchStmt *clause) {
    /*printCommon(clause, "catch") << '\n';
    printRec(clause->getErrorPattern());
    if (auto guard = clause->getGuardExpr()) {
      OS << '\n';
      printRec(guard);
    }
    OS << '\n';
    printRec(clause->getBody());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/

    OS << "if(";
    printSwitchCondition("error", clause->getErrorPattern(), clause->getGuardExpr());
    OS << ") {";
    printPatternDeclarations("error", clause->getErrorPattern());
    printRec(clause->getBody());
    OS << "\n}";
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
    Type T = GetTypeOfExpr(E);
    if (T.isNull() || !T->is<BuiltinIntegerType>())
      OS << (E->isNegative() ? "-" : "") << E->getDigitsText();
    else
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
    /*printCommon(E, "discard_assignment_expr");
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    //TODO move level up to assign_expr and ignore the assignment altogether, but this will do for now
    OS << "_.discardAssignment";
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
    
    OS << getTypeName(GetTypeOfExpr(E));
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
    
    OS << std::regex_replace(rString, std::regex("#L"), regex_escape(dumpToStr(skipInOutExpr(E->getBase()))));
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
    /*printCommon(E, "dot_self_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    printRec(E->getSubExpr());
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
        string += "set(#I, #ASS, #AA)";
      }
      else {
        string += "get(#I, #AA)";
      }
    }
    
    string = std::regex_replace(string, std::regex("#L"), regex_escape(dumpToStr(skipInOutExpr(E->getBase()))));
    
    functionArgsCall = skipWrapperExpressions(E->getIndex());
    string = std::regex_replace(string, std::regex("#AA"), regex_escape(dumpToStr(skipInOutExpr(E->getIndex()))));
    
    string = handleInfo(string, E->getBase());
    
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
    
    /*std::string string = dumpToStr(E->getSubExpr());
    if(auto *dotSyntaxCallExpr = dyn_cast<DotSyntaxCallExpr>(E->getSubExpr())) {
      if(auto *declRefExpr = dyn_cast<DeclRefExpr>(dotSyntaxCallExpr->getFn())) {
        //an operator closure
        string = string.substr(string.find("#PRENOL") + 7);//7="#PRENOL".count()
        string = std::regex_replace(string, std::regex("#A0"), "a");
        string = std::regex_replace(string, std::regex("#A1"), "b");
        string = "(a, b) => " + string;
      }
    }
    OS << string;*/
    
    //OS << "(a, b) => " << dumpToStr(E->getSubExpr()) << "(a, b)";
    OS << dumpToStr(E->getSubExpr());
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
    //CAREFUL: ignoring the bridge, because the plan for now is to internally use Array for NSArray too
    //that might change later though
    //printCommon(E, "bridge_to_objc_expr") << '\n';
    printRec(E->getSubExpr());
    //PrintWithColorRAII(OS, ParenthesisColor) << ')';
  }
  void visitLoadExpr(LoadExpr *E) {
    //I think we can just ignore this, as it's just a wrapper
    /*printCommon(E, "load_expr") << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }
  void visitMetatypeConversionExpr(MetatypeConversionExpr *E) {
    /*printCommon(E, "metatype_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    printRec(E->getSubExpr());
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
    /*printCommon(E, "force_try_expr");
    OS << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }

  void visitOptionalTryExpr(OptionalTryExpr *E) {
    /*printCommon(E, "optional_try_expr");
    OS << '\n';*/
    OS << "_optionalTry(() => ";
    printRec(E->getSubExpr());
    OS << ")";
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
  }

  void visitTryExpr(TryExpr *E) {
    /*printCommon(E, "try_expr");
    OS << '\n';*/
    printRec(E->getSubExpr());
    /*PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
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
    OS << PrintDecl(OS).printFuncSignature(E->getParameters(), nullptr, nullptr);
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
    
    OS << "() => ";
    printRec(E->getSingleExpressionBody());
  }

  void visitDynamicTypeExpr(DynamicTypeExpr *E) {
    /*printCommon(E, "metatype_expr");
    OS << '\n';
    printRec(E->getBase());
    PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    printGenerics = true;
    OS << "_clarifyGenerics(" + getTypeName(GetTypeOfExpr(E)) + ")";
    printGenerics = false;
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
  
  std::string handleInfo(std::string lrString, Expr *lExpr) {
    
    if(lrString.find("#I") != std::string::npos) {
      lExpr = skipInOutExpr(lExpr);
      std::string iString = "null";
      if (auto lDeclrefExpr = dyn_cast<DeclRefExpr>(lExpr)) {
        if (lDeclrefExpr->getDeclRef().isSpecialized()) {
          iString = "{";
          auto substitutions = lDeclrefExpr->getDeclRef().getSubstitutions();
          auto params = substitutions.getGenericSignature()->getGenericParams();
          int i = 0;
          for(Type T: substitutions.getReplacementTypes()) {
            if(i) iString += ", ";
            iString += params[i]->getName().get();
            printGenerics = true;
            iString += ": _clarifyGenerics(" + getTypeName(T) + ")";
            printGenerics = false;
            i++;
          }
          iString += "}";
        }
      }
      lrString = std::regex_replace(lrString, std::regex("#I"), regex_escape(iString));
    }
    
    return lrString;
  }

  void printApplyExpr(Expr *lExpr, Expr *rExpr, std::string defaultSuffix = "(#I, #AA)") {
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
      if (auto initDeclRef = dyn_cast<DeclRefExpr>(lConstructor->getFn())) {
        if (auto initDecl = dyn_cast<ConstructorDecl>(initDeclRef->getDecl())) {
          std::string replacement = getReplacement(initDecl);
          if(replacement.length()) {
            lString = replacement;
          }
          else {
            lString = "_create(" + dumpToStr(lConstructor->getArg()) + ", '" + getName(initDecl) + "', #I, #AA)";
            lString = handleInfo(lString, initDeclRef);
          }
          defaultSuffix = "";
        }
      }
    }
    else {
      lString = dumpToStr(lExpr);
    }
    
    bool isAss = false;
    if(lString.find("#ISASS") != std::string::npos) {
      isAss = true;
      lString = std::regex_replace(lString, std::regex("#ISASS"), "");
    }
    
    if(lString.find("#PRENOL") != std::string::npos) {
      lString = lString.substr(lString.find("#PRENOL") + 7/*"#PRENOL".count()*/);
    }
    
    std::string lrString;
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
    //otherwise we replace #AA in left-hand side; if no #AA present, we assume the default .#AA or (#AA)
    else {
      if(lString.find("#A") == std::string::npos) lString += defaultSuffix;
      lrString = std::regex_replace(lString, std::regex("#AA"), regex_escape(rString));
    }
    
    lrString = handleInfo(lrString, lExpr);
    
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
    if(auto *declRefExpr = dyn_cast<DeclRefExpr>(E->getFn())) {
      if(getMemberIdentifier(declRefExpr->getDecl()) == "Swift.(file).Optional.none") {
        OS << "null";
        return;
      }
    }
    
    printApplyExpr(skipInOutExpr(E->getArg()), E->getFn(), ".#AA");
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
    std::string condition = tempVal.expr + " != null";
    
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
    else {
      OS << "((" << optionalCondition.back() << ") ? (" << expr << ") : null)";
    }*/
    
    if(optionalCondition.back().length()) {
      OS << "((" << optionalCondition.back() << ") ? (" << expr << ") : null)";
    }
    else {
      OS << expr;
    }
    
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
      /*OS << "\n";

      if (type.isNull())
        OS << "<<null>>";
      else {
        Indent += 2;
        visit(type, label);
        Indent -=2;
      }*/
      visit(type, label);
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
      /*printCommon(label, "name_alias_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());

      for (auto arg : T->getInnermostGenericArgs())
        printRec(arg);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
    }

    void visitParenType(ParenType *T, StringRef label) {
      /*printCommon(label, "paren_type");
      dumpParameterFlags(T->getParameterFlags());
      printRec(T->getUnderlyingType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getUnderlyingType());
    }

    void visitTupleType(TupleType *T, StringRef label) {
      //TODO
      OS << "'?tuple_type'";
      /*printCommon(label, "tuple_type");
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
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
    }

#define REF_STORAGE(Name, name, ...) \
    void visit##Name##StorageType(Name##StorageType *T, StringRef label) { \
      printCommon(label, #name "_storage_type"); \
      printRec(T->getReferentType()); \
      PrintWithColorRAII(OS, ParenthesisColor) << ')'; \
    }
#include "swift/AST/ReferenceStorage.def"

    void visitEnumType(EnumType *T, StringRef label) {
      /*printCommon(label, "enum_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
    }

    void visitStructType(StructType *T, StringRef label) {
      /*printCommon(label, "struct_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
    }

    void visitClassType(ClassType *T, StringRef label) {
      /*printCommon(label, "class_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
    }

    void visitProtocolType(ProtocolType *T, StringRef label) {
      /*printCommon(label, "protocol_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
    }

    void visitMetatypeType(MetatypeType *T, StringRef label) {
      /*printCommon(label, "metatype_type");
      if (T->hasRepresentation())
        OS << " " << getMetatypeRepresentationString(T->getRepresentation());
      printRec(T->getInstanceType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getInstanceType());
    }

    void visitExistentialMetatypeType(ExistentialMetatypeType *T,
                                      StringRef label) {
      /*printCommon(label, "existential_metatype_type");
      if (T->hasRepresentation())
        OS << " " << getMetatypeRepresentationString(T->getRepresentation());
      printRec(T->getInstanceType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getInstanceType());
    }

    void visitModuleType(ModuleType *T, StringRef label) {
      printCommon(label, "module_type");
      printField("module", T->getModule()->getName());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitDynamicSelfType(DynamicSelfType *T, StringRef label) {
      /*printCommon(label, "dynamic_self_type");
      printRec(T->getSelfType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getSelfType());
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

    void visitPrimaryArchetypeType(PrimaryArchetypeType *T, StringRef label, bool noGenericAccess = false) {
      /*printArchetypeCommon(T, "primary_archetype_type", label);
      printField("name", T->getFullName());
      OS << "\n";
      auto genericEnv = T->getGenericEnvironment();
      if (auto owningDC = genericEnv->getOwningDeclContext()) {
        owningDC->printContext(OS, Indent + 2);
      }
      printArchetypeNestedTypes(T);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      auto genericEnv = T->getGenericEnvironment();
      if (auto owningDC = genericEnv->getOwningDeclContext()) {
        if (auto nominalDC = dyn_cast<NominalTypeDecl>(owningDC)) {
          OS << "this.";
        }
        OS << "$info" << owningDC;
        if(!noGenericAccess) OS << "." << T->getFullName();
      }
    }
    void visitNestedArchetypeType(NestedArchetypeType *T, StringRef label) {
      /*printArchetypeCommon(T, "nested_archetype_type", label);
      printField("name", T->getFullName());
      printField("parent", T->getParent());
      printField("assoc_type", T->getAssocType()->printRef());
      printArchetypeNestedTypes(T);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      ArchetypeType *base = T;
      while(true) {
        if(auto *NAT = dyn_cast<NestedArchetypeType>(base)) {
          base = NAT->getParent();
        }
        else break;
      }
      PrimaryArchetypeType *primaryArchetypeType = dyn_cast<PrimaryArchetypeType>(base);
      assert(primaryArchetypeType && "I thought there'd always be PrimaryArchetypeType at base :(");
      visitPrimaryArchetypeType(primaryArchetypeType, label, true);
      OS << "." << T->getFullName();
    }
    void visitOpenedArchetypeType(OpenedArchetypeType *T, StringRef label) {
      printArchetypeCommon(T, "opened_archetype_type", label);
      printRec("opened_existential", T->getOpenedExistentialType());
      printField("opened_existential_id", T->getOpenedExistentialID());
      printArchetypeNestedTypes(T);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';
    }

    void visitGenericTypeParamType(GenericTypeParamType *T, StringRef label, std::string chain = "") {
      /*printCommon(label, "generic_type_param_type");
      printField("depth", T->getDepth());
      printField("index", T->getIndex());
      if (auto decl = T->getDecl())
        printField("decl", decl->printRef());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if(auto decl = T->getDecl()) {
        OS << "{$genericType: '" << getName(decl) << "'";
        if(chain.length()) OS << ", $subchain: '" << chain << "'";
        OS << "}";
      }
    }

    void visitDependentMemberType(DependentMemberType *T, StringRef label, std::string chain = "") {
      /*printCommon(label, "dependent_member_type");
      if (auto assocType = T->getAssocType()) {
        printField("assoc_type", assocType->printRef());
      } else {
        printField("name", T->getName().str());
      }
      printRec("base", T->getBase());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if(chain.length()) chain = "." + chain;
      chain = T->getName().str().data() + chain;
      if(auto *dependentMemberType = dyn_cast<DependentMemberType>(T->getBase().getPointer())) {
        visitDependentMemberType(dependentMemberType, label, chain);
      }
      else if(auto *genericTypeParamType = dyn_cast<GenericTypeParamType>(T->getBase().getPointer())) {
        visitGenericTypeParamType(genericTypeParamType, label, chain);
      }
      else {
        llvm_unreachable("I thought it'd be always DependentMemberType or GenericTypeParamType");
      }
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
      //TODO
      OS << "'?function_type'";
      /*printAnyFunctionTypeCommon(T, label, "function_type");
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
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
      /*printCommon(label, "array_slice_type");
      printRec(T->getBaseType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getSinglyDesugaredType());
    }

    void visitOptionalType(OptionalType *T, StringRef label) {
      /*printCommon(label, "optional_type");
      printRec(T->getBaseType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getSinglyDesugaredType());
    }

    void visitDictionaryType(DictionaryType *T, StringRef label) {
      /*printCommon(label, "dictionary_type");
      printRec("key", T->getKeyType());
      printRec("value", T->getValueType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getSinglyDesugaredType());
    }

    void visitProtocolCompositionType(ProtocolCompositionType *T,
                                      StringRef label) {
      /*printCommon(label, "protocol_composition_type");
      if (T->hasExplicitAnyObject())
        OS << " any_object";
      for (auto proto : T->getMembers()) {
        printRec(proto);
      }
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      
      /*std::string result = "class {}";
      for (auto proto : T->getMembers()) {
        std::string str;
        llvm::raw_string_ostream stream(str);
        proto->dump(stream);
        result = "_mixin(" + result + ", " + stream.str() + ")";
      }
      OS << result;*/
      OS << "'?protocol_composition_type'";
    }

    void visitLValueType(LValueType *T, StringRef label) {
      /*printCommon(label, "lvalue_type");
      printRec(T->getObjectType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getObjectType());
    }

    void visitInOutType(InOutType *T, StringRef label) {
      /*printCommon(label, "inout_type");
      printRec(T->getObjectType());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      printRec(T->getObjectType());
    }

    void visitUnboundGenericType(UnboundGenericType *T, StringRef label) {
      /*printCommon(label, "unbound_generic_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
    }

    void visitBoundGenericClassType(BoundGenericClassType *T, StringRef label) {
      /*printCommon(label, "bound_generic_class_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
    }

    void visitBoundGenericStructType(BoundGenericStructType *T,
                                     StringRef label) {
      /*printCommon(label, "bound_generic_struct_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (printGenerics) {
        OS << "{Self:";
      }
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
      if(printGenerics) {
        auto params = T->getDecl()->getGenericParams()->getParams();
        int i = 0;
        for (auto arg : T->getGenericArgs()) {
          OS << ", " << params[i++]->getName() << ": ";
          printRec(arg);
        }
        OS << "}";
      }
    }

    void visitBoundGenericEnumType(BoundGenericEnumType *T, StringRef label) {
      /*printCommon(label, "bound_generic_enum_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      PrintWithColorRAII(OS, ParenthesisColor) << ')';*/
      if (T->getParent()) {
        printRec("parent", T->getParent());
        OS << ".";
      }
      OS << getName(T->getDecl());
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
  //os << "\n";
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
