#pragma once

#include "plang/AST/AstBase.h"
#include "plang/Basic/RequiredRecordLayouts.h"
#include "plang/Basic/StringUtil.h"

#include <algorithm>
#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace plang {

struct RecordTypeNode;
struct TypeNode;

/// Number of distinct ordinals a set can hold.  A set is lowered to one bit
/// per ordinal, so this is both the width of the set representation and the
/// upper bound Sema enforces on a set's base type.  256 covers `set of char`,
/// every enumeration, and subranges up to that width, matching what the
/// mainstream Pascal implementations accept.
inline constexpr int PlangMaxSetElements = 256;

// PlangMaxBindingName (BindingType.name's capacity) is declared in
// plang/Basic/RequiredRecordLayouts.h, which codegen and the runtime also
// read; see that header for why.

/// Capacity given to a string whose own capacity is not written down: the
/// undiscriminated `string` parameter-form of EP §6.7.3.1, and the result of
/// concatenating one.  A conforming implementation would carry the actual
/// parameter's capacity at run time; until it does, every such string is as
/// wide as the widest one that can be passed to it.
inline constexpr int PlangMaxStringCapacity = 255;

enum class TypeKind {
    Error,      // placeholder for unresolvable types; compatible with everything
                // to suppress error cascades
    Nil,        // type of the 'nil' pointer constant
    Integer,
    Real,
    Complex,    // EP §6.4.2.2: complex number type; representation { double, double }
    Boolean,
    Char,
    String,     // ISO 7185 unbounded string (char*)
    VarString,  // EP §6.4.3.3 string(N); field: StrCapacity
    Enum,       // ordinal; fields: EnumValues, Name
    Subrange,   // ordinal; fields: SubBase (underlying ordinal type)
    Array,      // fields: IndexType, ElemType, Packed
    Record,     // fields: RecordFields
    Set,        // fields: ElemType (ordinal element type), Packed
    File,       // fields: ElemType (element type; null for untyped file)
    Pointer,    // fields: PointeeType
    Procedure,       // callable with no return value; fields: Params
    Function,        // callable with return value; fields: Params, RetType
    ConformantArray, // EP §6.7.3.7: fields: ConformantBounds, ElemType
    SchemaInstance,  // EP §6.4.7: fields: SchemaName, SchemaDiscs, SchemaBody
    Schema,          // EP §6.4.7 undiscriminated, as in `procedure p(var v: vec)`
                     // and `^vec`.  Same fields as SchemaInstance, but the
                     // discriminant values arrive at run time, so SchemaDiscs
                     // carries only names and types.  See SchemaFixedLayout.

    Last = Schema,
};

/// How many semantic type kinds there are.
///
/// The same tripwire NumTypeKinds gives the AST walks, for the same reason and
/// against a different enumeration -- NumTypeKinds counts the fourteen *type
/// denoters* the parser produces, and this counts what they resolve to.
///
/// Several switches over TypeKind end in a `default:`, and have to: a set base
/// type is checked for four kinds and every other kind is simply not a set
/// base, which is not a list anyone should have to extend.  But a default is
/// also what makes a new kind quiet, and some of these defaults are wrong
/// rather than conservative -- a scalar the definite-assignment walk does not
/// know is one is silently not tracked, and a structured type the file check
/// does not know about lets a file be passed by value.
///
/// So a site that would be wrong states the count it was written against.
/// Adding a kind moves this and stops the build at each of them, with a
/// message saying what to go and teach.
inline constexpr int NumSemaTypeKinds = static_cast<int>(TypeKind::Last) + 1;

