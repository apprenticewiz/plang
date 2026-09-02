#include "plang/Sema/Sema.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Arith.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/RequiredRecordLayouts.h"
#include "plang/Basic/SemaUtil.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Token.h"
#include "plang/Lex/Scanner.h"
#include "plang/Parse/Parser.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>

using namespace plang;

// ---------------------------------------------------------------------------
// Sema constructor — build the built-in type singletons
// ---------------------------------------------------------------------------

Sema::Sema(DiagnosticsEngine& Diags, LangOptions Opts)
    : Opts  (Opts),
      Diags (Diags),
      // Reference point checkExpr/constBound/buildExtentForm's own stack-
      // headroom check (SemaExpr.cpp/SemaType.cpp, issue #556) measures
      // usage from -- see StackBaseline's own comment (Sema.h) for why this
      // needs to be a live measurement rather than a term-count constant,
      // the same reasoning Parser::StackBaseline is captured for. Captured
      // here, at construction, rather than lazily on checkExpr's first call,
      // so it reflects the shallowest point this Sema instance is ever
      // active at.
      StackBaseline(captureStackBaseline())
    // TyInt, TyReal, etc. are reference members bound to Ctx_ singletons;
    // Ctx_ is default-constructed above, so the singletons are already live.
{}

// ---------------------------------------------------------------------------
// Diagnostic helpers
// ---------------------------------------------------------------------------

void Sema::error(SourceLocation Loc, std::string_view Msg) {
    Diags.report(Loc, DiagSeverity::Error, std::string{Msg});
}

void Sema::warning(SourceLocation Loc, std::string_view Msg) {
    Diags.report(Loc, DiagSeverity::Warning, std::string{Msg});
}

void Sema::error(SourceLocation Loc, DiagID ID,
                 std::initializer_list<std::string_view> Args) {
    Diags.report(Loc, ID, Args);
}

void Sema::warning(SourceLocation Loc, DiagID ID,
                   std::initializer_list<std::string_view> Args) {
    Diags.report(Loc, ID, Args);
}

bool Sema::isIdenticalType(const std::shared_ptr<Type>& a,
                            const std::shared_ptr<Type>& b) const {
    return Ctx_.identical(a, b);
}

namespace {
/// Records that this type-denoter was written as the body of `type Decl = ...`.
///
/// An enumeration or record is identified by its declaration, so it needs the
/// declared name to be told apart from the next one; until this runs they are
/// all called "(enum)" or "(record)" and would compare equal.  A NAMED array
/// is the identical story (issue #178): it too needs the declared name to be
/// told apart from a same-shaped array declared elsewhere, and until this
/// runs it is called "array[1..N] of T" the same as any other array of that
/// shape.  Read isAnonymousNominal's own comment (Type.h) for why an array
/// needs one more thing Enum/Record do not -- Sema.cpp's Phase 3b routes a
/// named array's OWN resolution through TypeContext::makeArrayUncached
/// rather than the interning getArray, so that the rename below lands on an
/// object nothing else shares, the same precondition Enum/Record get for
/// free by never being interned in the first place.
///
/// A plain structural type (Set, Pointer, Subrange, ... — and an ANONYMOUS
/// array) keeps the descriptive name it already has, and an alias of an
/// existing type must keep the name of the type it aliases — renaming there
/// would rename the one canonical object out from under every other user of
/// it.
void nameNominalType(Type& T, const std::string& DeclName) {
    // EP §6.4.2.5: a restricted type is a new type made for this definition
    // alone, so naming it renames nothing that anything else can see.
    if (!T.Name.empty() && !isAnonymousNominal(T) && !T.isRestricted()) return;
    T.Name      = DeclName;
    T.Anonymous = false;
}

/// TP-only: whether CodeGen's typed-constant lowering (buildTypedConstInit,
/// CGTypedConst.cpp) can fold a value of type \p T into a compile-time
/// llvm::Constant.  Scalars fold directly; an array or a fixed (non-variant)
/// record folds if every element/field type does.  Deliberately excludes
/// String/VarString/Set/File/Pointer/Procedure/Function/ConformantArray/
/// SchemaInstance/Schema and a record with a variant part -- not because TP7
/// itself refuses them, but because this first implementation does not yet
/// have a lowering for them; see err_typed_const_unsupported_type's own
/// comment (DiagnosticSemaKinds.def) for the reasoning behind the line.
bool typedConstTypeSupported(const Type& T) {
    if (T.isOrdinal() || T.Kind == TypeKind::Real) return true;
    if (T.Kind == TypeKind::Array)
        return T.ElemType && typedConstTypeSupported(*T.ElemType);
    if (T.Kind == TypeKind::Record) {
        if (T.RecordDecl && T.RecordDecl->Variant) return false;
        for (const auto& F : T.RecordFields)
            if (!F.Ty || !typedConstTypeSupported(*F.Ty)) return false;
        return true;
    }
    return false;
}

/// TimeStamp's and BindingType's RecordFields lists are necessarily
/// hand-written here -- they carry Sema-level subrange types the Basic-layer
/// field-list macros in RequiredRecordLayouts.h cannot reference -- so
/// nothing else ties a field added, renamed or reordered on one side to its
/// counterpart on the other.  Called once per list, right after it is built.
void checkRequiredRecordFields(const std::vector<Type::Field>& Fields,
                                std::initializer_list<const char*> Expected,
                                const char* TypeName) {
    if (Fields.size() != Expected.size())
        llvm::report_fatal_error(
            llvm::Twine("plang sema: ") + TypeName + " has "
            + llvm::Twine(Expected.size()) + " required fields and Sema built "
            + llvm::Twine(Fields.size()), false);
    size_t I = 0;
    for (const char* Name : Expected) {
        if (Fields[I].Name != Name)
            llvm::report_fatal_error(
                llvm::Twine("plang sema: ") + TypeName + " field "
                + llvm::Twine(I) + " is '" + Name + "' in its layout and '"
                + Fields[I].Name + "' as Sema built it", false);
        ++I;
    }
}
} // namespace

bool Sema::hasErrors() const { return Diags.hasErrors(); }

// ---------------------------------------------------------------------------
// Built-in registration
// ---------------------------------------------------------------------------

void Sema::registerBuiltins() {
    // BindingType first: `binding` is declared below with it as its result, and
    // the loop that declares it cannot wait for a type built afterwards.
    //
    // EP §6.4.3.4 requires both 'name', of an implementation-defined
    // variable-string-type, and 'bound'.  The capacity is this implementation's
    // choice.
    {
        auto TyBindName = std::make_shared<Type>();
        TyBindName->Kind        = TypeKind::VarString;
        TyBindName->Name        = "string";
        TyBindName->StrCapacity = PlangMaxBindingName;

        TyBindingType = std::make_shared<Type>();
        TyBindingType->Kind = TypeKind::Record;
        TyBindingType->Name = "BindingType";
        TyBindingType->RecordFields = {
            { "name",  TyBindName },
            { "bound", TyBool     },
        };
#define PLANG_FIELD_NAME_ONLY(Member, LLVMTy) #Member,
        checkRequiredRecordFields(TyBindingType->RecordFields,
            {PLANG_BINDINGTYPE_FIELDS(PLANG_FIELD_NAME_ONLY)}, "BindingType");
#undef PLANG_FIELD_NAME_ONLY
    }

    // One loop over Builtins.def.  Every name is declared whatever the dialect,
    // NotInDialect records that this one is not for the dialect in force, so
    // that using `card` under -std=iso7185 is told what it is rather than that
    // it is undefined.  Before this, ten names worked that way and nineteen --
    // the whole Extended Pascal block below the string functions -- did not.
    const auto resultType = [&](BuiltinResult R) -> std::shared_ptr<Type> {
        switch (R) {
        case BuiltinResult::None:        return nullptr;
        case BuiltinResult::Int:         return TyInt;
        case BuiltinResult::Real:        return TyReal;
        case BuiltinResult::Char:        return TyChar;
        case BuiltinResult::Bool:        return TyBool;
        case BuiltinResult::Str:         return TyStr;
        case BuiltinResult::Complex:     return TyComplex;
        case BuiltinResult::BindingType: return TyBindingType;
        }
        return nullptr;
    };

#define BUILTIN(Id_, Spelling_, Kind_, Dialects_, Min_, Max_, Result_, ArgKind_) \
    {                                                                          \
        Symbol S;                                                              \
        S.Kind        = SymbolKind::Builtin;                                   \
        S.Name        = Spelling_;                                             \
        S.BuiltinKind = BuiltinID::Id_;                                        \
        S.IsFunction  = builtinIsFunction(BuiltinID::Id_);                     \
        S.ReturnType  = resultType(builtinResult(BuiltinID::Id_));             \
        S.NotInDialect= !Opts.inDialect(builtinDialects(BuiltinID::Id_));      \
        S.IsRequiredIdentifier = true;                                         \
        (void)Symtab.define(std::move(S));                                     \
    }
#include "plang/Basic/Builtins.def"

    // Predefined constants
    {
        // ISO §6.4.2.2: maxint is a constant, so it may stand wherever one may —
        // `vnum: -maxint..maxint` among them.  Codegen knew its value and Sema
        // did not, so a bound written with it would not fold and the subrange
        // was rejected as though the name were a variable.
        Symbol Maxint;
        Maxint.Kind = SymbolKind::Const;
        Maxint.Name = "maxint";
        Maxint.Ty = TyInt;
        // The largest value the dialect's integer holds.  ISO §6.4.2.2 leaves
        // the range implementation-defined and plang's is 64 bits wide; Turbo's
        // Integer is 16, so its maxint is 32767 -- and that is not a free
        // choice, because a program that overflows at 32767 on a real Turbo and
        // not here is not compiling as Turbo Pascal.
        Maxint.ConstOrdinal    = static_cast<int64_t>(
            (~0ULL >> (64 - Opts.defaultIntWidth() + 1)));
        Maxint.HasConstOrdinal = true;
        Maxint.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(Maxint));
    }
    {
        Symbol Pi;
        Pi.Kind = SymbolKind::Const;
        Pi.Name = "pi";
        Pi.Ty = TyReal;
        Pi.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(Pi));
    }

    // -std=turbo only: PChar/PAnsiChar -- both names the one dedicated
    // TypeContext::getPChar() singleton (NOT the getPointer(TyChar) any
    // literal `^Char` in the program resolves to; see that accessor's own
    // comment for why the two must not be the same object).  PAnsiChar is
    // registered as a plain synonym, the same one-Type-two-names shape FPC
    // itself uses when Char is the single-byte AnsiChar (i.e. always, since
    // plang has no WideChar): declaring `p: PChar` and `q: PAnsiChar` gives
    // two variables of the identical type, not two merely-compatible ones.
    if (Opts.turbo()) {
        Symbol PCharSym;
        PCharSym.Kind = SymbolKind::TypeAlias;
        PCharSym.Name = "PChar";
        PCharSym.Ty   = TyPChar;
        PCharSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(PCharSym));

        Symbol PAnsiCharSym;
        PAnsiCharSym.Kind = SymbolKind::TypeAlias;
        PAnsiCharSym.Name = "PAnsiChar";
        PAnsiCharSym.Ty   = TyPChar;
        PAnsiCharSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(PAnsiCharSym));
    }

    // -std=turbo only: ExitCode -- the value emitMain (CodeGenProcs.cpp)
    // returns to the OS when the program block ends normally, rather than
    // through Halt(n) (which takes its own status and never touches this).
    // The FIRST predefined identifier this project registers as a mutable
    // Var rather than a Const/Builtin: every other required name above is
    // read-only from the program's point of view (a constant) or callable
    // (a builtin), and neither SymbolKind fits something a program is meant
    // to assign to, like `ExitCode := 5;`, before falling off the end of
    // its block.
    //
    // LinkName -- otherwise EP §6.11.2's cross-module rename field, unused
    // outside that -- is repurposed here to record which external symbol
    // this identifier is bound to: "plang_tp_exitcode", whose one real
    // definition lives in the runtime (runtime/plang_sys.cpp -- see that
    // definition's own comment for why the storage has to live there rather
    // than in each compiled object).  CodeGen has no access to this table
    // (it walks the AST, not Sema's Symbols -- ResolvedType/ResolvedBuiltin
    // annotations on the AST nodes are the only bridge between the two), so
    // Codegen::Impl::emitPredefinedGlobals (CodeGenProcs.cpp) does not
    // actually read this field back out; it independently declares (never
    // defines) an LLVM global under the identical literal name.  The two
    // sides staying in agreement is hand-kept, the same way every other
    // plang_err_*/plang_tp_* runtime entry point's name already is between
    // its RtFns.getExternFnN(...) call site and its extern "C" definition in
    // runtime/*.cpp -- there is no single source of truth to fall out of
    // sync FROM.  LinkName is set here anyway because Symbol is the
    // authoritative record of what this identifier actually is, and a later
    // Tier 3 (FileMode, RandSeed, DosError, TextAttr) is expected to follow
    // the same pattern: a Symbol with a LinkName here, and its own literal
    // string match in a codegen-side emitPredefinedGlobals entry.
    if (Opts.turbo()) {
        Symbol ExitCodeSym;
        ExitCodeSym.Kind     = SymbolKind::Var;
        ExitCodeSym.Name     = "ExitCode";
        ExitCodeSym.Ty       = TyInt;
        ExitCodeSym.LinkName = "plang_tp_exitcode";
        ExitCodeSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(ExitCodeSym));
    }

    // -std=turbo only: RandSeed -- the first of the "later Tier 3" names
    // ExitCode's own comment, just above, said would reuse its exact
    // mechanism, and this does: a predefined mutable Var, LinkName bound to
    // a runtime global (runtime/plang_sys.cpp's plang_tp_randseed) that
    // every compiled object only DECLARES (Codegen::Impl::
    // emitPredefinedGlobals), so a multi-file program shares one generator
    // state no matter how many objects it links from.  Unlike ExitCode,
    // whose width follows Integer's own dialect width (TyInt), RandSeed is
    // fixed at 32 bits regardless: real Turbo Pascal's own RandSeed is
    // declared LongInt, not Integer, so Ctx_.getInt(32, /*Signed=*/true) is
    // used directly here rather than TyInt -- both sides (this Symbol and
    // emitPredefinedGlobals's LLVM global) have to agree on 32 bits the same
    // way ExitCode's two sides agree on Integer's own width.
    if (Opts.turbo()) {
        Symbol RandSeedSym;
        RandSeedSym.Kind     = SymbolKind::Var;
        RandSeedSym.Name     = "RandSeed";
        RandSeedSym.Ty       = Ctx_.getInt(32, /*Signed=*/true);
        RandSeedSym.LinkName = "plang_tp_randseed";
        RandSeedSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(RandSeedSym));
    }

    // -std=turbo only: HeapError and ExitProc -- Turbo's own settable
    // procedural-VALUE predefined variables (Tier 2's procedural types/
    // values, VarEntry.h's isProcVar substrate reused here rather than
    // inventing a second function-pointer mechanism), plus ErrorAddr, an
    // ordinary Pointer-typed one alongside them.  Follows ExitCode's own
    // pattern just above (LinkName names the runtime global CodeGen's
    // emitPredefinedGlobals independently declares under the identical
    // literal string -- see ExitCode's own comment for why the two sides
    // stay in agreement by hand rather than a shared source of truth), with
    // one difference: NEITHER HeapError NOR ExitProc is ever CALLED from
    // Pascal source in real Turbo Pascal -- only the RUNTIME calls through
    // either (plang_tp_getmem on an allocation failure; the exit sequence on
    // Halt/RunError/normal termination, runtime/plang_sys.cpp) -- so, unlike
    // an ordinary program-declared `var f: TProc`, only the ASSIGNMENT side
    // ('HeapError := MyHandler') has to type-check here; nothing needs to
    // CALL either one through plang IR, which is the only thing CodeGen's
    // VarEntry::isProcVar/procType exist to support (emitPredefinedGlobals'
    // own comment explains the rest).
    if (Opts.turbo()) {
        // HeapError: function(Size: Int64): Int64.  Deliberately Int64/
        // Int64, not real Borland's Word/Integer -- runtime/plang_sys.cpp's
        // plang_tp_getmem calls through this as a raw C function pointer
        // directly, not through codegen-emitted LLVM IR the way an ordinary
        // procedural-value call is, and only a full-register-width return
        // is guaranteed free of the x86-64 SysV ABI's unspecified-upper-bits
        // hazard a narrower one (Turbo's own 16-bit Integer) would risk; see
        // plang_tp_getmem's own comment for the full reasoning.  Size
        // matches GetMem/FreeMem's own Int64 Size parameter (Builtins.def)
        // for the same reason a mismatched pair would be worse than either
        // alone: a handler that cannot see the value GetMem was actually
        // asked for is strictly less useful than one that can.
        auto Int64Ty = Ctx_.getInt(64, /*Signed=*/true);
        auto HeapErrorTy = std::make_shared<Type>();
        HeapErrorTy->Kind = TypeKind::Function;
        HeapErrorTy->Params.push_back(
            Type::Param{ /*IsVar=*/false, "Size", Int64Ty, /*IsConst=*/false });
        HeapErrorTy->RetType = Int64Ty;
        HeapErrorTy->Name = "function(Size: Int64): Int64";
        Symbol HeapErrorSym;
        HeapErrorSym.Kind     = SymbolKind::Var;
        HeapErrorSym.Name     = "HeapError";
        HeapErrorSym.Ty       = HeapErrorTy;
        HeapErrorSym.LinkName = "plang_tp_heaperror";
        HeapErrorSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(HeapErrorSym));

        // ExitProc: procedure -- no arguments, no result, exactly real
        // Turbo Pascal's own shape; no ABI-width hazard to work around the
        // way HeapError's return above has, since there is no return value
        // at all.
        auto ExitProcTy = std::make_shared<Type>();
        ExitProcTy->Kind = TypeKind::Procedure;
        ExitProcTy->Name = "procedure";
        Symbol ExitProcSym;
        ExitProcSym.Kind     = SymbolKind::Var;
        ExitProcSym.Name     = "ExitProc";
        ExitProcSym.Ty       = ExitProcTy;
        ExitProcSym.LinkName = "plang_tp_exitproc";
        ExitProcSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(ExitProcSym));

        // ErrorAddr: an ordinary untyped Pointer, not a procedural value --
        // see runtime/plang_sys.cpp's plang_tp_erroraddr for the scope this
        // was deliberately simplified to (set only at RunError and at a
        // nonzero-status Halt, not at every individual runtime-fault site).
        Symbol ErrorAddrSym;
        ErrorAddrSym.Kind     = SymbolKind::Var;
        ErrorAddrSym.Name     = "ErrorAddr";
        ErrorAddrSym.Ty       = Ctx_.getGenericPointer();
        ErrorAddrSym.LinkName = "plang_tp_erroraddr";
        ErrorAddrSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(ErrorAddrSym));
    }

    // -std=turbo only: FileMode -- the first Tier 3 predefined identifier to
    // actually follow the "reuse ExitCode's mechanism" plan ExitCode's own
    // comment above set out.  Real Borland/FPC's FileMode: Byte controls the
    // access/sharing mode Reset opens a file with, defaulting to 2
    // (read-write) -- confirmed against `fpc -Mtp` (a fresh program's own
    // FileMode reads 2 before anything touches it).  Nothing in this item
    // reads FileMode back to change how Reset opens anything (that is a
    // later item's job, same as InOutRes); it is registered here purely as
    // a working, real, assignable predefined variable with the correct
    // default, exactly like ExitCode.  Kept as TyInt (16-bit Integer) rather
    // than a distinct Byte type, matching ExitCode's own storage width --
    // plang_tp_filemode (runtime/plang_sys.cpp) is declared int16_t for the
    // identical "both sides must agree on the byte width" reason ExitCode's
    // own comment gives, and the real magnitude (2) fits either width.
    if (Opts.turbo()) {
        Symbol FileModeSym;
        FileModeSym.Kind     = SymbolKind::Var;
        FileModeSym.Name     = "FileMode";
        FileModeSym.Ty       = TyInt;
        FileModeSym.LinkName = "plang_tp_filemode";
        FileModeSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(FileModeSym));
    }

    // -std=turbo only: InOutRes -- the hidden global IOResult (registered as
    // an ordinary Func just below, not here) reads and clears; FileMode's
    // own comment just above already named this as the mechanism's next
    // user.  Real Borland/FPC's InOutRes is a Word (confirmed against the
    // local `fpc -Mtp` install's own rtl/inc sources); this is Ctx_.getInt
    // (64, true), NOT TyInt, for the identical reason RandSeed's own Symbol
    // just above is Ctx_.getInt(32, true) rather than TyInt -- the runtime
    // storage this LinkName binds to (plang_tp_inoutres, runtime/
    // plang_sys.cpp) is a fixed int64_t regardless of Integer's own dialect
    // width, a DELIBERATE divergence from Borland's 16-bit Word documented
    // at length on that global's own definition, and both sides of a
    // LinkName binding must always agree on the exact width (RandSeed's own
    // comment).  A program may read and assign InOutRes directly (real
    // Turbo Pascal allows this, even though IOResult is the documented,
    // idiomatic way to consume it) -- registering it as an ordinary
    // predefined Var, exactly like ExitCode/RandSeed/FileMode, is what makes
    // that work "for free" the same way it already does for those three.
    if (Opts.turbo()) {
        Symbol InOutResSym;
        InOutResSym.Kind     = SymbolKind::Var;
        InOutResSym.Name     = "InOutRes";
        InOutResSym.Ty       = Ctx_.getInt(64, /*Signed=*/true);
        InOutResSym.LinkName = "plang_tp_inoutres";
        InOutResSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(InOutResSym));
    }

    // -std=turbo only: Input/Output as predefined text-file VARIABLES,
    // registered here -- BEFORE check() below ever reaches the
    // Prog.FileParams loop that gives ISO 7185/Extended Pascal's own
    // `program p(input, output);` heading its implicit Vars -- rather than
    // only when a program's heading actually lists them.  Real Turbo Pascal
    // needs neither an ISO-style program heading nor any declaration at all
    // to use Input/Output; they are simply always there.  Registering them
    // unconditionally here, rather than teaching this function to consult
    // Prog.FileParams and register them only when the heading does NOT
    // already list them, is the cleaner of the two shapes the collision
    // this creates could be resolved by: registerBuiltins() has never taken
    // the ProgramNode (only check(), below, sees it), and threading it
    // through would make every OTHER registerBuiltins() call site --
    // there is more than one test and tool entry point that calls this to
    // populate a fresh Symtab without a full program in hand -- either
    // fabricate a ProgramNode or grow a second overload.  Instead, the
    // Prog.FileParams loop in check() itself is the one taught to
    // recognize that 'input'/'output' already exist under Turbo and skip
    // redefining them rather than treating the ISO heading syntax as a
    // duplicate-parameter error -- see that loop's own comment for the
    // other half of this.
    //
    // Ty is Ctx_.getText(), the exact type the Prog.FileParams loop below
    // gives 'input'/'output' under ISO/EP -- so code that reads or writes
    // through either name type-checks identically under every dialect.
    // LinkName follows ExitCode's own mechanism (this function's own
    // comment on that Symbol) with one difference: Input/Output are
    // WHOLE-STRUCT globals (PascalFile, not a scalar), so
    // Codegen::Impl::emitPredefinedGlobals declares each under
    // fileStructType() rather than a scalar LLVM type -- see that
    // function's own comment for the rest.
    if (Opts.turbo()) {
        Symbol InputSym;
        InputSym.Kind     = SymbolKind::Var;
        InputSym.Name     = "Input";
        InputSym.Ty       = Ctx_.getText();
        InputSym.LinkName = "plang_input";
        InputSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(InputSym));

        Symbol OutputSym;
        OutputSym.Kind     = SymbolKind::Var;
        OutputSym.Name     = "Output";
        OutputSym.Ty       = Ctx_.getText();
        OutputSym.LinkName = "plang_output";
        OutputSym.IsRequiredIdentifier = true;
        (void)Symtab.define(std::move(OutputSym));
    }

    // -std=turbo only: the sized-integer ladder, AnsiChar, and the untyped
    // Pointer type -- the type names the rest of Tier 2 is written against.
    //
    // TypeContext::getInt interns by {Bits, Signed} alone, so SmallInt (16,
    // signed) and LongWord (32, unsigned) below are literally the same Type
    // object as Integer and Cardinal respectively -- see getInt's own
    // comment for why that is fine and how a diagnostic still names them
    // sensibly rather than always saying "integer".
    //
    // AnsiChar is Turbo's 8-bit character type, which is exactly plang's one
    // and only Char: ISO 7185/EP have never had a second, wider character
    // type for it to need distinguishing from, so it is registered as an
    // alias of TyChar rather than a new Type, the same way SmallInt/LongWord
    // above alias an existing integer Type rather than mint their own.
    if (Opts.turbo()) {
        auto declareTypeAlias = [&](const char* Name, std::shared_ptr<Type> Ty) {
            Symbol S;
            S.Kind = SymbolKind::TypeAlias;
            S.Name = Name;
            S.Ty   = std::move(Ty);
            S.IsRequiredIdentifier = true;
            (void)Symtab.define(std::move(S));
        };
        declareTypeAlias("ShortInt", Ctx_.getInt(8,  /*Signed=*/true));
        declareTypeAlias("Byte",     Ctx_.getInt(8,  /*Signed=*/false));
        declareTypeAlias("SmallInt", Ctx_.getInt(16, /*Signed=*/true));
        declareTypeAlias("Word",     Ctx_.getInt(16, /*Signed=*/false));
        declareTypeAlias("LongInt",  Ctx_.getInt(32, /*Signed=*/true));
        declareTypeAlias("Cardinal", Ctx_.getInt(32, /*Signed=*/false));
        declareTypeAlias("LongWord", Ctx_.getInt(32, /*Signed=*/false));
        declareTypeAlias("Int64",    Ctx_.getInt(64, /*Signed=*/true));
        declareTypeAlias("QWord",    Ctx_.getInt(64, /*Signed=*/false));
        declareTypeAlias("AnsiChar", TyChar);
        declareTypeAlias("Pointer",  Ctx_.getGenericPointer());

        // -std=turbo only: the loose Boolean-family widths and Single, the
        // second floating type.  ByteBool/WordBool/LongBool reuse
        // TypeKind::Boolean with IsLooseBool set (see that field's own
        // comment for the strict-vs-loose distinction and the empirical
        // fpc trail behind it) the same way the sized-integer ladder just
        // above reuses TypeKind::Integer; Single reuses TypeKind::Real with
        // Width=32.  Neither trips a NumSemaTypeKinds sentinel, by design.
        declareTypeAlias("ByteBool", Ctx_.getLooseBoolean(8));
        declareTypeAlias("WordBool", Ctx_.getLooseBoolean(16));
        declareTypeAlias("LongBool", Ctx_.getLooseBoolean(32));
        declareTypeAlias("Single",   Ctx_.getSingle());
    }

    // EP §6.4.2.2: predefined constants
    if (Opts.extendedPascal()) {
        auto makeConst = [](const char* Name, std::shared_ptr<Type> Ty,
                            std::optional<int64_t> Ord = std::nullopt) {
            Symbol S;
            S.Kind = SymbolKind::Const;
            S.Name = Name;
            S.Ty   = std::move(Ty);
            // An ordinal constant carries its value so that a bound written with
            // it folds; the real ones have no ordinal value to carry.
            if (Ord) { S.ConstOrdinal = *Ord; S.HasConstOrdinal = true; }
            S.IsRequiredIdentifier = true;
            return S;
        };
        (void)Symtab.define(makeConst("maxchar", TyChar, 255));
        (void)Symtab.define(makeConst("minreal", TyReal));
        (void)Symtab.define(makeConst("maxreal", TyReal));
        (void)Symtab.define(makeConst("epsreal", TyReal));

        // EP §6.4.3.4: predefined TimeStamp record type.  Note 4 there spells
        // the field types out as a Pascal declaration: DateValid/TimeValid
        // Boolean, year plain integer, and month/day/hour/minute/second each
        // a subrange (1..12, 1..31, 0..23, 0..59, 0..59) -- five of the eight
        // fields, not all six numeric ones.  Declaring them plain integer,
        // as the other five were, left the default range check that every
        // other subrange field gets nowhere to attach, so `t.month := 13`
        // compiled and ran silently.
        {
            auto TyTS = std::make_shared<Type>();
            TyTS->Kind = TypeKind::Record;
            TyTS->Name = "TimeStamp";
            TyTS->RecordFields = {
                { "DateValid", TyBool },
                { "year",      TyInt  },
                { "month",     Ctx_.getSubrange(TyInt, 1, 12) },
                { "day",       Ctx_.getSubrange(TyInt, 1, 31) },
                { "TimeValid", TyBool },
                { "hour",      Ctx_.getSubrange(TyInt, 0, 23) },
                { "minute",    Ctx_.getSubrange(TyInt, 0, 59) },
                { "second",    Ctx_.getSubrange(TyInt, 0, 59) },
            };
#define PLANG_FIELD_NAME_ONLY(Member, LLVMTy) #Member,
            checkRequiredRecordFields(TyTS->RecordFields,
                {PLANG_TIMESTAMP_FIELDS(PLANG_FIELD_NAME_ONLY)}, "TimeStamp");
#undef PLANG_FIELD_NAME_ONLY
            Symbol TSym;
            TSym.Kind = SymbolKind::TypeAlias;
            TSym.Name = "TimeStamp";
            TSym.Ty   = TyTS;
            TSym.IsRequiredIdentifier = true;
            (void)Symtab.define(std::move(TSym));
        }
        // The BindingType record itself is built at the top, because the
        // builtin loop declares `binding` with it as a result type.  What is
        // left here is making the name visible as a type.
        {
            Symbol BTSym;
            BTSym.Kind = SymbolKind::TypeAlias;
            BTSym.Name = "BindingType";
            BTSym.Ty   = TyBindingType;
            BTSym.IsRequiredIdentifier = true;
            (void)Symtab.define(std::move(BTSym));
        }
    }
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

