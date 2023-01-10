/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/type/matcher/v3/range.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_TYPE_MATCHER_V3_RANGE_PROTO_UPB_H_
#define XDS_TYPE_MATCHER_V3_RANGE_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_type_matcher_v3_Int64RangeMatcher;
struct xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher;
struct xds_type_matcher_v3_Int32RangeMatcher;
struct xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher;
struct xds_type_matcher_v3_DoubleRangeMatcher;
struct xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher;
typedef struct xds_type_matcher_v3_Int64RangeMatcher xds_type_matcher_v3_Int64RangeMatcher;
typedef struct xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher;
typedef struct xds_type_matcher_v3_Int32RangeMatcher xds_type_matcher_v3_Int32RangeMatcher;
typedef struct xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher;
typedef struct xds_type_matcher_v3_DoubleRangeMatcher xds_type_matcher_v3_DoubleRangeMatcher;
typedef struct xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher;
extern const upb_MiniTable xds_type_matcher_v3_Int64RangeMatcher_msginit;
extern const upb_MiniTable xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_msginit;
extern const upb_MiniTable xds_type_matcher_v3_Int32RangeMatcher_msginit;
extern const upb_MiniTable xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_msginit;
extern const upb_MiniTable xds_type_matcher_v3_DoubleRangeMatcher_msginit;
extern const upb_MiniTable xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_msginit;
struct xds_type_matcher_v3_Matcher_OnMatch;
struct xds_type_v3_DoubleRange;
struct xds_type_v3_Int32Range;
struct xds_type_v3_Int64Range;
extern const upb_MiniTable xds_type_matcher_v3_Matcher_OnMatch_msginit;
extern const upb_MiniTable xds_type_v3_DoubleRange_msginit;
extern const upb_MiniTable xds_type_v3_Int32Range_msginit;
extern const upb_MiniTable xds_type_v3_Int64Range_msginit;



/* xds.type.matcher.v3.Int64RangeMatcher */

