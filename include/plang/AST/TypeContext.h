#pragma once

#include "plang/Sema/Type.h"

#include <cstdint>
#include <map>
#include <utility>
#include <memory>
#include <set>
#include <string>

namespace plang {

// ---------------------------------------------------------------------------
// TypeContext — ASTContext-style canonical type store
// ---------------------------------------------------------------------------

/// Owns canonical Type instances and returns the same shared_ptr for
/// structurally equivalent types.  Scalar built-in types are singletons.
/// The structural kinds (Subrange, Array, Set, Pointer, File, VarString) are
/// interned, so two spellings of the same structural type yield pointer-equal
/// shared_ptrs and `identical(a, b)` is a pointer comparison.
///
/// Keys are built from the canonical *addresses* of the component types, never
/// from their display names.  A display name does not identify a type: every
/// anonymous subrange is called "subrange", every anonymous enumeration
/// "(enum)" and every anonymous record "(record)", so a name-keyed cache
/// silently merges unrelated types.  That is a bug this cache has had twice —
/// once for sets over distinct subranges, once for pointers to distinct
/// enumerations — so the addresses are the key everywhere now.
///
/// The nominal kinds (Enum, Record) are deliberately *not* interned: ISO
/// §6.4.2.3 makes each enumerated-type definition a distinct type, and a
/// record type is likewise identified by its declaration.  One Type object per
/// declaration is exactly the right representation for them.
///
/// A NAMED array type is nominal for the identical reason (ISO §6.4.2.3,
/// §6.4.3.2 -- issue #178) and gets the identical treatment: getArray below
/// interns only the ANONYMOUS spelling (an array type-denoter written inline,
/// with no `type` declaration of its own), and a named one is instead minted
/// through makeArrayUncached, one object per declaration, exactly as Enum and
/// Record already are.  Array could not simply join Enum/Record's "never
/// interned" rule outright: unlike them, its anonymous form is common (every
/// `var v: array[1..N] of T` writes one) and the interning is what keeps two
/// such spellings sharing a representation, which is worth keeping for the
/// common case and is not itself a bug -- see Sema::isAssignCompatible's
/// Array arm for the identity-vs-structural line this draws at Sema's level.
class TypeContext {
public:
    /// \p DefaultIntWidth is what an unqualified `integer` is.  It is 64 for
    /// ISO 7185 and Extended Pascal, which have one integer type; Turbo has
    /// six at four widths and its `Integer` is 16 bits.
    ///
    /// \p PointerWidthBits is what --target= (LangOptions::PointerWidthBits)
    /// resolved to, or 64 when none was given.  Stamped onto every Pointer,
    /// Nil and String Type this context mints (Type::Width, repurposed for
    /// those three kinds -- see its comment) the same way DefaultIntWidth is
    /// stamped onto Integer, so that Sema::byteSizeOf/byteAlignOf read a
    /// target-correct answer for `^T` without themselves depending on LLVM.
    /// \p Turbo gates TP7 chapter 19's storage-width-selection rule in
    /// getSubrange (and, via its narrowestStorage helper, an enum's own
    /// width by member count in SemaType.cpp's EnumTypeNode arm) -- see
    /// getSubrange's own comment.  ISO 7185 and Extended Pascal must answer
    /// exactly as they always have (Width=64, IsSigned=true for every
    /// subrange, whatever its host), so this defaults to false and every
    /// dialect other than Turbo must pass false explicitly.
    explicit TypeContext(unsigned DefaultIntWidth = 64,
                         unsigned PointerWidthBits = 64,
                         bool Turbo = false) {
        // Through the cache, not beside it.  Sema binds TyInt as a *reference*
        // to the member this returns, and `identical` is a pointer comparison,
        // so an `integer` minted here and an `integer` handed out by getInt
        // would be two objects that fail every identity check against each
        // other -- silently, since neither is wrong on its own.
        DefaultIntWidth_   = DefaultIntWidth;
        PointerWidthBits_  = PointerWidthBits;
        Turbo_             = Turbo;
        TyInt_  = getInt(DefaultIntWidth, /*Signed=*/true);
        TyReal_ = Type::makeReal();
        TyCplx_ = Type::makeComplex();
        TyBool_ = Type::makeBoolean();
        TyChar_ = Type::makeChar();
        TyStr_  = Type::makeString();
        TyStr_->Width = PointerWidthBits_;
        TyNil_  = Type::makeNil();
        TyNil_->Width = PointerWidthBits_;
        // Turbo `PChar`/`PAnsiChar`: a DEDICATED singleton, deliberately NOT
        // minted through getPointer(TyChar_) below.  getPointer interns by
        // pointee identity, so a getPointer(TyChar_)-built PChar would be the
        // exact same Type* as whatever `^Char` written anywhere in the
        // program resolves to (SemaType.cpp's PointerTypeNode case calls
        // getPointer(Base) for every `^T` a program writes, PChar included if
        // it were minted that way) -- a distinct object here is what a
        // pointer-identity Sema gate would need to tell "the type spelled
        // PChar" apart from "some Pointer whose pointee happens to be Char".
        //
        // In the end Sema's actual PChar-arithmetic gate (SemaExpr.cpp) does
        // NOT key off this object's identity -- see its own comment for why:
        // real `fpc -Mtp` was checked empirically and grants +/-/indexing to
        // ANY pointer-to-Char, including a user's own `type P = ^Char`, not
        // only to the name `PChar`.  This singleton exists anyway, both
        // because a dedicated Type* is what plang's diagnostics show ("PChar"
        // rather than "^char") for a variable declared with this exact
        // spelling, and because it keeps the identity route available should
        // a future dialect (Delphi's `{$POINTERMATH ON}` semantics differ
        // from Turbo's) need to draw the nominal distinction after all.
        TyPChar_ = std::make_shared<Type>();
        TyPChar_->Kind        = TypeKind::Pointer;
        TyPChar_->Name        = "PChar";
        TyPChar_->PointeeType = TyChar_;
        TyPChar_->Width       = PointerWidthBits_;
        // Turbo `Pointer`: the generic, untyped pointer -- PointeeType left
        // null on purpose (it names no specific type), also NOT interned via
        // getPointer for the same "must not collide with structural ^T"
        // reason PChar isn't.
        TyPointer_ = std::make_shared<Type>();
        TyPointer_->Kind = TypeKind::Pointer;
        TyPointer_->Name = "Pointer";
        TyPointer_->Width = PointerWidthBits_;
        // Turbo `Single`: Real's own Width default (64, the struct default
        // Type::makeReal() leaves untouched) is what TyReal_ above already
        // is, so only this second rung needs its Width set explicitly.
        TySingle_ = Type::makeReal();
        TySingle_->Name  = "single";
        TySingle_->Width = 32;
        TyErr_  = Type::makeError();
        // ISO §6.4.3.5: text is one predefined type, not a fresh type per
        // mention, so `var f: text` and `procedure p(var g: text)` agree.
        TyText_ = std::make_shared<Type>();
        TyText_->Kind = TypeKind::File;
        TyText_->Name = "text";
    }

