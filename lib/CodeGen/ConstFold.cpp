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
        if (n->Op == TokenKind::Minus)
            if (auto v = tryEvalConstInt(*n->Operand, known)) return -*v;
        if (n->Op == TokenKind::Plus)
            return tryEvalConstInt(*n->Operand, known);
        return std::nullopt;
    }
    if (auto* n = llvm::dyn_cast<BinaryExpr>(&e)) {
        const auto l = tryEvalConstInt(*n->Left,  known);
        const auto r = tryEvalConstInt(*n->Right, known);
        if (!l || !r) return std::nullopt;
        switch (n->Op) {
        case TokenKind::Plus:  return *l + *r;
        case TokenKind::Minus: return *l - *r;
        case TokenKind::Times: return *l * *r;
        case TokenKind::Div:   return *r ? std::optional{*l / *r} : std::nullopt;
        case TokenKind::Mod:   return *r ? std::optional{isoMod(*l, *r)}
                                         : std::nullopt;
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
        if (auto* vi = dyn_cast<llvm::ConstantInt>(vc))
            return llvm::ConstantInt::get(i64Ty,
                static_cast<uint64_t>(-vi->getSExtValue()), true);
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
            switch (n->Op) {
            case TokenKind::Plus:  return llvm::ConstantInt::get(i64Ty, l + r, true);
            case TokenKind::Minus: return llvm::ConstantInt::get(i64Ty, l - r, true);
            case TokenKind::Times: return llvm::ConstantInt::get(i64Ty, l * r, true);
            case TokenKind::Div:   return r ? llvm::ConstantInt::get(i64Ty, l / r, true) : nullptr;
            case TokenKind::Mod:   return r ? llvm::ConstantInt::get(i64Ty, isoMod(l, r), true) : nullptr;
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