bool Sema::check(const ProgramNode& Prog) {
    Symtab.pushScope(); // global scope
    registerBuiltins();

    // Register program file-parameters as text file variables (ISO §6.10).
    // This lets 'input' and 'output' -- the two the language understands
    // without any declaration at all -- and every other name in the list
    // resolve to something for the rest of this function, whether or not the
    // program block goes on to give it a real declaration of its own; see the
    // BeforePop hook below for the check that it did.  A name repeated in the
    // list collides right here, in this same scope, so report the failure
    // Symtab.define already detects instead of discarding it as before.
    // -std=turbo only: 'input'/'output' were already registered above, by
    // registerBuiltins() itself, as predefined text-file Vars -- see that
    // registration's own comment for why unconditionally, rather than only
    // when this program's own heading lists them.  Real Turbo Pascal does
    // not forbid the ISO heading syntax, it simply does not require it, so
    // a Turbo program that still writes `program p(input, output);` is
    // naming something that is already there, not colliding with
    // anything: Symtab.define would fail on either name here (it is
    // already taken in this same scope) and, unless skipped, that failure
    // would be reported as though the heading itself repeated a name --
    // exactly the spurious err_duplicate_param a program that did nothing
    // wrong must not get.  A literal repeat within the heading itself
    // (`program p(input, input);`) is still caught, via TurboSkippedNames
    // below, so this loosening does not also swallow an actual mistake.
    std::set<std::string> TurboSkippedNames;
    for (const auto& Name : Prog.FileParams) {
        const std::string Lower = toLower(Name);
        if (Opts.turbo() && (Lower == "input" || Lower == "output")) {
            if (!TurboSkippedNames.insert(Lower).second)
                error(Prog.Loc, diag::err_duplicate_param, {Name});
            continue;
        }
        Symbol S;
        S.Kind = SymbolKind::Var;
        S.Name = Name;
        S.Ty   = Ctx_.getText();
        if (!Symtab.define(std::move(S)))
            error(Prog.Loc, diag::err_duplicate_param, {Name});
    }

    // EP §6.11.2: an interface settles what its implementation module exports,
    // and the two may be written in either order, so the export lists are all
    // collected before anything is harvested against them.
    for (auto* Mod : Prog.Modules)
        if (Mod->IsInterface && !Mod->Exports.empty()) {
            auto& List = ExportLists_[toLower(Mod->Name)];
            List.insert(List.end(), Mod->Exports.begin(), Mod->Exports.end());
        }

    // EP §6.11.1: an interface part promises a body somewhere.  Both parts
    // are ordinarily written in the same file (module-declaration lets them
    // come in either order, but not one alone) -- unlike an *implementation*
    // with no interface part in this file, which is the legitimate
    // "module-identification" form a module compiled apart from its own
    // interface, or a self-contained module with an implicit interface,
    // legitimately uses.  Left unchecked, an interface with no matching
    // implementation anywhere in this file compiled clean with no
    // diagnostic and no useful output (no .pmi under -c, since nothing was
    // ever `processModuleBody`'d to emit one; a confusing undefined
    // `__plang_init_*` at link time otherwise) -- catching the real mistake
    // only far downstream of where it was actually made.
    for (const auto* Mod : Prog.Modules)
        if (Mod->IsInterface) {
            const bool HasImpl = std::any_of(
                Prog.Modules.begin(), Prog.Modules.end(),
                [&](const ModuleNode* M) {
                    return !M->IsInterface && eqCI(M->Name, Mod->Name);
                });
            if (!HasImpl)
                error(Mod->Loc, diag::err_module_interface_without_implementation,
                      {Mod->Name});
        }

    // EP §6.11: process module definitions before the program block.
    // Module interfaces register export stubs; bodies register full symbols.
    for (auto* Mod : Prog.Modules) {
        if (Mod->IsInterface)
            processModuleInterface(*Mod);
        else
            processModuleBody(*Mod);
    }

    // EP §6.11.3: process program-level import clauses.
    if (!Prog.Imports.empty())
        processImports(Prog.Imports);

    // Turbo Tier 4, Cluster A item 1: a program's own top-level 'uses'
    // clause.  Pushed here, in the same outer (global) scope processImports
    // above populates, so the program's own block -- checked by checkBlock
    // just below -- sees the units' exports as ONE level out from its own
    // declarations, exactly the way it already sees the required
    // identifiers registerBuiltins() defined into this same scope.  Popped
    // after checkBlock returns, before this function's own final
    // Symtab.popScope() -- see pushUnitUsesScopes's own comment for why the
    // scope-stack ORDER alone is what gives later-'uses'd units precedence.
    const size_t UnitScopesPushed =
        Opts.turbo() ? pushUnitUsesScopes(Prog.Uses) : 0;

    // ISO §6.10: every program-parameter but 'input' and 'output' must be
    // given a defining declaration in the program block itself -- the heading
    // only says which of the block's own declarations are external files (or,
    // under Extended Pascal, other bindable entities); it is not itself one.
    // Checked from checkBlock's BeforePop hook, while the block's own scope
    // -- where such a declaration would live -- is still the current one; by
    // the time checkBlock returns below, that scope has already been popped.
    // lookupCurrent (this scope only) is deliberate: the implicit text-file
    // Var the loop above defined for every name lives in the OUTER scope
    // pushed at the top of this function, so it never stands in for a
    // declaration the block itself never wrote -- it exists only so the rest
    // of the program can still refer to the name, not to satisfy this check.
    std::set<std::string> CheckedProgramParams;
    checkBlock(*Prog.Block, /*BeforePop=*/[&] {
        for (const auto& Name : Prog.FileParams) {
            const std::string Lower = toLower(Name);
            if (Lower == "input" || Lower == "output") continue;
            if (!CheckedProgramParams.insert(Lower).second)
                continue; // one diagnostic per distinct name, not per repeat
            if (!Symtab.lookupCurrent(Name))
                error(Prog.Loc, diag::err_program_param_not_declared, {Name});
        }
    }, /*IsGlobalScope=*/true);
    popUnitUsesScopes(UnitScopesPushed);
    Symtab.popScope();
    return !hasErrors();
}

// ---------------------------------------------------------------------------
// EP §6.11: Module processing
// ---------------------------------------------------------------------------

void Sema::processModuleInterface(const ModuleNode& Mod) {
    const std::string Key = toLower(Mod.Name);

    // check() collects the export lists of every interface in the file up
    // front; one loaded from a .pmi arrives here on its own and has to record
    // its own before its declarations are harvested against it.
    if (!Mod.Exports.empty() && !ExportLists_.count(Key)) {
        auto& List = ExportLists_[Key];
        List.insert(List.end(), Mod.Exports.begin(), Mod.Exports.end());
    }

    // Remembered so that the implementation module, checked separately below
    // (and possibly with none of its own import-part, EP §6.11.1's usual
    // abbreviation), can still resolve a name this interface's own headings
    // were written against.
    if (!Mod.Imports.empty() && !ModuleInterfaceImports_.count(Key))
        ModuleInterfaceImports_[Key] = Mod.Imports;

    // The headings in the interface are the signatures an importer needs when
    // the implementation is compiled separately, so they are resolved here in
    // exactly the way the implementation's own declarations would be.
    if (Mod.Body)
        processModuleBody(Mod);
}