UPB_INLINE xds_type_matcher_v3_Int64RangeMatcher* xds_type_matcher_v3_Int64RangeMatcher_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_Int64RangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_Int64RangeMatcher_msginit, arena);
}
UPB_INLINE xds_type_matcher_v3_Int64RangeMatcher* xds_type_matcher_v3_Int64RangeMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_Int64RangeMatcher* ret = xds_type_matcher_v3_Int64RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_Int64RangeMatcher_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_Int64RangeMatcher* xds_type_matcher_v3_Int64RangeMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_Int64RangeMatcher* ret = xds_type_matcher_v3_Int64RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_Int64RangeMatcher_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_Int64RangeMatcher_serialize(const xds_type_matcher_v3_Int64RangeMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_Int64RangeMatcher_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_Int64RangeMatcher_serialize_ex(const xds_type_matcher_v3_Int64RangeMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_Int64RangeMatcher_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE bool xds_type_matcher_v3_Int64RangeMatcher_has_range_matchers(const xds_type_matcher_v3_Int64RangeMatcher* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(0, 0));
}
UPB_INLINE void xds_type_matcher_v3_Int64RangeMatcher_clear_range_matchers(const xds_type_matcher_v3_Int64RangeMatcher* msg) {
  _upb_array_detach(msg, UPB_SIZE(0, 0));
}
UPB_INLINE const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* const* xds_type_matcher_v3_Int64RangeMatcher_range_matchers(const xds_type_matcher_v3_Int64RangeMatcher* msg, size_t* len) {
  return (const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* const*)_upb_array_accessor(msg, UPB_SIZE(0, 0), len);
}

UPB_INLINE xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher** xds_type_matcher_v3_Int64RangeMatcher_mutable_range_matchers(xds_type_matcher_v3_Int64RangeMatcher* msg, size_t* len) {
  return (xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher**)_upb_array_mutable_accessor(msg, UPB_SIZE(0, 0), len);
}
UPB_INLINE xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher** xds_type_matcher_v3_Int64RangeMatcher_resize_range_matchers(xds_type_matcher_v3_Int64RangeMatcher* msg, size_t len, upb_Arena* arena) {
  return (xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(0, 0), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* xds_type_matcher_v3_Int64RangeMatcher_add_range_matchers(xds_type_matcher_v3_Int64RangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* sub = (struct xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(0, 0), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

/* xds.type.matcher.v3.Int64RangeMatcher.RangeMatcher */

UPB_INLINE xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_msginit, arena);
}
UPB_INLINE xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* ret = xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* ret = xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_serialize(const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_serialize_ex(const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE bool xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_has_ranges(const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(4, 8));
}
UPB_INLINE void xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_clear_ranges(const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg) {
  _upb_array_detach(msg, UPB_SIZE(4, 8));
}
UPB_INLINE const struct xds_type_v3_Int64Range* const* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_ranges(const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg, size_t* len) {
  return (const struct xds_type_v3_Int64Range* const*)_upb_array_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE bool xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_has_on_match(const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_clear_on_match(const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const upb_Message*) = NULL;
}
UPB_INLINE const struct xds_type_matcher_v3_Matcher_OnMatch* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_on_match(const xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct xds_type_matcher_v3_Matcher_OnMatch*);
}

UPB_INLINE struct xds_type_v3_Int64Range** xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_mutable_ranges(xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg, size_t* len) {
  return (struct xds_type_v3_Int64Range**)_upb_array_mutable_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE struct xds_type_v3_Int64Range** xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_resize_ranges(xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg, size_t len, upb_Arena* arena) {
  return (struct xds_type_v3_Int64Range**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(4, 8), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct xds_type_v3_Int64Range* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_add_ranges(xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_v3_Int64Range* sub = (struct xds_type_v3_Int64Range*)_upb_Message_New(&xds_type_v3_Int64Range_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(4, 8), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_set_on_match(xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher *msg, struct xds_type_matcher_v3_Matcher_OnMatch* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct xds_type_matcher_v3_Matcher_OnMatch*) = value;
}
UPB_INLINE struct xds_type_matcher_v3_Matcher_OnMatch* xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_mutable_on_match(xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_matcher_v3_Matcher_OnMatch* sub = (struct xds_type_matcher_v3_Matcher_OnMatch*)xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_on_match(msg);
  if (sub == NULL) {
    sub = (struct xds_type_matcher_v3_Matcher_OnMatch*)_upb_Message_New(&xds_type_matcher_v3_Matcher_OnMatch_msginit, arena);
    if (!sub) return NULL;
    xds_type_matcher_v3_Int64RangeMatcher_RangeMatcher_set_on_match(msg, sub);
  }
  return sub;
}

/* xds.type.matcher.v3.Int32RangeMatcher */

UPB_INLINE xds_type_matcher_v3_Int32RangeMatcher* xds_type_matcher_v3_Int32RangeMatcher_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_Int32RangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_Int32RangeMatcher_msginit, arena);
}
UPB_INLINE xds_type_matcher_v3_Int32RangeMatcher* xds_type_matcher_v3_Int32RangeMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_Int32RangeMatcher* ret = xds_type_matcher_v3_Int32RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_Int32RangeMatcher_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_Int32RangeMatcher* xds_type_matcher_v3_Int32RangeMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_Int32RangeMatcher* ret = xds_type_matcher_v3_Int32RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_Int32RangeMatcher_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_Int32RangeMatcher_serialize(const xds_type_matcher_v3_Int32RangeMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_Int32RangeMatcher_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_Int32RangeMatcher_serialize_ex(const xds_type_matcher_v3_Int32RangeMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_Int32RangeMatcher_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE bool xds_type_matcher_v3_Int32RangeMatcher_has_range_matchers(const xds_type_matcher_v3_Int32RangeMatcher* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(0, 0));
}
UPB_INLINE void xds_type_matcher_v3_Int32RangeMatcher_clear_range_matchers(const xds_type_matcher_v3_Int32RangeMatcher* msg) {
  _upb_array_detach(msg, UPB_SIZE(0, 0));
}
UPB_INLINE const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* const* xds_type_matcher_v3_Int32RangeMatcher_range_matchers(const xds_type_matcher_v3_Int32RangeMatcher* msg, size_t* len) {
  return (const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* const*)_upb_array_accessor(msg, UPB_SIZE(0, 0), len);
}

UPB_INLINE xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher** xds_type_matcher_v3_Int32RangeMatcher_mutable_range_matchers(xds_type_matcher_v3_Int32RangeMatcher* msg, size_t* len) {
  return (xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher**)_upb_array_mutable_accessor(msg, UPB_SIZE(0, 0), len);
}
UPB_INLINE xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher** xds_type_matcher_v3_Int32RangeMatcher_resize_range_matchers(xds_type_matcher_v3_Int32RangeMatcher* msg, size_t len, upb_Arena* arena) {
  return (xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(0, 0), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* xds_type_matcher_v3_Int32RangeMatcher_add_range_matchers(xds_type_matcher_v3_Int32RangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* sub = (struct xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(0, 0), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

/* xds.type.matcher.v3.Int32RangeMatcher.RangeMatcher */

UPB_INLINE xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_msginit, arena);
}
UPB_INLINE xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* ret = xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* ret = xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_serialize(const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_serialize_ex(const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE bool xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_has_ranges(const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(4, 8));
}
UPB_INLINE void xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_clear_ranges(const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg) {
  _upb_array_detach(msg, UPB_SIZE(4, 8));
}
UPB_INLINE const struct xds_type_v3_Int32Range* const* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_ranges(const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg, size_t* len) {
  return (const struct xds_type_v3_Int32Range* const*)_upb_array_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE bool xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_has_on_match(const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_clear_on_match(const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const upb_Message*) = NULL;
}
UPB_INLINE const struct xds_type_matcher_v3_Matcher_OnMatch* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_on_match(const xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct xds_type_matcher_v3_Matcher_OnMatch*);
}

UPB_INLINE struct xds_type_v3_Int32Range** xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_mutable_ranges(xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg, size_t* len) {
  return (struct xds_type_v3_Int32Range**)_upb_array_mutable_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE struct xds_type_v3_Int32Range** xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_resize_ranges(xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg, size_t len, upb_Arena* arena) {
  return (struct xds_type_v3_Int32Range**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(4, 8), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct xds_type_v3_Int32Range* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_add_ranges(xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_v3_Int32Range* sub = (struct xds_type_v3_Int32Range*)_upb_Message_New(&xds_type_v3_Int32Range_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(4, 8), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_set_on_match(xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher *msg, struct xds_type_matcher_v3_Matcher_OnMatch* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct xds_type_matcher_v3_Matcher_OnMatch*) = value;
}
UPB_INLINE struct xds_type_matcher_v3_Matcher_OnMatch* xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_mutable_on_match(xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_matcher_v3_Matcher_OnMatch* sub = (struct xds_type_matcher_v3_Matcher_OnMatch*)xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_on_match(msg);
  if (sub == NULL) {
    sub = (struct xds_type_matcher_v3_Matcher_OnMatch*)_upb_Message_New(&xds_type_matcher_v3_Matcher_OnMatch_msginit, arena);
    if (!sub) return NULL;
    xds_type_matcher_v3_Int32RangeMatcher_RangeMatcher_set_on_match(msg, sub);
  }
  return sub;
}

/* xds.type.matcher.v3.DoubleRangeMatcher */

UPB_INLINE xds_type_matcher_v3_DoubleRangeMatcher* xds_type_matcher_v3_DoubleRangeMatcher_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_DoubleRangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_DoubleRangeMatcher_msginit, arena);
}
UPB_INLINE xds_type_matcher_v3_DoubleRangeMatcher* xds_type_matcher_v3_DoubleRangeMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_DoubleRangeMatcher* ret = xds_type_matcher_v3_DoubleRangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_DoubleRangeMatcher_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_DoubleRangeMatcher* xds_type_matcher_v3_DoubleRangeMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_DoubleRangeMatcher* ret = xds_type_matcher_v3_DoubleRangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_DoubleRangeMatcher_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_DoubleRangeMatcher_serialize(const xds_type_matcher_v3_DoubleRangeMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_DoubleRangeMatcher_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_DoubleRangeMatcher_serialize_ex(const xds_type_matcher_v3_DoubleRangeMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_DoubleRangeMatcher_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE bool xds_type_matcher_v3_DoubleRangeMatcher_has_range_matchers(const xds_type_matcher_v3_DoubleRangeMatcher* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(0, 0));
}
UPB_INLINE void xds_type_matcher_v3_DoubleRangeMatcher_clear_range_matchers(const xds_type_matcher_v3_DoubleRangeMatcher* msg) {
  _upb_array_detach(msg, UPB_SIZE(0, 0));
}
UPB_INLINE const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* const* xds_type_matcher_v3_DoubleRangeMatcher_range_matchers(const xds_type_matcher_v3_DoubleRangeMatcher* msg, size_t* len) {
  return (const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* const*)_upb_array_accessor(msg, UPB_SIZE(0, 0), len);
}

UPB_INLINE xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher** xds_type_matcher_v3_DoubleRangeMatcher_mutable_range_matchers(xds_type_matcher_v3_DoubleRangeMatcher* msg, size_t* len) {
  return (xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher**)_upb_array_mutable_accessor(msg, UPB_SIZE(0, 0), len);
}
UPB_INLINE xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher** xds_type_matcher_v3_DoubleRangeMatcher_resize_range_matchers(xds_type_matcher_v3_DoubleRangeMatcher* msg, size_t len, upb_Arena* arena) {
  return (xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(0, 0), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* xds_type_matcher_v3_DoubleRangeMatcher_add_range_matchers(xds_type_matcher_v3_DoubleRangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* sub = (struct xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(0, 0), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

/* xds.type.matcher.v3.DoubleRangeMatcher.RangeMatcher */

UPB_INLINE xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_msginit, arena);
}
UPB_INLINE xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* ret = xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* ret = xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_serialize(const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_serialize_ex(const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE bool xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_has_ranges(const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(4, 8));
}
UPB_INLINE void xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_clear_ranges(const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg) {
  _upb_array_detach(msg, UPB_SIZE(4, 8));
}
UPB_INLINE const struct xds_type_v3_DoubleRange* const* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_ranges(const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg, size_t* len) {
  return (const struct xds_type_v3_DoubleRange* const*)_upb_array_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE bool xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_has_on_match(const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_clear_on_match(const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const upb_Message*) = NULL;
}
UPB_INLINE const struct xds_type_matcher_v3_Matcher_OnMatch* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_on_match(const xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct xds_type_matcher_v3_Matcher_OnMatch*);
}

UPB_INLINE struct xds_type_v3_DoubleRange** xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_mutable_ranges(xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg, size_t* len) {
  return (struct xds_type_v3_DoubleRange**)_upb_array_mutable_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE struct xds_type_v3_DoubleRange** xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_resize_ranges(xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg, size_t len, upb_Arena* arena) {
  return (struct xds_type_v3_DoubleRange**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(4, 8), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct xds_type_v3_DoubleRange* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_add_ranges(xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_v3_DoubleRange* sub = (struct xds_type_v3_DoubleRange*)_upb_Message_New(&xds_type_v3_DoubleRange_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(4, 8), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_set_on_match(xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher *msg, struct xds_type_matcher_v3_Matcher_OnMatch* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct xds_type_matcher_v3_Matcher_OnMatch*) = value;
}
UPB_INLINE struct xds_type_matcher_v3_Matcher_OnMatch* xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_mutable_on_match(xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_matcher_v3_Matcher_OnMatch* sub = (struct xds_type_matcher_v3_Matcher_OnMatch*)xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_on_match(msg);
  if (sub == NULL) {
    sub = (struct xds_type_matcher_v3_Matcher_OnMatch*)_upb_Message_New(&xds_type_matcher_v3_Matcher_OnMatch_msginit, arena);
    if (!sub) return NULL;
    xds_type_matcher_v3_DoubleRangeMatcher_RangeMatcher_set_on_match(msg, sub);
  }
  return sub;
}

extern const upb_MiniTable_File xds_type_matcher_v3_range_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_TYPE_MATCHER_V3_RANGE_PROTO_UPB_H_ */