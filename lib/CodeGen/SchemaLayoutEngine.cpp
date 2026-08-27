#include "SchemaLayoutEngine.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

uint64_t SchemaLayoutEngine::schemaHeaderBytes(const Type& schema) {
    const uint64_t raw = schema.SchemaDiscs.size() * 8;
    uint64_t a = 8;
    if (const TypeNode* body = SchemaTypes.schemaBodyNodeOf(schema))
        a = std::max(a, rtAlignOfTypeNode(body));
    return (raw + a - 1) / a * a;
}

llvm::Value* SchemaLayoutEngine::emitExtentForm(const ExtentForm& F,
                                                 const std::vector<llvm::Value*>& discs) {
    using Op = Type::ExtentForm::Op;
    const auto arg = [&](size_t I) { return emitExtentForm(F.Args[I], discs); };
    switch (F.Kind) {
    case Op::Const: return llvm::ConstantInt::get(i64Ty(), F.Value, /*isSigned=*/true);
    case Op::Disc:
        if (F.Value < 0 || static_cast<size_t>(F.Value) >= discs.size())
            codegenICE("schema extent names discriminant "
                       + llvm::Twine(F.Value) + " of "
                       + llvm::Twine(discs.size()));
        return discs[static_cast<size_t>(F.Value)];
    case Op::Neg: return B.CreateNeg(arg(0), "ext.neg");
    case Op::Add: return B.CreateAdd(arg(0), arg(1), "ext.add");
    case Op::Sub: return B.CreateSub(arg(0), arg(1), "ext.sub");
    case Op::Mul: return B.CreateMul(arg(0), arg(1), "ext.mul");
    case Op::Div: case Op::Mod: {
        auto* L = arg(0);
        auto* R = arg(1);
        // A zero divisor in a bound is diagnosed where the expression is
        // checked; guarding here keeps the emitted code from trapping if one
        // ever reaches this far.
        auto* Safe = B.CreateSelect(
            B.CreateICmpEQ(R, llvm::ConstantInt::get(i64Ty(), 0)), llvm::ConstantInt::get(i64Ty(), 1), R, "ext.div.safe");
        return F.Kind == Op::Div ? B.CreateSDiv(L, Safe, "ext.div")
                                 : B.CreateSRem(L, Safe, "ext.mod");
    }
    case Op::Pow: {
        // EP §6.8.3.2 with an integer base: a small loop rather than a call,
        // and it folds away entirely when both sides are constants.
        auto* fn = RtFns.getExternFnN("plang_ipow", i64Ty(), {i64Ty(), i64Ty()});
        return B.CreateCall(fn, {arg(0), arg(1)}, "ext.pow");
    }
    }
    codegenICE("a schema extent form with no case");
    return nullptr;
}

std::optional<std::pair<llvm::Value*, llvm::Value*>>
SchemaLayoutEngine::boundsOfDenoter(const TypeNode& D, std::span<llvm::Value* const> discs) {
    if (!D.ExtentLow || !D.ExtentHigh || discs.empty())
        return std::nullopt;
    std::vector<llvm::Value*> discsVec(discs.begin(), discs.end());
    auto* lo = emitExtentForm(*D.ExtentLow,  discsVec);
    auto* hi = emitExtentForm(*D.ExtentHigh, discsVec);
    if (!lo || !hi) return std::nullopt;
    return std::pair{lo, hi};
}

namespace {
/// The denoter under any `packed`, and whether one was there.
const TypeNode* peelPacked(const TypeNode* tn, bool* wasPacked = nullptr) {
    while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(tn)) {
        if (wasPacked) *wasPacked = true;
        tn = pk->Inner.get();
    }
    return tn;
}
/// Whether what \p tn denotes has an extent fixed by a discriminant.  Sema
/// marks it; see Type::ExtentVaries.
bool nodeExtentVaries(const TypeNode* tn) {
    return tn && tn->ResolvedType && tn->ResolvedType->ExtentVaries;
}
} // namespace