/// The name an importer sees for \p Declared, and whether it is exported at
/// all, according to the export-list \p List.
static const ExportItem* findExport(const std::vector<ExportItem>& List,
                                    const std::string& Declared) {
    for (const auto& Item : List)
        if (eqCI(Item.Name, Declared)) return &Item;
    return nullptr;
}

void Sema::harvestModuleExports(const ModuleNode& Mod) {
    const std::string Key = toLower(Mod.Name);
    auto& Exports = ModuleExports_[Key];

    const auto ListIt  = ExportLists_.find(Key);
    const bool Selects = ListIt != ExportLists_.end();

    // EP §6.11.2: an export-range names the first and last of a run of
    // constants, which is a stretch of one enumerated type.  Collecting the
    // ranges first means each symbol can be tested against them in one pass.
    struct Range { const Type* Ty; int Lo; int Hi; bool Protected; };
    std::vector<Range> Ranges;
    if (Selects) {
        for (const auto& Item : ListIt->second) {
            if (Item.RangeEnd.empty()) continue;
            const Symbol* First = Symtab.lookup(Item.Name);
            const Symbol* Last  = Symtab.lookup(Item.RangeEnd);
            if (!First || !Last || First->Kind != SymbolKind::EnumValue
                                || Last->Kind  != SymbolKind::EnumValue
                                || First->declaredType().get() != Last->declaredType().get()) {
                error(Item.Loc, diag::err_export_range_not_constants,
                      {Item.Name, Item.RangeEnd});
                continue;
            }
            Ranges.push_back({First->declaredType().get(), First->OrdinalValue,
                              Last->OrdinalValue, Item.Protected});
        }
    }


    // EP §6.11.1: the module-block is given every declaration of the heading,
    // not only the exported ones — the widget example turns on exactly that,
    // its restricted type being implemented in terms of a type it keeps in.
    auto& IfaceDecls = ModuleInterfaceDecls_[Key];

    Symtab.forEachInCurrentScope([&](Symbol& Sym) {
        // A label names a place in this module's own code; an importer has
        // nothing it may do with one.
        if (Sym.Kind == SymbolKind::Label) return;

        // The symbol is declared here, so this module is what its mangled name
        // is built from, whatever an importer chooses to call it.
        Sym.Module = Key;
        if (Mod.IsInterface) IfaceDecls.push_back(Sym);

        Symbol Out = Sym;
        // An interface heading is a forward declaration only within the module
        // that will define it; to an importer it is simply a callable.
        Out.IsForward = false;
        if (Selects) {
            const ExportItem* Item = findExport(ListIt->second, Sym.Name);
            if (!Item) {
                // An enumeration constant may be reached by a range instead,
                // and one covered by no clause at all stays inside.
                bool InRange = false;
                for (const auto& R : Ranges)
                    if (Sym.Kind == SymbolKind::EnumValue && Sym.Ty.get() == R.Ty
                        && Sym.OrdinalValue >= R.Lo && Sym.OrdinalValue <= R.Hi) {
                        InRange = true;
                        Out.IsProtected = Out.IsProtected || R.Protected;
                        break;
                    }
                if (!InRange) return;
            } else if (!Item->RangeEnd.empty()) {
                // The first of a run is written as the range's own name, so it
                // is found here rather than by the range test above — and was
                // dropped for it: `red..green` exported everything but red.
                Out.IsProtected = Out.IsProtected || Item->Protected;
            } else {
                if (!Item->Alias.empty()) {
                    Out.LinkName = Sym.Name;
                    Out.Name     = Item->Alias;
                }
                // EP §6.11.2: 'protected' on export leaves the value readable
                // and the variable unassignable wherever it is imported.
                Out.IsProtected = Out.IsProtected || Item->Protected;
            }
        }

        const std::string LoName = toLower(Out.Name);
        for (auto& E : Exports)
            if (toLower(E.Name) == LoName) { E = Out; return; }
        Exports.push_back(std::move(Out));
    });
}

// ---------------------------------------------------------------------------
// Issue #180/#304: mtime-based staleness check for a published interface file
// ---------------------------------------------------------------------------

namespace {
// A case-insensitive search for "<Key>.pas" in exactly ONE directory (the
// directory an interface file was itself found in) -- deliberately narrower
// than loadUnitInterfaceExports's own SearchDirs-wide fallback, since the
// question here is only "is there a companion source sitting right beside
// THIS published interface", not "can a .pas for this name be found
// somewhere on the search path".  A .pmi's companion is not guaranteed to
// exist under this name at all (EP allows several modules in one hand-named
// file, e.g. multi.pas publishing a.pmi/bad.pmi/good.pmi -- see
// writePMIFiles' own comment), so coming up empty here is the ordinary,
// silent, no-warning case, not a search failure to report.
std::string findCompanionPas(const std::string& Dir, const std::string& Key) {
    const std::string Fast = Dir + "/" + Key + ".pas";
    if (llvm::sys::fs::exists(Fast)) return Fast;
    std::error_code EC;
    for (llvm::sys::fs::directory_iterator It(Dir, EC), End;
            It != End && !EC; It.increment(EC)) {
        llvm::SmallString<64> Stem = llvm::sys::path::filename(It->path());
        if (eqCI(std::string(Stem), Key + ".pas")) return std::string(It->path());
    }
    return "";
}
} // namespace

void Sema::warnIfInterfaceStale(SourceLocation Loc, const std::string& InterfacePath,
                                 const std::string& PasPath) {
    llvm::sys::fs::file_status PasStatus;
    llvm::sys::fs::file_status IfaceStatus;
    if (llvm::sys::fs::status(PasPath, PasStatus)) return;
    if (llvm::sys::fs::status(InterfacePath, IfaceStatus)) return;

    const auto PasTime = PasStatus.getLastModificationTime();
    bool Stale = PasTime > IfaceStatus.getLastModificationTime();

    // Also check the paired .o -- same directory, same base name as the
    // interface file itself (that is how both writePMIFiles and
    // writeTUIFile's own callers in Frontend.cpp name it, and how every
    // lit test in test/Module/SeparateCompilation and test/Turbo/Units
    // invokes `-c ... -o <name>.o` already).  A program that only reads the
    // .tui/.pmi's declarations never touches the .o, but a program being
    // LINKED against the compiled unit/module does, and that .o is exactly
    // what silently carries stale code in this item's own repro: the
    // interface text can be byte-identical (so re-publishing it would prove
    // nothing) while the implementation behind it has changed.
    if (!Stale) {
        llvm::SmallString<256> ObjPath(InterfacePath);
        llvm::sys::path::replace_extension(ObjPath, "o");
        llvm::sys::fs::file_status ObjStatus;
        if (llvm::sys::fs::exists(ObjPath) &&
                !llvm::sys::fs::status(ObjPath, ObjStatus))
            Stale = PasTime > ObjStatus.getLastModificationTime();
    }

    if (Stale)
        warning(Loc, diag::warn_stale_interface_file, {InterfacePath, PasPath});
}

void Sema::processModuleBody(const ModuleNode& Mod) {
    if (!Mod.Body) return;

    // The module's own imports go in a scope outside the block's, for two
    // reasons: the body must see them, and harvestModuleExports must not —
    // it reads the block scope alone, so what a module imports is not
    // silently re-exported along with what it declares.
    Symtab.pushScope();
    const std::string SavedUnit = CurrentUnit_;
    CurrentUnit_ = toLower(Mod.Name);

    // EP §6.11.1: the implementation of a module is given the declarations of
    // its interface, so it need not repeat the types and headings it was
    // written against.  They go in the same scope the imports do, outside the
    // block's, so a declaration the implementation makes for itself wins and
    // harvestModuleExports does not read them back out.
    const bool SavedInImpl = InModuleImplementation_;
    if (!Mod.IsInterface)
        if (auto Iface = ModuleInterfaceDecls_.find(CurrentUnit_);
                Iface != ModuleInterfaceDecls_.end()) {
            for (const auto& Sym : Iface->second) (void)Symtab.define(Sym);
            InModuleImplementation_ = true;
            // EP §6.11.1: a heading the interface declared (most notably one
            // repeated here only as its bare name, "function DoubleIt;") is
            // re-checked against the block that gives it a body, and that
            // re-check resolves the heading's parameter and result types all
            // over again.  Those types may name something the interface
            // reached only through an import of its own, one this
            // implementation is not written to repeat — so the interface's
            // import-part goes into scope here too, alongside its
            // declarations, rather than being silently unavailable.
            if (auto IfaceImports = ModuleInterfaceImports_.find(CurrentUnit_);
                    IfaceImports != ModuleInterfaceImports_.end())
                processImports(IfaceImports->second);
        }

    if (!Mod.Imports.empty()) processImports(Mod.Imports);

    // A module body is checked exactly as a program block is.  It used to be
    // scanned only for the signatures an importer needs, which left every
    // statement inside it unanalysed: a type error in a module reached codegen
    // as an internal error, or compiled to the wrong thing in silence.
    checkBlock(*Mod.Body, [&] {
        // EP §6.11.1: 'to begin do' and 'to end do' are part of the module and
        // read the variables it declares, so they belong to this scope.
        if (Mod.InitStmt)  checkStmt(Mod.InitStmt.get());
        if (Mod.FinalStmt) checkStmt(Mod.FinalStmt.get());
        harvestModuleExports(Mod);
    }, /*IsGlobalScope=*/true, /*IsModuleBlock=*/true,
       /*IsInterfaceBlock=*/Mod.IsInterface);

    InModuleImplementation_ = SavedInImpl;
    CurrentUnit_ = SavedUnit;
    Symtab.popScope();
}

void Sema::processImports(const std::vector<ImportClause>& Imports) {
    // EP §6.11.3: two imports may not bring distinct objects into one scope
    // under the same identifier.  Symtab::define simply keeps the first, which
    // makes the winner a matter of the order the import clauses were written.
    std::map<std::string, std::string> ImportedFrom; // name → module as written

    for (const auto& Clause : Imports) {
        std::string Key = toLower(Clause.ModuleName);

        // Built-in pseudo-modules: always allowed, no symbols to add.
        // registerBuiltins() has already put theirs in scope.
        if (isBuiltinModule(Key)) continue;

        auto It = ModuleExports_.find(Key);
        if (It == ModuleExports_.end()) {
            // Try every candidate .pmi path in turn.  A candidate that
            // EXISTS but is broken (malformed, or names a different module)
            // must not shadow a working one elsewhere on the search path --
            // only a genuine success stops the search; a broken candidate is
            // remembered and the search keeps going, exactly like "does not
            // exist" does already.
            PMILoadResult LastFailure{PMILoadResult::Status::Ok, ""};
            std::string   LastFailurePath;
            auto tryCandidate = [&](const std::string& PMIPath) {
                if (!llvm::sys::fs::exists(PMIPath)) return false;
                PMILoadResult R = loadPMI(Key, PMIPath);
                if (R.St == PMILoadResult::Status::Ok) {
                    It = ModuleExports_.find(Key);
                    // Issue #180/#304: this .pmi is a real, successfully-
                    // loaded interface -- see if a companion "<Key>.pas"
                    // sits right beside it and, if so, whether it is newer
                    // than the .pmi or its paired .o (findCompanionPas's
                    // own comment on why coming up empty here is the
                    // ordinary case, not a search failure).
                    const std::string PmiDir =
                        std::string(llvm::sys::path::parent_path(PMIPath));
                    const std::string PasCandidate =
                        findCompanionPas(PmiDir.empty() ? "." : PmiDir, Key);
                    if (!PasCandidate.empty())
                        warnIfInterfaceStale(Clause.Loc, PMIPath, PasCandidate);
                    return true;
                }
                if (R.St != PMILoadResult::Status::Unreadable) {
                    LastFailure     = R;
                    LastFailurePath = PMIPath;
                }
                return false;
            };

            // Search in ModuleSearchPaths first, then the current directory.
            // The filename uses Key (already lowercased), matching how
            // writePMIFiles names the file it publishes -- Pascal module
            // names are case-insensitive, so the file has to be found
            // whichever case the import clause spells it in, even on a
            // case-sensitive filesystem. The module's true identity still
            // travels inside the file and is checked below (WrongModule).
            bool Found = false;
            for (const auto& Dir : Opts.ModuleSearchPaths)
                if (tryCandidate(Dir + "/" + Key + ".pmi")) {
                    Found = true;
                    break;
                }
            if (!Found) Found = tryCandidate("./" + Key + ".pmi");

            if (!Found) {
                switch (LastFailure.St) {
                case PMILoadResult::Status::ParseFailed:
                    error(Clause.Loc, diag::err_malformed_module_interface,
                          {Clause.ModuleName, LastFailurePath, LastFailure.Detail});
                    break;
                case PMILoadResult::Status::WrongModule:
                    error(Clause.Loc, diag::err_module_interface_name_mismatch,
                          {LastFailurePath, Clause.ModuleName, LastFailure.Detail});
                    break;
                default:
                    // Nothing declares this module and no interface for it was
                    // found, so every name the clause was to bring in is missing.
                    // Saying so once here beats an undeclared-identifier error at
                    // each use, none of which mentions the module.
                    error(Clause.Loc, diag::err_unknown_module,
                          {Clause.ModuleName});
                }
                continue;
            }
        }

        // EP §6.11.3: every name in the import-list must be one the interface
        // exports.  A misspelling would otherwise import nothing and be
        // reported only where the name was expected to be usable.
        for (const auto& Wanted : Clause.Names) {
            bool Found = false;
            for (const auto& Sym : It->second)
                if (eqCI(Sym.Name, Wanted)) { Found = true; break; }
            if (!Found)
                error(Clause.Loc, diag::err_import_name_not_exported,
                      {Wanted, Clause.ModuleName});
        }

        for (const auto& Sym : It->second) {
            // 'only' makes the list the whole of what is imported; without it
            // the list only says which names are being renamed.
            if (Clause.Selective) {
                bool Wanted = false;
                for (const auto& OnlyName : Clause.Names)
                    if (eqCI(OnlyName, Sym.Name)) { Wanted = true; break; }
                if (!Wanted) continue;
            }

            Symbol DefSym = Sym;

            // EP §6.11.3: renaming on import.  The declaring module still
            // knows it by the name it exported, which is what its mangled
            // name is built from, so that is kept as the link name.
            for (const auto& [From, To] : Clause.Renames)
                if (eqCI(From, Sym.Name)) {
                    if (DefSym.LinkName.empty()) DefSym.LinkName = Sym.Name;
                    DefSym.Name = To;
                    break;
                }

            // EP §6.11.2: a qualified import is reachable only as M.name, which
            // the parser has already folded into a single identifier.
            if (Clause.Qualified)
                DefSym.Name = Clause.ModuleName + "." + DefSym.Name;

            // Re-importing the same name from the same module is how a diamond
            // of imports reaches one declaration twice, and is not a clash.
            auto [Prev, Fresh] =
                ImportedFrom.try_emplace(toLower(DefSym.Name), Clause.ModuleName);
            if (!Fresh && toLower(Prev->second) != Key) {
                error(Clause.Loc, diag::err_import_name_clash,
                      {DefSym.Name, Prev->second, Clause.ModuleName});
                continue;
            }
            // The name this unit knows it by, pointing at the module that
            // declares it — which is not the module it was imported from when
            // that module got it from somewhere else.
            if (!DefSym.Module.empty())
                ImportOwners_[CurrentUnit_].emplace(
                    toLower(DefSym.Name),
                    ImportedName{DefSym.Module,
                                 DefSym.Kind == SymbolKind::Proc,
                                 DefSym.LinkName});
            // Define in current scope; ignore duplicates (module may overlap builtins).
            (void)Symtab.define(DefSym);
        }
    }
}

// ---------------------------------------------------------------------------
// Turbo Tier 4, Cluster A item 1: unit uses / scoping / shadowing
// ---------------------------------------------------------------------------
//
// The whole mechanism is: push one SymbolTable scope per 'uses'd unit, in
// the order they were written, UNDER an always-present implicit `System`
// scope, and let SymbolTable::lookup's existing innermost-first search do
// the rest.  Two units that both export a name never share a scope to clash
// in (SymbolTable::define's clash check is per-scope, and each unit gets its
// own), so the later-pushed (later-named) one's export is simply what lookup
// finds first -- last-uses-wins shadowing, entirely for free, with zero
// change to define's clash policy anywhere.  This is deliberately NOT how
// EP's own processImports works (that merges every import into ONE shared
// scope and hard-errors on a same-name clash, err_import_name_clash) --
// EP's own behavior is correct for EP and untouched by any of this.
//
// Qualification (UnitName.Identifier) reuses the very same per-scope
// SymbolTable entries: each export is defined TWICE in its unit's own
// scope, once under its bare name and once under "unitname.name" (the
// parser has already folded 'A.X' into one dotted IdentExpr wherever 'A' is
// a name QualifiedModules_ knows about -- see ParseUnit.cpp's own comment on
// why every 'uses'd unit goes in there, Turbo having no 'qualified'
// keyword).  A shadowed export is still reachable through its OWN unit's
// dotted name even though the bare name now resolves to whatever shadowed
// it, because the dotted entry lives in the shadowed unit's own scope,
// undisturbed by anything pushed on top of it.