/// A semantic type produced by resolving an AST TypeNode.
/// This is a flat struct with a kind tag and optional fields — enough to drive
/// type-compatibility checks without a full class hierarchy.
struct Type {
    /// Discriminates which union-like group of fields is active.
    TypeKind    Kind{TypeKind::Error};
    /// Display name used in error messages.  For reading only: two types are
    /// never told apart by this, because anonymous ones share a description.
    std::string Name;
    /// How many bits a value of this type occupies, and whether it is signed.
    ///
    /// Meaningful for Integer, Subrange, Enum and Boolean, and for Real it is
    /// the float width.  Every other kind leaves it at the default and nothing
    /// reads it.
    ///
    /// ISO 7185 and Extended Pascal have one integer type and stamp 64 on all
    /// of it, so `getIntNTy(ctx, Width)` is the i64 those dialects already
    /// emitted.  Turbo has Byte, ShortInt, Word, Integer, LongInt and Comp at
    /// four different widths, and the width has to travel with the type: it is
    /// what SizeOf answers, what a variable typecast's legality rule compares,
    /// and what a `file of T` image is made of.
    unsigned Width{64};
    bool     IsSigned{true};

    /// Enum and Record only: written inline rather than declared, so it has no
    /// declared name to be identified by.  See isAnonymousNominal.
    bool        Anonymous{false};

    // --- Enum ---
    /// Declared value names for an enumerated type.
    std::vector<std::string> EnumValues;

    // --- VarString ---
    /// Capacity (N) of an EP string(N) type; 0 for unbounded String.
    int64_t StrCapacity{0};

    /// EP §6.4.7: an extent of this type is fixed by a discriminant whose value
    /// is not known until run time -- `string(cap)` or `array[1..n]` written in
    /// the body of a schema that is used without its discriminants.
    ///
    /// The body of such a schema is resolved once against a probe binding, so
    /// StrCapacity and the index bounds below hold the probe's answer and are
    /// NOT the storage.  Nothing may fold against them: the object carries its
    /// discriminants and the layout is worked out from those at run time.  A
    /// type that reaches codegen with this set is laid out by CodegenSchema's
    /// run-time path rather than by the specialising one.
    bool ExtentVaries{false};

    // --- Subrange ---
    /// Underlying ordinal type for a subrange.
    std::shared_ptr<Type> SubBase;
    /// Declared lower and upper bounds of the subrange (used as intern key).
    /// Both are 0 when bounds are unknown or not yet resolved.
    int64_t SubLo{0};
    int64_t SubHi{0};

    // --- Restricted (EP §6.4.2.5) ---
    /// The type whose values this one's stand for, one for one.  Set only for
    /// a type written 'restricted T'; the underlying type of any other type is
    /// that type itself.  The rest of the fields are copied from it, so the
    /// two are laid out and lowered alike and differ only in what may be done
    /// with them.
    std::shared_ptr<Type> RestrictedOf;

    // --- Pointer ---
    /// Type the pointer points to.
    std::shared_ptr<Type> PointeeType;

    // --- Array ---
    /// Index type (usually a subrange type).
    std::shared_ptr<Type> IndexType;
    /// Element type; also used for Set and File.
    std::shared_ptr<Type> ElemType;
    /// True if the type was declared with the 'packed' qualifier.
    bool Packed{false};

    // --- ConformantArray (EP §6.7.3.7) ---
    /// One dimension's bound variable names and ordinal type.
    struct ConformantBound {
        std::string           LoBoundName; // name of lo bound variable (e.g. "lo")
        std::string           HiBoundName; // name of hi bound variable (e.g. "hi")
        std::shared_ptr<Type> OrdType;     // resolved ordinal type for the dimension
    };
    /// One entry per dimension of the conformant array.
    std::vector<ConformantBound> ConformantBounds;

    // --- Record ---
    /// One field in a record type.
    struct Field {
        /// Field name as declared.
        std::string           Name;
        /// Declared type of this field.
        std::shared_ptr<Type> Ty;
        /// True if this field is a variant tag field.
        bool                  IsTagField{false};
    };
    /// Fields of this record type.  A variant part contributes its tag and the
    /// fields of every alternative, flattened, because a field reference names
    /// one without saying which variant it came from.  Storage is a separate
    /// question — see RecordDecl.
    std::vector<Field> RecordFields;
    /// The declaration this record type was resolved from, or null.  The
    /// flattened field list above cannot say which fields share storage under
    /// ISO §6.4.3.3, and only the declaration still has the variant tree that
    /// does, so codegen lays the record out from here.
    const RecordTypeNode* RecordDecl{nullptr};

