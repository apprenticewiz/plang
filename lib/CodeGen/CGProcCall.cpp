#include "CGProcCall.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"

#include "CodegenICE.h"

using namespace plang;

void CGProcCall::emitCallStmt(const CallStmt& s) {
    std::string lo = toLower(s.Name);

    // ISO §6.6.3.1: a procedural parameter is called through the pair it
    // arrived as.  Checked before the required procedures, so a parameter
    // named `page` or `get` is still the parameter.
    if (auto* pve = SymTab.findVar(s.Name); pve && pve->isProcParam) {
        (void)ClosureAbi.emitProcParamCall(*pve, s.Args);
        return;
    }

    // ISO §6.2.2.10: a required procedure identifier may be redeclared, and
    // then it denotes what the program declared and not the required one.  The
    // chain below dispatches on spelling alone, so without this a program that
    // declares its own `close` reaches a required procedure that takes
    // different arguments — which it then emitted a call to with none of them.
    // Sema resolved the name in the scope it was written in and is the only
    // thing that knows which won.
    if (s.ResolvedBuiltin == BuiltinID::None) {
        emitUserProcCall(s);
        return;
    }

    if (lo == "write" || lo == "writeln") {
        Builtins.emitBuiltinWrite(s.Args, lo == "writeln");
        return;
    }
    if (lo == "read") {
        Builtins.emitBuiltinRead(s.Args);
        return;
    }
    if (lo == "readln") {
        Builtins.emitBuiltinReadln(s.Args);
        return;
    }
    // EP §6.7.5.5: both require a destination/source plus at least one value.
    if (lo == "writestr" && s.Args.size() >= 2) {
        Builtins.emitBuiltinWriteStr(s.Args);
        return;
    }
    if (lo == "readstr" && s.Args.size() >= 2) {
        Builtins.emitBuiltinReadStr(s.Args);
        return;
    }
    if (lo == "page") {
        if (!s.Args.empty() && FileVars.isFileVar(*s.Args[0])) {
            auto* fp = FileVars.fileVarPtr(*s.Args[0]);
            B.CreateCall(RtFns.getExternFnN("plang_page_file",
                llvm::Type::getVoidTy(Ctx), {PtrTy}), {fp});
        } else {
            B.CreateCall(RtFns.getRuntimeFn("plang_page", nullptr), {});
        }
        return;
    }
    if ((lo == "reset" || lo == "rewrite") && !s.Args.empty()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        // A string(n) filename has no NUL terminator of its own -- only the
        // { length, bytes } struct every EP string carries -- and
        // plang_reset/plang_rewrite take `const char *`, so it has to be
        // marshalled into one the same way a var-string reaches any other
        // C-string-shaped runtime entry point.
        auto* nm = s.Args.size() > 1
            ? StrCall.emitCStrArg(*s.Args[1])
            : llvm::ConstantPointerNull::get(PtrTy);
        // §6.4.3.5 makes a text file a sequence of lines, each ended by a line
        // marker.  Turning one round to read it has to finish the line the
        // writing left open, and whether there is a line to finish is a
        // question only about a text file.
        auto* fn = RtFns.getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy, I8Ty});
        B.CreateCall(fn, {fp, nm,
            llvm::ConstantInt::get(I8Ty,
                FileVars.isTypedBinaryFileVar(*s.Args[0]) ? 0 : 1)});
        return;
    }
    if ((lo == "get" || lo == "put") && !s.Args.empty()) {
        // ISO §6.5.5: both move one component, so both need its width.
        auto* fp  = FileVars.fileVarPtr(*s.Args[0]);
        auto* esz = llvm::ConstantInt::get(I64Ty, FileVars.getFileElemSize(*s.Args[0]));
        auto* fn  = RtFns.getExternFnN("plang_" + lo + "_file",
            llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty});
        B.CreateCall(fn, {fp, esz});
        return;
    }
    if (lo == "close" && !s.Args.empty()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_close",
            llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {fp});
        return;
    }
    // EP §6.7.5.2: extend / update
    if ((lo == "extend" || lo == "update") && !s.Args.empty()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        // Same filename marshalling reset/rewrite need just above -- a
        // string(n) actual is a { length, bytes } struct, and plang_extend/
        // plang_update take `const char *`.
        auto* nm = s.Args.size() > 1
            ? StrCall.emitCStrArg(*s.Args[1])
            : llvm::ConstantPointerNull::get(PtrTy);
        auto* fn = RtFns.getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy});
        B.CreateCall(fn, {fp, nm});
        return;
    }
    // EP §6.7.5.2: SeekRead / SeekWrite / SeekUpdate.  n is a value of the
    // file's declared INDEX TYPE (ISO §6.7.5.2's own pre-assertion measures
    // "ord(n)-ord(a)"), not a byte offset and not a 0-based component count
    // -- so `file[5..10] of integer; SeekWrite(f, 5)` must land on the FIRST
    // component, not five components in.
    if ((lo == "seekread" || lo == "seekwrite" || lo == "seekupdate")
        && s.Args.size() >= 2) {
        auto* fp      = FileVars.fileVarPtr(*s.Args[0]);
        auto* idx     = ToI64(EmitExpr(*s.Args[1]));
        int64_t esz   = FileVars.getFileElemSize(*s.Args[0]);
        int64_t ilo   = FileVars.getFileIndexLow(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_" + lo,
            llvm::Type::getVoidTy(Ctx), {PtrTy, I64Ty, I64Ty, I64Ty});
        B.CreateCall(fn, {fp, idx, llvm::ConstantInt::get(I64Ty, esz),
                                llvm::ConstantInt::get(I64Ty, ilo)});
        return;
    }
    // EP §6.7.5.6: bind(f, b) / unbind(f) — associate/dissociate file binding
    if (lo == "bind" && s.Args.size() >= 2) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* bp = EmitLValue(*s.Args[1]);
        auto* fn = RtFns.getExternFnN("plang_bind",
                                llvm::Type::getVoidTy(Ctx), {PtrTy, PtrTy});
        B.CreateCall(fn, {fp, bp});
        return;
    }
    if (lo == "unbind" && !s.Args.empty()) {
        auto* fp = FileVars.fileVarPtr(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_unbind",
                                llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {fp});
        return;
    }

    // EP §6.7.5.8: GetTimeStamp(t) — fill t with current date/time
    if (lo == "gettimestamp" && !s.Args.empty()) {
        auto* tPtr = EmitLValue(*s.Args[0]);
        auto* fn = RtFns.getExternFnN("plang_gettimestamp",
                                llvm::Type::getVoidTy(Ctx), {PtrTy});
        B.CreateCall(fn, {tPtr});
        return;
    }

    // ISO §6.7.5.4 transfer procedures.
    if ((lo == "pack" || lo == "unpack") && s.Args.size() == 3) {
        PackUnpack.emitPackUnpack(s, /*isPack=*/lo == "pack");
        return;
    }

    if (lo == "halt" || lo == "exit") {
        // EP §6.7.5.7 halt takes no argument; halt(n) is the widespread extension
        // that sets the exit status.
        auto* status = s.Args.empty() ? llvm::ConstantInt::get(I64Ty, 0)
                                      : ToI64(EmitExpr(*s.Args[0]));
        B.CreateCall(RtFns.getRuntimeHaltFn(), {status});
        B.CreateUnreachable();
        return;
    }
    if (lo == "new" && !s.Args.empty()) {
        // EP §6.7.5.3: new(p, d1..ds) when p's domain-type is a schema-name.
        if (const auto& pt = s.Args[0]->ResolvedType;
                pt && pt->Kind == TypeKind::Pointer && pt->PointeeType
                && pt->PointeeType->Kind == TypeKind::Schema) {
            Schema.emitNewSchema(*s.Args[0], *pt->PointeeType,
                          std::span(s.Args).subspan(1));
            return;
        }
        auto* addr = EmitLValue(*s.Args[0]);
        // How much to allocate is a question about the pointer's type, not
        // about how the declaration was written.  A pointer reached through a
        // type name has no PointerTypeNode to read, and the old fallback of
        // one pointer's worth silently under-allocated for anything larger.
        // R1, reopened by review 5.  Sema's answer was already here -- as the
        // FALLBACK, reached only when the denoter route returned 0.  The
        // denoter route walks `typeAliases` by SPELLING at the use site, so a
        // pointer declared `var g: pt` in a program where a procedure declares
        // its own `pt` allocated the INNER pt's domain: 16 bytes for a
        // ten-element array, and glibc aborted on the corrupted heap.  Plain
        // ISO 7185, and the shape the 0.1.5/0.1.6 corruptions had.
        //
        // The R1 rule went into llvmTypeOfNode's NamedTypeNode branch, which is
        // why this survived it: the name is re-bound HERE, before any TypeNode
        // is lowered, so llvmTypeOfNode is handed the inner declaration's base
        // and answers correctly for the wrong type.  A site that resolves a
        // name before reaching the rule is not covered by the rule.
        int64_t            Bytes   = 0;
        const TypeNode*    domain  = nullptr;
        const plang::Type* pointee = nullptr;
        if (const auto& pt = s.Args[0]->ResolvedType;
                pt && pt->Kind == TypeKind::Pointer && pt->PointeeType)
            pointee = pt->PointeeType.get();
        if (pointee)
            Bytes = (int64_t)Mod.getDataLayout().getTypeAllocSize(
                Types.llvmTypeOfSemaType(*pointee));

        // The domain DENOTER is still wanted, for the initial state below --
        // Sema's Type records a RecordDecl and nothing more general, so a
        // `value` clause on a non-record domain is only reachable through the
        // node.  It is accepted only when it agrees with the size Sema gave:
        // the same spelling walk that mis-sized the allocation also picked the
        // wrong type's `value` clause, memcpying 400 bytes of one type's
        // initial value into another's 4-byte allocation.  A disagreement means
        // the denoter was re-resolved somewhere else, so it is not this
        // variable's domain and its initial state is not this variable's.
        //
        // The size-agreement check is not enough on its own: `ve->typeNode` is
        // `g`'s OWN declaration, written wherever `g` was declared -- module
        // scope, say -- and not in the procedure calling `new(g)`.  denoterOf
        // walked `typeAliases` for that FOREIGN node's name, so a procedure
        // that merely shadows the pointer's own type name with an unrelated,
        // SAME-SIZE one slipped straight through the check: `new(g)` inside a
        // procedure with its own local `type pt = ^inner_dom` (one field,
        // like the real domain) applied inner_dom's `value` clause to g's
        // real, unrelated allocation.  initialStateShapeOf is the fix already
        // used for exactly this pattern elsewhere: it follows
        // NamedTypeNode::Denotes, which Sema recorded in the scope `pt` was
        // actually written in.
        if (auto* id = llvm::dyn_cast<IdentExpr>(s.Args[0].get()))
            if (auto* ve = SymTab.findVar(id->Name))
                if (auto* ptn = llvm::dyn_cast_or_null<PointerTypeNode>(
                        InitialStateShapeOf(ve->typeNode))) {
                    const TypeNode* d = ptn->Base.get();
                    const auto dsz = (int64_t)Mod.getDataLayout()
                        .getTypeAllocSize(Types.llvmTypeOfNode(*d));
                    if (Bytes == 0 || dsz == Bytes) {
                        domain = d;
                        if (Bytes == 0) Bytes = dsz;
                    }
                }
        // The domain type, for the initial state below.  Only the identifier
        // route set it, so `new(h.p)` and `new(a[1])` applied no initial state
        // at all: the size already fell back to Sema's type and this did not.
        // A record is what carries field `value` clauses, and Sema's type
        // knows the declaration it came from.
        if (!domain && pointee) domain = pointee->RecordDecl;
        if (Bytes == 0)
            codegenICE("new() cannot determine the size of what '"
                       + std::string(s.Args[0]->ResolvedType
                                     ? s.Args[0]->ResolvedType->Name : "?")
                       + "' points to");
        auto* ptr = B.CreateCall(RtFns.getRuntimeNewFn(),
                                       {llvm::ConstantInt::get(I64Ty, Bytes)});
        B.CreateStore(ptr, addr);
        // EP §6.6: the variable new creates begins in whatever state its type
        // says a variable of it begins in, as a declared one would.
        if (domain && HasInitialState(domain))
            EmitInitialState(ptr, Types.llvmTypeOfNode(*domain), domain);
        return;
    }
    if (lo == "dispose" && !s.Args.empty()) {
        auto* val = EmitExpr(*s.Args[0]);
        B.CreateCall(RtFns.getRuntimeDisposeFn(), {val});
        return;
    }

    emitUserProcCall(s);
}