    /// ISO §6.4.4 lets a pointer name a type that names the pointer back, so
    /// the type graph has cycles: ^node and the record holding a ^node field
    /// each keep the other's shared_ptr alive, and neither is ever freed.  The
    /// context outlives every type it hands out, so it is the one place that
    /// can cut the links once nothing will read them again.
    ///
    /// Issue #791's follow-up: an Object type is never interned (same as
    /// Enum/Record -- see this class's own header comment) and so is never a
    /// root the loop below would otherwise reach ON ITS OWN.  Ordinarily that
    /// is fine, the same way it is fine for Enum/Record: the walk still finds
    /// a nominal type as long as SOME interned/cached type's link leads to
    /// it (a `^TFoo` field does exactly that for a record, and did for
    /// Object too, until now). But issue #791 gave a method parameter
    /// naming its own enclosing Object type a DIRECT shared_ptr cycle
    /// (T -> ObjectMethods[i].Params[j].Ty == T, or the same for a self-typed
    /// return type) that need not pass through anything interned at all --
    /// `TFoo = object procedure Copy(Other: TFoo); end` alone, no pointer or
    /// array anywhere, leaves T reachable only from itself.  ObjectRoots_
    /// (populated by Sema::resolveObjectType, right where each Object type
    /// is minted) gives the walk below a way to reach it regardless, and the
    /// ObjectMethods loop right after this one's structural fields breaks
    /// the actual cyclic edge -- Params/RetType are the only shared_ptr<Type>
    /// fields ObjectMethods holds (see Type::Method's own comment); the rest
    /// of Type::Method (Name, VmtSlot, Heading, ...) holds nothing that could
    /// keep a Type alive.  A confirmed-acyclic field (Parent: ISO/EP/Turbo
    /// all refuse a type inheriting from itself, so Sema always rejects that
    /// declaration before a real cycle could form there) is left alone, same
    /// as before.
    ~TypeContext() {
        // Collected as shared_ptr, and held that way until every link is cut.
        // Raw pointers were enough only while everything reachable was also
        // owned by a cache.  A type whose extent a schema discriminant fixes is
        // deliberately NOT interned -- folding against the probe's bounds is
        // exactly what must not happen -- so it is reachable here and owned
        // only by the field that names it.  Clearing that field's owner dropped
        // the last reference, and the loop then walked into a Type it had
        // already freed.  Found by AddressSanitizer, not by the suite.
        std::set<Type*> Seen;
        std::vector<std::shared_ptr<Type>> Alive;
        for (auto* Cache : {&SubrangeCache_, &ArrayCache_, &PointerCache_,
                            &SetCache_, &FileCache_})
            for (auto& [K, T] : *Cache) collect(T, Seen, Alive);
        for (auto& [Cap, T] : VarStringCache_) collect(T, Seen, Alive);
        for (auto& [Cap, T] : ShortStringCache_) collect(T, Seen, Alive);
        // See this destructor's own comment (issue #791's follow-up) for why
        // Object types need their own root list rather than riding along
        // with something interned.
        for (const auto& T : ObjectRoots_) collect(T, Seen, Alive);
        for (const auto& Sp : Alive) {
            Type* T = Sp.get();
            T->SubBase.reset();
            T->PointeeType.reset();
            T->IndexType.reset();
            T->ElemType.reset();
            T->RetType.reset();
            T->SchemaBody.reset();
            T->RecordFields.clear();
            T->Params.clear();
            // Issue #791's follow-up: Params/RetType are the only
            // shared_ptr<Type> fields a Method holds (see this destructor's
            // own top comment) -- clearing them here is what actually
            // breaks a self-typed parameter's T -> ObjectMethods[i].
            // Params[j].Ty == T edge, the same way T->RecordFields.clear()
            // just above already broke a self-typed FIELD's edge.
            for (auto& M : T->ObjectMethods) {
                M.RetType.reset();
                for (auto& P : M.Params) P.Ty.reset();
            }
        }
    }