uint64_t SchemaLayoutEngine::rtAlignOfTypeNode(const TypeNode* tn) {
    // `packed` is peeled and then IGNORED here, because llvmTypeOfNode ignores
    // it too: a PackedTypeNode wrapper lowers to its inner type unchanged, so
    // `packed array[1..3] of integer` is [3 x i64] and aligned like one.
    // Answering 1 for it made this walk disagree with the layout it has to
    // reproduce.  Where `packed` really does pack is a RECORD, below.
    const TypeNode* d = peelPacked(tn);
    // Alignment is static even where size is not -- a string(cap) is
    // i64-aligned for every cap, and an array is aligned as its element is --
    // so the three denoters that can hold a varying extent are answered
    // STRUCTURALLY, without asking for a type they may not have.
    //
    // Not from the node's annotation, which is what this used to consult.  One
    // declaration serves every instantiation and carries whichever was resolved
    // last, so inside a run-time layout walk that annotation is some other
    // instance's and not this one's.  Nothing here reads it any more.
    if (llvm::isa<StringTypeNode>(d)) return 8;
    if (auto* at = llvm::dyn_cast<ArrayTypeNode>(d))
        return rtAlignOfTypeNode(at->Element.get());
    if (auto* rt = llvm::dyn_cast<RecordTypeNode>(d)) {
        // `packed record ... end` is a RecordTypeNode carrying Packed, NOT a
        // PackedTypeNode wrapper -- the parser only wraps `packed <name>`.  So
        // the peel above never saw it, this returned the widest member's
        // alignment, and layoutOf built a StructType with packed=true and
        // alignment 1 for the very same declaration.  rtSizeOfTypeNode has
        // always honoured rt->Packed for the SIZE, so the file disagreed with
        // itself about one flag.
        if (rt->Packed) return 1;
        uint64_t a = 1;
        for (const auto& fd : rt->Fields)
            a = std::max(a, rtAlignOfTypeNode(fd.Type.get()));
        // The variant part counts too.  Omitting it under-aligned a record
        // whose only strictly-aligned member is inside a variant -- the size
        // walk pads to this alignment, so the two disagreed about where the
        // record ends as well as about where it may start.
        if (rt->Variant) a = std::max(a, rtVariantAlign(*rt->Variant));
        return a;
    }
    // A nested instantiation is aligned as its body is; the discriminants
    // change its size and not its alignment.
    if (auto* sn = llvm::dyn_cast<SchemaTypeNode>(d); sn && sn->ResolvedBody)
        if (const TypeNode* body = SchemaTypes.schemaBodyNodeOf(*sn->ResolvedBody))
            return rtAlignOfTypeNode(body);
    // Everything else -- a name, a subrange, an enumeration, a set, a file --
    // has no extent written in it and is aligned as the DataLayout says.
    return Mod.getDataLayout().getABITypeAlign(LlvmTypeOfNode(*d)).value();
}

llvm::Value* SchemaLayoutEngine::alignUpV(llvm::Value* v, uint64_t align) {
    if (align <= 1) return v;
    auto* mask = i64c(static_cast<int64_t>(align - 1));
    auto* sum  = B.CreateAdd(v, mask, "align.add");
    return B.CreateAnd(sum, B.CreateNot(mask), "align.up");
}

