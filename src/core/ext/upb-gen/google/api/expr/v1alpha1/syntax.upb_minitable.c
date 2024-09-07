/* This file was generated by upb_generator from the input file:
 *
 *     google/api/expr/v1alpha1/syntax.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "google/api/expr/v1alpha1/syntax.upb_minitable.h"
#include "google/protobuf/duration.upb_minitable.h"
#include "google/protobuf/struct.upb_minitable.h"
#include "google/protobuf/timestamp.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub google_api_expr_v1alpha1_ParsedExpr_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__SourceInfo_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_ParsedExpr__fields[2] = {
  {2, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__ParsedExpr_msg_init = {
  &google_api_expr_v1alpha1_ParsedExpr_submsgs[0],
  &google_api_expr_v1alpha1_ParsedExpr__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.ParsedExpr",
#endif
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Expr_submsgs[7] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Constant_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr__Ident_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr__Select_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr__Call_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr__CreateList_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr__CreateStruct_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr__Comprehension_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Expr__fields[8] = {
  {2, 16, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 24), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 24), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 24), -9, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(12, 24), -9, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(12, 24), -9, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(12, 24), -9, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Expr_msg_init = {
  &google_api_expr_v1alpha1_Expr_submsgs[0],
  &google_api_expr_v1alpha1_Expr__fields[0],
  UPB_SIZE(24, 32), 8, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(120), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Expr",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f000010, &upb_psv8_1bt},
    {0x001800080300001a, &upb_pom_1bt_max64b},
    {0x0018000804010022, &upb_pom_1bt_max64b},
    {0x001800080502002a, &upb_pom_1bt_max64b},
    {0x0018000806030032, &upb_pom_1bt_max64b},
    {0x001800080704003a, &upb_pom_1bt_max64b},
    {0x0018000808050042, &upb_pom_1bt_max64b},
    {0x001800080906004a, &upb_pom_1bt_max128b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField google_api_expr_v1alpha1_Expr_Ident__fields[1] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Expr__Ident_msg_init = {
  NULL,
  &google_api_expr_v1alpha1_Expr_Ident__fields[0],
  UPB_SIZE(16, 24), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Expr.Ident",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Expr_Select_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Expr_Select__fields[3] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 9), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Expr__Select_msg_init = {
  &google_api_expr_v1alpha1_Expr_Select_submsgs[0],
  &google_api_expr_v1alpha1_Expr_Select__fields[0],
  UPB_SIZE(32, 40), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Expr.Select",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f000012, &upb_pss_1bt},
    {0x000900003f000018, &upb_psb1_1bt},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Expr_Call_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Expr_Call__fields[3] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 40), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Expr__Call_msg_init = {
  &google_api_expr_v1alpha1_Expr_Call_submsgs[0],
  &google_api_expr_v1alpha1_Expr_Call__fields[0],
  UPB_SIZE(32, 48), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Expr.Call",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f000012, &upb_pss_1bt},
    {0x002800003f01001a, &upb_prm_1bt_max64b},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Expr_CreateList_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Expr_CreateList__fields[2] = {
  {1, 8, 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Array | (int)kUpb_LabelFlags_IsPacked | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Expr__CreateList_msg_init = {
  &google_api_expr_v1alpha1_Expr_CreateList_submsgs[0],
  &google_api_expr_v1alpha1_Expr_CreateList__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Expr.CreateList",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prm_1bt_max64b},
    {0x001000003f000012, &upb_ppv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Expr_CreateStruct_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr__CreateStruct__Entry_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Expr_CreateStruct__fields[2] = {
  {1, UPB_SIZE(12, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 24), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Expr__CreateStruct_msg_init = {
  &google_api_expr_v1alpha1_Expr_CreateStruct_submsgs[0],
  &google_api_expr_v1alpha1_Expr_CreateStruct__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Expr.CreateStruct",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Expr_CreateStruct_Entry_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Expr_CreateStruct_Entry__fields[5] = {
  {1, UPB_SIZE(32, 40), 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, 24, -13, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, 24, -13, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 48), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 16), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Expr__CreateStruct__Entry_msg_init = {
  &google_api_expr_v1alpha1_Expr_CreateStruct_Entry_submsgs[0],
  &google_api_expr_v1alpha1_Expr_CreateStruct_Entry__fields[0],
  UPB_SIZE(40, 56), 5, kUpb_ExtMode_NonExtendable, 5, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Expr.CreateStruct.Entry",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x002800003f000008, &upb_psv8_1bt},
    {0x0018000c02000012, &upb_pos_1bt},
    {0x0018000c0300001a, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f000028, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Expr_Comprehension_submsgs[5] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Expr_Comprehension__fields[8] = {
  {1, UPB_SIZE(32, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 32), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, 40, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 56), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 64), 66, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(24, 72), 67, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(28, 80), 68, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(48, 88), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Expr__Comprehension_msg_init = {
  &google_api_expr_v1alpha1_Expr_Comprehension_submsgs[0],
  &google_api_expr_v1alpha1_Expr_Comprehension__fields[0],
  UPB_SIZE(56, 104), 8, kUpb_ExtMode_NonExtendable, 8, UPB_FASTTABLE_MASK(120), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Expr.Comprehension",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x002800003f00001a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x005800003f000042, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Constant_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Timestamp_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Constant__fields[9] = {
  {1, 16, -9, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, 16, -9, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {3, 16, -9, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {4, 16, -9, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {5, 16, -9, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {6, 16, -9, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, 16, -9, kUpb_NoSub, 12, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {8, 16, -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, 16, -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__Constant_msg_init = {
  &google_api_expr_v1alpha1_Constant_submsgs[0],
  &google_api_expr_v1alpha1_Constant__fields[0],
  UPB_SIZE(24, 32), 9, kUpb_ExtMode_NonExtendable, 9, UPB_FASTTABLE_MASK(120), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.Constant",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0010000801000008, &upb_pov4_1bt},
    {0x0010000802000010, &upb_pob1_1bt},
    {0x0010000803000018, &upb_pov8_1bt},
    {0x0010000804000020, &upb_pov8_1bt},
    {0x0010000805000029, &upb_pof8_1bt},
    {0x0010000806000032, &upb_pos_1bt},
    {0x001000080700003a, &upb_pob_1bt},
    {0x0010000808000042, &upb_pom_1bt_maxmaxb},
    {0x001000080901004a, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_SourceInfo_submsgs[3] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__SourceInfo__PositionsEntry_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__SourceInfo__MacroCallsEntry_msg_init},
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__SourceInfo__Extension_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_SourceInfo__fields[6] = {
  {1, UPB_SIZE(24, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(32, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 40), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Array | (int)kUpb_LabelFlags_IsPacked | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 48), 0, 0, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 56), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(20, 64), 0, 2, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__SourceInfo_msg_init = {
  &google_api_expr_v1alpha1_SourceInfo_submsgs[0],
  &google_api_expr_v1alpha1_SourceInfo__fields[0],
  UPB_SIZE(40, 72), 6, kUpb_ExtMode_NonExtendable, 6, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.SourceInfo",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x002800003f00001a, &upb_ppv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x004000003f020032, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_SourceInfo_Extension_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__SourceInfo__Extension__Version_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_SourceInfo_Extension__fields[3] = {
  {1, UPB_SIZE(20, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 32), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Array | (int)kUpb_LabelFlags_IsPacked | (int)kUpb_LabelFlags_IsAlternate | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 40), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__SourceInfo__Extension_msg_init = {
  &google_api_expr_v1alpha1_SourceInfo_Extension_submsgs[0],
  &google_api_expr_v1alpha1_SourceInfo_Extension__fields[0],
  UPB_SIZE(32, 48), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.SourceInfo.Extension",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x002000003f000012, &upb_ppv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField google_api_expr_v1alpha1_SourceInfo_Extension_Version__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, 16, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__SourceInfo__Extension__Version_msg_init = {
  NULL,
  &google_api_expr_v1alpha1_SourceInfo_Extension_Version__fields[0],
  24, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.SourceInfo.Extension.Version",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000008, &upb_psv8_1bt},
    {0x001000003f000010, &upb_psv8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField google_api_expr_v1alpha1_SourceInfo_PositionsEntry__fields[2] = {
  {1, 16, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, 32, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__SourceInfo__PositionsEntry_msg_init = {
  NULL,
  &google_api_expr_v1alpha1_SourceInfo_PositionsEntry__fields[0],
  48, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.SourceInfo.PositionsEntry",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f000008, &upb_psv8_1bt},
    {0x002000003f000010, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_SourceInfo_MacroCallsEntry_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &google__api__expr__v1alpha1__Expr_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_SourceInfo_MacroCallsEntry__fields[2] = {
  {1, 16, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, 32, 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__SourceInfo__MacroCallsEntry_msg_init = {
  &google_api_expr_v1alpha1_SourceInfo_MacroCallsEntry_submsgs[0],
  &google_api_expr_v1alpha1_SourceInfo_MacroCallsEntry__fields[0],
  48, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(8), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.SourceInfo.MacroCallsEntry",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f000008, &upb_psv8_1bt},
  })
};

static const upb_MiniTableField google_api_expr_v1alpha1_SourcePosition__fields[4] = {
  {1, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 8, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, 12, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {4, 16, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google__api__expr__v1alpha1__SourcePosition_msg_init = {
  NULL,
  &google_api_expr_v1alpha1_SourcePosition__fields[0],
  UPB_SIZE(32, 40), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "google.api.expr.v1alpha1.SourcePosition",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f00000a, &upb_pss_1bt},
    {0x000800003f000010, &upb_psv4_1bt},
    {0x000c00003f000018, &upb_psv4_1bt},
    {0x001000003f000020, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[16] = {
  &google__api__expr__v1alpha1__ParsedExpr_msg_init,
  &google__api__expr__v1alpha1__Expr_msg_init,
  &google__api__expr__v1alpha1__Expr__Ident_msg_init,
  &google__api__expr__v1alpha1__Expr__Select_msg_init,
  &google__api__expr__v1alpha1__Expr__Call_msg_init,
  &google__api__expr__v1alpha1__Expr__CreateList_msg_init,
  &google__api__expr__v1alpha1__Expr__CreateStruct_msg_init,
  &google__api__expr__v1alpha1__Expr__CreateStruct__Entry_msg_init,
  &google__api__expr__v1alpha1__Expr__Comprehension_msg_init,
  &google__api__expr__v1alpha1__Constant_msg_init,
  &google__api__expr__v1alpha1__SourceInfo_msg_init,
  &google__api__expr__v1alpha1__SourceInfo__Extension_msg_init,
  &google__api__expr__v1alpha1__SourceInfo__Extension__Version_msg_init,
  &google__api__expr__v1alpha1__SourceInfo__PositionsEntry_msg_init,
  &google__api__expr__v1alpha1__SourceInfo__MacroCallsEntry_msg_init,
  &google__api__expr__v1alpha1__SourcePosition_msg_init,
};

const upb_MiniTableFile google_api_expr_v1alpha1_syntax_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  16,
  0,
  0,
};

#include "upb/port/undef.inc"