    TypeContext(const TypeContext&)            = delete;
    TypeContext& operator=(const TypeContext&) = delete;

    // ---- Scalar singletons -------------------------------------------------
    const std::shared_ptr<Type>& getInteger() const { return TyInt_;  }
    const std::shared_ptr<Type>& getReal()    const { return TyReal_; }
    const std::shared_ptr<Type>& getComplex() const { return TyCplx_; }
    const std::shared_ptr<Type>& getBoolean() const { return TyBool_; }
    const std::shared_ptr<Type>& getChar()    const { return TyChar_; }
    const std::shared_ptr<Type>& getString()  const { return TyStr_;  }
    const std::shared_ptr<Type>& getNil()     const { return TyNil_;  }
    const std::shared_ptr<Type>& getError()   const { return TyErr_;  }
    const std::shared_ptr<Type>& getText()    const { return TyText_; }
    /// Turbo `PChar`/`PAnsiChar`.  See the constructor's comment for why this
    /// is a dedicated object rather than getPointer(getChar()).
    const std::shared_ptr<Type>& getPChar()   const { return TyPChar_; }
    /// Turbo `Single`: the second floating type, 32 bits wide.  A dedicated
    /// singleton rather than a {Width}-keyed cache like getInt's integer
    /// ladder -- unlike Integer's four-plus widths, Real only ever has this
    /// one other rung (Extended/Comp are refused outright, not implemented;
    /// see resolveNamedUnrestricted's err_turbo_unsupported_float_type), so
    /// a cache would buy nothing a second named field does not already give.
    const std::shared_ptr<Type>& getSingle()  const { return TySingle_; }

    /// The canonical integer type of a given width and signedness.
    ///
    /// Interned like the structural types and for the same reason: `Word` and
    /// `Word` have to be one type, or assigning one to the other fails an
    /// identity check that has nothing to say about why.
    ///
    /// A consequence of interning by {Bits, Signed} alone is that some Turbo
    /// spellings name the SAME Type object: `SmallInt` (16, signed) is
    /// `Integer`'s own object, and `LongWord` (32, unsigned) is `Cardinal`'s.
    /// Type::Name is a field ON that shared object, so it cannot hold two
    /// spellings at once, and it must not be overwritten after the fact by
    /// whichever caller happens to register second -- that would make an
    /// unrelated diagnostic about plain `Integer` say "SmallInt", or the
    /// reverse, depending only on registration order.  Instead the name is
    /// chosen exactly once, here, at mint time, from Bits/Signed alone, so it
    /// is independent of what gets registered when:
    ///   - the dialect's own unqualified `integer` (DefaultIntWidth_, signed)
    ///     keeps makeInteger()'s "integer", unchanged from before this ladder
    ///     existed, so every existing ISO/EP/Turbo-Integer diagnostic reads
    ///     exactly as it did.
    ///   - every other {Bits, Signed} pair -- the sized rungs `-std=turbo`
    ///     adds -- gets the ladder's own name, so a diagnostic about a `Word`
    ///     says "Word" and not "integer".
    /// For the two collisions above this still shows one name for two
    /// spellings, but that is correct rather than approximate: SmallInt IS
    /// Integer and LongWord IS Cardinal (same width, same signedness, same
    /// representation), so either name is an accurate description of the
    /// other's variable.
    std::shared_ptr<Type> getInt(unsigned Bits, bool Signed) {
        auto& Slot = IntCache_[{Bits, Signed}];
        if (!Slot) {
            auto T      = Type::makeInteger();
            T->Width    = Bits;
            T->IsSigned = Signed;
            if (Bits != DefaultIntWidth_ || !Signed)
                if (const char* N = sizedIntegerName(Bits, Signed))
                    T->Name = N;
            Slot = std::move(T);
        }
        return Slot;
    }