std::optional<std::pair<llvm::Value*, llvm::Value*>>
SchemaLayoutEngine::rtIndexBounds(const ArrayTypeNode& at) {
    // Bounds written as expressions may read a discriminant, so they are
    // emitted against whatever is bound now rather than folded.
    // R3: the closed forms first.  They name the discriminants by index and
    // hold no identifier, so nothing in the scope doing the allocating can
    // capture anything; re-emitting at.Low/at.High below resolves their names
    // here, which is the defect 0.1.6 shipped a scope barrier to guard.
    if (auto* lo = extentOf(at.ExtentLow))
        if (auto* hi = extentOf(at.ExtentHigh))
            return std::pair{lo, hi};
    // The expression fallback that used to sit here is gone.  It was taken by
    // 45 tests until the form-recording block was moved above the ArrayTypeNode
    // branch that had been returning before it -- array bounds had never had
    // forms at all -- and by none afterwards.  Deleting it is what stops a
    // bound being re-resolved at the use site rather than merely preferring
    // not to.
    if (at.Low && at.High) {
        // Only a bound inside a schema body needs a form.  Outside one there is
        // nothing to vary and no discriminant to vary with, and the ordinary
        // constant answer -- Sema's, via arrayIndexRange below -- is the
        // answer.  Keeping the walk total on a fixed array is what lets it be
        // run over an ordinary record and compared with the static layout.
        if (rtDiscs_)
            codegenICE("a schema array bound with no closed form to evaluate "
                       "against the discriminants");
    }
    // ISO §6.4.3.2: an index named by its ordinal type has no bound
    // expressions at all -- `array[colour]` leaves Low and High null, and
    // dereferencing them here is what crashed the compiler.  The extent is the
    // whole of that type and cannot vary, so ask the same helper the static
    // layout asks.  One question, one answer, and the two walks agree by
    // construction rather than by my having written the arithmetic twice.
    if (auto r = ArrayIndexRangeFn(at))
        return std::pair<llvm::Value*, llvm::Value*>{i64c(r->first),
                                                     i64c(r->second)};
    return std::nullopt;
}

