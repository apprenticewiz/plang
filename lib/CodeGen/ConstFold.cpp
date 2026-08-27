#include "ConstFold.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Arith.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Token.h"
#include "llvm/Support/Casting.h"

using namespace plang;

// The value of a constant expression, or nothing when it is not one this can
// work out.  'known' maps lowercase names to LLVM Values (may be null).
//
// Absence is spelled as absence rather than as a number: an array bound that
// did not fold used to come back as the caller's fallback, and a fallback of 0
// makes `array [0..n]` a perfectly ordinary one-element range that nothing
// downstream can tell from a real one.
std::optional<int64_t> tryEvalConstInt(
        const ExprNode& e,
        const std::unordered_map<std::string, llvm::Value*>* known) {
    // R2.  Sema folded this expression in the scope it was WRITTEN in, and
    // that is the answer.  Everything below re-folds it here, against a table
    // holding whatever is innermost where the expression is being lowered --
    // a different question the moment a name is redeclared between the two
    // points, and the source of every finding in class A of
    // docs/single-source-of-truth.md.
    //
    // Sema does not record a value it folded against a schema's probe binding,
    // so a bound over a discriminant still falls through to the code below and
    // is emitted against the discriminants actually in hand.
    if (e.ConstVal) return *e.ConstVal;
    if (auto* n = llvm::dyn_cast<IntLitExpr>(&e))  return n->Value;
    if (auto* n = llvm::dyn_cast<BoolLitExpr>(&e)) return n->Value ? 1 : 0;
    // ISO §6.1.7: a one-character string is a char-type constant, so it is an
    // ordinal and may appear as an array or subrange bound.
    if (auto* n = llvm::dyn_cast<StringLitExpr>(&e))
        if (n->Value.size() == 1)
            return static_cast<int64_t>(
                static_cast<unsigned char>(n->Value[0]));
    if (auto* n = llvm::dyn_cast<IdentExpr>(&e)) {
        if (known) {
            auto it = known->find(toLower(n->Name));
            if (it != known->end())
                if (auto* ci = llvm::dyn_cast_or_null<llvm::ConstantInt>(it->second))
                    // A char constant is held as i8 and its ordinal is 0..255;
                    // Pascal has no negative char.  Reading every entry back
                    // sign-extended made `maxchar` fold to -1 here while Sema
                    // folded it to 255.  Latent rather than live -- a char
                    // bound pairs with another char, so -1 always produced a
                    // non-positive extent and tripped the "did not fold"
                    // fallback into Sema's answer -- but a landmine for any
                    // future caller without that guard.
                    return ci->getBitWidth() == 8
                               ? static_cast<int64_t>(ci->getZExtValue())
                               : ci->getSExtValue();
        }
        return std::nullopt;
    }
    if (auto* n = llvm::dyn_cast<UnaryExpr>(&e)) {
        // Issue #202: -minint overflows (its magnitude, 2^63, is one past
        // maxint); checkedNeg (Arith.h) declines rather than compute the
        // wrapped result, the same as Sema's own fold (SemaType.cpp).
        if (n->Op == TokenKind::Minus)
            if (auto v = tryEvalConstInt(*n->Operand, known)) return checkedNeg(*v);
        if (n->Op == TokenKind::Plus)
            return tryEvalConstInt(*n->Operand, known);
        return std::nullopt;
    }
    if (auto* n = llvm::dyn_cast<BinaryExpr>(&e)) {
        const auto l = tryEvalConstInt(*n->Left,  known);
        const auto r = tryEvalConstInt(*n->Right, known);
        if (!l || !r) return std::nullopt;
        switch (n->Op) {
        // Issue #202: checked the same way Sema's own fold is -- nullopt on
        // overflow rather than a wrapped value handed back as the constant.
        case TokenKind::Plus:  return checkedAdd(*l, *r);
        case TokenKind::Minus: return checkedSub(*l, *r);
        case TokenKind::Times: return checkedMul(*l, *r);
        // Issue #201: divOverflows is minint div/mod -1, the one nonzero
        // divisor with no representable result -- it SIGFPE-traps this
        // hardware's `idiv` the same way a zero divisor already does, so it
        // needs the same guard as the zero check right here.
        case TokenKind::Div:
            return (*r && !divOverflows(*l, *r))
                 ? std::optional{*l / *r} : std::nullopt;
        case TokenKind::Mod:
            return (*r && !divOverflows(*l, *r))
                 ? std::optional{isoMod(*l, *r)} : std::nullopt;
        default:               return std::nullopt;
        }
    }
    return std::nullopt;
}

int64_t evalConstInt(const ExprNode& e, int64_t fallback,
                     const std::unordered_map<std::string, llvm::Value*>* known) {
    return tryEvalConstInt(e, known).value_or(fallback);
}