    /// Turbo's loose Boolean-family variants: ByteBool (8), WordBool (16),
    /// LongBool (32).  Interned like getInt's sized-integer ladder and for
    /// the same reason -- two `WordBool`s have to be one type -- but keyed
    /// on Width alone rather than {Width, Signed}: every loose Boolean is
    /// the same "strictness" (see Type::IsLooseBool), so Width is the only
    /// axis that varies.
    ///
    /// The dialect's own strict `boolean` is NOT minted through here -- see
    /// getBoolean()/TyBool_ -- so \p Bits is only ever 8, 16 or 32 in
    /// practice (Sema::registerBuiltins is the only caller).
    std::shared_ptr<Type> getLooseBoolean(unsigned Bits) {
        auto& Slot = LooseBoolCache_[Bits];
        if (!Slot) {
            auto T = std::make_shared<Type>();
            T->Kind        = TypeKind::Boolean;
            T->Width       = Bits;
            T->IsLooseBool = true;
            // Every Boolean is an ordinal numbered from zero (ISO §6.4.2.2),
            // never negative -- the same reasoning Type::makeBoolean's own
            // comment gives for strict Boolean, which this is not built
            // through (it needs IsLooseBool, which makeBoolean does not
            // set), so it has to be repeated here rather than inherited.
            // Left at the struct default (true) this told
            // CodegenImpl::ordinalIsUnsigned a WordBool/LongBool holding a
            // raw value with its top bit set (WordBool(40000) = 0x9C40) was
            // SIGNED, and a `<`/`>` comparison against one would compare its
            // bit pattern as a negative i16 instead of the unsigned 40000
            // the loose family's own "any bit pattern, read as a plain
            // unsigned magnitude" contract promises.
            T->IsSigned    = false;
            T->Name        = looseBooleanName(Bits);
            Slot = std::move(T);
        }
        return Slot;
    }

    /// Turbo's generic, untyped `Pointer`.  Deliberately NOT minted through
    /// getPointer/PointerCache_: that cache is keyed by pointee identity, and
    /// this type has none -- PointeeType stays null, the same "no specific
    /// domain type" state getNil's TyNil_ singleton already models for `nil`.
    /// Sema::isAssignCompatible's Pointer/Pointer arm reads that null as
    /// license to accept any other pointer type, in either direction; see its
    /// own comment.
    const std::shared_ptr<Type>& getGenericPointer() const { return TyPointer_; }

    // ---- Structural types — return canonical instances ---------------------

    /// How an ordinal type reads in a diagnostic.
    static std::string describeOrdinal(const Type& T) { return T.Name; }

    /// TP7 chapter 19's storage-width-selection rule (-std=turbo only): the
    /// narrowest of {8, 16, 32}-bit signed or unsigned storage that holds
    /// both \p Lo and \p Hi, unsigned tried first at each width (so a
    /// non-negative range never picks a signed class it does not need), and
    /// falling back to signed 64-bit -- the same width/signedness ISO 7185
    /// and Extended Pascal always answer, see DefaultIntWidth_ -- when
    /// nothing narrower fits; Lo/Hi are already int64_t, so that fallback
    /// always holds them.
    ///
    /// Used for a numeric subrange's own declared bounds in getSubrange
    /// below, and, at Lo=0, for an enum's own width by member count
    /// (SemaType.cpp's EnumTypeNode arm: an N-member enum's ordinals are
    /// always 0..N-1, ISO §6.4.2.2) -- one rule, two call sites, since an
    /// enum's implicit range is exactly the same shape of question a written
    /// subrange's bounds are.
    ///
    /// Verified empirically against `fpc -Mtp` (Free Pascal's Turbo-Pascal
    /// mode) on a local install: `1..100` -> 1 byte; `-100..100` -> 1 byte
    /// signed (-100 needs the sign bit even though 100 alone would not);
    /// `-200..200` -> 2 bytes signed (-200 does not fit signed-8's
    /// -128..127); `0..1000000` -> 4 bytes; `0..255` -> 1 byte unsigned;
    /// `0..256` -> 2 bytes; a 3-member enum -> 1 byte; a 300-member enum ->
    /// 2 bytes.
    static std::pair<unsigned, bool> narrowestStorage(int64_t Lo, int64_t Hi) {
        for (unsigned W : {8u, 16u, 32u}) {
            if (Lo >= 0 && static_cast<uint64_t>(Hi) <= (uint64_t{1} << W) - 1)
                return {W, false};
            const int64_t SMin = -(int64_t{1} << (W - 1));
            const int64_t SMax = (int64_t{1} << (W - 1)) - 1;
            if (Lo >= SMin && Hi <= SMax)
                return {W, true};
        }
        return {64, true};
    }