llvm::Value* SchemaLayoutEngine::rtSizeOfTypeNode(const TypeNode* tn) {
    const TypeNode* d = peelPacked(tn);
    // This used to ask the node whether its extent varies and take the
    // DataLayout's answer when it said no.  That question has no reliable
    // answer here: one declaration serves every instantiation and carries the
    // annotation of whichever Sema resolved last, so in a program with both
    // `^t` and `t(20)` the probe walk read the INSTANCE's `string(20)` field,
    // decided it was fixed, and sized it from syntax the discriminants are not
    // bound in -- 264 bytes for a 32-byte field.  The record's own fields then
    // sat past the end of it and a whole-value copy read 272 bytes out of 40.
    //
    // So the syntax is read wherever the syntax is where the extent is written,
    // with the discriminants bound.  A fixed extent emits its constant and
    // folds to exactly what the DataLayout would have said, so this agrees with
    // the static layout for everything that has one, and is simply the answer
    // for everything that does not.

    // A subrange is as wide as its host ordinal whatever its bounds are, so a
    // discriminant in them changes the CHECK and not the storage.
    if (llvm::isa<SubrangeTypeNode>(d))
        return i64c(static_cast<int64_t>(
            Mod.getDataLayout().getTypeAllocSize(LlvmTypeOfNode(*d))));
    if (auto* st = llvm::dyn_cast<StringTypeNode>(d)) {
        // R3: the capacity's closed form.  The expression fallback that used
        // to sit here is gone: replacing it with an internal error and running
        // the suite showed it taken by NO test, while the array-bound fallback
        // beside it is taken by 45 -- so this one was dead and that one is not.
        //
        // Deleting it rather than leaving it is the point of the phase.  What
        // it did was re-emit the declaration's capacity expression at the use
        // site, which is the defect this work exists to remove; leaving a
        // second path that can still do it means the class is bypassed rather
        // than gone.  A capacity that reaches here without a form is a hole in
        // the compiler, and says so.
        auto* cap = extentOf(st->ExtentLow);
        if (!cap) {
            // Outside a schema body there is no form and nothing to vary, and
            // the static size IS the answer -- which is what lets this walk be
            // run over an ordinary record and checked against the static
            // layout.  Inside one, a capacity with no form is a hole.
            if (rtDiscs_)
                codegenICE("a schema string capacity with no closed form to "
                           "evaluate against the discriminants");
            return i64c(static_cast<int64_t>(
                Mod.getDataLayout().getTypeAllocSize(LlvmTypeOfNode(*d))));
        }
        return alignUpV(B.CreateAdd(i64c(8), cap, "str.size"), 8);
    }
    if (auto* at = llvm::dyn_cast<ArrayTypeNode>(d)) {
        auto bounds = rtIndexBounds(*at);
        if (!bounds) {
            if (rtDiscs_)
                codegenICE("a schema array bound that cannot be evaluated");
            return i64c(static_cast<int64_t>(
                Mod.getDataLayout().getTypeAllocSize(LlvmTypeOfNode(*d))));
        }
        auto* count = B.CreateAdd(
            B.CreateSub(bounds->second, bounds->first), i64c(1),
            "arr.count");
        count = B.CreateSelect(
            B.CreateICmpSLT(count, i64c(1)), i64c(1), count, "arr.count.min");
        auto* stride = alignUpV(rtSizeOfTypeNode(at->Element.get()),
                                rtAlignOfTypeNode(at->Element.get()));
        return B.CreateMul(count, stride, "arr.size");
    }
    if (auto* rt = llvm::dyn_cast<RecordTypeNode>(d)) {
        llvm::Value* off = rtWalkFields(rt->Fields, i64c(0), rt->Packed,
                                        /*stopAt=*/nullptr, nullptr);
        if (rt->Variant)
            off = rtWalkVariant(*rt->Variant, off, rt->Packed, nullptr, nullptr);
        // A record is padded to its own alignment, as a struct is, so that an
        // array of them strides correctly.
        return alignUpV(off, rt->Packed ? 1 : rtAlignOfTypeNode(d));
    }
    // A denoter with no extent written in it -- a name, an enumeration, a set,
    // a file.  Nothing in one of those can depend on a discriminant, so the
    // static answer is the answer.  A denoter that SAYS it varies and still
    // reached here is a node kind whose extent nothing knows how to recover,
    // which is an internal error rather than a size: keeping that loud is the
    // whole reason this is a check and not a fallthrough.
    // R3: a schema instantiated INSIDE another schema's body.  Its own
    // discriminants are arithmetic over the enclosing ones -- the standard's
    // own `matrix(m,n) = array[1..m] of vector(n)` is exactly this -- so they
    // are evaluated first and the inner body is then walked against them.
    // Without this the instantiation was laid out from the probe, and the
    // allocation came out one element wide in every instance.
    if (auto* sn = llvm::dyn_cast<SchemaTypeNode>(d);
            sn && !sn->ActualForms.empty() && rtDiscs_ && sn->ResolvedBody) {
        std::vector<llvm::Value*> inner;
        inner.reserve(sn->ActualForms.size());
        for (const auto& F : sn->ActualForms)
            inner.push_back(emitExtentForm(F, *rtDiscs_));
        if (const TypeNode* body = SchemaTypes.schemaBodyNodeOf(*sn->ResolvedBody)) {
            // Bound as a schema in its own right: the inner body's extents are
            // forms over ITS discriminants, and where a form could not be built
            // the fallback needs the inner names in scope.  Resolving the inner
            // body happens while the OUTER probe is in force, so its bounds are
            // never forms over the outer names -- which is why binding, rather
            // than only swapping the values, is what this needs.
            RtDiscScope disc(*this, inner);
            return rtSizeOfTypeNode(body);
        }
    }
    if (nodeExtentVaries(d))
        codegenICE("a schema body denoter with no run-time layout");
    return i64c(static_cast<int64_t>(
        Mod.getDataLayout().getTypeAllocSize(LlvmTypeOfNode(*d))));
}