    // --- Callable (Procedure, Function) ---
    /// One parameter in a procedure or function signature.
    struct Param {
        /// True if the parameter is passed by reference (var parameter).
        bool                  IsVar;
        /// Parameter name as declared.
        std::string           Name;
        /// Declared type of this parameter.
        std::shared_ptr<Type> Ty;
    };
    /// Parameters of this callable type.
    std::vector<Param>    Params;
    /// Return type; null for procedures.
    std::shared_ptr<Type> RetType;

    // --- SchemaInstance and Schema (EP §6.4.7) ---
    /// Name of the schema this is an instance of.
    std::string SchemaName;
    /// One discriminant per schema parameter.  Value is meaningful only for
    /// SchemaInstance; for Schema the value is supplied at run time and Ty
    /// gives the declared ordinal type.
    struct SchemaDisc {
        std::string           Name;
        int64_t               Value{0};
        std::shared_ptr<Type> Ty;
    };
    std::vector<SchemaDisc> SchemaDiscs;

    /// EP §6.4.7 R3: see plang::ExtentForm.  Aliased here because most callers
    /// reach it through the schema type.
    using ExtentForm = plang::ExtentForm;
    /// Array-bodied Schema only: the body's bounds as closed forms.  Absent
    /// when the bound is not expressible as one, in which case codegen keeps
    /// the older route.
    std::optional<ExtentForm> SchemaLowForm, SchemaHighForm;
    /// The resolved underlying type (e.g. array[1..10] of real).  For Schema
    /// this is the body resolved with probe discriminants: its element and
    /// field types are accurate but its extent is only accurate when
    /// SchemaFixedLayout is set.
    std::shared_ptr<Type>   SchemaBody;
    /// The declaration denoter the body was resolved from.  Codegen re-emits
    /// the body's own bound and capacity expressions against the run-time
    /// discriminants, and it used to find them by looking the SCHEMA NAME up in
    /// a flat map keyed by bare name across the whole compilation -- so a
    /// schema declared in a nested procedure, shadowing an outer one of the
    /// same name, handed back the wrong body.  A type knows its own
    /// declaration; the name does not identify it.
    const TypeNode*         SchemaBodyNode{nullptr};
    /// Schema only: true when the body's storage layout does not depend on the
    /// discriminants, so SchemaBody describes the storage exactly.  When false
    /// the body is an array whose bounds are computed at run time.
    bool SchemaFixedLayout{false};

    /// EP §6.4.7: the discriminants that were in force where this type was
    /// resolved, sorted by name.  A record written inside a schema body is a
    /// different type in every instantiation — `array [0..n]` in `poly(2)` and
    /// in `poly(5)` are not the same array — but there is only one declaration
    /// of it, and a layout worked out from the declaration alone cannot tell
    /// the instances apart.  Carrying the values here lets one be laid out
    /// without reference to the others.
    std::vector<std::pair<std::string, int64_t>> SchemaBindings;

    // ---------------------------------------------------------------------------
    // Queries
    // ---------------------------------------------------------------------------

    /// EP §6.4.2.5: true for a type written 'restricted T'.
    bool isRestricted() const { return RestrictedOf != nullptr; }

    /// Returns true if this is the error sentinel type.
    bool isError()  const { return Kind == TypeKind::Error; }
    /// Returns true if this is the nil pointer constant type.
    bool isNil()    const { return Kind == TypeKind::Nil;   }

    /// Returns true for Integer, Boolean, Char, Enum, and Subrange — types that
    /// can be used as ordinals, for-loop variables, or set base types.
    bool isOrdinal() const {
        return Kind == TypeKind::Integer || Kind == TypeKind::Boolean ||
               Kind == TypeKind::Char    || Kind == TypeKind::Enum    ||
               Kind == TypeKind::Subrange;
    }