    /// Canonical subrange type.
    std::shared_ptr<Type> getSubrange(std::shared_ptr<Type> base,
                                      int64_t lo, int64_t hi) {
        // Key on the base's identity and the bounds, so array[1..3] differs
        // from array[1..100] and a subrange of one enumeration differs from
        // the same numeric range over another.
        std::string k = "sub:" + addrKey(base) + ":" +
                        std::to_string(lo) + ":" + std::to_string(hi);
        auto& slot = SubrangeCache_[k];
        if (!slot) {
            auto T     = std::make_shared<Type>();
            T->Kind    = TypeKind::Subrange;
            // Bounds rather than the word "subrange", so two of them can be
            // told apart on sight; the host type comes first when it is not
            // plain integer, since the bounds alone are then just ordinals.
            T->Name    = std::to_string(lo) + ".." + std::to_string(hi);
            if (base && base->Kind != TypeKind::Integer && !base->Name.empty())
                T->Name = base->Name + " " + T->Name;
            // As wide as its host type where the host is an integer, which is
            // where narrowing means anything: a subrange of Byte is a byte.
            //
            // Over a char or a boolean it is a full ordinal instead outside
            // Turbo, which is what plang has always stored one as -- `packed
            // array['a'..'z']` holds i64 components under ISO 7185/EP even
            // though a char is an i8.  That is inconsistent, and narrowing it
            // is a change to how ISO 7185 and Extended Pascal lay memory out,
            // so it does not belong in the change that merely makes width a
            // property types carry -- ISO 7185/EP keep the wide default
            // below unconditionally, gated on Turbo_ being false.
            if (base && base->Kind == TypeKind::Integer) {
                if (Turbo_) {
                    // TP7 ch.19: narrow to THESE bounds, not to the host's
                    // own width -- `type Grade = 1..100` fits a byte even
                    // though the bounds' own type (whatever `integer` means
                    // in this dialect) is wider.  See narrowestStorage.
                    const auto W = narrowestStorage(lo, hi);
                    T->Width     = W.first;
                    T->IsSigned  = W.second;
                } else {
                    T->Width    = base->Width;
                    T->IsSigned = base->IsSigned;
                }
            } else if (Turbo_ && base &&
                       (base->Kind == TypeKind::Char ||
                        base->Kind == TypeKind::Boolean ||
                        base->Kind == TypeKind::Enum)) {
                // Turbo's own natural host width, not DefaultIntWidth_ below:
                // a char or boolean host is always 8 bits (Type::makeChar/
                // makeBoolean), and an enum host's Width was already narrowed
                // by its own member count (SemaType.cpp's EnumTypeNode arm)
                // by the time anything can write a subrange over it -- ISO
                // §6.4.2.2 requires the enumeration declared before use.
                // Before this rule, a Char/Boolean/Enum-hosted subrange under
                // Turbo fell into the DefaultIntWidth_ branch below and got
                // stamped 16 (Turbo's own `integer` width) instead of the
                // natural 8-bit -- or an enum's own narrower-than-16 width --
                // its host actually has; that was a known, previously
                // deferred anomaly this rule also fixes.
                T->Width    = base->Width;
                T->IsSigned = false;
            } else {
                // ISO 7185/EP always land here (Turbo_ is false for both, see
                // the constructor), whatever the host is.  So does Turbo over
                // a host this rule does not otherwise special-case.  ISO
                // §6.4.2.2 numbers a char/boolean/enum host from zero, so
                // their values -- and a subrange's -- are never negative:
                // `'a'..'z'` holds 97..122 however its bounds are written.
                //
                // Saying so matters even though Type::makeChar/makeBoolean
                // and the Enum construction site (SemaType.cpp) now also set
                // their own IsSigned to false for the identical reason: this
                // line is written independently of theirs (Width, right
                // above, has to be -- DefaultIntWidth_ is not any host's own
                // Width), so a rule that instead widened by whatever the host
                // answered would be one accidental IsSigned=true away from
                // sign-extending a char subrange.
                T->Width    = DefaultIntWidth_;
                T->IsSigned = false;
            }
            T->SubBase = std::move(base);
            T->SubLo   = lo;
            T->SubHi   = hi;
            slot = std::move(T);
        }
        return slot;
    }

    /// Canonical array type, for an ANONYMOUS array type-denoter (no `type`
    /// declaration names it).  Two inline spellings of the same shape share
    /// this one object, so they stay structurally interchangeable -- see
    /// Sema::isAssignCompatible's Array arm and Type::Anonymous.
    ///
    /// A NAMED array type-denoter (the direct body of `type X = array...`)
    /// must NOT be minted here: Sema's caller for that case uses
    /// makeArrayUncached below instead, precisely so the object it gets back
    /// is not this shared one -- see that function's own comment.
    std::shared_ptr<Type> getArray(std::shared_ptr<Type> idx,
                                   std::shared_ptr<Type> elem,
                                   bool packed) {
        std::string k = "arr:" + addrKey(idx) + ":" + addrKey(elem)
                      + ":" + (packed ? "P" : "U");
        auto& slot = ArrayCache_[k];
        if (!slot) slot = buildArray(std::move(idx), std::move(elem), packed);
        return slot;
    }

