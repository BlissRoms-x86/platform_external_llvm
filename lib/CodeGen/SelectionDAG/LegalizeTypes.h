//===-- LegalizeTypes.h - Definition of the DAG Type Legalizer class ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the DAGTypeLegalizer class.  This is a private interface
// shared between the code that implements the SelectionDAG::LegalizeTypes
// method.
//
//===----------------------------------------------------------------------===//

#ifndef SELECTIONDAG_LEGALIZETYPES_H
#define SELECTIONDAG_LEGALIZETYPES_H

#define DEBUG_TYPE "legalize-types"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"

namespace llvm {

//===----------------------------------------------------------------------===//
/// DAGTypeLegalizer - This takes an arbitrary SelectionDAG as input and
/// hacks on it until the target machine can handle it.  This involves
/// eliminating value sizes the machine cannot handle (promoting small sizes to
/// large sizes or splitting up large values into small values) as well as
/// eliminating operations the machine cannot handle.
///
/// This code also does a small amount of optimization and recognition of idioms
/// as part of its processing.  For example, if a target does not support a
/// 'setcc' instruction efficiently, but does support 'brcc' instruction, this
/// will attempt merge setcc and brc instructions into brcc's.
///
class VISIBILITY_HIDDEN DAGTypeLegalizer {
  TargetLowering &TLI;
  SelectionDAG &DAG;
public:
  // NodeIDFlags - This pass uses the NodeID on the SDNodes to hold information
  // about the state of the node.  The enum has all the values.
  enum NodeIDFlags {
    /// ReadyToProcess - All operands have been processed, so this node is ready
    /// to be handled.
    ReadyToProcess = 0,

    /// NewNode - This is a new node that was created in the process of
    /// legalizing some other node.
    NewNode = -1,

    /// Processed - This is a node that has already been processed.
    Processed = -2

    // 1+ - This is a node which has this many unlegalized operands.
  };
private:
  enum LegalizeAction {
    Legal,          // The target natively supports this type.
    PromoteInteger, // Replace this integer type with a larger one.
    ExpandInteger,  // Split this integer type into two of half the size.
    PromoteFloat,   // Convert this float type to a same size integer type.
    ExpandFloat,    // Split this float type into two of half the size.
    Scalarize,      // Replace this one-element vector type with its element type.
    Split           // This vector type should be split into smaller vectors.
  };

  /// ValueTypeActions - This is a bitvector that contains two bits for each
  /// simple value type, where the two bits correspond to the LegalizeAction
  /// enum from TargetLowering.  This can be queried with "getTypeAction(VT)".
  TargetLowering::ValueTypeActionImpl ValueTypeActions;

  /// getTypeAction - Return how we should legalize values of this type, either
  /// it is already legal, or we need to promote it to a larger integer type, or
  /// we need to expand it into multiple registers of a smaller integer type, or
  /// we need to scalarize a one-element vector type into the element type, or
  /// we need to split a vector type into smaller vector types.
  LegalizeAction getTypeAction(MVT VT) const {
    switch (ValueTypeActions.getTypeAction(VT)) {
    default:
      assert(false && "Unknown legalize action!");
    case TargetLowering::Legal:
      return Legal;
    case TargetLowering::Promote:
      return PromoteInteger;
    case TargetLowering::Expand:
      // Expand can mean
      // 1) split scalar in half, 2) convert a float to an integer,
      // 3) scalarize a single-element vector, 4) split a vector in two.
      if (!VT.isVector()) {
        if (VT.isInteger())
          return ExpandInteger;
        else if (VT.getSizeInBits() ==
                 TLI.getTypeToTransformTo(VT).getSizeInBits())
          return PromoteFloat;
        else
          return ExpandFloat;
      } else if (VT.getVectorNumElements() == 1) {
        return Scalarize;
      } else {
        return Split;
      }
    }
  }

  /// isTypeLegal - Return true if this type is legal on this target.
  bool isTypeLegal(MVT VT) const {
    return ValueTypeActions.getTypeAction(VT) == TargetLowering::Legal;
  }