    /// Returns true if this is an Integer, Real, or Complex type.
    /// Complex is included so that the binary-expression numeric gates permit
    /// complex operands (EP §6.8.3.2 Table 3).
    bool isNumeric() const {
        if (Kind == TypeKind::Integer || Kind == TypeKind::Real
                || Kind == TypeKind::Complex) return true;
        if (Kind == TypeKind::Subrange && SubBase) return SubBase->isNumeric();
        return false;
    }

    /// Returns true for integer and for a subrange of it.  A subrange takes its
    /// values from its host type (ISO §6.4.2.4), so a subrange of integer holds
    /// integers and is an operand of every operator integer is one of —
    /// `div` and `mod` among them, which is what this exists for.
    bool isIntegral() const {
        if (Kind == TypeKind::Integer) return true;
        if (Kind == TypeKind::Subrange && SubBase) return SubBase->isIntegral();
        return false;
    }

    /// Returns the Field* of a named record field, nullptr if not found.
    /// Comparison is case-insensitive (Pascal identifiers are case-insensitive).
    [[nodiscard]] const Field* fieldByName(std::string_view N) const {
        auto It = std::ranges::find_if(RecordFields,
            [&](const Field& F) { return eqCI(F.Name, N); });
        return It != RecordFields.end() ? &*It : nullptr;
    }

    // ---------------------------------------------------------------------------
    // Factories for the eight built-in types (each call allocates a new object;
    // Sema stores the results as member shared_ptrs to avoid repeated allocation)
    // ---------------------------------------------------------------------------

    [[nodiscard]] static std::shared_ptr<Type> makeInteger()  { auto T = std::make_shared<Type>(); T->Kind = TypeKind::Integer;  T->Name = "integer"; return T; }
    [[nodiscard]] static std::shared_ptr<Type> makeReal()     { auto T = std::make_shared<Type>(); T->Kind = TypeKind::Real;     T->Name = "real";    return T; }
    [[nodiscard]] static std::shared_ptr<Type> makeComplex()  { auto T = std::make_shared<Type>(); T->Kind = TypeKind::Complex;  T->Name = "complex"; return T; }
    [[nodiscard]] static std::shared_ptr<Type> makeBoolean()  { auto T = std::make_shared<Type>(); T->Kind = TypeKind::Boolean;  T->Name = "boolean"; T->Width = 8; return T; }
    [[nodiscard]] static std::shared_ptr<Type> makeChar()     { auto T = std::make_shared<Type>(); T->Kind = TypeKind::Char;     T->Name = "char";    T->Width = 8; return T; }
    [[nodiscard]] static std::shared_ptr<Type> makeString()   { auto T = std::make_shared<Type>(); T->Kind = TypeKind::String;    T->Name = "string";  return T; }
    [[nodiscard]] static std::shared_ptr<Type> makeVarString(int64_t Cap) {
        auto T = std::make_shared<Type>();
        T->Kind = TypeKind::VarString;
        T->Name = "string(" + std::to_string(Cap) + ")";
        T->StrCapacity = Cap;
        return T;
    }
    [[nodiscard]] static std::shared_ptr<Type> makeNil()      { auto T = std::make_shared<Type>(); T->Kind = TypeKind::Nil;      T->Name = "nil";     return T; }
    [[nodiscard]] static std::shared_ptr<Type> makeError()    { auto T = std::make_shared<Type>(); T->Kind = TypeKind::Error;    T->Name = "<error>"; return T; }
};

/// True for an enumeration or record written inline rather than declared.
///
/// Enumerations and records are identified by their declaration (ISO §6.4.2.3,
/// §6.4.3.3), so comparing declared names is how two of them are told apart.
/// One written inline has no declared name, and must not be told apart by its
/// display name — every such type used to be called "(record)", which made
/// every record type compatible with every other one.  Name is for reading;
/// this flag is for deciding.
[[nodiscard]] inline bool isAnonymousNominal(const Type& T) {
    return T.Anonymous;
}