    /// The same array type, deliberately NOT interned.  Two callers need this:
    ///
    ///   - An array whose extent is fixed by a schema discriminant was
    ///     resolved against a probe binding, so its recorded bounds are the
    ///     probe's; sharing one object with the array that genuinely has
    ///     those bounds is what would let a fold read the probe's answer as
    ///     the program's.  See Type::ExtentVaries.
    ///   - A NAMED array type-denoter (issue #178): ISO §6.4.2.3/§6.4.3.2
    ///     make each `type` declaration a distinct type, the same rule
    ///     Enum/Record already get (see this class's own comment).  Sema's
    ///     Phase 3b (Sema.cpp) calls this instead of getArray for exactly
    ///     that one case, so the Type it gets back is safe for
    ///     nameNominalType to rename and un-anonymize in place -- an object
    ///     getArray had handed out would still be sitting in ArrayCache_
    ///     above, and renaming IT would rename every other, unrelated array
    ///     of the identical shape right along with it.
    std::shared_ptr<Type> makeArrayUncached(std::shared_ptr<Type> idx,
                                            std::shared_ptr<Type> elem,
                                            bool packed) {
        return buildArray(std::move(idx), std::move(elem), packed);
    }

    /// What --target= (LangOptions::PointerWidthBits) resolved to, or 64 when
    /// none was given -- the same width stamped onto every Pointer, Nil and
    /// String Type this context mints (see this class's own constructor
    /// comment).  Exposed so a caller that mints its OWN Type object for a
    /// pointer-shaped kind TypeContext has no factory for -- Procedure/
    /// Function, resolved directly in SemaType.cpp's ProcedureTypeNode arm
    /// rather than through a getXxx() here (ISO §6.6.3.6 congruity compares
    /// these structurally, so interning would buy nothing) -- can stamp the
    /// same target-correct width rather than leaving Type::Width at its
    /// struct-default.
    unsigned pointerWidthBits() const { return PointerWidthBits_; }

    /// Canonical pointer type.
    std::shared_ptr<Type> getPointer(std::shared_ptr<Type> base) {
        std::string k = "ptr:" + addrKey(base);
        auto& slot = PointerCache_[k];
        if (!slot) {
            auto T          = std::make_shared<Type>();
            T->Kind         = TypeKind::Pointer;
            T->Name         = "^" + base->Name;
            T->PointeeType  = std::move(base);
            T->Width        = PointerWidthBits_;
            slot = std::move(T);
        }
        return slot;
    }

    /// Points a forward-declared pointer at the type its domain-name turned out
    /// to name (ISO §6.4.4), and re-files it under that type.
    ///
    /// `^node` written before `node` is declared is built over a placeholder,
    /// so it is interned under the placeholder's address.  Without re-filing,
    /// a later `^node` would look up the real type, miss, and mint a second
    /// pointer type that is not identical to the first.
    ///
    /// The placeholder's OLD entry is erased, not just overwritten with a new
    /// one: every key here is an address (see the class comment), and this is
    /// the one place a key's address can go on to name something else.  The
    /// placeholder is a Sema-owned stub (Kind=Error) whose only remaining
    /// owner, after the line below replaces PointeeType, is whatever pinned
    /// it before this call; today that is the AST, since Sema::resolveType
    /// (SemaType.cpp) stamps every TypeNode's resolved type onto the node for
    /// the whole compilation, so the stub outlives this TypeContext and its
    /// address is never freed, let alone reused, while this cache can still
    /// be read.  Leaving the stale entry behind is therefore inert *today*,
    /// but it is load-bearing on that pinning: the moment anything frees a
    /// stub while its TypeContext lives on, a later, unrelated Type placed at
    /// the freed address would collide with the stale "ptr:<addr>" key and
    /// getPointer would hand back this pointer's identity instead of minting
    /// (or finding) its own -- two structurally unrelated types made
    /// pointer-equal, which is exactly what `identical` exists to not do.
    void rebindPointer(const std::shared_ptr<Type>& ptr,
                       std::shared_ptr<Type> pointee) {
        PointerCache_.erase("ptr:" + addrKey(ptr->PointeeType));
        ptr->Name        = "^" + pointee->Name;
        ptr->PointeeType = pointee;
        PointerCache_["ptr:" + addrKey(pointee)] = ptr;
    }