size_t Sema::pushUnitUsesScopes(const std::vector<UsedUnit>& Uses) {
    // The implicit `System`: every Turbo program and unit gets one, always
    // pushed FIRST (so it ends up the least-recently-pushed, most-easily-
    // shadowed of the group) -- confirmed against real `fpc -Mtp` that a
    // real unit's own export can indeed shadow a System-provided identifier
    // of the same name (see this item's own report).  System exports
    // nothing of its own here: Tier 1-3 already registered Random, the
    // file-model builtins, and everything else Turbo needs as ordinary
    // dialect-gated Symbol::Builtin entries in registerBuiltins()'s global
    // scope (Sema::check's very first scope, opened before this ever runs),
    // wholly independent of any unit system.  This scope's only job is to be
    // the fixed floor of the shadow stack that is always there, so a REAL
    // unit built later (item 2/3's own scope, not this one's) can define an
    // actual identifier here without disturbing this function's contract.
    Symtab.pushScope(/*IsBlock=*/false);
    size_t Count = 1;

    for (const auto& U : Uses) {
        // Issue #699: 'System' need not be named at all (it is always
        // implicitly there, just above), but real TP7/fpc also accept it
        // being named explicitly -- confirmed against a local `fpc -Mtp`
        // build.  There is no "system.tui"/"system.pas" on disk for
        // loadUnitInterfaceExports to find (its exports already live in
        // registerBuiltins()'s own global scope, not in any unit file), so
        // this has to be recognized before that lookup runs, the same way
        // processImports's own isBuiltinModule check short-circuits EP's
        // built-in pseudo-modules.
        if (eqCI(U.Name, "System")) continue;
        Symtab.pushScope(/*IsBlock=*/false);
        ++Count;
        const std::string Lower = toLower(U.Name);
        for (const Symbol& Sym : loadUnitInterfaceExports(U.Name, U.Loc)) {
            // Fresh scope, one unit's exports only: two exports of the same
            // unit cannot collide here (they didn't collide when the unit's
            // own interface was checked, either), and nothing else has been
            // defined into this scope yet, so `define` can never decline
            // either of these two calls.
            (void)Symtab.define(Sym);
            Symbol Qualified   = Sym;
            Qualified.Name     = U.Name + "." + Sym.Name;
            (void)Symtab.define(std::move(Qualified));

            // Turbo Tier 4, Cluster A item 2: record what CodeGen needs to
            // mangle a reference to this export correctly once it is
            // genuinely a separate object file (CGLinkage::importOwner/
            // importLinkName, ModuleImports.h's own comment on why codegen
            // cannot work this out from the AST alone) -- exactly the same
            // table EP's own processImports already fills for `import`,
            // reused here unmodified, keyed by CurrentUnit_ (the unit or
            // program whose 'uses' this is; "" for a program, same
            // convention EP's own program-level import already uses).  Only
            // a Var/VarParam/Proc actually needs this (a Const is inlined by
            // registerUsedUnitConsts and never referenced by mangled symbol
            // at all; a TypeAlias has no runtime symbol; skipping those two
            // avoids polluting the table with entries nothing ever reads).
            if ((Sym.Kind == SymbolKind::Var || Sym.Kind == SymbolKind::Proc)
                    && !Sym.Module.empty())
                ImportOwners_[CurrentUnit_].emplace(
                    toLower(Sym.Name),
                    ImportedName{Sym.Module, Sym.Kind == SymbolKind::Proc,
                                 Sym.LinkName});
        }
    }
    return Count;
}

void Sema::popUnitUsesScopes(size_t Count) {
    for (size_t I = 0; I < Count; ++I) Symtab.popScope();
}

const std::vector<Symbol>&
Sema::loadUnitInterfaceExports(const std::string& UnitName, SourceLocation Loc) {
    const std::string Key = toLower(UnitName);
    if (auto It = UnitExports_.find(Key); It != UnitExports_.end())
        return It->second;

    if (!UnitLoading_.insert(Key).second) {
        error(Loc, diag::err_unit_circular_uses, {UnitName});
        return UnitExports_[Key]; // left empty; caller sees "exports nothing"
    }

    // Turbo Tier 4, Cluster A item 2: find this unit's own real, published
    // interface file first -- "<name-lowercased>.tui", written by a prior
    // `plang -c <name>.pas` compile (Frontend.cpp's buildTUIContent /
    // writeTUIFile) -- and only fall back to re-parsing "<name>.pas" itself
    // when no .tui exists.  This is genuine separate compilation: a program
    // that 'uses' a unit whose .tui was published finds and reads THAT, not
    // the unit's own source, which this item's own report proves by deleting
    // the .pas after publishing and recompiling the importer unchanged.
    //
    // The .pas fallback is kept, deliberately, for two reasons rather than
    // just backward compatibility with Cluster A item 1's own tests: (1) it
    // lets Sema type-check against a unit that has not been compiled yet
    // (no object file, so anything it calls or reads storage from will fail
    // to LINK, but a program can still be checked against its declarations),
    // and (2) it is what makes two units' mutual IMPLEMENTATION 'uses'
    // (this item's own circularity rule -- see err_unit_circular_uses' own
    // comment and this item's report) actually bootstrap for real separate
    // compilation: compiling unit B on its own (`plang -c b.pas`) when B's
    // implementation 'uses A' and neither has been compiled yet has nothing
    // to find as "a.tui" (A has not been compiled -- that would be the truly
    // circular order), so it falls back to A's own source, reads A's
    // INTERFACE only (never A's implementation, which is unaffected by
    // whether A also happens to 'uses' B back), and succeeds -- exactly
    // mirroring how real `fpc -Mtp` auto-compiles a not-yet-compiled unit's
    // source on demand.
    //
    // A .tui is always MACHINE-written by this same compiler's own
    // writeTUIFile, always lowercase by construction, so an exact lowercase
    // match is the whole of what its own case-insensitivity needs (same
    // reasoning as EP's own .pmi search just below).  A unit's "<name>.pas"
    // is hand-written by whoever wrote the unit, and real Turbo/fpc
    // convention capitalizes it however the unit's own identifier is spelled
    // (CycleB.pas, System.pas, Crt.pas, ...), so that candidate still needs
    // the real case-insensitive directory scan this item's own predecessor
    // already had.
    auto findExact = [](const std::string& Dir, const std::string& Name) -> std::string {
        const std::string Candidate = Dir + "/" + Name;
        return llvm::sys::fs::exists(Candidate) ? Candidate : std::string();
    };
    auto findCaseInsensitive = [&](const std::string& Dir) -> std::string {
        const std::string Fast = Dir + "/" + Key + ".pas";
        if (llvm::sys::fs::exists(Fast)) return Fast;
        std::error_code EC;
        for (llvm::sys::fs::directory_iterator It(Dir, EC), End;
                It != End && !EC; It.increment(EC)) {
            llvm::SmallString<64> Stem = llvm::sys::path::filename(It->path());
            if (eqCI(std::string(Stem), Key + ".pas")) return std::string(It->path());
        }
        return "";
    };
    std::string Path;
    std::string TuiDir;
    bool        Found = false;
    bool        IsTUI = false;
    std::vector<std::string> SearchDirs = Opts.ModuleSearchPaths;
    SearchDirs.push_back(".");
    // Tier 4, Cluster B item 4: the shipped-RTL default, tried only after a
    // user's own -I directories and the current directory -- see
    // LangOptions::UnitSearchPaths's own comment for why this is a third,
    // separate list rather than folded into ModuleSearchPaths above.
    SearchDirs.insert(SearchDirs.end(), Opts.UnitSearchPaths.begin(),
                       Opts.UnitSearchPaths.end());
    // Issue #700: this has to be ONE pass over SearchDirs, trying both a
    // .tui and a .pas in each directory before moving on to the next
    // directory -- not two separate passes (all dirs' .tui, THEN all dirs'
    // .pas). Two separate passes let a later, less specific directory's
    // .tui (e.g. the shipped RTL's crt.tui) beat an earlier, more specific
    // directory's own .pas (e.g. the current directory's own Crt.pas),
    // inverting the shadowing order UnitSearchPaths's own comment promises
    // ("the current directory is meant to shadow the shipped RTL, not the
    // other way around"). Preferring .tui over .pas WITHIN the same
    // directory is still correct and deliberate (see this function's own
    // comment above on why a directory's own published interface beats
    // re-parsing its companion source), it just cannot be allowed to reach
    // across directories.
    for (const auto& Dir : SearchDirs) {
        if (Path = findExact(Dir, Key + ".tui"); !Path.empty()) {
            Found = true; IsTUI = true; TuiDir = Dir;
            break;
        }
        if (Path = findCaseInsensitive(Dir); !Path.empty()) {
            Found = true;
            break;
        }
    }
    if (!Found) {
        error(Loc, diag::err_unknown_unit, {UnitName, Key});
        UnitLoading_.erase(Key);
        return UnitExports_[Key];
    }
    // Issue #180/#304: only a genuinely PUBLISHED interface (a .tui) can be
    // stale relative to a companion source -- the .pas fallback just below
    // parses the unit's own current source directly, so by construction
    // there is nothing for it to go stale against.
    if (IsTUI) {
        const std::string PasCandidate = findCompanionPas(TuiDir, Key);
        if (!PasCandidate.empty()) warnIfInterfaceStale(Loc, Path, PasCandidate);
    }

    std::ifstream UnitFile(Path);
    if (!UnitFile) {
        error(Loc, diag::err_unknown_unit, {UnitName, Key});
        UnitLoading_.erase(Key);
        return UnitExports_[Key];
    }
    std::ostringstream SS;
    SS << UnitFile.rdbuf();

    // Parsed with its own throwaway Diagnostics/SourceManager, exactly the
    // way loadPMI parses a .pmi's content just below -- a used unit's own
    // syntax errors are this unit's problem to report (via err_malformed_
    // unit, giving the FIRST one as detail), not something the USING
    // program's own diagnostic stream should carry located Diagnostic
    // objects for (which would dangle once this throwaway SourceManager
    // goes out of scope).
    DiagnosticsEngine UnitDiags;
    LangOptions       UnitOpts = Opts;
    UnitOpts.Std = LangOptions::Standard::Turbo; // a unit file is always Turbo syntax
    SourceManager UnitSrcMgr;
    Scanner USc(UnitSrcMgr, "<" + Path + ">", SS.str(), UnitDiags, UnitOpts);
    Parser  UP(std::move(USc), UnitDiags, UnitOpts);
    auto    UnitProg = UP.parse();
    if (!UnitProg || UnitDiags.hasErrors() || !UnitProg->BareUnit) {
        std::string Detail;
        for (const auto& D : UnitDiags.diagnostics())
            if (D.Severity == DiagSeverity::Error) { Detail = D.Message; break; }
        error(Loc, diag::err_malformed_unit, {UnitName, Path, Detail});
        UnitLoading_.erase(Key);
        return UnitExports_[Key];
    }
    if (!eqCI(UnitProg->BareUnit->Name, UnitName)) {
        error(Loc, diag::err_unit_name_mismatch,
              {Path, UnitName, UnitProg->BareUnit->Name});
        UnitLoading_.erase(Key);
        return UnitExports_[Key];
    }

    const UnitNode* UN = UnitProg->BareUnit.get();
    LoadedUnitNodes_[Key] = UN;
    // Turbo Tier 5, Cluster B item 8: CurrentUnit_ has to name THIS foreign
    // unit (Key), not whatever unit/program is doing the 'uses', while its
    // interface is checked -- resolveObjectType and checkField both stamp
    // Sema's current CurrentUnit_ onto whatever they declare
    // (Type::DeclaringModule, Field::DeclaringModule) as "which module owns
    // this", and an object type's own layout/mangling correctness across a
    // unit boundary depends on that being the type's REAL declaring unit,
    // not the importer.  checkUnit already gets this right for a unit
    // compiled directly (it sets CurrentUnit_ = Key before calling
    // checkUnitInterfaceOnly itself); this was the one path that reached
    // checkUnitInterfaceOnly without doing the same.
    const std::string SavedUnitForInterface = CurrentUnit_;
    CurrentUnit_ = Key;
    checkUnitInterfaceOnly(*UN); // fills UnitExports_[Key]
    CurrentUnit_ = SavedUnitForInterface;

    // The Symbols just harvested, and any Type resolved while checking this
    // interface, may point back into UN's own AST nodes (a record Type
    // remembers the TypeDef it was laid out from, the same way a .pmi's
    // loaded interface does -- see loadPMI's own comment) -- kept alive as
    // long as this Sema is.
    LoadedUnitFiles_.push_back(std::move(UnitProg));

    UnitLoading_.erase(Key);
    return UnitExports_[Key];
}

void Sema::checkUnitInterfaceOnly(const UnitNode& Unit) {
    const std::string Key = toLower(Unit.Name);
    // Deliberately does NOT pre-populate UnitExports_[Key] before recursing
    // into this unit's own 'uses' below: loadUnitInterfaceExports's cache
    // check (UnitExports_.find) has to see "not yet loaded" for as long as
    // this call is still in progress, or a genuine cycle would look like a
    // (harmless, empty) cache hit instead of being caught by UnitLoading_ --
    // which the CALLER of this function (loadUnitInterfaceExports) has
    // already inserted Key into before ever calling this, so re-entrancy is
    // already guarded without this function needing a marker of its own.
    const size_t Pushed = pushUnitUsesScopes(Unit.InterfaceUses);
    checkBlock(*Unit.InterfaceBlock, /*BeforePop=*/[&] {
        auto& Exports = UnitExports_[Key];
        Symtab.forEachInCurrentScope([&](Symbol& Sym) {
            // A label denotes a place in this unit's own code; nothing that
            // merely uses the unit can do anything with one.
            if (Sym.Kind == SymbolKind::Label) return;
            Sym.Module = Key;
            // Issue #694: Pascal identifiers are case-insensitive, so a
            // caller may spell an export differently from how its own unit
            // declared it (getdate vs GetDate) and Sema resolves it fine --
            // but CodeGen's mangled symbol name has to be the SAME string on
            // both ends of the link, and the only spelling both the defining
            // unit's own compile and every importer can agree on is the one
            // the interface itself declared.  Recording it here as LinkName
            // (left as-is if some earlier step, e.g. a rename-on-import,
            // already set one -- see the Renames handling just below in
            // processImports) is what CGLinkage::importLinkName consults;
            // without it, importLinkName falls back to the call site's own
            // (possibly differently-cased) spelling instead.
            if (Sym.LinkName.empty()) Sym.LinkName = Sym.Name;
            Exports.push_back(Sym);
        });
    }, /*IsGlobalScope=*/false, /*IsModuleBlock=*/false, /*IsInterfaceBlock=*/true);
    popUnitUsesScopes(Pushed);
}

bool Sema::checkUnit(const UnitNode& Unit) {
    Symtab.pushScope(); // global scope, mirroring check()'s own opening scope
    registerBuiltins();
    const std::string Key = toLower(Unit.Name);
    const std::string SavedUnit = CurrentUnit_;
    CurrentUnit_ = Key;

    // Interface section: sees System + this unit's own InterfaceUses, and
    // nothing else -- in particular, NOT ImplementationUses (Cluster A item
    // 1's own requirement 3: a name available only through the
    // implementation's own 'uses' must be rejected from an interface-section
    // declaration).  checkUnitInterfaceOnly pushes and pops its own uses
    // scopes; harvests into UnitExports_[Key].
    checkUnitInterfaceOnly(Unit);

    // Implementation section: sees BOTH the interface's own declarations
    // (given to it the same way EP's own processModuleBody gives an
    // implementation module its interface's declarations -- defined into a
    // scope of their own, outside the implementation block's) AND
    // ImplementationUses, nested INSIDE (more innermost than) InterfaceUses
    // -- Cluster A item 1's own requirement 3's positive half: the
    // implementation can see what the interface saw, plus more.
    const size_t IfacePushed = pushUnitUsesScopes(Unit.InterfaceUses);
    Symtab.pushScope(/*IsBlock=*/false);
    for (const auto& Sym : UnitExports_[Key]) (void)Symtab.define(Sym);
    const size_t ImplPushed = pushUnitUsesScopes(Unit.ImplementationUses);
    if (Unit.ImplementationBlock)
        checkBlock(*Unit.ImplementationBlock, /*BeforePop=*/[&] {
            // The unit's single optional initialization block runs in the
            // scope of the implementation's own top-level declarations (as
            // well as everything the implementation and interface 'uses'
            // brought in) -- checked here, while that scope is still
            // current, rather than after checkBlock returns and has already
            // popped it.
            if (Unit.InitBody) checkCompound(*Unit.InitBody);
            // Issue #698: compare the implementation's own top-level
            // declarations against the interface's headings while THIS
            // scope (the implementation block's own) is still current --
            // see checkUnitImplConformance's own comment for why that
            // matters.
            checkUnitImplConformance(Key);
        });
    else if (Unit.InitBody) {
        // No implementation declarations at all, just 'implementation
        // begin ... end.' -- InitBody still has interface decls/uses and
        // implementation uses in scope, just no implementation-private
        // locals of its own.
        checkCompound(*Unit.InitBody);
    }
    // Issue #698: whether or not there was an implementation block at all,
    // nothing has looked for a matching body for any of the interface's own
    // headings yet UNLESS checkUnitImplConformance ran just above (inside
    // checkBlock's BeforePop) -- which needs a real block to run inside of.
    // With none, no name in the interface can possibly have been given a
    // body, so every heading is unconditionally unimplemented.
    if (!Unit.ImplementationBlock)
        for (const auto& Sym : UnitExports_[Key])
            if (Sym.Kind == SymbolKind::Proc)
                error(Sym.DeclLoc, diag::err_unit_impl_never_defined, {Sym.Name});
    popUnitUsesScopes(ImplPushed);
    Symtab.popScope(); // the reinjected-interface-declarations scope
    popUnitUsesScopes(IfacePushed);

    CurrentUnit_ = SavedUnit;
    Symtab.popScope(); // global scope
    return !hasErrors();
}

// Issue #698: a unit's interface may declare `procedure Foo(X: Integer);`
// while its implementation defines `procedure Foo(X: LongInt)` (or omits
// Foo altogether) with nothing ever comparing the two -- the implementation
// simply shadows the interface's own Proc symbol, unchecked, in the more
// deeply nested scope checkBlock gives the implementation's own top-level
// declarations.  This runs from checkUnit's BeforePop, while that scope --
// the implementation's own, exactly where Turbo requires a matching body to
// be a TOP-LEVEL declaration -- is still current, so lookupCurrent alone is
// enough to find it with no risk of a same-named export of some OTHER used
// unit (sitting in a scope in between) being found instead. Worded and
// structured like checkOutOfLineMethodBody's own heading comparison (Turbo
// Tier 5, Cluster A item 1) just above in this file, which is the same
// interface-heading-vs-body relationship for an object's method.
void Sema::checkUnitImplConformance(const std::string& Key) {
    for (const auto& IfaceSym : UnitExports_[Key]) {
        if (IfaceSym.Kind != SymbolKind::Proc) continue;
        const Symbol* Impl = Symtab.lookupCurrent(IfaceSym.Name);
        if (!Impl || Impl->Kind != SymbolKind::Proc || Impl->IsForward) {
            error(IfaceSym.DeclLoc, diag::err_unit_impl_never_defined, {IfaceSym.Name});
            continue;
        }
        if (Impl->IsFunction != IfaceSym.IsFunction
                || Impl->Params.size() != IfaceSym.Params.size()) {
            error(Impl->DeclLoc, diag::err_unit_impl_param_count,
                  {IfaceSym.Name, std::to_string(Impl->Params.size()),
                   std::to_string(IfaceSym.Params.size())});
            continue;
        }
        for (size_t I = 0; I < Impl->Params.size(); ++I) {
            const auto& ImplP  = Impl->Params[I];
            const auto& IfaceP = IfaceSym.Params[I];
            if (ImplP.IsUntyped != IfaceP.IsUntyped) {
                error(Impl->DeclLoc, diag::err_unit_impl_param_type,
                      {ImplP.Name, IfaceSym.Name,
                       ImplP.IsUntyped ? "untyped" : ImplP.Ty->Name,
                       IfaceP.IsUntyped ? "untyped" : IfaceP.Ty->Name});
                continue;
            }
            if (ImplP.IsUntyped) continue;
            if (!ImplP.Ty || !IfaceP.Ty) continue;
            if (!sameParamType(ImplP.Ty, IfaceP.Ty))
                error(Impl->DeclLoc, diag::err_unit_impl_param_type,
                      {ImplP.Name, IfaceSym.Name, ImplP.Ty->Name, IfaceP.Ty->Name});
        }
        if (IfaceSym.IsFunction && Impl->ReturnType && IfaceSym.ReturnType
                && !isIdenticalType(Impl->ReturnType, IfaceSym.ReturnType))
            error(Impl->DeclLoc, diag::err_unit_impl_return_type, {IfaceSym.Name});
    }
}