/// The ordinal that bit 0 of a set over \p Base stands for.
///
/// A set is one bit per ordinal, so a base type reaching below zero needs the
/// window shifted to reach it.  Only a negative lower bound shifts it: every
/// base type starting at zero or above keeps bit 0 meaning ordinal 0, which
/// is what lets `set of 0..10` and `set of 0..200` share a representation the
/// way ISO §6.4.5 expects of compatible set types.
[[nodiscard]] inline int64_t setBaseOffset(const Type& Base) {
    return (Base.Kind == TypeKind::Subrange && Base.SubLo < 0) ? Base.SubLo : 0;
}

/// setBaseOffset for a set type rather than its base type.
[[nodiscard]] inline int64_t setOffsetOf(const Type& SetTy) {
    return SetTy.ElemType ? setBaseOffset(*SetTy.ElemType) : 0;
}

/// The first and last values of an ordinal type, or nothing when it has no
/// bounded range.  ISO §6.4.3.2 lets an array index be named by its type —
/// `array[color]` — and the extent then has to come from the type itself.
/// `integer` is deliberately excluded: an array over the whole of it is not
/// something an implementation can lay out.
[[nodiscard]] inline std::optional<std::pair<int64_t, int64_t>>
ordinalRange(const Type& T) {
    switch (T.Kind) {
    case TypeKind::Boolean:  return std::pair<int64_t, int64_t>{0, 1};
    case TypeKind::Char:     return std::pair<int64_t, int64_t>{0, 255};
    case TypeKind::Subrange: return std::pair<int64_t, int64_t>{T.SubLo, T.SubHi};
    case TypeKind::Enum:
        if (T.EnumValues.empty()) return std::nullopt;
        return std::pair<int64_t, int64_t>{
            0, static_cast<int64_t>(T.EnumValues.size()) - 1};
    default: return std::nullopt;
    }
}

/// The ordinal type a value is really drawn from: for a subrange, the host
/// type it was cut out of; for anything else, the type itself.  A subrange of
/// char is still char where the question is how to write one of its values.
[[nodiscard]] inline const Type& ordinalBase(const Type& T) {
    return (T.Kind == TypeKind::Subrange && T.SubBase) ? *T.SubBase : T;
}

/// An ordinal value written the way the program would have written it — the
/// character, the enumeration identifier, `true` or `false`.  A diagnostic
/// that says 97 where the program said 'a' is asking the reader to do the
/// decoding.  The number is the last resort, for values that have no other
/// spelling.
[[nodiscard]] inline std::string spellOrdinal(const Type& T, int64_t V) {
    const Type& Base = ordinalBase(T);
    switch (Base.Kind) {
    case TypeKind::Char:
        // Outside the printable range there is no character to show, so the
        // ordinal is the honest answer, written the way Pascal would take it.
        if (V >= 32 && V < 127) return "'" + std::string(1, static_cast<char>(V)) + "'";
        return "chr(" + std::to_string(V) + ")";
    case TypeKind::Enum:
        if (V >= 0 && V < static_cast<int64_t>(Base.EnumValues.size()))
            return Base.EnumValues[static_cast<size_t>(V)];
        return std::to_string(V);
    case TypeKind::Boolean:
        return V ? "true" : "false";
    default:
        return std::to_string(V);
    }
}

/// ISO §6.4.3.2: a string-type is `packed array[1..n] of char` with n > 1.
/// The standard gives it powers no other array has — it is written, compared,
/// assigned a string literal, concatenated, and taken length/substr/trim/index
/// of (ISO 10206 §6.4.3.3.1's note: "each string-type value is a value of the
/// canonical-string-type", so it satisfies every operator whose table names a
/// canonical-string-type operand) — so it is worth telling apart by name
/// rather than repeating the conditions at each of those places.  n = 1 is
/// excluded because a one-character literal is a char, not a string.
[[nodiscard]] inline bool isCharStringType(const Type& T) {
    return T.Kind == TypeKind::Array && T.Packed
        && T.ElemType  && T.ElemType->Kind  == TypeKind::Char
        && T.IndexType && T.IndexType->Kind == TypeKind::Subrange
        && T.IndexType->SubLo == 1 && T.IndexType->SubHi > 1;
}