// EP §6.8.2: evaluate a nonvarying (constant) expression to an LLVM Constant.
// Returns null if the expression cannot be folded at compile time (e.g. variable
// references, function calls).  Previously-defined constants are resolved via
// the 'known' map (lowercase name -> llvm::Constant*).
llvm::Constant* evalConst(
        const ExprNode& e,
        const std::unordered_map<std::string, llvm::Value*>& known,
        llvm::LLVMContext& ctx,
        llvm::IntegerType* i64Ty,
        llvm::Type* dblTy) {
    using llvm::dyn_cast;

    if (auto* n = dyn_cast<IntLitExpr>(&e))
        return llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(n->Value), true);
    if (auto* n = dyn_cast<RealLitExpr>(&e))
        return llvm::ConstantFP::get(dblTy, n->Value);
    if (auto* n = dyn_cast<BoolLitExpr>(&e))
        return llvm::ConstantInt::getBool(ctx, n->Value);

    if (auto* n = dyn_cast<IdentExpr>(&e)) {
        auto it = known.find(toLower(n->Name));
        if (it != known.end())
            return dyn_cast<llvm::Constant>(it->second);
        return nullptr;
    }

    if (auto* n = dyn_cast<UnaryExpr>(&e)) {
        auto* vc = evalConst(*n->Operand, known, ctx, i64Ty, dblTy);
        if (!vc || n->Op != TokenKind::Minus) return nullptr;
        if (auto* vi = dyn_cast<llvm::ConstantInt>(vc)) {
            // Issue #202: -minint overflows; decline (null, the same answer
            // as every other expression this cannot fold) instead of
            // computing the wrapped result.
            auto neg = checkedNeg(vi->getSExtValue());
            return neg ? llvm::ConstantInt::get(
                             i64Ty, static_cast<uint64_t>(*neg), true)
                       : nullptr;
        }
        if (auto* vf = dyn_cast<llvm::ConstantFP>(vc))
            return llvm::ConstantFP::get(dblTy,
                -vf->getValueAPF().convertToDouble());
        return nullptr;
    }

    if (auto* n = dyn_cast<BinaryExpr>(&e)) {
        auto* lc = evalConst(*n->Left,  known, ctx, i64Ty, dblTy);
        auto* rc = evalConst(*n->Right, known, ctx, i64Ty, dblTy);
        if (!lc || !rc) return nullptr;

        // Integer × Integer
        auto* li = dyn_cast<llvm::ConstantInt>(lc);
        auto* ri = dyn_cast<llvm::ConstantInt>(rc);
        if (li && ri) {
            int64_t l = li->getSExtValue(), r = ri->getSExtValue();
            // Issue #202/#201: checked the same way tryEvalConstInt and
            // Sema's own fold are -- null (decline) on overflow rather than
            // a wrapped value baked into the IR as the constant, or, for
            // minint div/mod -1, a SIGFPE computing it right here.
            auto asConst = [&](std::optional<int64_t> v) -> llvm::Constant* {
                return v ? llvm::ConstantInt::get(i64Ty, *v, true) : nullptr;
            };
            switch (n->Op) {
            case TokenKind::Plus:  return asConst(checkedAdd(l, r));
            case TokenKind::Minus: return asConst(checkedSub(l, r));
            case TokenKind::Times: return asConst(checkedMul(l, r));
            case TokenKind::Div:
                return (r && !divOverflows(l, r))
                     ? llvm::ConstantInt::get(i64Ty, l / r, true) : nullptr;
            case TokenKind::Mod:
                return (r && !divOverflows(l, r))
                     ? llvm::ConstantInt::get(i64Ty, isoMod(l, r), true) : nullptr;
            default:               return nullptr;
            }
        }

        // Real × Real (with widening from int)
        auto toDouble = [](llvm::Constant* c) -> std::optional<double> {
            if (auto* cf = dyn_cast<llvm::ConstantFP>(c))
                return cf->getValueAPF().convertToDouble();
            if (auto* ci = dyn_cast<llvm::ConstantInt>(c))
                return static_cast<double>(ci->getSExtValue());
            return std::nullopt;
        };
        auto lv = toDouble(lc), rv = toDouble(rc);
        if (lv && rv) {
            switch (n->Op) {
            case TokenKind::Plus:   return llvm::ConstantFP::get(dblTy, *lv + *rv);
            case TokenKind::Minus:  return llvm::ConstantFP::get(dblTy, *lv - *rv);
            case TokenKind::Times:  return llvm::ConstantFP::get(dblTy, *lv * *rv);
            case TokenKind::Divide: return llvm::ConstantFP::get(dblTy, *lv / *rv);
            default:                return nullptr;
            }
        }
        return nullptr;
    }
    return nullptr;
}