// ---------------------------------------------------------------------------
// EP §6.11: .pmi file loading (separate compilation)
// ---------------------------------------------------------------------------

Sema::PMILoadResult Sema::loadPMI(const std::string& Key, const std::string& Path) {
    // Read the .pmi file contents.  Not found/not readable is not this
    // function's problem to diagnose -- the caller tries more search-path
    // candidates first, and only reports "no module found" once every one
    // of them has come up empty.
    std::ifstream PMIFile(Path);
    if (!PMIFile) return {PMILoadResult::Status::Unreadable, ""};
    std::ostringstream SS;
    SS << PMIFile.rdbuf();
    return loadPMIFromBuffer(Key, Path, SS.str());
}

Sema::PMILoadResult Sema::loadPMIFromBuffer(const std::string& Key,
                                            const std::string& PathForDiag,
                                            const std::string& Content) {
    // A .pmi holds the interface of one module, written as an EP module
    // heading.  The parser reads a file of modules followed by a program, so
    // an empty program is appended to make one.  The wrapper's own name has
    // to be a valid EP identifier -- "__pmi__" used to be used here, but its
    // leading, trailing, AND doubled underscore each trip ISO 10206 §6.1.3's
    // own placement rule (see Scanner::scanIdentifierOrKeyword), so every
    // load used to carry at least one guaranteed, unrelated error that had
    // nothing to do with whatever the real .pmi content said.  "pmiwrapper"
    // has no underscore at all, so it trips neither that rule nor the
    // EP-extension-underscore warning, and the diagnostic stream below can
    // now be trusted to reflect only the content that was actually read.
    std::string Wrapped = Content;
    Wrapped += "\nprogram pmiwrapper;\nbegin end.\n";

    // Parse the wrapped content using an in-memory scanner.  PMIDiags and
    // PMISrcMgr are both local to this call -- a Diagnostic's own SourceLoc
    // would dangle the moment this function returns, so what a caller can
    // safely keep is PMIDiags's own fully-formatted Message text (built
    // eagerly in report(), not deferred), never the located diagnostic
    // objects themselves.
    DiagnosticsEngine PMIDiags;
    // Use EP mode so 'forward' and other EP keywords are recognized.
    LangOptions PMIOpts = Opts;
    PMIOpts.Std = LangOptions::Standard::ISO10206;
    SourceManager PMISrcMgr;
    Scanner PSc(PMISrcMgr, "<" + PathForDiag + ">", Wrapped, PMIDiags, PMIOpts);
    Parser  PP(std::move(PSc), PMIDiags, PMIOpts);
    auto Prog = PP.parse();
    // Both !Prog and PMIDiags.hasErrors() matter now that the wrapper itself
    // is clean.  Parser::parse() already returns null once its own
    // ErrorCount is nonzero, but ErrorCount only counts errors the Parser
    // itself raised through Parser::emitError -- a Scanner-level error (an
    // invalid character, an unterminated string, a misplaced underscore
    // inside the .pmi's OWN content) reports straight to PMIDiags via
    // Scanner::emitError without ever touching the Parser's counter, so
    // Parser::parse() can still hand back a non-null (if partial) tree.
    // Checking only !Prog, as this code used to before the wrapper was
    // fixed, would silently accept such a file: a hand-edited or
    // wrongly-generated .pmi that a Scanner error flags as malformed must
    // not be trusted just because parsing recovered enough to produce a
    // tree.
    if (!Prog || PMIDiags.hasErrors()) {
        std::string Detail;
        for (const auto& D : PMIDiags.diagnostics())
            if (D.Severity == DiagSeverity::Error) { Detail = D.Message; break; }
        return {PMILoadResult::Status::ParseFailed, Detail};
    }

    // Read exactly as an interface written in this file would be, so that
    // separate compilation and single-file compilation agree by construction.
    bool        Matched = false;
    std::string OtherInterfaces;
    for (auto* Mod : Prog->Modules) {
        if (!Mod->IsInterface) continue;
        if (eqCI(Mod->Name, Key)) {
            processModuleInterface(*Mod);
            Matched = true;
        } else {
            if (!OtherInterfaces.empty()) OtherInterfaces += ", ";
            OtherInterfaces += "'" + Mod->Name + "'";
        }
    }
    // A .pmi that parses cleanly but names a different module (renamed,
    // stale, copy-pasted from elsewhere) used to fall through to the exact
    // same "no module found" as a file that does not exist at all.
    if (!Matched) return {PMILoadResult::Status::WrongModule, OtherInterfaces};

    // The types resolved above point back into this tree — a record type
    // remembers the declaration it was laid out from — and codegen follows
    // those pointers long after this call has returned, so it is kept.
    LoadedInterfaces_.push_back(std::move(Prog));
    return {PMILoadResult::Status::Ok, ""};
}

std::vector<const ModuleNode*> Sema::loadedInterfaces() const {
    std::vector<const ModuleNode*> Mods;
    for (const auto& Prog : LoadedInterfaces_)
        for (const auto* Mod : Prog->Modules)
            if (Mod->IsInterface && Mod->Body) Mods.push_back(Mod);
    return Mods;
}

// ---------------------------------------------------------------------------
// Block processing — six phases
// ---------------------------------------------------------------------------

bool Sema::isBindableDenoter(const TypeNode& Node) {
    // EP §6.4.1: written on the denoter itself, or inherited from the type it
    // names — `type bf = bindable text; var f: bf` declares f bindable just as
    // `var f: bindable text` does.
    if (Node.Bindable) return true;
    if (const auto* Named = llvm::dyn_cast<NamedTypeNode>(&Node)) {
        const Symbol* Sym = Symtab.lookup(Named->Name);
        return Sym && Sym->Kind == SymbolKind::TypeAlias && Sym->IsBindable;
    }
    return false;
}

void Sema::checkBindingCall(const std::string& LowerName, SourceLocation Loc,
                            const std::vector<std::unique_ptr<ExprNode>>& Args) {
    // EP §6.7.5.6, §6.7.6.8: bind takes the variable and a BindingType value,
    // unbind and binding take the variable alone.
    const size_t Want = (LowerName == "bind") ? 2 : 1;
    if (Args.size() != Want) {
        const auto Expected = std::to_string(Want);
        const auto Got      = std::to_string(Args.size());
        error(Loc, diag::err_wrong_arg_count,
              {std::string_view(LowerName), std::string_view(Expected),
               std::string_view(Got)});
        for (const auto& A : Args) (void)checkExpr(*A);
        return;
    }

    // The first parameter is a bindable-variable-access: an entire variable,
    // not a component of one and not an expression.  checkExpr still has to
    // run over it (its return value, the ordinary per-expression checks it
    // performs, is not what is used below -- Sym is), because it is what
    // annotates the node's ResolvedType; CodeGen's own global-variable
    // registration walk (needed for a MODULE-IMPORTED variable, which has
    // no local declaration of its own to have already registered it) keys
    // off that annotation, so skipping it here left an imported bindable
    // variable's address never materialized: `bind(F, B)` for an imported
    // F reached CodeGen with a null pointer for F and crashed the LLVM IR
    // verifier instead of emitting a real call.
    (void)checkExpr(*Args[0]);
    const auto* Id = llvm::dyn_cast<IdentExpr>(Args[0].get());
    const Symbol* Sym = Id ? Symtab.lookup(Id->Name) : nullptr;
    const bool IsVar = Sym && (Sym->Kind == SymbolKind::Var
                            || Sym->Kind == SymbolKind::VarParam);
    if (!IsVar) {
        error(Loc, diag::err_bind_needs_variable, {LowerName});
    } else if (!Sym->IsBindable) {
        error(Loc, diag::err_bind_not_bindable, {Id->Name, LowerName});
    } else if (!Sym->hasDeclaredType() || Sym->declaredType()->Kind != TypeKind::File) {
        // EP §6.4.1 allows any type to be bindable and leaves what binding one
        // means to the implementation.  plang binds a file to a path and has
        // nothing to offer for anything else, so it says so.
        error(Loc, diag::err_bind_nonfile_unsupported,
              {Id->Name, Sym->hasDeclaredType() ? Sym->declaredType()->Name : std::string("?")});
    }

    for (size_t I = 1; I < Args.size(); ++I) {
        auto T = checkExpr(*Args[I]);
        if (!T->isError() && TyBindingType
            && !isIdenticalType(T, TyBindingType))
            error(Loc, diag::err_bind_needs_binding_type, {T->Name});
    }
}

void Sema::scanLabelNesting(const StmtNode* S,
                            std::vector<const StmtNode*>& NestStack) {
    if (!S) return;
    if (auto* Ls = llvm::dyn_cast<LabeledStmt>(S)) {
        if (!NestStack.empty()) {
            LabelEnclosingStmt[Ls->Label] = NestStack.back();
            // Recorded on the symbol as well, because a goto in a procedure
            // declared here is checked with that procedure's own nesting in
            // LabelEnclosingStmt, not this block's.
            if (Symbol* Sym = Symtab.lookup(Ls->Label);
                Sym && Sym->Kind == SymbolKind::Label)
                Sym->LabelNested = true;
        }
        scanLabelNesting(Ls->Stmt.get(), NestStack);
        return;
    }
    if (auto* Cs = llvm::dyn_cast<CompoundStmt>(S)) {
        for (const auto& St : Cs->Stmts) scanLabelNesting(St.get(), NestStack);
        return;
    }

    // Everything below is a structured statement, so it goes on the stack for
    // the labels beneath it to name as their enclosing one.
    NestStack.push_back(S);
    if (auto* Is = llvm::dyn_cast<IfStmt>(S)) {
        scanLabelNesting(Is->Then.get(), NestStack);
        scanLabelNesting(Is->Else.get(), NestStack);
    } else if (auto* Fs = llvm::dyn_cast<ForStmt>(S)) {
        scanLabelNesting(Fs->Body.get(), NestStack);
    } else if (auto* Ws = llvm::dyn_cast<WhileStmt>(S)) {
        scanLabelNesting(Ws->Body.get(), NestStack);
    } else if (auto* Rs = llvm::dyn_cast<RepeatStmt>(S)) {
        for (const auto& St : Rs->Stmts) scanLabelNesting(St.get(), NestStack);
    } else if (auto* Cas = llvm::dyn_cast<CaseStmt>(S)) {
        for (const auto& Arm : Cas->Arms) scanLabelNesting(Arm.Body.get(), NestStack);
        scanLabelNesting(Cas->Else.get(), NestStack);
    } else if (auto* Wts = llvm::dyn_cast<WithStmt>(S)) {
        scanLabelNesting(Wts->Body.get(), NestStack);
    }
    NestStack.pop_back();
}

