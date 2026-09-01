#include "CGCallMarshal.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/DerivedTypes.h"

#include "ClosureAndCallABI.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"

using namespace plang;

void CGCallMarshal::marshalArgs(const std::string& mangledName, llvm::FunctionType* calleeTy,
                                 std::span<const std::unique_ptr<plang::ExprNode>> Args,
                                 std::vector<llvm::Value*>& args) const {
    // EP §6.7.3.7: look up conformant param dimensions for this callee.
    // ConformantDimsOf(mangledName, astArgIdx) is the dimension count for the
    // i-th AST argument position.  0 means the param is not conformant (emit
    // normally).
    size_t pi = args.size(); // LLVM arg index (after any leading frame/Self)
    for (size_t astArgIdx = 0; astArgIdx < Args.size(); ++astArgIdx) {
        const auto& arg = Args[astArgIdx];

        // ISO §6.6.3.1: procedural param — entry point plus its frame.
        if (const auto* pt = ProcParamArg(mangledName, astArgIdx)) {
            ClosureAbi.pushProcParamArgs(args, *arg, *pt);
            pi = args.size();
            continue;
        }

        // Check if this AST arg position is conformant.
        // EP §6.4.7: schema param — body pointer plus its discriminants.
        if (unsigned nd = SchemaArgDiscsOf(mangledName, astArgIdx); nd > 0) {
            Schema.pushSchemaArgs(args, *arg, nd);
            pi = args.size();
            continue;
        }

        const size_t dims = ConformantDimsOf(mangledName, astArgIdx);
        if (dims > 0) {
            ClosureAbi.pushConformantArgs(args, *arg, dims);
            pi += 1 + 2 * dims;
        } else {
            // Regular param (var or value).
            std::optional<int64_t> destSetBase = ParamSetBaseOf(mangledName, astArgIdx);
            args.push_back(Sets.alignSetArg(
                StrCall.emitCallArg(*arg,
                    pi < calleeTy->getNumParams()
                        ? calleeTy->getParamType(pi) : nullptr,
                    ParamIsByRef(mangledName, astArgIdx)),
                *arg, destSetBase));
            ++pi;
        }
    }
}

llvm::Value* CGCallMarshal::spillStructReturnIfNeeded(const plang::ExprNode& e,
                                                       llvm::Value* ret) const {
    if (ExprIsVarStr(e) && ret->getType()->isStructTy()) {
        auto* tmp = CreateEntryAlloca(ret->getType(), "str.ret");
        B.CreateStore(ret, tmp);
        return tmp;
    }
    if (ExprIsShortStr(e) && ret->getType()->isStructTy()) {
        auto* tmp = CreateEntryAlloca(ret->getType(), "sstr.ret");
        B.CreateStore(ret, tmp);
        return tmp;
    }
    return ret;
}