/// Walk \p fields accumulating from \p off.  With \p stopAt set, returns the
/// offset of that field and sets *found; otherwise returns the offset one past
/// the last.  Size and offset come from ONE walk on purpose: worked out
/// separately, they drift, and that is how a field ends up outside the
/// allocation.
llvm::Value* SchemaLayoutEngine::rtWalkFields(const std::vector<FieldDecl>& fields,
                                               llvm::Value* off, bool packed,
                                               const std::string* stopAt, bool* found,
                                               std::vector<std::pair<std::string, llvm::Value*>>* collect) {
    for (const auto& fd : fields) {
        const uint64_t a = packed ? 1 : rtAlignOfTypeNode(fd.Type.get());
        for (const auto& nm : fd.Names) {
            off = alignUpV(off, a);
            // rtAllFieldOffsets wants every field's offset from the SAME walk
            // rtFieldOffset(rt, name) would otherwise redo from zero for each
            // name in turn -- one walk here instead of one per field is what
            // keeps a caller totalling every field's offset from being
            // quadratic in the field count.
            if (collect) collect->emplace_back(nm, off);
            if (stopAt && eqCI(nm, *stopAt)) { if (found) *found = true; return off; }
            off = B.CreateAdd(off, rtSizeOfTypeNode(fd.Type.get()),
                              "rec.off");
        }
    }
    return off;
}

/// §6.4.3.3: the alternatives of a variant part share one run of storage, so
/// the part is as big as the largest of them -- a max taken at run time here,
/// since an alternative's own size may depend on a discriminant.  The tag, if
/// there is one, is an ordinary field ahead of that run.
///
/// One walk, with \p stopAt selecting between "how big is the part" and "where
/// is this field in it", exactly as rtWalkFields already does for a fixed part.
llvm::Value* SchemaLayoutEngine::rtWalkVariant(const VariantPart& vp,
                                                llvm::Value* off, bool packed,
                                                const std::string* stopAt, bool* found,
                                                bool nested,
                                                std::vector<std::pair<std::string, llvm::Value*>>* collect) {
    // ISO §6.4.3.3 makes the tag-field OPTIONAL: `case boolean of` selects on a
    // type with no field to store it in.  This gated on TagType alone while the
    // static layout gates on the field having a NAME, so a tagless selector
    // reserved eight bytes for a tag that does not exist and put every
    // alternative that far past where an ordinary access looks.
    if (vp.TagType && !vp.TagField.empty()) {
        const uint64_t a = packed ? 1 : rtAlignOfTypeNode(vp.TagType.get());
        off = alignUpV(off, a);
        if (collect) collect->emplace_back(vp.TagField, off);
        if (stopAt && eqCI(vp.TagField, *stopAt)) { *found = true; return off; }
        off = B.CreateAdd(off, rtSizeOfTypeNode(vp.TagType.get()), "tag.off");
    }
    // Only the OUTERMOST run is pre-aligned, because in the static layout only
    // the outermost run is a struct element of its own and so gets the part's
    // alignment from LLVM.  A nested part lives inside that element, where
    // layoutVariantCase places its fields by their own alignment and nothing
    // else -- so pre-aligning a nested run here put `k` four bytes past where
    // an ordinary read of it looked, and a whole-value copy between `q^` and a
    // discriminated instance quietly swapped a character for a space.
    if (!nested) off = alignUpV(off, packed ? 1 : rtVariantRunAlign(vp));
    // Walked from off rather than from zero and added on afterwards.  Those two
    // are the same number only when off is already aligned to the widest field
    // in the part, which is exactly what the pre-align used to guarantee and
    // what a nested run does not have.
    llvm::Value* widest = off;
    for (const auto& vc : vp.Cases) {
        // One call, serving both readings: searching, this is the field's
        // offset when it is here and the end of the alternative when it is not,
        // which is the same number the size walk wants.  The search used to
        // walk the alternative a SECOND time to get that end, emitting every
        // field's size arithmetic twice.
        llvm::Value* end = rtWalkFields(vc.Fields, off, packed, stopAt, found, collect);
        if (found && *found) return end;
        if (vc.NestedVariant) {
            end = rtWalkVariant(*vc.NestedVariant, end, packed, stopAt, found,
                                /*nested=*/true, collect);
            if (found && *found) return end;
        }
        widest = B.CreateSelect(B.CreateICmpUGT(end, widest),
                                end, widest, "variant.max");
    }
    return widest;
}