void Sema::checkBlock(const BlockNode& Block,
                      llvm::function_ref<void()> BeforePop,
                      bool IsGlobalScope,
                      bool IsModuleBlock,
                      bool IsInterfaceBlock) {
    Symtab.pushScope();

    // ISO §6.2.2: a procedure or function's formal-parameter-list and its
    // block are one region, so a name this block is about to declare must not
    // repeat one of CurrentProc's parameters -- see EnclosingParamNames_'s
    // comment for why Symtab.define's own per-scope duplicate check cannot
    // see that collision by itself.  Checked once, up front, against the raw
    // declarations rather than at each phase's own Symtab.define call below,
    // so every kind of declaration this block can introduce is covered by
    // one check instead of by several that could drift out of sync with each
    // other.  Empty (so this is skipped outright) for a program or module
    // block, which has no enclosing parameter scope to collide with.
    if (!EnclosingParamNames_.empty()) {
        auto checkNotParam = [&](const std::string& Name, SourceLocation Loc) {
            if (EnclosingParamNames_.count(toLower(Name)))
                error(Loc, diag::err_duplicate_declaration, {Name});
        };
        for (const auto& Cd : Block.Consts) checkNotParam(Cd.Name, Cd.Value->Loc);
        for (const auto& Td : Block.Types)  checkNotParam(Td.Name, Td.Type->Loc);
        for (const auto& Vg : Block.Vars)
            for (size_t Idx = 0; Idx < Vg.Names.size(); ++Idx)
                checkNotParam(Vg.Names[Idx],
                              Idx < Vg.NameLocs.size() ? Vg.NameLocs[Idx]
                                                        : Vg.Type->Loc);
        for (const auto& Proc : Block.Procs) checkNotParam(Proc->Name, Proc->Loc);
    }

    // A block nested in this one checks its own body before this one does, and
    // must not be left holding this block's answers.  Restored at the bottom.
    auto SavedBlockLabels = std::move(CurrentBlockLabels);
    CurrentBlockLabels.clear();

    // Phase 1 — Labels
    for (const auto& Lbl : Block.Labels) {
        CurrentBlockLabels.insert(Lbl);
        const SourceLocation T = Block.Loc;
        // ISO §6.1.6: a label is a digit-sequence in the closed interval 0 to
        // 9999.  The parser has already reduced it to its apparent integral
        // value, so the range is read off the digits that are left.  Turbo
        // Pascal additionally allows an ordinary identifier as a label (the
        // parser's canonicalLabel has already lower-cased it), on top of --
        // not instead of -- the digit-sequence form, whose 9999 cap still
        // applies under Turbo exactly as it does under ISO 7185/EP.
        bool IsNumeric = !Lbl.empty();
        for (char C : Lbl) if (!std::isdigit(static_cast<unsigned char>(C))) { IsNumeric = false; break; }
        if (!IsNumeric) {
            if (!Opts.turbo())
                error(T, diag::err_label_must_be_integer, {Lbl});
        } else if (Lbl.size() > 4)
            error(T, diag::err_label_out_of_range, {Lbl});

        Symbol S;
        S.Kind    = SymbolKind::Label;
        S.Name    = Lbl;
        S.DeclLoc = Block.Loc;
        S.LabelInModuleBlock = IsModuleBlock;
        if (!Symtab.define(S))
            error(T, diag::err_duplicate_label, {Lbl});
    }

    // Phase 2 — Constants
    //
    // A type may be as wide as a constant says (array[1..max]), so constants
    // come first.  EP §6.8.7 then lets a constant be a structured value, which
    // names a type — the one thing here that has to wait for the types below.
    //
    // EP §6.2.1 also lets const and type sections interleave in any order, so
    // a constant may just as well name an enum value whose type section
    // appears LATER in this same block -- textual position within the
    // free-order declaration part carries no meaning.  Phase 3 below is what
    // actually defines those enum-value symbols, so any const that reaches
    // for one not yet in scope has to wait for it too, the same as a
    // structured-value const waits for the type it names.  Collecting the
    // pending names up front (rather than just catching "undefined
    // identifier" after the fact) is what tells that case apart from a
    // genuinely undefined identifier, which must still be reported here.
    std::vector<const ConstDef*> StructuredConsts;
    // TP-only: typed constants (Cd.Type != null) are deferred the same way
    // and for the same reason -- resolving the declared type may need a
    // 'type' section elsewhere in this free-declaration-order block -- but
    // are defined completely differently (defineTypedConst below), so they
    // get their own list rather than sharing StructuredConsts.
    std::vector<const ConstDef*> TypedConsts;
    std::set<std::string> PendingEnumNames;
    for (const auto& Td : Block.Types)
        if (auto* En = llvm::dyn_cast<EnumTypeNode>(Td.Type.get()))
            for (const auto& Val : En->Values)
                PendingEnumNames.insert(toLower(Val));
    auto refsPendingEnum = [&](const ExprNode* E) {
        bool Found = false;
        walkExprs(E, [&](const ExprNode* X) {
            if (Found) return;
            if (auto* Id = llvm::dyn_cast<IdentExpr>(X))
                if (!Symtab.lookup(Id->Name) && PendingEnumNames.count(toLower(Id->Name)))
                    Found = true;
        }, StackBaseline);
        return Found;
    };
    auto defineConst = [&](const ConstDef& Cd) {
        auto ValType = checkExpr(*Cd.Value);
        Symbol S;
        S.Kind = SymbolKind::Const;
        // Whether constBound below declined ONLY because Cd.Value's own
        // resolved type is a genuinely narrow Turbo Integer that rejected a
        // result the natural 64-bit width would have accepted -- see
        // NarrowFoldOverflow_'s comment (Sema.h).  Every OTHER caller of
        // constBound (array/subrange bounds, case labels, ...) already has
        // its own "not a constant expression" diagnostic for a plain
        // decline; a `const` declaration has none, so
        // `const Big = 30000 + 30000;` under -std=turbo used to silently
        // define a constant with no known ordinal value at all, the same
        // way a genuinely non-constant initializer still does (and must
        // keep doing, for ISO 7185/EP as much as for Turbo -- that decline
        // stays silent here exactly as it always has).
        const bool SavedNarrowOverflow = NarrowFoldOverflow_;
        NarrowFoldOverflow_   = false;
        const auto V          = constBound(*Cd.Value);
        const bool Overflowed = NarrowFoldOverflow_;
        NarrowFoldOverflow_   = SavedNarrowOverflow;
        if (V) {
            S.ConstOrdinal    = *V;
            S.HasConstOrdinal = true;
        } else if (const auto R = constRealBound(*Cd.Value)) {
            S.ConstReal    = *R;
            S.HasConstReal = true;
        } else if (Overflowed) {
            const auto [Lo, Hi] = narrowIntBounds(ValType->Width, ValType->IsSigned);
            error(Cd.Value->Loc, diag::err_const_expr_out_of_range,
                  {ValType->Name, std::to_string(Lo), std::to_string(Hi)});
        }
        S.Name    = Cd.Name;
        S.Ty    = ValType;
        S.DeclLoc = Cd.Value->Loc;
        if (!Symtab.define(S))
            error(Cd.Value->Loc, diag::err_duplicate_declaration, {Cd.Name});
    };
    // TP-only: a typed constant becomes a SymbolKind::Var (Symbol::
    // IsTypedConst), never a SymbolKind::Const -- see IsTypedConst's own
    // comment (SymbolTable.h) for why that is what makes it correctly
    // refused as an array bound or a case label, with no extra rejection
    // logic needed anywhere else.
    auto defineTypedConst = [&](const ConstDef& Cd) {
        auto T = resolveType(*Cd.Type);
        // Reported and left there: a type CodeGen cannot fold makes whether
        // the WRITTEN value would itself have folded a moot second question,
        // and asking it anyway (checkTypedConstFoldable below) only bloats
        // one bad declaration into two diagnostics about it.
        const bool TypeSupported = T->isError() || typedConstTypeSupported(*T);
        if (!TypeSupported)
            error(Cd.Value->Loc, diag::err_typed_const_unsupported_type,
                  {Cd.Name, T->Name});
        // EP §6.8.7.1's own convention, reused here: a structured value is
        // written without a type name, the type being the one the place it
        // appears in calls for -- ExpectedValueType_ hands that in to
        // checkStructuredValue.  A scalar initializer reads it the same way
        // ordinary assignment-compatibility checking always has.
        ExpectedValueType_ = T;
        auto ValType = checkExpr(*Cd.Value);
        ExpectedValueType_ = nullptr;
        if (!T->isError() && !ValType->isError()
                && !isAssignCompatible(*T, *ValType))
            error(Cd.Value->Loc, diag::err_typed_const_type_mismatch,
                  {ValType->Name, Cd.Name, T->Name});
        if (!T->isError() && TypeSupported)
            checkTypedConstFoldable(*Cd.Value, Cd.Name);
        Symbol S;
        S.Kind         = SymbolKind::Var;
        S.Name         = Cd.Name;
        S.Ty           = T;
        S.DeclLoc      = Cd.Value->Loc;
        S.IsTypedConst = true;
        if (!Symtab.define(S))
            error(Cd.Value->Loc, diag::err_duplicate_declaration, {Cd.Name});
    };
    for (const auto& Cd : Block.Consts) {
        if (Cd.Type)
            TypedConsts.push_back(&Cd);
        else if (llvm::isa<StructuredValueExpr>(Cd.Value.get())
                || refsPendingEnum(Cd.Value.get()))
            StructuredConsts.push_back(&Cd);
        else
            defineConst(Cd);
    }

    // Phase 3a — Per-type stubs for forward pointer references (ISO §6.4.4).
    // Each stub is a unique shared_ptr with Kind=Unresolved and Name=type-name
    // so that Phase 3c can identify which type a captured forward reference
    // pointed to. (We can't use the TyErr singleton because singletons have
    // no identifying name, and Kind=Unresolved -- rather than Kind=Error --
    // is what lets a stub be told apart from a genuine type error by its Kind
    // alone; see TypeKind::Unresolved's own comment, Sema/Type.h.)
    // EP §6.4.7: Schema definitions are registered immediately as Schema symbols
    // (no forward-pointer stub needed since schema bodies are not resolved eagerly).
    //
    // Each stub's address is also the key TypeContext::getPointer interns
    // `^<stub>` under, until Phase 3c's rebindPointer re-files it -- see that
    // function's comment for why the stub must stay alive at least that
    // long.  It then stays alive well past that, for free: resolveType
    // (SemaType.cpp) stamps every TypeNode's resolved type onto the node
    // itself, and a stub is exactly what a not-yet-declared name resolves to,
    // so the AST holds a shared_ptr to it for the rest of the compilation.
    for (const auto& Td : Block.Types) {
        if (!Td.SchemaParams.empty()) {
            // Schema definition — register directly as Schema kind.
            Symbol S;
            S.Kind = SymbolKind::Schema;
            S.Name = Td.Name;
            // Borrowed pointer back to the declaration, so resolveSchemaParams
            // can resolve SchemaDeclParams/SchemaBodyNode from it later, on
            // demand, without this loop needing to do that work itself.
            S.SchemaDeclTypeDef = &Td;
            if (!Symtab.define(S))
                error(Td.Type->Loc, diag::err_duplicate_type_decl, {Td.Name});
        } else {
            auto Stub = std::make_shared<Type>();
            Stub->Kind = TypeKind::Unresolved;
            Stub->Name = Td.Name;   // name survives Phase 3b; Phase 3c uses it to look up
            Symbol S;
            S.Kind = SymbolKind::TypeAlias;
            S.Name = Td.Name;
            S.Ty   = Stub;
            S.TypeDeclNode = Td.Type.get();
            if (!Symtab.define(S))
                error(Td.Type->Loc, diag::err_duplicate_type_decl, {Td.Name});
        }
    }

    // Phase 3b — Resolve type bodies; replace each stub pointer in the symbol table.
    // We do NOT mutate stubs in-place here so that enum-value symbols (which capture
    // the freshly-resolved shared_ptr) stay consistent.  Instead Phase 3c below
    // walks the resolved types and patches any pointer PointeeType that still holds
    // an unresolved stub (identified by Kind=Unresolved).
    // EP §6.4.7: Schema definitions were already registered in Phase 3a; their
    // discriminants are resolved below, in Phase 3b(ii), after this loop.
    for (const auto& Td : Block.Types) {
        if (!Td.SchemaParams.empty()) {
            // Resolved in Phase 3b(ii) below, after every ordinary type here
            // has its real body -- see that phase's comment for why.
        } else {
            // Turbo Tier 5, Cluster A item 1: give resolveType (by way of
            // resolveTypeImpl) this declaration's own name BEFORE calling
            // it, so an ObjectTypeNode reached as this call's direct
            // argument can use it -- see PendingObjectTypeName_'s own
            // comment (Sema.h) for why this can't simply be read back out
            // AFTER resolveType returns, the way nameNominalType below
            // names every other kind.
            PendingObjectTypeName_ = Td.Name;
            // Issue #178: unlike the object case just above, an array
            // type-denoter does not need its OWN declared name while it is
            // being resolved -- nameNominalType below still supplies that,
            // after the fact, the same as it does for Enum/Record.  What it
            // needs to know eagerly is only THAT it is one, so the
            // ArrayTypeNode arm (SemaType.cpp) can route it through
            // TypeContext::makeArrayUncached instead of the interning
            // getArray; see PendingArrayTypeIsNamed_'s own comment (Sema.h).
            // Set unconditionally, exactly as PendingObjectTypeName_ is
            // just above: harmless (read, then cleared, unused) for every
            // Td.Type that is not directly an ArrayTypeNode.
            PendingArrayTypeIsNamed_ = true;
            auto Resolved = resolveType(*Td.Type);
            nameNominalType(*Resolved, Td.Name);

            Symbol* Existing = Symtab.lookupCurrent(Td.Name);
            if (Existing && Existing->Kind == SymbolKind::TypeAlias) {
                Existing->Ty         = Resolved;   // replace stub pointer
                Existing->IsBindable = isBindableDenoter(*Td.Type);
                Existing->DeclLoc    = Td.Type->Loc;
            } else {
                Symbol S;
                S.Kind    = SymbolKind::TypeAlias;
                S.Name    = Td.Name;
                S.Ty      = Resolved;
                S.IsBindable = isBindableDenoter(*Td.Type);
                S.DeclLoc = Td.Type->Loc;
                if (!Symtab.define(S))
                    error(Td.Type->Loc, diag::err_duplicate_type_decl, {Td.Name});
            }
        }
    }

    // Phase 3b(ii) — Resolve every schema's discriminant parameter types, now
    // that Phase 3b above has resolved every ORDINARY type's real body.
    //
    // This used to run BEFORE Phase 3b (as "Phase 3b(i)"), because ISO
    // §6.2.2.9 lets a pointer's domain type be declared later in the same
    // type-definition-part, and `type pl = ^t; t(n: integer) = ...` needed
    // t's SchemaBodyNode set before ^t was resolved -- otherwise
    // resolveUndiscriminatedSchema took its silent error return and the
    // pointer carried an error pointee all the way to codegen.  But a
    // discriminant's TYPE NAME is not a pointer domain: EP §6.2.1(k) keeps
    // the ordinary forward-reference prohibition for it, the same as a
    // record field's.  Running this loop before Phase 3b meant EVERY
    // ordinary type was still Phase 3a's Kind=Error stub whenever a
    // discriminant named one, so `Box(c: Color) = record ... end` was
    // rejected as "Color is used here before its declaration" even with
    // Color declared FIRST (#17) -- resolveNamed has no way to tell
    // "unresolved because nothing has run yet" from "unresolved because it
    // is later in the file".
    //
    // resolveSchemaParams is idempotent, and is also called on demand from
    // SchemaTypeNode resolution and resolveUndiscriminatedSchema
    // (SemaType.cpp): an ordinary type-alias in this very block may itself
    // instantiate a schema (`type MyBox = Box(5);`, resolved by Phase 3b
    // above, not here), which needs SchemaDeclParams filled in before this
    // loop would otherwise reach it.  That on-demand call is also what still
    // keeps `type pl = ^t; t(n: integer) = ...` working: resolving ^t makes
    // resolveUndiscriminatedSchema resolve t's params itself, the moment
    // they are needed, independent of where this loop sits.
    for (const auto& Td : Block.Types) {
        if (Td.SchemaParams.empty()) continue;
        Symbol* Existing = Symtab.lookupCurrent(Td.Name);
        if (!Existing || Existing->Kind != SymbolKind::Schema) continue;
        resolveSchemaParams(*Existing);
    }

    // Phase 3c — Recursive pointer fixup (ISO §6.4.4 forward references).
    // After Phase 3b every type name is resolved, but pointer types formed during
    // Phase 3b that referenced a not-yet-defined type hold a stub with
    // Kind=Unresolved.  The stub's Name identifies the target; walk all types
    // recursively and patch such dangling pointers.  This handles both:
    //   PNode = ^Node;  Node = record … end   (top-level forward reference)
    //   Node  = record … next: ^Node end       (self-referential type)
    //   PList = array[1..N] of ^Node           (element-type forward reference)
    // EP §6.4.7: Schema symbols are skipped (their bodies are resolved lazily).
    std::function<void(std::shared_ptr<Type>&)> fixForwardPtrs =
        [&](std::shared_ptr<Type>& T) {
            if (!T) return;
            if (T->Kind == TypeKind::Pointer
                    && T->PointeeType && T->PointeeType->isUnresolved()) {
                Symbol* Sym = Symtab.lookup(T->PointeeType->Name);
                if (Sym && Sym->Kind == SymbolKind::TypeAlias
                        && Sym->hasDeclaredType() && !Sym->declaredType()->isError()
                        && !Sym->declaredType()->isUnresolved()) {
                    // Re-file as well as patch: the pointer was interned under
                    // the placeholder, and a later `^Node` looks it up under
                    // the real type.
                    Ctx_.rebindPointer(T, Sym->declaredType());
                }
            }
            if (T->Kind == TypeKind::Record)
                for (auto& F : T->RecordFields)
                    fixForwardPtrs(F.Ty);
            // Array and File share the ElemType field (Set does too, but a
            // set's base type is required to be ordinal, so a pointer can
            // never legally sit there and there is no stub to patch).
            if (T->Kind == TypeKind::Array || T->Kind == TypeKind::File)
                fixForwardPtrs(T->ElemType);
        };
    for (const auto& Td : Block.Types) {
        Symbol* Sym = Symtab.lookupCurrent(Td.Name);
        if (Sym && Sym->Kind == SymbolKind::TypeAlias)
            // fixForwardPtrs takes shared_ptr<Type>& and may reassign it in
            // place (rebindPointer) -- declaredType() returns a const&, so
            // this one call site needs the mutable field itself, not the
            // accessor.
            fixForwardPtrs(Sym->Ty);
        // Schema symbols: skip (lazily resolved)
    }

    // Phase 3d — The constants deferred above: ones that name a type, and
    // ones that reach for an enum value whose type section sits later in
    // this block, now that the types exist.
    for (const ConstDef* Cd : StructuredConsts) defineConst(*Cd);

    // Phase 3e — TP-only typed constants, deferred for the same reason as
    // Phase 3d's structured constants just above (see TypedConsts' own
    // comment).
    for (const ConstDef* Cd : TypedConsts) defineTypedConst(*Cd);

    // Phase 4 — Variables
    for (const auto& Vg : Block.Vars) {
        auto T = resolveType(*Vg.Type);

        // A global (program- or module-level) variable becomes one linked
        // object, and every access to it -- including ones from the runtime
        // library plang links against -- is a 32-bit PC-relative relocation.
        // Past a couple of GiB those relocations overflow at link time with a
        // confusing ld.lld error pointing at some unrelated runtime function
        // rather than at this declaration.  Caught here instead, well under
        // that ceiling: see err_global_var_too_large's comment for why 1 GiB.
        //
        // A local (procedure/function-body) variable hits no relocation, but
        // gets no pass on size either (#223): it is a stack `alloca`, and one
        // this large hangs the LLVM backend lowering it long before it would
        // ever fit a real stack. Same threshold, same reasoning -- only the
        // diagnostic differs, so it names the variable's actual scope.
        if (!T->isError()) {
            constexpr uint64_t VarByteLimit = 1ull << 30; // 1 GiB
            if (auto Sz = byteSizeOf(*T); Sz && *Sz > VarByteLimit) {
                const DiagID Id = IsGlobalScope ? diag::err_global_var_too_large
                                                 : diag::err_local_var_too_large;
                for (const auto& Nm : Vg.Names)
                    error(Vg.Type->Loc, Id,
                          {Nm, std::to_string(*Sz),
                           std::to_string(VarByteLimit)});
            }
        }

        const bool Bindable = isBindableDenoter(*Vg.Type);
        for (size_t Idx = 0; Idx < Vg.Names.size(); ++Idx) {
            const auto& Nm = Vg.Names[Idx];
            // Each name in a multi-name group ("a, b: integer") gets
            // diagnostics (e.g. warn_unused_variable) pointed at its own
            // token rather than at the shared type that follows the group.
            SourceLocation NmLoc =
                Idx < Vg.NameLocs.size() ? Vg.NameLocs[Idx] : Vg.Type->Loc;
            Symbol S;
            S.Kind    = SymbolKind::Var;
            S.Name    = Nm;
            S.Ty    = T;
            S.IsBindable = Bindable;
            S.DeclLoc = NmLoc;
            if (!Symtab.define(S))
                error(Vg.Type->Loc, diag::err_duplicate_declaration, {Nm});
        }
        // TP-only: 'absolute' overlays this declaration's storage onto an
        // existing variable's (CodeGen wires the new symbol to the aliased
        // one's own pointer -- see CodeGenProcs.cpp).  It needs exactly one
        // variable to overlay onto -- see AbsoluteExpr's own comment
        // (AstDecl.h) for why a name list is refused rather than aliasing
        // every one of them onto the same target -- and a target that is
        // itself addressable storage, the same requirement isLValue already
        // enforces for a 'var' argument or Turbo's own '@' operator.  Real
        // TP7 places no further restriction: an overlay of any size onto any
        // variable is unchecked and unsafe by design, so (unlike an ordinary
        // declaration) the new type's size is deliberately not compared
        // against the target's.
        if (Vg.AbsoluteExpr) {
            if (Vg.Names.size() != 1)
                error(Vg.AbsoluteExpr->Loc, diag::err_absolute_multiple_names);
            (void)checkExpr(*Vg.AbsoluteExpr);
            const bool IsTargetLValue = isLValue(*Vg.AbsoluteExpr);
            if (!IsTargetLValue)
                error(Vg.AbsoluteExpr->Loc, diag::err_absolute_target_not_variable);
            // Issue #639: a program-scope 'absolute' has to be wired to its
            // target's ADDRESS before any function (including main) exists
            // to run instructions in (CodeGenProcs.cpp's emitGlobals,
            // globalAbsoluteAddr) -- unlike the identical LOCAL case
            // (emitBlockAllocas), which has a real entry block open and can
            // just call emitLValue on any designator, constant or not.  So
            // a COMPONENT target ('absolute B[2]', as opposed to a bare
            // 'absolute B') is restricted here to exactly the shapes
            // globalAbsoluteAddr can fold to a compile-time constant: an
            // IdentExpr base under any number of IndexExpr layers, each
            // with a compile-time-constant index -- fpc -Mtp enforces the
            // identical restriction ("Illegal expression" for a
            // non-constant index), confirmed empirically.  Without this,
            // CodeGen used to hit an internal error (LLVM ERROR, no source
            // location) on exactly this shape instead of a located
            // diagnostic.  A record FieldExpr is deliberately NOT accepted
            // yet either (no test coverage for its own struct-GEP path);
            // isLValue above already refused anything not addressable at
            // all (a DerefExpr's pointer VALUE is itself only known at run
            // time, so it stays refused here too).
            if (IsTargetLValue && IsGlobalScope
                    && !llvm::isa<IdentExpr>(Vg.AbsoluteExpr.get())) {
                std::function<void(const ExprNode&)> checkConstComponent =
                    [&](const ExprNode& Target) {
                        if (llvm::isa<IdentExpr>(&Target)) return;
                        if (auto* Ix = llvm::dyn_cast<IndexExpr>(&Target)) {
                            checkConstComponent(*Ix->Array);
                            if (!constBound(*Ix->Index))
                                error(Ix->Index->Loc,
                                      diag::err_absolute_global_component_not_const);
                            return;
                        }
                        error(Target.Loc,
                              diag::err_absolute_global_component_not_const);
                    };
                checkConstComponent(*Vg.AbsoluteExpr);
            }
        }
        // EP §6.4.1: optional 'value' initializer.
        if (Vg.InitExpr) {
            // EP §6.8.7.1: a structured value written here names no type, the
            // variable's own being the one it is a value of.
            ExpectedValueType_ = T;
            auto InitT = checkExpr(*Vg.InitExpr);
            ExpectedValueType_ = nullptr;
            if (!T->isError() && !InitT->isError()
                && !isAssignCompatible(*T, *InitT))
                error(Vg.Type->Loc, diag::err_value_init_type_mismatch,
                      {InitT->Name, T->Name});
            checkStringCapacity(*T, *Vg.InitExpr);
            // isAssignCompatible above accepts any integer literal for a
            // subrange destination, same gap checkInitialState had for a
            // type-denoter's own 'value' clause (#254): `var x: 1..10 value
            // 500;` compiled clean and x started life outside its subrange.
            warnIfConstantOutOfRange(*T, *Vg.InitExpr);
            adoptSetType(*Vg.InitExpr, T);
        }
    }

    // Phase 5a — Procedure / function signature stubs
    for (const auto& Proc : Block.Procs) {
        // Turbo Tier 5, Cluster A item 1: an out-of-line method body
        // ('procedure T.M; ...', ProcDecl::OwnerType non-empty) is matched
        // to its in-class heading by composite key rather than treated as
        // an ordinary (possibly forward-declared) procedure of its own --
        // see checkMethodBody's own comment (Sema.h).
        if (!Proc->OwnerType.empty()) checkMethodBody(*Proc);
        else                          checkProcSignature(*Proc);
    }

    // Phase 5.5 — Pre-scan: find labels placed inside structured statements.
    // This populates LabelEnclosingStmt for Phase 6's checkGoto so that
    // forward gotos (goto appears before the label in source) are also caught.
    {
        LabelEnclosingStmt.clear();
        std::vector<const StmtNode*> NestStack;
        scanLabelNesting(Block.Body.get(), NestStack);
    }
    // Save a snapshot before Phase 5b overwrites LabelEnclosingStmt via inner checkBlock calls.
    auto SavedLabelEnclosing = LabelEnclosingStmt;

    // Phase 5b — Procedure / function bodies
    for (const auto& Proc : Block.Procs) {
        // Turbo Tier 5, Cluster A item 4: a method body's own statements are
        // now checked, with 'Self' and this type's own fields (ancestor-
        // inherited included) exposed as an implicit with-scope --
        // pushMethodSelfScope, above -- for exactly the duration of
        // checkProcBody.  Pushed OUTSIDE checkProcBody's own scope (which
        // holds the method's parameters and locals), so a parameter or
        // local shadows a same-named field exactly the way an ordinary
        // nested scope shadows an outer one -- confirmed against a local
        // fpc -Mtp build.
        if (!Proc->OwnerType.empty()) {
            const int Pushed = pushMethodSelfScope(*Proc);
            if (Pushed > 0) {
                checkProcBody(*Proc);
                Symtab.popScope();
                // Turbo Tier 5, issue #571: pop pushMethodSelfScope's own
                // ImplicitCallReceivers_ entry, pushed in the same call.
                ImplicitCallReceivers_.pop_back();
            }
            // Pushed == 0: Phase 5a's own heading lookup already failed and
            // reported a diagnostic (err_object_method_body_without_heading
            // or similar) -- left unchecked rather than resolving its
            // fields as unrelated globals, exactly as before this item.
            continue;
        }
        if (!Proc->IsForward) {
            checkProcBody(*Proc);
        }
    }

    // Restore the current block's label-nesting info (Phase 5b clobbered it).
    LabelEnclosingStmt = std::move(SavedLabelEnclosing);

    // Phase 6 — Compound body
    checkStmt(Block.Body.get());

    // Phase 6.1 — Definite assignment (§6.5.1, §6.8.3.9, §6.6.2).
    // After the body has been checked, so that the walk is over statements
    // whose names have all been resolved and whose types are known.
    checkDefiniteAssignment(Block);

    // Phase 6.5 — Inter-procedural for-loop threat detection (ISO §6.8.3.9).
    // "Neither a for-statement nor any procedure-and-function-declaration-part of
    // the block that closest-contains a for-statement shall contain a statement
    // threatening the variable denoted by the control-variable."  The declaration
    // part is searched whether or not the loop calls into it, which is what the
    // clause says, and the search reaches the procedures nested inside it, which
    // the declaration part contains in its turn.
    //
    // What is looked for is a threat to the *variable*, so a procedure declaring
    // the same name of its own is passed over: `i` inside it is its own `i`.
    if (!Block.Procs.empty() && Block.Body) {
        // Collect every ForStmt in the compound body (at all nesting depths).
        std::vector<const ForStmt*> ForLoops;
        walkStmts(Block.Body.get(), [&](const StmtNode* S) {
            if (auto* F = llvm::dyn_cast<ForStmt>(S)) ForLoops.push_back(F);
        }, StackBaseline);

        for (const auto* F : ForLoops)
            for (const auto& Proc : Block.Procs)
                checkProcForThreats(*Proc, F->Var, F->Loc, Block.Procs);
    }

    // Phase 6.9 — Statements that belong to this block but hang off the node
    // that owns it, and anything the caller must read from this scope.  It runs
    // before the label audit so that a label placed in one of those statements
    // counts as placed.
    if (BeforePop) BeforePop();

    // Phase 7 — Declaration usage audit.
    //
    // A variable nothing mentions is not an error of any kind: the program is
    // correct and does what it says.  It is reported because a declaration
    // that does nothing is almost always left over from an edit, and because
    // it costs nothing to say so.  Held back when the block already has
    // errors, since a name may be unmentioned only because the statement that
    // would have mentioned it did not survive checking.
    if (!hasErrors()) {
        Symtab.forEachInCurrentScope([&](Symbol& Sym) {
            if (Sym.Kind != SymbolKind::Var || Sym.Referenced) return;
            // A variable another compilation unit can reach is used by
            // definition, whatever this block does with it.
            if (!Sym.Module.empty() || Sym.IsProtected) return;
            warning(Sym.DeclLoc, diag::warn_unused_variable, {Sym.Name});
        });
    }

    // Phase 7.5 — Label usage audit
    Symtab.forEachInCurrentScope([&](Symbol& Sym) {
        if (Sym.Kind != SymbolKind::Label) return;
        const SourceLocation T = Sym.DeclLoc;
        if (!Sym.LabelPlaced)
            // ISO §6.8.1: a declared label must have a corresponding labeled statement.
            error(T, diag::err_label_never_placed, {Sym.Name});
        else if (!Sym.LabelReferenced)
            warning(T, diag::warn_label_unreachable, {Sym.Name});
    });

    // Phase 7.6 — Forward-declaration completion audit (ISO §6.6.1).
    //
    // The forward directive promises a defining occurrence of the same
    // procedure- or function-identifier "later in the same block".  Phase 5a
    // clears IsForward the moment a matching heading is found, but nothing
    // audited the remainder: a forward declaration with no matching
    // definition anywhere in the block compiled clean and failed only at
    // link time, against the mangled name (e.g. "undefined symbol
    // pas_never_defined") rather than being caught here against the source
    // identifier (#266).
    //
    // Skipped for a module interface's own block: EP §6.11.2 records every
    // heading there as IsForward regardless of the 'forward' keyword, since
    // the heading alone is the whole declaration and its body is given in a
    // separate implementation block -- a different scope this audit, being
    // per-scope, never sees.
    if (!IsInterfaceBlock) {
        Symtab.forEachInCurrentScope([&](Symbol& Sym) {
            if (Sym.Kind != SymbolKind::Proc || !Sym.IsForward) return;
            error(Sym.DeclLoc, diag::err_forward_never_defined, {Sym.Name});
        });
    }

    // Phase 7.6b — Method-body completion audit (Turbo Tier 5, Cluster A
    // item 1), the Method sibling of Phase 7.6 just above: an in-class
    // method heading with no out-of-line body anywhere in this block, and
    // that is not itself 'abstract' (which needs none by construction --
    // err_object_abstract_method_has_body, checkMethodBody, is the opposite
    // mistake), compiled clean under this item's own predecessor and would
    // otherwise only fail at link time.  Skipped for an interface block for
    // the identical reason Phase 7.6 is: a unit interface's own object type
    // gets its methods' bodies in a separate implementation block/scope
    // this per-scope audit never sees -- exactly the same UnitExports_
    // re-registration gap that already leaves Phase 7.6 unable to catch a
    // missing INTERFACE proc body either (checkUnit, above).
    if (!IsInterfaceBlock) {
        Symtab.forEachInCurrentScope([&](Symbol& Sym) {
            if (Sym.Kind != SymbolKind::Method) return;
            if (Sym.IsMethodAbstract || Sym.MethodBody) return;
            error(Sym.DeclLoc, diag::err_object_method_never_defined,
                  {Sym.MethodOwnerType, Sym.Decl ? Sym.Decl->Name : Sym.Name});
        });
    }

    CurrentBlockLabels = std::move(SavedBlockLabels);
    Symtab.popScope();
}