  /// PromotedIntegers - For integer nodes that are below legal width, this map
  /// indicates what promoted value to use.
  DenseMap<SDOperand, SDOperand> PromotedIntegers;

  /// ExpandedIntegers - For integer nodes that need to be expanded this map
  /// indicates which operands are the expanded version of the input.
  DenseMap<SDOperand, std::pair<SDOperand, SDOperand> > ExpandedIntegers;

  /// PromotedFloats - For floating point nodes converted to integers of
  /// the same size, this map indicates the converted value to use.
  DenseMap<SDOperand, SDOperand> PromotedFloats;

  /// ExpandedFloats - For float nodes that need to be expanded this map
  /// indicates which operands are the expanded version of the input.
  DenseMap<SDOperand, std::pair<SDOperand, SDOperand> > ExpandedFloats;

  /// ScalarizedVectors - For nodes that are <1 x ty>, this map indicates the
  /// scalar value of type 'ty' to use.
  DenseMap<SDOperand, SDOperand> ScalarizedVectors;

  /// SplitVectors - For nodes that need to be split this map indicates
  /// which operands are the expanded version of the input.
  DenseMap<SDOperand, std::pair<SDOperand, SDOperand> > SplitVectors;

  /// ReplacedNodes - For nodes that have been replaced with another,
  /// indicates the replacement node to use.
  DenseMap<SDOperand, SDOperand> ReplacedNodes;

  /// Worklist - This defines a worklist of nodes to process.  In order to be
  /// pushed onto this worklist, all operands of a node must have already been
  /// processed.
  SmallVector<SDNode*, 128> Worklist;

public:
  explicit DAGTypeLegalizer(SelectionDAG &dag)
    : TLI(dag.getTargetLoweringInfo()), DAG(dag),
    ValueTypeActions(TLI.getValueTypeActions()) {
    assert(MVT::LAST_VALUETYPE <= 32 &&
           "Too many value types for ValueTypeActions to hold!");
  }

  void run();

  /// ReanalyzeNode - Recompute the NodeID and correct processed operands
  /// for the specified node, adding it to the worklist if ready.
  void ReanalyzeNode(SDNode *N) {
    N->setNodeId(NewNode);
    AnalyzeNewNode(N);
  }

  void NoteReplacement(SDOperand From, SDOperand To) {
    ExpungeNode(From);
    ExpungeNode(To);
    ReplacedNodes[From] = To;
  }

private:
  void AnalyzeNewNode(SDNode *&N);

  void ReplaceValueWith(SDOperand From, SDOperand To);
  void ReplaceNodeWith(SDNode *From, SDNode *To);

  void RemapNode(SDOperand &N);
  void ExpungeNode(SDOperand N);

  // Common routines.
  SDOperand CreateStackStoreLoad(SDOperand Op, MVT DestVT);
  SDOperand MakeLibCall(RTLIB::Libcall LC, MVT RetVT,
                        const SDOperand *Ops, unsigned NumOps, bool isSigned);

  SDOperand BitConvertToInteger(SDOperand Op);
  SDOperand JoinIntegers(SDOperand Lo, SDOperand Hi);
  void SplitInteger(SDOperand Op, SDOperand &Lo, SDOperand &Hi);
  void SplitInteger(SDOperand Op, MVT LoVT, MVT HiVT,
                    SDOperand &Lo, SDOperand &Hi);

  SDOperand GetVectorElementPointer(SDOperand VecPtr, MVT EltVT,
                                    SDOperand Index);

  //===--------------------------------------------------------------------===//
  // Integer Promotion Support: LegalizeIntegerTypes.cpp
  //===--------------------------------------------------------------------===//

  SDOperand GetPromotedInteger(SDOperand Op) {
    SDOperand &PromotedOp = PromotedIntegers[Op];
    RemapNode(PromotedOp);
    assert(PromotedOp.Val && "Operand wasn't promoted?");
    return PromotedOp;
  }
  void SetPromotedInteger(SDOperand Op, SDOperand Result);