    /// Issue #791's follow-up: called once by Sema::resolveObjectType
    /// (SemaType.cpp), right where each Object type is minted, so
    /// ~TypeContext's cycle-breaking walk always has a root to reach it
    /// from -- see that destructor's own top comment and ObjectRoots_'s own
    /// comment for why an Object type specifically needs this.
    void trackObjectType(std::shared_ptr<Type> T) {
        ObjectRoots_.push_back(std::move(T));
    }

    /// Canonical file type.  ISO §6.4.3.5 gives `text` its own predefined
    /// identity; see getText.
    std::shared_ptr<Type> getFile(std::shared_ptr<Type> elem,
                                  std::shared_ptr<Type> index) {
        std::string k = "file:" + addrKey(elem) + ":" + addrKey(index);
        auto& slot = FileCache_[k];
        if (!slot) {
            auto T       = std::make_shared<Type>();
            T->Kind      = TypeKind::File;
            T->Name      = elem ? "file of " + elem->Name : "file";
            T->ElemType  = std::move(elem);
            T->IndexType = std::move(index);
            slot = std::move(T);
        }
        return slot;
    }

    /// Canonical EP §6.4.3.3 string(N) type.
    std::shared_ptr<Type> getVarString(int64_t capacity) {
        auto& slot = VarStringCache_[capacity];
        if (!slot) slot = Type::makeVarString(capacity);
        return slot;
    }

    /// Canonical Turbo string[N] type.  A SEPARATE cache from VarString's
    /// above, keyed the same way (by capacity alone) but never sharing a slot
    /// with it: the two have different, incompatible binary layouts (see
    /// TypeKind::ShortString), so interning them together would hand back
    /// VarString's i64-headed struct for a ShortString(N) of the same N --
    /// silent, serious corruption rather than a mere naming collision.
    std::shared_ptr<Type> getShortString(int64_t capacity) {
        auto& slot = ShortStringCache_[capacity];
        if (!slot) slot = Type::makeShortString(capacity);
        return slot;
    }

    /// Canonical set type.
    std::shared_ptr<Type> getSet(std::shared_ptr<Type> elem, bool packed) {
        std::string k = "set:" + addrKey(elem) + ":" + (packed ? "P" : "U");
        auto& slot = SetCache_[k];
        if (!slot) {
            auto T      = std::make_shared<Type>();
            T->Kind     = TypeKind::Set;
            T->Name     = "set of " + describeOrdinal(*elem);
            T->ElemType = std::move(elem);
            T->Packed   = packed;
            slot = std::move(T);
        }
        return slot;
    }

    // ---- Identity check ---------------------------------------------------

    /// Returns true iff `a` and `b` are the same canonical type instance.
    /// Also returns true when either is the error sentinel (to suppress cascades).
    bool identical(const std::shared_ptr<Type>& a,
                   const std::shared_ptr<Type>& b) const {
        if (!a || !b)          return false;
        if (a->isError() || b->isError()) return true; // suppress cascades
        return a.get() == b.get();
    }

private:
    /// Shared by getArray and makeArrayUncached, so an interned array and a
    /// varying one differ in nothing but whether they are shared.
    static std::shared_ptr<Type> buildArray(std::shared_ptr<Type> idx,
                                            std::shared_ptr<Type> elem,
                                            bool packed) {
        auto T   = std::make_shared<Type>();
        T->Kind  = TypeKind::Array;
        // Enum/Record's own starting state (SemaType.cpp): every array minted
        // here starts anonymous, whether or not this particular call is the
        // one Sema.cpp's Phase 3b makes for a NAMED array type-denoter.
        // nameNominalType (Sema.cpp) is what clears it, exactly as it does
        // for Enum/Record, once resolveType has returned to Phase 3b and it
        // is known this was in fact a `type` declaration's own body -- see
        // makeArrayUncached's comment for why that rename is only safe on
        // the object THAT call receives, never on one shared through
        // ArrayCache_ (getArray) below.
        T->Anonymous = true;
        // Include index bounds in the display name so diagnostics say
        // "array[1..3] of integer" rather than just "array of integer".
        // nameNominalType overwrites this with the declared name for a
        // named array, the same as it does for Enum/Record's own
        // structural starting name.
        T->Name  = "array[" + std::to_string(idx->SubLo) + ".."
                 + std::to_string(idx->SubHi) + "] of " + elem->Name;
        T->ElemType  = std::move(elem);
        T->IndexType = std::move(idx);
        T->Packed    = packed;
        return T;
    }