void Sema::checkMethodBody(const ProcDecl& Proc) {
    // Proc is the OUT-OF-LINE body ('procedure T.M; begin ... end;');
    // Proc.OwnerType is "T", Proc.Name is "M".  Look its heading up by the
    // same composite key resolveObjectType registered it under
    // (Sema::objectMethodKey) -- a plain Symtab.lookup (innermost-scope-
    // first), not a Parent-chain walk: the heading was registered into the
    // SAME scope the object type's own TypeAlias lives in, which is exactly
    // the scope Proc's own out-of-line body is being checked in too (both
    // are declarations in the same block).
    const std::string Key = objectMethodKey(Proc.OwnerType, Proc.Name);
    Symbol* Heading = Symtab.lookup(Key);
    if (!Heading || Heading->Kind != SymbolKind::Method) {
        error(Proc.Loc, diag::err_object_method_body_without_heading,
              {Proc.OwnerType, Proc.Name});
        return;
    }
    if (Heading->IsMethodAbstract) {
        error(Proc.Loc, diag::err_object_abstract_method_has_body,
              {Proc.OwnerType, Proc.Name});
        return;
    }
    if (Heading->MethodBody) {
        error(Proc.Loc, diag::err_object_method_body_duplicate,
              {Proc.OwnerType, Proc.Name});
        return;
    }

    // Resolve this body's own heading (parameters, return type) exactly the
    // way checkProcSignature resolves an ordinary procedure's, then compare
    // against the in-class heading's already-resolved signature -- the same
    // three checks (arity, per-parameter type, return type) an ordinary
    // forward declaration's body is checked against, just worded for a
    // method (err_object_method_param_count/_param_type/_return_type
    // instead of err_forward_param_*).
    std::vector<Type::Param> ResolvedParams;
    for (const auto& Pg : Proc.Params) {
        auto Pt = Pg.Type ? resolveParamType(*Pg.Type, Pg.IsVar) : nullptr;
        for (const auto& Nm : Pg.Names)
            ResolvedParams.push_back({Pg.IsVar, Nm, Pt, Pg.IsConst, /*IsUntyped=*/!Pg.Type});
    }
    std::shared_ptr<Type> Ret;
    if (Proc.IsFunction && Proc.ReturnType) Ret = resolveType(*Proc.ReturnType);

    if (ResolvedParams.size() != Heading->Params.size()) {
        error(Proc.Loc, diag::err_object_method_param_count,
              {Proc.OwnerType, Proc.Name,
               std::to_string(ResolvedParams.size()),
               std::to_string(Heading->Params.size())});
    } else {
        for (size_t I = 0; I < ResolvedParams.size(); ++I) {
            if (ResolvedParams[I].IsUntyped != Heading->Params[I].IsUntyped) {
                error(Proc.Loc, diag::err_object_method_param_type,
                      {ResolvedParams[I].Name, Proc.OwnerType, Proc.Name,
                       ResolvedParams[I].IsUntyped ? "untyped" : ResolvedParams[I].Ty->Name,
                       Heading->Params[I].IsUntyped ? "untyped" : Heading->Params[I].Ty->Name});
                continue;
            }
            if (ResolvedParams[I].IsUntyped) continue;
            if (!ResolvedParams[I].Ty || !Heading->Params[I].Ty) continue;
            if (!sameParamType(ResolvedParams[I].Ty, Heading->Params[I].Ty))
                error(Proc.Loc, diag::err_object_method_param_type,
                      {ResolvedParams[I].Name, Proc.OwnerType, Proc.Name,
                       ResolvedParams[I].Ty->Name, Heading->Params[I].Ty->Name});
        }
    }
    if (Proc.IsFunction && Ret && Heading->ReturnType
            && !isIdenticalType(Ret, Heading->ReturnType))
        error(Proc.Loc, diag::err_object_method_return_type, {Proc.OwnerType, Proc.Name});

    Heading->MethodBody = &Proc;

    // Turbo Tier 5, Cluster A item 4: resolved once here, rather than
    // re-looked-up by spelling every time something wants it, exactly the
    // reasoning ForwardHeading's own comment (AstDecl.h) gives for pointing
    // straight at the resolved answer instead of the name that led to it.
    // Symtab.lookup, not lookupCurrent: an out-of-line method body is
    // checked in the SAME scope its object type's own TypeAlias lives in
    // (SymbolKind::Method's own comment), so a plain lookup already finds
    // it -- and reaches an ENCLOSING scope's type of the same name if this
    // scope, oddly, does not have one, which cannot happen for a
    // known-valid Heading but costs nothing to allow.
    if (const Symbol* TypeSym = Symtab.lookup(Proc.OwnerType);
            TypeSym && TypeSym->Kind == SymbolKind::TypeAlias && TypeSym->hasDeclaredType()
            && !TypeSym->declaredType()->isError())
        Proc.ResolvedOwnerType = TypeSym->declaredType();
}

int Sema::pushMethodSelfScope(const ProcDecl& Proc) {
    if (!Proc.ResolvedOwnerType) return 0;
    Symtab.pushScope(/*IsBlock=*/false);
    // 'Self' itself: a Var of the method's own owning object type, exactly
    // like an ordinary object variable would resolve, so that an explicit
    // 'Self.Field' or passing 'Self' on to another call type-checks through
    // the SAME machinery an ordinary object-typed variable already does.
    {
        Symbol SelfSym;
        SelfSym.Kind = SymbolKind::Var;
        SelfSym.Name = "Self";
        SelfSym.Ty   = Proc.ResolvedOwnerType;
        (void)Symtab.define(std::move(SelfSym));
    }
    // Every field this type has -- ancestor-inherited included, since
    // RecordFields is already the flattened ancestor-then-own list (see its
    // own comment, Type.h) -- exposed unqualified, the same as
    // pushWithScope's own plain-Record branch exposes 'with r do' fields.
    for (const auto& F : Proc.ResolvedOwnerType->RecordFields) {
        Symbol FS;
        FS.Kind = SymbolKind::Var;
        FS.Name = F.Name;
        FS.Ty   = F.Ty;
        // Issue #626: marks this as a FIELD (own or inherited) rather than
        // a genuine local -- see Symbol::IsSelfScopeField's own comment for
        // why resultFrameFor needs to tell the two apart.
        FS.IsSelfScopeField = true;
        (void)Symtab.define(std::move(FS));
    }
    // Turbo Tier 5, issue #571: 'Self' itself as an IMPLICIT-CALL receiver
    // too -- see ImplicitCallReceivers_'s own comment for the whole design.
    // The sole caller (checkBlock's Phase 5b) pops this in lockstep with the
    // Symtab scope just pushed above, right after checkProcBody returns.
    ImplicitCallReceivers_.push_back(Proc.ResolvedOwnerType);
    return 1;
}

Sema::ImplicitMethodLookup
Sema::findImplicitCallMethod(const std::string& Name) const {
    for (auto It = ImplicitCallReceivers_.rbegin();
            It != ImplicitCallReceivers_.rend(); ++It) {
        if (!*It) continue; // a with-target with no methods of its own (record/schema)
        const MethodLookup ML = findObjectMethod(**It, Name);
        if (ML.M) return {*It, ML.Owner, ML.M};
    }
    return {};
}