  /// ZExtPromotedInteger - Get a promoted operand and zero extend it to the
  /// final size.
  SDOperand ZExtPromotedInteger(SDOperand Op) {
    MVT OldVT = Op.getValueType();
    Op = GetPromotedInteger(Op);
    return DAG.getZeroExtendInReg(Op, OldVT);
  }

  // Integer Result Promotion.
  void PromoteIntegerResult(SDNode *N, unsigned ResNo);
  SDOperand PromoteIntRes_BIT_CONVERT(SDNode *N);
  SDOperand PromoteIntRes_BUILD_PAIR(SDNode *N);
  SDOperand PromoteIntRes_Constant(SDNode *N);
  SDOperand PromoteIntRes_CTLZ(SDNode *N);
  SDOperand PromoteIntRes_CTPOP(SDNode *N);
  SDOperand PromoteIntRes_CTTZ(SDNode *N);
  SDOperand PromoteIntRes_EXTRACT_VECTOR_ELT(SDNode *N);
  SDOperand PromoteIntRes_FP_ROUND(SDNode *N);
  SDOperand PromoteIntRes_FP_TO_XINT(SDNode *N);
  SDOperand PromoteIntRes_INT_EXTEND(SDNode *N);
  SDOperand PromoteIntRes_LOAD(LoadSDNode *N);
  SDOperand PromoteIntRes_SDIV(SDNode *N);
  SDOperand PromoteIntRes_SELECT   (SDNode *N);
  SDOperand PromoteIntRes_SELECT_CC(SDNode *N);
  SDOperand PromoteIntRes_SETCC(SDNode *N);
  SDOperand PromoteIntRes_SHL(SDNode *N);
  SDOperand PromoteIntRes_SimpleIntBinOp(SDNode *N);
  SDOperand PromoteIntRes_SRA(SDNode *N);
  SDOperand PromoteIntRes_SRL(SDNode *N);
  SDOperand PromoteIntRes_TRUNCATE(SDNode *N);
  SDOperand PromoteIntRes_UDIV(SDNode *N);
  SDOperand PromoteIntRes_UNDEF(SDNode *N);

  // Integer Operand Promotion.
  bool PromoteIntegerOperand(SDNode *N, unsigned OperandNo);
  SDOperand PromoteIntOp_ANY_EXTEND(SDNode *N);
  SDOperand PromoteIntOp_BUILD_PAIR(SDNode *N);
  SDOperand PromoteIntOp_BR_CC(SDNode *N, unsigned OpNo);
  SDOperand PromoteIntOp_BRCOND(SDNode *N, unsigned OpNo);
  SDOperand PromoteIntOp_BUILD_VECTOR(SDNode *N);
  SDOperand PromoteIntOp_FP_EXTEND(SDNode *N);
  SDOperand PromoteIntOp_FP_ROUND(SDNode *N);
  SDOperand PromoteIntOp_INT_TO_FP(SDNode *N);
  SDOperand PromoteIntOp_INSERT_VECTOR_ELT(SDNode *N, unsigned OpNo);
  SDOperand PromoteIntOp_MEMBARRIER(SDNode *N);
  SDOperand PromoteIntOp_SELECT(SDNode *N, unsigned OpNo);
  SDOperand PromoteIntOp_SETCC(SDNode *N, unsigned OpNo);
  SDOperand PromoteIntOp_SIGN_EXTEND(SDNode *N);
  SDOperand PromoteIntOp_STORE(StoreSDNode *N, unsigned OpNo);
  SDOperand PromoteIntOp_TRUNCATE(SDNode *N);
  SDOperand PromoteIntOp_ZERO_EXTEND(SDNode *N);

  void PromoteSetCCOperands(SDOperand &LHS,SDOperand &RHS, ISD::CondCode Code);

  //===--------------------------------------------------------------------===//
  // Integer Expansion Support: LegalizeIntegerTypes.cpp
  //===--------------------------------------------------------------------===//

  void GetExpandedInteger(SDOperand Op, SDOperand &Lo, SDOperand &Hi);
  void SetExpandedInteger(SDOperand Op, SDOperand Lo, SDOperand Hi);