/// What the SHARED RUN of a variant part must be aligned to: the widest thing
/// placed inside it, which is every alternative's fields and, for a nested
/// part, its tag and its own run -- a nested tag lives inside the outer run
/// rather than beside it.
///
/// Deliberately NOT the outer tag.  That is a member of the record and not of
/// the run, and it sits ahead of the run in a slot of its own; counting it here
/// would round the run's storage up to the tag's width for a variant whose tag
/// is wider than any of its alternatives.  rtVariantAlign adds it back for the
/// question the tag does bear on, which is what the whole record needs.
///
/// The static layout in CodeGenTypes.cpp accumulated this same number for
/// itself while placing fields.  It calls this instead: two implementations of
/// one number is how a cap on it came to be written down three times, and the
/// blob went 8-aligned around a member that needed 16.
uint64_t SchemaLayoutEngine::rtVariantRunAlign(const VariantPart& vp) {
    uint64_t a = 1;
    for (const auto& vc : vp.Cases) {
        for (const auto& fd : vc.Fields)
            a = std::max(a, rtAlignOfTypeNode(fd.Type.get()));
        if (const auto* nv = vc.NestedVariant.get()) {
            if (nv->TagType && !nv->TagField.empty())
                a = std::max(a, rtAlignOfTypeNode(nv->TagType.get()));
            a = std::max(a, rtVariantRunAlign(*nv));
        }
    }
    return a;
}

/// What a record containing this part must be aligned to.  The tag counts here
/// -- it is a member like any other -- and a tag with no field name occupies
/// nothing, so it constrains nothing.
uint64_t SchemaLayoutEngine::rtVariantAlign(const VariantPart& vp) {
    uint64_t a = (vp.TagType && !vp.TagField.empty())
                     ? rtAlignOfTypeNode(vp.TagType.get()) : 1;
    return std::max(a, rtVariantRunAlign(vp));
}

/// Every field's offset, from ONE walk of \p rt.  A caller that wants more
/// than one field's offset (checkFieldOffsetAgreement, checking all of them)
/// used to ask rtFieldOffset once per field, and rtFieldOffset restarts its
/// walk from the top every time -- an O(fields) walk run once per field is
/// O(fields^2). This does the one walk rtFieldOffset itself already does for
/// a single field, but keeps every field's offset from it instead of
/// discarding all but the one asked for.
std::vector<std::pair<std::string, llvm::Value*>>
SchemaLayoutEngine::rtAllFieldOffsets(const RecordTypeNode& rt) {
    std::vector<std::pair<std::string, llvm::Value*>> offsets;
    llvm::Value* off = rtWalkFields(rt.Fields, i64c(0), rt.Packed,
                                     /*stopAt=*/nullptr, /*found=*/nullptr,
                                     &offsets);
    if (rt.Variant)
        rtWalkVariant(*rt.Variant, off, rt.Packed, /*stopAt=*/nullptr,
                      /*found=*/nullptr, /*nested=*/false, &offsets);
    return offsets;
}

llvm::Value* SchemaLayoutEngine::rtFieldOffset(const RecordTypeNode& rt,
                                                const std::string& field) {
    bool found = false;
    llvm::Value* off = rtWalkFields(rt.Fields, i64c(0), rt.Packed, &field, &found);
    if (found) return off;
    if (rt.Variant) {
        auto* v = rtWalkVariant(*rt.Variant, off, rt.Packed, &field, &found);
        if (found) return v;
    }
    codegenICE("record has no field named '" + field + "'");
    return nullptr;
}