void Sema::checkProcSignature(const ProcDecl& Proc) {
    // ISO §6.6.1: the defining occurrence of a procedure declared 'forward'
    // gives the name and nothing else — the parameter list and result type
    // were written once, at the declaration, and are not repeated.  Bind the
    // two together before resolving anything, so that what follows reads one
    // heading and the two occurrences agree by construction.  Repeating the
    // heading is accepted as well, which is what most Pascals allow; that path
    // resolves it a second time and compares the results below.
    if (!Proc.IsForward && Proc.Params.empty() && !Proc.ReturnType) {
        const Symbol* Fwd = Symtab.lookupCurrent(Proc.Name);
        // EP §6.11.1: in the implementation of a module the heading was
        // written in the interface, a scope further out, and a body given
        // there as the name alone is the same abbreviation as after 'forward'.
        if (!Fwd && InModuleImplementation_) Fwd = Symtab.lookup(Proc.Name);
        if (Fwd && Fwd->Kind == SymbolKind::Proc && Fwd->IsForward && Fwd->Decl
                && Fwd->IsFunction == Proc.IsFunction)
            Proc.ForwardHeading = Fwd->Decl;
    }
    const ProcDecl& H = Proc.heading();

    // Resolve parameter types and return type.
    std::vector<Type::Param> ResolvedParams;
    for (const auto& Pg : H.Params) {
        // Turbo untyped parameter: Pg.Type is deliberately null -- see
        // ParamGroup::Type's own comment (AstType.h).
        auto T = Pg.Type ? resolveParamType(*Pg.Type, Pg.IsVar) : nullptr;
        for (const auto& Nm : Pg.Names) {
            ResolvedParams.push_back(
                {Pg.IsVar, Nm, T, Pg.IsConst, /*IsUntyped=*/!Pg.Type});
        }
    }
    std::shared_ptr<Type> Ret;
    if (H.IsFunction && H.ReturnType) {
        Ret = resolveType(*H.ReturnType);
        // ISO 7185 §6.6.2 admits a simple-type or a pointer-type and nothing
        // else.  EP §6.6.2 lifts that to any type a value can be assigned
        // from, which leaves out only the file types: a function result is
        // assigned to, and §6.8.2.2 will not assign a file.
        if (Ret && !Ret->isError()) {
            const bool AnyFile = typeContainsFile(*Ret);
            const bool Simple  = Ret->isOrdinal() || Ret->isNumeric()
                              || Ret->Kind == TypeKind::Pointer
                              // Turbo: a function may return its own
                              // string[N] -- ordinary, idiomatic Turbo
                              // Pascal (`function F: string;`), the same
                              // shape real Turbo/FPC accept.  A narrow,
                              // ShortString-only addition rather than
                              // following EP's own blanket "any
                              // assignable, file-free type" carve-out just
                              // below: Turbo has no schema/record/array-
                              // result convention (or Extended Pascal's own
                              // rationale) for this to generalize from, and
                              // widening "Simple" to admit those too would
                              // be well outside this item's scope.
                              || (Opts.turbo() && Ret->Kind == TypeKind::ShortString);
            if (AnyFile || (!Simple && !Opts.extendedPascal()))
                error(Proc.Loc, diag::err_function_result_type, {Ret->Name});
        }
    } else if (Proc.IsFunction) {
        // The parser lets a function heading arrive without a result type,
        // since only here can it be told from the defining occurrence of a
        // forward-declared function, which is written that way on purpose.
        error(Proc.Loc, diag::err_function_no_result_type, {Proc.Name});
    }

    // Check for a matching forward declaration.
    Symbol* Existing = Symtab.lookupCurrent(Proc.Name);
    if (Existing) {
        if (Existing->Kind == SymbolKind::Proc && Existing->IsForward && !Proc.IsForward) {
            // Verify the implementation signature matches the forward declaration.
            if (ResolvedParams.size() != Existing->Params.size()) {
                error(Proc.Loc, diag::err_forward_param_count,
                      {Proc.Name,
                       std::to_string(ResolvedParams.size()),
                       std::to_string(Existing->Params.size())});
            } else {
                for (size_t I = 0; I < ResolvedParams.size(); ++I) {
                    // Turbo untyped parameter: one side declared 'var x'
                    // with no type at all, so there is no Ty to compare --
                    // and, since IsUntyped disambiguates a deliberately
                    // untyped parameter from a Ty that is null because some
                    // OTHER resolution failed (which the `continue` just
                    // below still guards against), a mismatched pairing
                    // (one side untyped, the other not) is diagnosed here
                    // rather than silently accepted the way two
                    // independently-null Ty's used to be before this flag
                    // existed to tell them apart.
                    if (ResolvedParams[I].IsUntyped != Existing->Params[I].IsUntyped) {
                        error(Proc.Loc, diag::err_forward_param_type,
                              {ResolvedParams[I].Name, Proc.Name,
                               ResolvedParams[I].IsUntyped ? "untyped"
                                   : ResolvedParams[I].Ty->Name,
                               Existing->Params[I].IsUntyped ? "untyped"
                                   : Existing->Params[I].Ty->Name});
                        continue;
                    }
                    if (ResolvedParams[I].IsUntyped) continue; // both untyped: nothing more to compare
                    if (!ResolvedParams[I].Ty || !Existing->Params[I].Ty) continue;
                    // Not isIdenticalType: the two headings resolve the same
                    // parameter twice, and the forms that are not interned
                    // would come back as different types.
                    if (!sameParamType(ResolvedParams[I].Ty,
                                       Existing->Params[I].Ty))
                        error(Proc.Loc, diag::err_forward_param_type,
                              {ResolvedParams[I].Name, Proc.Name,
                               ResolvedParams[I].Ty->Name,
                               Existing->Params[I].Ty->Name});
                }
            }
            if (Proc.IsFunction && Ret && Existing->ReturnType &&
                !isIdenticalType(Ret, Existing->ReturnType))
                error(Proc.Loc, diag::err_forward_return_type, {Proc.Name});
            Existing->IsForward = false;
            return;
        }
        if (!(Existing->Kind == SymbolKind::Builtin)) {
            error(Proc.Loc, diag::err_duplicate_declaration, {Proc.Name});
            return;
        }
    }

    Symbol S;
    S.Kind       = SymbolKind::Proc;
    S.Name       = Proc.Name;
    S.IsFunction = Proc.IsFunction;
    S.IsForward  = Proc.IsForward;
    S.Params     = std::move(ResolvedParams);
    S.ReturnType = Ret;
    S.Decl       = &Proc;
    S.DeclLoc    = Proc.Loc;
    // Turbo procedural VALUES (see Symbol::IsNested's own comment): this
    // Proc's signature is being checked while CurrentProc is whatever
    // ENCLOSING routine's block Proc was declared inside, or null at the
    // program's own top level -- checkProcBody does not set CurrentProc to
    // &Proc itself until Phase 5b reaches this same Proc's own body, later.
    S.IsNested   = (CurrentProc != nullptr);
    (void)Symtab.define(std::move(S));
}

/// Records every value parameter of \p Proc that its body modifies.
///
/// A conformant array passed by value must be copied, and the copy costs what
/// the actual costs; a body that never modifies the formal cannot tell whether
/// it was copied, so it needs no copy.  See ProcDecl::ModifiedParams.
///
/// Conservative in the one place it has to be: a call to something whose
/// signature is not to hand counts as modifying every argument it is given.
void Sema::recordModifiedParams(const ProcDecl& Proc) {
    const ProcDecl& H = Proc.heading();
    std::set<std::string> Value;
    for (const auto& Pg : H.Params)
        if (!Pg.IsVar)
            for (const auto& Nm : Pg.Names) Value.insert(toLower(Nm));
    if (Value.empty() || !Proc.Body) return;

    // The variable an access is ultimately of: a[i], r.f and p^ all modify the
    // thing they are a part of.
    const auto baseOf = [](const ExprNode* E) -> std::string {
        for (int Hops = 0; E && Hops < 64; ++Hops) {
            if (auto* Id = llvm::dyn_cast<IdentExpr>(E))    return toLower(Id->Name);
            if (auto* Ix = llvm::dyn_cast<IndexExpr>(E))    { E = Ix->Array.get();  continue; }
            if (auto* Fd = llvm::dyn_cast<FieldExpr>(E))    { E = Fd->Record.get(); continue; }
            if (auto* Sb = llvm::dyn_cast<SubstringExpr>(E)){ E = Sb->Str.get();    continue; }
            // A dereference modifies what the pointer points AT, not the
            // pointer, so it stops here.
            return {};
        }
        return {};
    };
    const auto note = [&](const ExprNode* E) {
        const std::string B = baseOf(E);
        if (!B.empty() && Value.count(B)) Proc.ModifiedParams.insert(B);
    };

    walkStmts(Proc.Body->Body.get(), [&](const StmtNode* S) {
            if (auto* As = llvm::dyn_cast<AssignStmt>(S)) { note(As->Target.get()); return; }
            if (auto* Cs = llvm::dyn_cast<CallStmt>(S)) {
                const std::string Lo = toLower(Cs->Name);
                const Symbol* Callee = Symtab.lookup(Cs->Name);
                // read and readln store into every argument but the file.
                if (Lo == "read" || Lo == "readln") {
                    for (const auto& A : Cs->Args) note(A.get());
                    return;
                }
                for (size_t I = 0; I < Cs->Args.size(); ++I) {
                    const bool ByRef =
                        !Callee || I >= Callee->Params.size() || Callee->Params[I].IsVar;
                    if (ByRef) note(Cs->Args[I].get());
                }
            }
        }, StackBaseline);
    // A function call in an expression can take a var parameter too.
    walkStmts(Proc.Body->Body.get(), [&](const StmtNode* S) {
            forEachStmtExpr(S, [&](const ExprNode* E) {
                walkExprs(E, [&](const ExprNode* X) {
                    auto* Ce = llvm::dyn_cast<CallExpr>(X);
                    if (!Ce) return;
                    const Symbol* Callee = Symtab.lookup(Ce->Name);
                    for (size_t I = 0; I < Ce->Args.size(); ++I) {
                        const bool ByRef =
                            !Callee || I >= Callee->Params.size()
                            || Callee->Params[I].IsVar;
                        if (ByRef) note(Ce->Args[I].get());
                    }
                }, StackBaseline);
            });
        }, StackBaseline);
}

void Sema::checkProcBody(const ProcDecl& Proc) {
    const ProcDecl* Outer = CurrentProc;
    CurrentProc = &Proc;

    // TP-only: a nested procedure's own body starts with no loop context of
    // its own, whatever loop textually encloses ITS caller -- see
    // LoopDepth_'s own comment for why this is guaranteed by Phase
    // ordering alone and saved/restored here anyway, matching
    // CurrentRetType/EnclosingParamNames_ just below.
    const int SavedLoopDepth = LoopDepth_;
    LoopDepth_ = 0;

    // ISO §6.6.1: for the defining occurrence of a forward-declared procedure
    // this is the declaration's heading — the parameters the body names were
    // written there.
    const ProcDecl& H = Proc.heading();

    Symtab.pushScope();

    // Define parameters in the function's own scope.
    for (const auto& Pg : H.Params) {
        // Turbo untyped parameter (`procedure P(var x)`): Pg.Type is
        // deliberately null (ParamGroup::Type's own comment) and
        // resolveParamType has nothing to resolve -- see checkIdent's own
        // comment for what a bare use of the resulting Sym->declaredType()
        // == nullptr gets, and checkTypeCast's for the one thing it may
        // legally be used for.
        auto T = Pg.Type ? resolveParamType(*Pg.Type, Pg.IsVar) : nullptr;

        // Turbo const parameter, structured type: CodeGen passes it by
        // reference (CodeGenProcs.cpp), so -- like a 'var' parameter -- it
        // aliases the caller's own storage rather than copying into one of
        // its own, and is excluded from the gate below for the same reason.
        const bool ByRefLike = Pg.IsVar
            || (Pg.IsConst && T && !T->isError() && isStructuredForConstByRef(*T));

        // ISO §6.6.3.2: a by-value parameter is a variable of its own that
        // the actual is copied into, so CodeGen gives it exactly the same
        // fixed-size stack `alloca` as a local variable (createEntryAlloca,
        // CodeGenProcs.cpp) -- same 1 GiB gate as checkBlock's Phase 4
        // (#223), extended here for the identical reason (#410). A 'var'
        // parameter is deliberately excluded: it aliases the caller's own
        // storage rather than copying into one of its own, so no oversized
        // local is ever materialized for it.
        if (!ByRefLike && T && !T->isError()) {
            constexpr uint64_t ParamByteLimit = 1ull << 30; // 1 GiB
            if (auto Sz = byteSizeOf(*T); Sz && *Sz > ParamByteLimit) {
                for (const auto& Nm : Pg.Names)
                    error(Pg.Type->Loc, diag::err_param_too_large,
                          {Nm, std::to_string(*Sz),
                           std::to_string(ParamByteLimit)});
            }
        }

        for (size_t Idx = 0; Idx < Pg.Names.size(); ++Idx) {
            const auto& Nm = Pg.Names[Idx];
            // Each name in a multi-name group ("a, b: integer") gets
            // diagnostics (e.g. warn_unused_parameter) pointed at its own
            // token rather than at the shared type that follows the group.
            SourceLocation NmLoc =
                Idx < Pg.NameLocs.size() ? Pg.NameLocs[Idx]
                                        : (Pg.Type ? Pg.Type->Loc : Pg.Loc);
            Symbol S;
            S.Kind        = Pg.IsVar ? SymbolKind::VarParam : SymbolKind::Var;
            S.Name        = Nm;
            S.Ty          = T;
            S.DeclLoc     = NmLoc;
            S.IsProtected = Pg.IsProtected; // EP §6.7.3.1
            S.IsConstParam = Pg.IsConst;    // Turbo const parameter
            // EP §6.4.1: a parameter whose denoter is bindable stands for a
            // bindable variable, so bind reaches it through the parameter.
            // Pg.Type is null for a Turbo untyped parameter, which has no
            // denoter to ask isBindableDenoter about at all.
            S.IsBindable  = Pg.IsVar && Pg.Type && isBindableDenoter(*Pg.Type);
            // ISO §6.6.3.1: inside the body a procedural parameter is called
            // like any other procedure, so it is entered as one.  Ty keeps the
            // signature, which is what a further hand-off is checked against.
            if (T && isCallable(*T)) {
                S.Kind        = SymbolKind::Proc;
                S.IsFunction  = T->Kind == TypeKind::Function;
                S.IsProcParam = true;
                S.Params      = T->Params;
                S.ReturnType  = T->RetType;
            }
            if (!Symtab.define(S))
                error(Pg.Type ? Pg.Type->Loc : Pg.Loc, diag::err_duplicate_param, {Nm});

            // EP §6.7.3.7 / Turbo open array: for conformant array params,
            // register each lo/hi bound variable as a Var, typed with the
            // dimension's declared ordinal type (index-type-specification),
            // in the function scope.  Walk nested ConformantArray types to
            // register all dimensions.
            if (T && T->Kind == TypeKind::ConformantArray) {
                auto* Ct = T.get();
                while (Ct && Ct->Kind == TypeKind::ConformantArray) {
                    for (const auto& Cb : Ct->ConformantBounds) {
                        auto definebound = [&](const std::string& BoundName) {
                            if (BoundName.empty()) return;
                            Symbol Bs;
                            Bs.Kind    = SymbolKind::Var;
                            Bs.Name    = BoundName;
                            // EP §6.7.3.7: "applied occurrences ... shall
                            // denote the smallest [largest] value specified
                            // by the corresponding index-type" -- the type
                            // written in the schema, not always integer.
                            Bs.Ty      = Cb.OrdType ? Cb.OrdType : TyInt;
                            Bs.DeclLoc = Pg.Type->Loc;
                            // EP §6.7.3.7.1 NOTE 2: "The object denoted by a
                            // bound-identifier is neither constant nor a
                            // variable" -- it may be used but never assigned.
                            // Reuse the protected-parameter enforcement path
                            // (checkNotProtected) rather than a parallel one.
                            Bs.IsConformantBound = true;
                            if (!Symtab.define(Bs))
                                error(Pg.Type->Loc, diag::err_duplicate_param, {BoundName});
                        };
                        // Turbo open array: unlike EP's conformant-array
                        // group semantics, each NAME in a shared 'a, b: array
                        // of Integer' group needs its OWN independent bound
                        // pair (confirmed empirically against fpc -Mtp: a and
                        // b size independently at the call site), so the
                        // bound names are synthesized fresh per Nm here
                        // rather than read from Cb.LoBoundName/HiBoundName,
                        // which for an open array are always empty
                        // placeholders (parseTurboOpenArrayParamType's own
                        // comment).  CodeGenProcs.cpp's prologue mirrors this
                        // exact naming.
                        if (Ct->IsOpenArray) {
                            definebound(openArrayLowBoundName(Nm));
                            definebound(openArrayHighBoundName(Nm));
                        } else {
                            definebound(Cb.LoBoundName);
                            definebound(Cb.HiBoundName);
                        }
                    }
                    Ct = Ct->ElemType.get();
                }
            }
        }
    }

    // Resolve and store the return type so checkIdent / isLValue can find it
    // when the function name is used as the result variable (Pascal convention).
    // We do NOT put the function name into the symbol table as a Var — that
    // would shadow the Proc symbol and break recursive calls.
    auto SavedRetType   = CurrentRetType;
    const bool IsFunc   = H.IsFunction && H.ReturnType;
    if (IsFunc) {
        CurrentRetType = resolveType(*H.ReturnType);

        // A function's result -- named or the bare function identifier -- is
        // materialized as its own fixed-size stack `alloca` in CodeGen
        // (curRetAlloca, CodeGenProcs.cpp), copied into on every return path
        // exactly like a by-value parameter's copy-in above. Same 1 GiB gate
        // (#223, #410) and for the same reason.
        if (CurrentRetType && !CurrentRetType->isError()) {
            constexpr uint64_t RetByteLimit = 1ull << 30; // 1 GiB
            if (auto Sz = byteSizeOf(*CurrentRetType); Sz && *Sz > RetByteLimit) {
                error(H.ReturnType->Loc, diag::err_return_type_too_large,
                      {Proc.Name, std::to_string(*Sz),
                       std::to_string(RetByteLimit)});
            }
        }

        // The frame stays on the stack for as long as this function's block is
        // being checked, so a function declared inside it can find this result.
        FuncStack.push_back({&Proc, CurrentRetType, /*HasResult=*/false});
        // EP §6.7.2: if a named result variable was declared, register it as a
        // Var so it can be assigned and read by name inside the body.
        if (!H.ResultName.empty()) {
            Symbol RS;
            RS.Kind        = SymbolKind::Var;
            RS.Name        = H.ResultName;
            RS.Ty          = CurrentRetType;
            RS.DeclLoc     = Proc.Loc;
            RS.IsResultVar = true;
            if (!Symtab.define(RS))
                error(Proc.Loc, diag::err_duplicate_param, {H.ResultName});
        }
    } else {
        CurrentRetType = nullptr;
    }

    // ISO §6.2.2: the parameters just defined above and Proc.Body's own
    // declarations are one region.  checkBlock (about to run) pushes its own
    // scope for the latter, so it cannot see this scope's names through
    // Symtab.define alone -- snapshot them here and let checkBlock
    // cross-check its declarations against the snapshot instead.  Saved and
    // restored around the call so a nested procedure's own checkProcBody
    // invocation (reached from inside checkBlock, via Phase 5b) sees only
    // ITS OWN parameters while its body is checked, not this one's.
    auto SavedEnclosingParamNames = std::move(EnclosingParamNames_);
    EnclosingParamNames_.clear();
    Symtab.forEachInCurrentScope([&](Symbol& S) {
        EnclosingParamNames_.insert(toLower(S.Name));
    });

    // Turbo Tier 5, issue #617: see ProcBodyNestingDepth_'s own comment
    // (Sema.h) for why this brackets the SAME checkBlock call rather than
    // reusing IsGlobalScope or EnclosingParamNames_.
    if (Proc.Body) {
        ++ProcBodyNestingDepth_;
        checkBlock(*Proc.Body);
        --ProcBodyNestingDepth_;
    }

    EnclosingParamNames_ = std::move(SavedEnclosingParamNames);

    // ISO §6.7.3: a function must assign to its result variable at least once.
    // The assignment that does so may be written in a function nested inside
    // this one, which is why the answer is read off the frame rather than off a
    // flag the nested function would have overwritten.
    if (IsFunc) {
        const bool Assigned = FuncStack.back().HasResult;
        FuncStack.pop_back();
        if (!Assigned)
            error(Proc.Loc, diag::err_function_no_result, {Proc.Name});
    }

    // A parameter the body never names is worth reporting for the reason an
    // unused variable is, with one difference: a procedure passed as a
    // procedural parameter (§6.6.3.1) has the signature its formal demands
    // whether it wants every parameter or not, so this is a warning that some
    // correct programs will want turned off.
    if (!hasErrors()) {
        for (const auto& Pg : H.Params) {
            for (const auto& Nm : Pg.Names) {
                const Symbol* Sym = Symtab.lookupCurrent(Nm);
                if (!Sym || Sym->Referenced) continue;
                // A procedural parameter is called rather than read, and the
                // call marks it; anything else here is a plain parameter.
                if (Sym->IsProcParam) continue;
                warning(Sym->DeclLoc, diag::warn_unused_parameter, {Nm});
            }
        }
    }

    Symtab.popScope();
    CurrentProc    = Outer;
    CurrentRetType = SavedRetType;
    LoopDepth_     = SavedLoopDepth;

    // Every call in the body has been resolved by now, so the callees'
    // signatures are available to say which arguments travel by reference.
    recordModifiedParams(Proc);
}

// (Type resolution → SemaType.cpp)
// (Statement checking → SemaStmt.cpp)
// (Expression checking + call helpers → SemaExpr.cpp)

// Placeholder to keep EOF well-formed.
static_assert(true);