/// The n of a string-type, or 0 for anything else.
[[nodiscard]] inline int64_t charStringLength(const Type& T) {
    return isCharStringType(T) ? T.IndexType->SubHi : 0;
}

/// True for the type of a procedural or functional parameter (ISO §6.6.3.1).
[[nodiscard]] inline bool isCallable(const Type& T) {
    return T.Kind == TypeKind::Procedure || T.Kind == TypeKind::Function;
}

/// How a procedural parameter reads in a diagnostic, close to how it was
/// written: "function(integer): integer", "procedure(var real)".  Parameter
/// names are left out because ISO §6.6.3.6 does not compare them.
[[nodiscard]] inline std::string describeCallable(const Type& T) {
    std::string Out = T.Kind == TypeKind::Function ? "function(" : "procedure(";
    for (size_t I = 0; I < T.Params.size(); ++I) {
        if (I) Out += "; ";
        if (T.Params[I].IsVar) Out += "var ";
        Out += T.Params[I].Ty ? T.Params[I].Ty->Name : "?";
    }
    Out += ")";
    if (T.RetType) Out += ": " + T.RetType->Name;
    return Out;
}

/// What a schema type ultimately denotes: its body, and its body's body, for as
/// many levels as there are.
///
/// EP §6.4.7 lets a schema's body be another schema's instantiation --
/// `type vec(n: integer) = array[1..n] of integer; type v2(n: integer) = vec(n)`
/// -- so "look through the schema to what it really is" is a LOOP and not a
/// step.  It was written as a step in a dozen places, each of which then
/// answered a question about `vec(4)` where the answer had to be about
/// `array[1..4] of integer`.
///
/// The consequences were not uniform, which is why they were found one at a
/// time: the subscript check refused a legal `x[1]` outright, while codegen's
/// index path silently kept a lower bound of 0 and range-checked a `1..4` array
/// as `0..3` -- so `x[4]` trapped and `x[0]`, outside the array, did not.
///
/// The hop bound is a backstop.  A schema that contains itself is refused where
/// it is declared (err_schema_recursive), so a cycle cannot reach here.
inline const Type* schemaUnderlying(const Type* T) {
    for (int Hops = 0; T && Hops < 16; ++Hops) {
        if (T->Kind != TypeKind::Schema && T->Kind != TypeKind::SchemaInstance)
            break;
        if (!T->SchemaBody) break;
        T = T->SchemaBody.get();
    }
    return T;
}

inline std::shared_ptr<Type> schemaUnderlying(std::shared_ptr<Type> T) {
    for (int Hops = 0; T && Hops < 16; ++Hops) {
        if (T->Kind != TypeKind::Schema && T->Kind != TypeKind::SchemaInstance)
            break;
        if (!T->SchemaBody) break;
        T = T->SchemaBody;
    }
    return T;
}

/// True when a type denotes an EP string(N), looking through a schema whose body
/// is one.
///
/// EP §6.4.3.3 makes `string` a schema, so `type s(n: integer) = string(n)` and
/// a bare `string` parameter both denote strings without having Kind
/// VarString.  Every operator that asked the Kind directly therefore refused
/// them -- assignment, '+', comparison, length, substr -- and the lesson from
/// schemaUnderlying applies here too: widening one operator and not the rest
/// leaves the narrow ones supplying the answers, which is worse than widening
/// none.
inline bool isVarStringLike(const Type* T) {
    const Type* U = schemaUnderlying(T);
    return U && U->Kind == TypeKind::VarString;
}
inline bool isVarStringLike(const Type& T) { return isVarStringLike(&T); }

} // namespace plang