  // Integer Result Expansion.
  void ExpandIntegerResult(SDNode *N, unsigned ResNo);
  void ExpandIntRes_ANY_EXTEND        (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_AssertZext        (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_BIT_CONVERT       (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_BUILD_PAIR        (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_Constant          (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_CTLZ              (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_CTPOP             (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_CTTZ              (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_EXTRACT_VECTOR_ELT(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_LOAD          (LoadSDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_MERGE_VALUES      (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_SIGN_EXTEND       (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_SIGN_EXTEND_INREG (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_TRUNCATE          (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_UNDEF             (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_ZERO_EXTEND       (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_FP_TO_SINT        (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_FP_TO_UINT        (SDNode *N, SDOperand &Lo, SDOperand &Hi);

  void ExpandIntRes_Logical           (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_BSWAP             (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_ADDSUB            (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_ADDSUBC           (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_ADDSUBE           (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_SELECT            (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_SELECT_CC         (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_MUL               (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_SDIV              (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_SREM              (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_UDIV              (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_UREM              (SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void ExpandIntRes_Shift             (SDNode *N, SDOperand &Lo, SDOperand &Hi);

  void ExpandShiftByConstant(SDNode *N, unsigned Amt,
                             SDOperand &Lo, SDOperand &Hi);
  bool ExpandShiftWithKnownAmountBit(SDNode *N, SDOperand &Lo, SDOperand &Hi);

  // Integer Operand Expansion.
  bool ExpandIntegerOperand(SDNode *N, unsigned OperandNo);
  SDOperand ExpandIntOp_BIT_CONVERT(SDNode *N);
  SDOperand ExpandIntOp_BR_CC(SDNode *N);
  SDOperand ExpandIntOp_BUILD_VECTOR(SDNode *N);
  SDOperand ExpandIntOp_EXTRACT_ELEMENT(SDNode *N);
  SDOperand ExpandIntOp_SETCC(SDNode *N);
  SDOperand ExpandIntOp_SINT_TO_FP(SDOperand Source, MVT DestTy);
  SDOperand ExpandIntOp_STORE(StoreSDNode *N, unsigned OpNo);
  SDOperand ExpandIntOp_TRUNCATE(SDNode *N);
  SDOperand ExpandIntOp_UINT_TO_FP(SDOperand Source, MVT DestTy);

  void ExpandSetCCOperands(SDOperand &NewLHS, SDOperand &NewRHS,
                           ISD::CondCode &CCCode);

  //===--------------------------------------------------------------------===//
  // Float to Integer Conversion Support: LegalizeFloatTypes.cpp
  //===--------------------------------------------------------------------===//

  SDOperand GetPromotedFloat(SDOperand Op) {
    SDOperand &PromotedOp = PromotedFloats[Op];
    RemapNode(PromotedOp);
    assert(PromotedOp.Val && "Operand wasn't converted to integer?");
    return PromotedOp;
  }
  void SetPromotedFloat(SDOperand Op, SDOperand Result);

  // Result Float to Integer Conversion.
  void PromoteFloatResult(SDNode *N, unsigned OpNo);
  SDOperand PromoteFloatRes_BIT_CONVERT(SDNode *N);
  SDOperand PromoteFloatRes_BUILD_PAIR(SDNode *N);
  SDOperand PromoteFloatRes_ConstantFP(ConstantFPSDNode *N);
  SDOperand PromoteFloatRes_FADD(SDNode *N);
  SDOperand PromoteFloatRes_FCOPYSIGN(SDNode *N);
  SDOperand PromoteFloatRes_FMUL(SDNode *N);
  SDOperand PromoteFloatRes_FSUB(SDNode *N);
  SDOperand PromoteFloatRes_LOAD(SDNode *N);
  SDOperand PromoteFloatRes_XINT_TO_FP(SDNode *N);

  // Operand Float to Integer Conversion.
  bool PromoteFloatOperand(SDNode *N, unsigned OpNo);
  SDOperand PromoteFloatOp_BIT_CONVERT(SDNode *N);

  //===--------------------------------------------------------------------===//
  // Float Expansion Support: LegalizeFloatTypes.cpp
  //===--------------------------------------------------------------------===//

  void GetExpandedFloat(SDOperand Op, SDOperand &Lo, SDOperand &Hi);
  void SetExpandedFloat(SDOperand Op, SDOperand Lo, SDOperand Hi);

  // Float Result Expansion.
  void ExpandFloatResult(SDNode *N, unsigned ResNo);

  // Float Operand Expansion.
  bool ExpandFloatOperand(SDNode *N, unsigned OperandNo);

  //===--------------------------------------------------------------------===//
  // Scalarization Support: LegalizeVectorTypes.cpp
  //===--------------------------------------------------------------------===//

  SDOperand GetScalarizedVector(SDOperand Op) {
    SDOperand &ScalarizedOp = ScalarizedVectors[Op];
    RemapNode(ScalarizedOp);
    assert(ScalarizedOp.Val && "Operand wasn't scalarized?");
    return ScalarizedOp;
  }
  void SetScalarizedVector(SDOperand Op, SDOperand Result);

  // Vector Result Scalarization: <1 x ty> -> ty.
  void ScalarizeResult(SDNode *N, unsigned OpNo);
  SDOperand ScalarizeRes_BinOp(SDNode *N);
  SDOperand ScalarizeRes_UnaryOp(SDNode *N);

  SDOperand ScalarizeRes_BIT_CONVERT(SDNode *N);
  SDOperand ScalarizeRes_FPOWI(SDNode *N);
  SDOperand ScalarizeRes_INSERT_VECTOR_ELT(SDNode *N);
  SDOperand ScalarizeRes_LOAD(LoadSDNode *N);
  SDOperand ScalarizeRes_SELECT(SDNode *N);
  SDOperand ScalarizeRes_UNDEF(SDNode *N);
  SDOperand ScalarizeRes_VECTOR_SHUFFLE(SDNode *N);

  // Vector Operand Scalarization: <1 x ty> -> ty.
  bool ScalarizeOperand(SDNode *N, unsigned OpNo);
  SDOperand ScalarizeOp_BIT_CONVERT(SDNode *N);
  SDOperand ScalarizeOp_EXTRACT_VECTOR_ELT(SDNode *N);
  SDOperand ScalarizeOp_STORE(StoreSDNode *N, unsigned OpNo);

  //===--------------------------------------------------------------------===//
  // Vector Splitting Support: LegalizeVectorTypes.cpp
  //===--------------------------------------------------------------------===//

  void GetSplitVector(SDOperand Op, SDOperand &Lo, SDOperand &Hi);
  void SetSplitVector(SDOperand Op, SDOperand Lo, SDOperand Hi);

  // Vector Result Splitting: <128 x ty> -> 2 x <64 x ty>.
  void SplitResult(SDNode *N, unsigned OpNo);

  void SplitRes_UNDEF(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_LOAD(LoadSDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_BUILD_PAIR(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_INSERT_VECTOR_ELT(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_VECTOR_SHUFFLE(SDNode *N, SDOperand &Lo, SDOperand &Hi);

  void SplitRes_BUILD_VECTOR(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_CONCAT_VECTORS(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_BIT_CONVERT(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_UnOp(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_BinOp(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_FPOWI(SDNode *N, SDOperand &Lo, SDOperand &Hi);
  void SplitRes_SELECT(SDNode *N, SDOperand &Lo, SDOperand &Hi);

  // Vector Operand Splitting: <128 x ty> -> 2 x <64 x ty>.
  bool SplitOperand(SDNode *N, unsigned OpNo);

  SDOperand SplitOp_BIT_CONVERT(SDNode *N);
  SDOperand SplitOp_EXTRACT_SUBVECTOR(SDNode *N);
  SDOperand SplitOp_EXTRACT_VECTOR_ELT(SDNode *N);
  SDOperand SplitOp_RET(SDNode *N, unsigned OpNo);
  SDOperand SplitOp_STORE(StoreSDNode *N, unsigned OpNo);
  SDOperand SplitOp_VECTOR_SHUFFLE(SDNode *N, unsigned OpNo);
};

} // end namespace llvm.

#endif
