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
class TypeContext {
public:
    /// \p DefaultIntWidth is what an unqualified `integer` is.  It is 64 for
    /// ISO 7185 and Extended Pascal, which have one integer type; Turbo has
    /// six at four widths and its `Integer` is 16 bits.
    explicit TypeContext(unsigned DefaultIntWidth = 64) {
        // Through the cache, not beside it.  Sema binds TyInt as a *reference*
        // to the member this returns, and `identical` is a pointer comparison,
        // so an `integer` minted here and an `integer` handed out by getInt
        // would be two objects that fail every identity check against each
        // other -- silently, since neither is wrong on its own.
        DefaultIntWidth_ = DefaultIntWidth;
        TyInt_  = getInt(DefaultIntWidth, /*Signed=*/true);
        TyReal_ = Type::makeReal();
        TyCplx_ = Type::makeComplex();
        TyBool_ = Type::makeBoolean();
        TyChar_ = Type::makeChar();
        TyStr_  = Type::makeString();
        TyNil_  = Type::makeNil();
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

    /// The canonical integer type of a given width and signedness.
    ///
    /// Interned like the structural types and for the same reason: `Word` and
    /// `Word` have to be one type, or assigning one to the other fails an
    /// identity check that has nothing to say about why.
    std::shared_ptr<Type> getInt(unsigned Bits, bool Signed) {
        auto& Slot = IntCache_[{Bits, Signed}];
        if (!Slot) {
            auto T      = Type::makeInteger();
            T->Width    = Bits;
            T->IsSigned = Signed;
            Slot = std::move(T);
        }
        return Slot;
    }

    // ---- Structural types — return canonical instances ---------------------

    /// How an ordinal type reads in a diagnostic.
    static std::string describeOrdinal(const Type& T) { return T.Name; }

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
            // Over a char or a boolean it is a full ordinal instead, which is
            // what plang has always stored one as -- `packed array['a'..'z']`
            // holds i64 components today even though a char is an i8.  That is
            // inconsistent, and narrowing it is a change to how ISO 7185 and
            // Extended Pascal lay memory out, so it does not belong in the
            // change that merely makes width a property types carry.  Turbo,
            // where a char subrange really is one byte, is when to revisit it.
            if (base && base->Kind == TypeKind::Integer) {
                T->Width    = base->Width;
                T->IsSigned = base->IsSigned;
            } else {
                // Over a char, a boolean or an enumeration.  ISO §6.4.2.2
                // numbers those from zero, so their values are never negative
                // and the subrange's are not either -- `'a'..'z'` holds 97..122
                // however its bounds are written.
                //
                // Saying so matters: IsSigned defaults to true, and a rule that
                // widens by it would sign-extend a char subrange.  Nothing
                // reads IsSigned yet; the widening rule the Turbo runtime
                // boundary needs is the first thing that will, and it must not
                // inherit a field that is wrong for a third of the ordinals.
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

    /// Canonical array type.
    std::shared_ptr<Type> getArray(std::shared_ptr<Type> idx,
                                   std::shared_ptr<Type> elem,
                                   bool packed) {
        std::string k = "arr:" + addrKey(idx) + ":" + addrKey(elem)
                      + ":" + (packed ? "P" : "U");
        auto& slot = ArrayCache_[k];
        if (!slot) slot = buildArray(std::move(idx), std::move(elem), packed);
        return slot;
    }

    /// The same array type, deliberately NOT interned.  An array whose extent
    /// is fixed by a schema discriminant was resolved against a probe binding,
    /// so its recorded bounds are the probe's; sharing one object with the
    /// array that genuinely has those bounds is what would let a fold read the
    /// probe's answer as the program's.  See Type::ExtentVaries.
    std::shared_ptr<Type> makeArrayUncached(std::shared_ptr<Type> idx,
                                            std::shared_ptr<Type> elem,
                                            bool packed) {
        return buildArray(std::move(idx), std::move(elem), packed);
    }

    /// Canonical pointer type.
    std::shared_ptr<Type> getPointer(std::shared_ptr<Type> base) {
        std::string k = "ptr:" + addrKey(base);
        auto& slot = PointerCache_[k];
        if (!slot) {
            auto T          = std::make_shared<Type>();
            T->Kind         = TypeKind::Pointer;
            T->Name         = "^" + base->Name;
            T->PointeeType  = std::move(base);
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
    void rebindPointer(const std::shared_ptr<Type>& ptr,
                       std::shared_ptr<Type> pointee) {
        ptr->Name        = "^" + pointee->Name;
        ptr->PointeeType = pointee;
        PointerCache_["ptr:" + addrKey(pointee)] = ptr;
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
        // Include index bounds in the display name so diagnostics say
        // "array[1..3] of integer" rather than just "array of integer".
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
    }

    /// Cache-key fragment identifying a component type.  Null is a distinct
    /// component, as in `file` versus `file of char`.
    static std::string addrKey(const std::shared_ptr<Type>& T) {
        return std::to_string(reinterpret_cast<uintptr_t>(T.get()));
    }

    // Scalar singletons
    std::shared_ptr<Type> TyInt_, TyReal_, TyCplx_, TyBool_, TyChar_,
                          TyStr_, TyNil_, TyErr_, TyText_;

    /// What an unqualified `integer` is, and what an ordinal that is not
    /// narrowed by its host is stored as.
    unsigned DefaultIntWidth_{64};

    // Integer types, by width and signedness.
    std::map<std::pair<unsigned, bool>, std::shared_ptr<Type>> IntCache_;

    // Structural type caches (key → canonical instance)
    std::map<std::string, std::shared_ptr<Type>> SubrangeCache_;
    std::map<std::string, std::shared_ptr<Type>> ArrayCache_;
    std::map<std::string, std::shared_ptr<Type>> PointerCache_;
    std::map<std::string, std::shared_ptr<Type>> SetCache_;
    std::map<std::string, std::shared_ptr<Type>> FileCache_;
    std::map<int64_t,     std::shared_ptr<Type>> VarStringCache_;
};

} // namespace plang