void CGProcCall::emitUserProcCall(const CallStmt& s) {
    // User-defined procedure — walk the nesting hierarchy to find the right
    // LLVM mangled name (plang_outer__inner, not just plang_inner).
    std::string mangledName = Linkage.findMangledProc(s.Name);
    auto* callee = Mod.getFunction(mangledName);
    if (!callee) {
        // The procedure is not defined in this compilation unit; it must come
        // from a separately compiled module.  Create an external declaration
        // using LLVM types derived from the Sema-resolved argument types.
        std::vector<llvm::Type*> paramTys;
        for (const auto& Arg : s.Args) {
            if (Arg && Arg->ResolvedType && !Arg->ResolvedType->isError())
                paramTys.push_back(Types.llvmTypeOfSemaType(*Arg->ResolvedType));
            else
                paramTys.push_back(I64Ty); // safe fallback
        }
        auto* fnTy = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), paramTys, false);
        callee = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage,
                                        mangledName, &Mod);
    }

    std::vector<llvm::Value*> args;

    // If the callee is a nested procedure, build its static-link frame from
    // funcOuterVarNames_[callee] (recorded at definition) so the slot ordering
    // matches exactly, resolving each name through findVar() in the *current*
    // scope.  findVar() returns the local alloca for immediate outer vars and
    // the GEP-loaded pointer for deeper ones — composing correctly through any
    // number of nesting levels.
    if (auto* frame = BuildStaticLinkFrame(mangledName)) args.push_back(frame);

    // EP §6.7.3.7: look up conformant param dimensions for this callee.
    // ConformantDimsOf(mangledName, astArgIdx) is the dimension count for the
    // i-th AST argument position.  0 means the param is not conformant (emit
    // normally).
    size_t pi = args.size(); // LLVM arg index (after static link)
    for (size_t astArgIdx = 0; astArgIdx < s.Args.size(); ++astArgIdx) {
        const auto& arg = s.Args[astArgIdx];

        // ISO §6.6.3.1: procedural param — entry point plus its frame.
        if (const auto* pt = ProcParamArg(mangledName, astArgIdx)) {
            ClosureAbi.pushProcParamArgs(args, *arg, *pt);
            pi = args.size();
            continue;
        }

        // Check if this AST arg position is conformant.
        // EP §6.4.7: schema param — body pointer plus its discriminants.
        if (unsigned nd = Schema.schemaArgDiscs(mangledName, astArgIdx); nd > 0) {
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
                    pi < callee->arg_size()
                        ? callee->getFunctionType()->getParamType(pi) : nullptr,
                    ParamIsByRef(mangledName, astArgIdx)),
                *arg, destSetBase));
            ++pi;
        }
    }
    B.CreateCall(callee, args);
}
