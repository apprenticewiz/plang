#pragma once

/// RequiredRecordLayouts.h — TimeStamp and BindingType, declared once
///
/// EP §6.4.3.4 requires both TimeStamp and BindingType, and — like PascalFile
/// (see PascalFileLayout.h, which this follows) — three different things have
/// to agree about what each one is: Sema's RecordFields list, codegen's LLVM
/// StructType, and the runtime's own C++ struct. All three used to be
/// hand-maintained separately, held together only by comments asserting they
/// matched — including, for BindingType.name's capacity, a *second* hand-kept
/// "255" (runtime's own PLANG_BINDING_NAME_CAP) duplicating this header's
/// PlangMaxBindingName instead of using it.  Nothing would have caught any of
/// them drifting apart.
///
/// The C++ structs and the field-list X-macros are declared once, here.
/// Sema's RecordFields necessarily stays a separate, hand-written list — it
/// carries Sema-level subrange types this Basic-layer header cannot reference
/// — but it is checked for the same field count and names as the X-macros
/// list (see registerBuiltins()), and codegen checks the StructType it builds
/// against the C++ struct field-by-field and as a whole
/// (see timestampStructType()/bindingStructType()). No side can now change
/// alone.

#include <cstdint>

namespace plang {

/// Capacity of BindingType.name.  EP §6.4.3.4 requires the field but leaves
/// its variable-string-type up to the implementation; this is long enough
/// for any path the host filesystem will accept.
inline constexpr int PlangMaxBindingName = 255;

/// EP §6.4.3.4, Note 4 spells the field types out as a Pascal declaration:
/// DateValid/TimeValid Boolean, year plain integer, and month/day/hour/
/// minute/second each a subrange.  Sema attaches the real subrange bounds
/// (registerBuiltins()); every one of those ordinal fields shares its
/// storage width with plain integer here.
struct PlangTimeStamp {
    int8_t  DateValid;
    int64_t year, month, day;
    int8_t  TimeValid;
    int64_t hour, minute, second;
};

/// EP §6.4.3.4: 'name', of an implementation-defined variable-string type,
/// and 'bound' — both fields are required.
struct PlangBindingType {
    struct {
        int64_t len;
        char    data[PlangMaxBindingName];
    } name;
    int8_t bound;
};

/// Every TimeStamp field, in declaration order, as (member, the LLVM type
/// codegen builds it from).  See PLANG_FILE_FIELDS's own comment in
/// PascalFileLayout.h; the same pattern, for the same reason.
/// DateValid/TimeValid are i1, not i8: they hold Sema's Boolean, which
/// llvmTypeOfSemaTypeImpl maps to i1Ty everywhere else, and an i1 struct
/// member still allocates to one byte (see bindingStructType's 'bound' for
/// the same reasoning) -- so this matches PlangTimeStamp's int8_t either way.
#define PLANG_TIMESTAMP_FIELDS(X) \
    X(DateValid, i1Ty)            \
    X(year,      i64Ty)           \
    X(month,     i64Ty)           \
    X(day,       i64Ty)           \
    X(TimeValid, i1Ty)            \
    X(hour,      i64Ty)           \
    X(minute,    i64Ty)           \
    X(second,    i64Ty)

/// How many fields PLANG_TIMESTAMP_FIELDS lists.  timestampStructType()
/// asserts the type it builds has exactly this many elements.
inline constexpr unsigned PlangTimeStampFieldCount = 8;

/// BindingType's LLVM-type slot holds a call expression rather than a bare
/// member, evaluated in Codegen::Impl's member-function context exactly the
/// way PLANG_FILE_FIELDS's slots already are for ptrTy/i64Ty.
#define PLANG_BINDINGTYPE_FIELDS(X)              \
    X(name,  strStructType(PlangMaxBindingName)) \
    X(bound, i1Ty)

/// How many fields PLANG_BINDINGTYPE_FIELDS lists.  bindingStructType()
/// asserts the type it builds has exactly this many elements.
inline constexpr unsigned PlangBindingTypeFieldCount = 2;

} // namespace plang