    /// Every type reachable from \p T, so the destructor can reach the nominal
    /// types too: a record is not interned, and the only way to it is through
    /// whatever refers to it.
    /// Seen keeps the walk finite; Alive keeps every type found in it alive
    /// until the caller has finished cutting links, since cutting one link may
    /// otherwise free a type still waiting its turn.
    static void collect(const std::shared_ptr<Type>& T, std::set<Type*>& Seen,
                        std::vector<std::shared_ptr<Type>>& Alive) {
        if (!T || !Seen.insert(T.get()).second) return;
        Alive.push_back(T);
        collect(T->SubBase,     Seen, Alive);
        collect(T->PointeeType, Seen, Alive);
        collect(T->IndexType,   Seen, Alive);
        collect(T->ElemType,    Seen, Alive);
        collect(T->RetType,     Seen, Alive);
        collect(T->SchemaBody,  Seen, Alive);
        for (const auto& F : T->RecordFields) collect(F.Ty, Seen, Alive);
        for (const auto& P : T->Params)       collect(P.Ty, Seen, Alive);
        // Issue #791's follow-up: an Object method's own Params/RetType --
        // see the destructor's own top comment for why these, uniquely
        // among Type::Method's fields, need walking here at all.
        for (const auto& M : T->ObjectMethods) {
            collect(M.RetType, Seen, Alive);
            for (const auto& P : M.Params) collect(P.Ty, Seen, Alive);
        }
    }

    /// Cache-key fragment identifying a component type.  Null is a distinct
    /// component, as in `file` versus `file of char`.
    static std::string addrKey(const std::shared_ptr<Type>& T) {
        return std::to_string(reinterpret_cast<uintptr_t>(T.get()));
    }

    /// The Turbo sized-integer ladder's display name for a {Bits, Signed}
    /// pair that is not the dialect's own unqualified `integer`; see getInt.
    /// Null for a width nothing in the ladder names, so an unrecognized pair
    /// falls back to plain "integer" rather than a nonsense label.
    static const char* sizedIntegerName(unsigned Bits, bool Signed) {
        switch (Bits) {
        case 8:  return Signed ? "ShortInt" : "Byte";
        case 16: return Signed ? "Integer"  : "Word";
        case 32: return Signed ? "LongInt"  : "Cardinal";
        case 64: return Signed ? "Int64"    : "QWord";
        default: return nullptr;
        }
    }

    /// getLooseBoolean's display name for a width.  Unlike
    /// sizedIntegerName, every width the ladder mints has a real name here
    /// -- there is no "same object as the dialect's own unqualified type"
    /// collision for Boolean the way SmallInt/LongWord alias Integer/
    /// Cardinal, so a null fallback is never actually reached.
    static const char* looseBooleanName(unsigned Bits) {
        switch (Bits) {
        case 8:  return "ByteBool";
        case 16: return "WordBool";
        case 32: return "LongBool";
        default: return "boolean";
        }
    }

    // Scalar singletons
    std::shared_ptr<Type> TyInt_, TyReal_, TyCplx_, TyBool_, TyChar_,
                          TyStr_, TyNil_, TyErr_, TyText_, TyPChar_,
                          TyPointer_, TySingle_;

    /// What an unqualified `integer` is, and what an ordinal that is not
    /// narrowed by its host is stored as.
    unsigned DefaultIntWidth_{64};
    /// What --target= resolves a pointer to, in bits; see the constructor.
    unsigned PointerWidthBits_{64};
    /// Gates TP7 ch.19 storage-width narrowing in getSubrange; see the
    /// constructor's comment.  False for every dialect but Turbo.
    bool Turbo_{false};

    // Integer types, by width and signedness.
    std::map<std::pair<unsigned, bool>, std::shared_ptr<Type>> IntCache_;
    // Turbo's loose Boolean-family variants, by width; see getLooseBoolean.
    std::map<unsigned, std::shared_ptr<Type>> LooseBoolCache_;

    // Structural type caches (key → canonical instance)
    std::map<std::string, std::shared_ptr<Type>> SubrangeCache_;
    std::map<std::string, std::shared_ptr<Type>> ArrayCache_;
    std::map<std::string, std::shared_ptr<Type>> PointerCache_;
    std::map<std::string, std::shared_ptr<Type>> SetCache_;
    std::map<std::string, std::shared_ptr<Type>> FileCache_;
    std::map<int64_t,     std::shared_ptr<Type>> VarStringCache_;
    /// Turbo string[N]; see getShortString's own comment for why this is a
    /// separate cache and not a slot shared with VarStringCache_ above.
    std::map<int64_t,     std::shared_ptr<Type>> ShortStringCache_;

    /// Issue #791's follow-up: every Object type Sema::resolveObjectType has
    /// minted (SemaType.cpp), via trackObjectType below -- gives
    /// ~TypeContext's cycle-breaking walk a root to reach one from even when
    /// nothing interned (no pointer, no array, ...) ever refers to it.  Not
    /// a cache: Object types are deliberately never interned (this class's
    /// own header comment), so this is a plain append-only list, one entry
    /// per declaration, exactly mirroring why Enum/Record need none of this
    /// (nothing about them can hold a shared_ptr back to themselves -- only
    /// Object, by way of a method's own parameter or return type, can).
    std::vector<std::shared_ptr<Type>> ObjectRoots_;
};

} // namespace plang
