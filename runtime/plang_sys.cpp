/// plang_sys.cpp — Pascal system routines (C++23)
///
/// halt, new, and dispose — the only places generated Pascal programs touch
/// process control and the heap.

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstdlib>

#include "plang/Basic/PascalFileLayout.h"

namespace plang {

namespace {
/// Exit status for a failed ISO runtime check.  Distinct from 0 (normal halt)
/// and from the small codes a program is likely to pass to halt itself.
constexpr int PlangRuntimeErrorStatus = 70;

/// Module finalisers, in the order the modules were initialized.  A plain
/// array rather than a std::vector: the runtime is linked into generated
/// programs without the C++ standard library, and a function-local static
/// would want a guard variable from it as well.
void (**ModuleFinalisers)(void) = nullptr;
std::size_t ModuleFinaliserCount = 0;
std::size_t ModuleFinaliserCap   = 0;

/// Heap blocks a by-value conformant-array parameter (ISO §6.6.3.3) was
/// copied into, across every activation on the call stack that has not yet
/// given its own back -- LIFO, in the order the copies were actually made,
/// not in C-stack order, which is what lets one mechanism serve both a
/// normal return and a non-local goto (§6.8.1) that unwinds past several
/// activations at once with a single _longjmp.  Codegen::Impl::emitFunctionDef
/// marks the top of this before copying its own parameters and unwinds back
/// to that mark where its body ends; LabelGotoEngine::emitLabelLanding marks
/// it (after that same prologue) right before its _setjmp, and
/// LabelGotoEngine::closeLabelScope unwinds back to THAT mark on every edge a
/// longjmp can land on -- the one place execution goes on a non-local jump,
/// so it is the one place that is guaranteed to run regardless of how many
/// activations, or how much recursion, the goto skipped past.  A plain array
/// rather than a std::vector: see ModuleFinalisers just above.
void       **ConfArrStack = nullptr;
std::size_t  ConfArrTop   = 0;
std::size_t  ConfArrCap   = 0;

/// -std=turbo only: ParamCount/ParamStr's own backing storage (this file's
/// "-std=turbo" section, far below) -- kept here, in the file's existing
/// internal-state namespace, rather than beside the extern "C" functions
/// that read them, the same way ModuleFinalisers/ConfArrStack already are.
int    g_argc = 0;
char **g_argv = nullptr;
} // namespace

extern "C" {

/// EP §6.11.2: run every registered module finaliser, most recently
/// initialized module first (defined below, once module registration has
/// been introduced) -- forward-declared here because plang_halt, just
/// below, is the other way a program can terminate and has to reach the
/// same finalisers that falling off the end of the program block already
/// does (CodeGenProcs.cpp's emitMain).
void plang_module_finals_run(void);

/// -std=turbo only: Turbo's ErrorAddr, defined in this file's own
/// "-std=turbo" section far below -- forward-declared here for the same
/// reason plang_module_finals_run just above is: plang_halt sets this for a
/// nonzero status before that section's own definition appears in the file.
extern void *plang_tp_erroraddr;

/// Run every registered module finaliser, flush stdout, then terminate
/// (EP §6.7.5.7 \c halt).  The standard's halt takes no argument; \p Status
/// carries the common extension halt(n), and is zero for a bare halt.
///
/// A module's 'to end do' (EP §6.11.2) is specified to run as the program
/// terminates, not only when execution falls off the end of the program
/// block -- halt is the other way a Pascal program ends.  Before this called
/// in here too, halt reached std::exit directly, which no generated code
/// stands between, so every module's finalisation side effects (a closing
/// log line, a flushed handle) were silently lost whenever the program
/// halted instead of ending normally (issue #242).  plang_module_finals_run
/// pops each finaliser off its list before calling it, so one that itself
/// calls halt -- reentering this function -- still cannot run anything a
/// second time; it just drains whatever the outer call had not reached yet.
void plang_halt(int64_t Status) {
    // -std=turbo only: ErrorAddr, captured for a NONZERO status only --
    // Halt(0) is an ordinary successful exit, not a reported error.  See
    // this file's own "-std=turbo" section, far below, for plang_tp_erroraddr's
    // storage and the scope this was deliberately simplified to; see
    // plang_tp_runerror's identical use of this same builtin for why the
    // address captured here is a real, useful one (the caller's own `call
    // halt` site) and not a placeholder.  Harmless to always compute
    // regardless of dialect: this storage exists unconditionally (see its
    // own comment), and no ISO 7185/Extended Pascal program can ever read it
    // back -- Sema registers 'ErrorAddr' as a predefined identifier only
    // under -std=turbo.
    if (Status != 0)
        plang_tp_erroraddr = const_cast<void*>(__builtin_return_address(0));
    plang_module_finals_run();
    // NOT fflush(stdout): -std=turbo's own Input/Output (this file's
    // plang_input/plang_output) may have been redirected to a real file by
    // Assign/Rewrite (`Assign(Output, 'out.txt'); Rewrite(Output);`), and
    // std::exit below already flushes and closes every open C stream on
    // most platforms -- but that guarantee is exactly the assumption this
    // fixes: flushing ALL open streams here, explicitly, rather than
    // trusting std::exit's own implementation-defined cleanup to reach a
    // stream this runtime opened itself, is what makes a redirected
    // Output's buffered content reliably on disk before the process ends.
    std::fflush(nullptr);
    std::exit(static_cast<int>(Status));
}

/// EP §6.7.5.3: new(p, d1..ds) computes its size from a runtime discriminant
/// value (CodeGenSchema.cpp's emitNewSchema), and only the string-schema
/// capacity path is checked before this call -- every other size computed in
/// schemaBodySize flows straight into plang_new.  A negative size becomes a
/// huge size_t at the cast below, so this is the last line of defense
/// against a corrupted allocation, not a redundant check.
[[noreturn]] void plang_err_bad_alloc_size(int64_t Requested);

/// Allocate \p Bytes zero-initialized bytes.  Aborts on out-of-memory so
/// generated code never needs to check the result.
void *plang_new(int64_t Bytes) {
    if (Bytes < 0) plang_err_bad_alloc_size(Bytes);
    void *P = std::calloc(1, static_cast<std::size_t>(Bytes));
    if (!P) {
        std::fflush(stdout);
        std::fprintf(stderr, "plang runtime: out of memory\n");
        std::exit(PlangRuntimeErrorStatus);
    }
    return P;
}

/// Release a pointer previously obtained from plang_new (Pascal \c dispose).
void plang_dispose(void *P) {
    std::free(P);
}

// ---- ISO §6.6.3.3 / §6.8.1: value-conformant-array copies vs. non-local
// goto ----
//
// See ConfArrStack's own comment above for the shape of the problem these
// three answer together; CodeGenProcs.cpp and LabelGotoEngine.cpp are the
// two call sites.

/// The stack's current depth, to hand back to plang_confarr_unwind later.
int64_t plang_confarr_mark(void) {
    return static_cast<int64_t>(ConfArrTop);
}

/// Record \p P -- a block plang_new returned for a by-value conformant-array
/// parameter's copy -- so it can still be found and freed even if the
/// activation that made it is later abandoned by a non-local goto rather
/// than reached again itself.
void plang_confarr_push(void *P) {
    if (ConfArrTop == ConfArrCap) {
        std::size_t NewCap = ConfArrCap ? ConfArrCap * 2 : 16;
        auto *Grown = static_cast<void **>(
            std::realloc(ConfArrStack, NewCap * sizeof(void *)));
        if (!Grown) {
            std::fflush(stdout);
            std::fprintf(stderr, "plang runtime: out of memory\n");
            std::exit(PlangRuntimeErrorStatus);
        }
        ConfArrStack = Grown;
        ConfArrCap   = NewCap;
    }
    ConfArrStack[ConfArrTop++] = P;
}

/// Free every block pushed since \p Mark and drop them off the stack.
void plang_confarr_unwind(int64_t Mark) {
    while (ConfArrTop > static_cast<std::size_t>(Mark))
        std::free(ConfArrStack[--ConfArrTop]);
}

// ---- EP §6.11.2: module finalization order ----
//
// A module's 'to end do' runs after that of every module it imports, which is
// the reverse of the order they were initialized in.  A module initialiser
// registers its finaliser here once it has run, so the order falls out of what
// actually happened rather than having to be worked out ahead of time — which
// the program could not do anyway, since it cannot see what a separately
// compiled module imports.

/// Register \p Fn to run at finalization.  Called at the end of a module
/// initialiser, so registration order is initialization order.
void plang_module_final_push(void (*Fn)(void)) {
    if (ModuleFinaliserCount == ModuleFinaliserCap) {
        std::size_t NewCap = ModuleFinaliserCap ? ModuleFinaliserCap * 2 : 8;
        auto *Grown = static_cast<void (**)(void)>(
            std::realloc(ModuleFinalisers, NewCap * sizeof(void (*)(void))));
        if (!Grown) {
            // issue #301: this used to be std::abort() -- the ONLY one of
            // this file's own OOM/error paths that did not already follow
            // the fflush/report/exit(70) convention every other one here
            // uses (ConfArrStack's own OOM branch just above being the
            // closest example).  exit() is strictly more robust than abort()
            // for a condition like this one: it flushes every open C
            // stream, not just the stdout this already flushed by hand, and
            // still runs any registered atexit handler, where abort() does
            // neither -- and there is no corresponding benefit to keeping
            // abort() here, since an allocation failure in the module-
            // finaliser registry is not an internal-invariant violation a
            // core dump would help debug, just an ordinary resource
            // exhaustion this file already reports the same way everywhere
            // else.
            std::fflush(stdout);
            std::fputs("plang runtime: out of memory\n", stderr);
            std::exit(PlangRuntimeErrorStatus);
        }
        ModuleFinalisers   = Grown;
        ModuleFinaliserCap = NewCap;
    }
    ModuleFinalisers[ModuleFinaliserCount++] = Fn;
}

/// Run every registered finaliser, most recently initialized module first.
void plang_module_finals_run(void) {
    // Each is taken off before it is called, so a finaliser that reaches this
    // again — through halt, say — cannot run anything a second time.
    while (ModuleFinaliserCount > 0)
        ModuleFinalisers[--ModuleFinaliserCount]();
}

// ---- runtime error reporting ----
//
// ISO 7185 classifies each of these as an error.  Generated code performs the
// test inline and only calls in once it has already failed, so these are cold
// and never return.  stdout is flushed first: the diagnostic is useless if it
// appears before the output that led up to it.

/// ISO §6.7.2.2: the divisor of div/mod shall not be zero.
void plang_err_div_zero(const char *Op) {
    std::fflush(stdout);
    std::fprintf(stderr, "plang runtime: %s by zero\n", Op ? Op : "division");
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.7.2.2: the divisor of mod shall be positive.  Zero is reported by
/// plang_err_div_zero; this covers the negative case, for which the standard
/// defines no result rather than defining a negative one.
void plang_err_mod_divisor(int64_t D) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: mod by a non-positive divisor %" PRId64 "\n", D);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.7.2.2: div is defined for a nonzero divisor, but minint (-2^63) div
/// -1 is the one nonzero-divisor case that still has no answer -- like
/// abs(minint) (plang_err_abs_overflow), the mathematical result (+2^63) has
/// no positive int64_t representation.  Left alone this is signed-overflow
/// UB that, in practice, either raises SIGFPE in hardware (x86 idiv traps on
/// overflow the same way it traps on a zero divisor) or is folded away
/// entirely by the optimizer, depending on optimization level; like
/// plang_err_abs_overflow and plang_err_ipow_zero_zero, this fires for
/// exactly one fixed pair of operands, so it takes no argument.
[[noreturn]] void plang_err_div_overflow(void) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: -9223372036854775808 div -1 has no "
                 "representable result\n");
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.9.2.2: the length of a string value assigned to a string variable
/// shall not exceed the variable's capacity.
[[noreturn]] void plang_err_str_capacity(int64_t Len, int64_t Cap) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: string of length %" PRId64
                 " assigned to a string(%" PRId64 ")\n", Len, Cap);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.8.3.2: for 'pow' with an integer base the exponent shall not be
/// negative, since the result has to be an integer and a reciprocal is not.
[[noreturn]] void plang_err_ipow_negative(int64_t E) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: pow with a negative exponent %" PRId64
                 " and an integer base\n", E);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.8.3.2: "a factor of the form x pow y shall be an error if x is zero
/// and y is less than or equal to zero" -- 0 pow (negative) is already
/// caught by plang_err_ipow_negative above (EP's blanket rule for a negative
/// integer exponent), but 0 pow 0 is not negative and isoPow's own loop
/// answers 1 for it, silently, for the one shape the standard singles out as
/// undefined even before asking what x**0 usually is.
[[noreturn]] void plang_err_ipow_zero_zero(void) {
    std::fflush(stdout);
    std::fprintf(stderr, "plang runtime: 0 pow 0 is undefined\n");
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.8.3.2: an integer base keeps an integer result, but int64_t cannot
/// represent every such result -- like minint div -1 (plang_err_div_overflow)
/// and abs(minint) (plang_err_abs_overflow), an out-of-range result is
/// signed-overflow UB left uncaught, and in practice wraps to a silently
/// wrong value instead of the error the language expects here.
[[noreturn]] void plang_err_ipow_overflow(int64_t Base, int64_t Exp) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: %" PRId64 " pow %" PRId64
                 " has no representable result\n", Base, Exp);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.8.3.2: "a factor of the form x**y shall be an error if x is zero
/// and y is less than or equal to zero"; and, separately, for a real or
/// integer (non-complex) base, "an error if x is negative" -- x**y is
/// defined by exp(y*ln(x)), and ln has no real value for x <= 0.  Neither
/// was checked, so plang's libm-backed '**' silently answered these instead
/// of raising the error the standard requires: (-2.0)**2.0 by way of
/// std::pow's own extension for an integral exponent, 0.0**0.0 by way of
/// std::pow's C99 convention that any**0 is 1.
[[noreturn]] void plang_err_pow_domain(double Base, double Exp) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: %g ** %g is undefined (the base must be "
                 "positive, or zero with a positive exponent)\n", Base, Exp);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.8.3.2's "an error if x is zero and y is less than or equal to
/// zero" is shared by a complex base -- complex values are not ordered, so
/// there is no "negative base" case to add alongside it the way plain '**'
/// has above, only the zero-base one, checked against the exponent's real
/// part.  CGBinaryOps.cpp's complex '**'/pow dispatch skipped this guard
/// entirely, so a zero complex base with a non-positive-real-part exponent
/// silently rode std::pow down to Inf/NaN instead of trapping.
[[noreturn]] void plang_err_cpow_domain(double ARe, double AIm, double BRe, double BIm) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: (%g,%g) ** (%g,%g) is undefined (a zero "
                 "base requires a positive-real-part exponent)\n",
                 ARe, AIm, BRe, BIm);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.6.6.3: round/trunc convert a real value to an integer, but not
/// every real has a representable int64_t counterpart -- round(1e30) has no
/// integer answer at all.  static_cast<int64_t> of an out-of-range double is
/// undefined behaviour, and in practice (x86-64 cvttsd2si) silently produces
/// INT64_MIN for every such input, indistinguishable from the one real
/// integer value that actually converts to it.  plang_math.cpp's
/// plang_trunc/plang_round check the range before the cast and call in here
/// instead of letting that sentinel escape as if it were a real answer.
[[noreturn]] void plang_err_real_to_int_range(const char *Op, double X) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: %s(%g) has no representable integer result\n",
                 Op, X);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.6.6.2: sqrt(x) is defined only for x >= 0 -- std::sqrt answers a
/// silent NaN for a negative argument instead, which (like the pow domain
/// errors just above) would otherwise flow into program output undetected
/// rather than being reported the way the standard's "error" calls for.
[[noreturn]] void plang_err_sqrt_domain(double X) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: sqrt(%g) is undefined (the argument must be "
                 "non-negative)\n", X);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.6.6.2: ln(x) is defined only for x > 0 -- std::log silently answers
/// NaN for a negative argument and -inf at exactly zero, the same
/// undetected-domain-error gap plang_err_sqrt_domain closes for sqrt.
[[noreturn]] void plang_err_ln_domain(double X) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: ln(%g) is undefined (the argument must be "
                 "positive)\n", X);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.6.2: arg(z) is z's phase angle, atan2(im, re) -- undefined at the
/// origin, where every angle is equally valid.  Like plang_err_ipow_zero_zero,
/// this fires for exactly one fixed input, so it takes no argument.
[[noreturn]] void plang_err_arg_domain(void) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: arg(0,0) is undefined (the origin has no "
                 "phase angle)\n");
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.5.4: the identifying value of a pointer variable being dereferenced
/// shall not be nil.
[[noreturn]] void plang_err_nil_deref(void) {
    std::fflush(stdout);
    std::fprintf(stderr, "plang runtime: dereference of nil\n");
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.5.6: "It shall be a dynamic-violation if the variable is already
/// bound to an external entity" -- unlike most of the conditions this file
/// reports, the standard names this one a dynamic-violation outright, not
/// the weaker "error" a processor may leave undetected.
[[noreturn]] void plang_err_bind_already_bound(void) {
    std::fflush(stdout);
    std::fprintf(stderr, "plang runtime: bind of a file that is already bound\n");
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.5.6: the binding table is a small fixed array (plang_file.cpp),
/// not an ISO/EP-mandated limit -- reported deterministically rather than
/// silently dropping the binding once every slot is in use.
[[noreturn]] void plang_err_binding_table_full(void) {
    std::fflush(stdout);
    std::fprintf(stderr, "plang runtime: too many bound files (limit 64)\n");
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.5.4: substr's result must lie wholly within the source string.
[[noreturn]] void plang_err_substr(int64_t I, int64_t N, int64_t Len) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: substr(s, %" PRId64 ", %" PRId64
                 ") is outside a string of length %" PRId64 "\n", I, N, Len);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.5.6: a substring-variable has a fixed string type, so the value
/// assigned to it must have exactly as many characters as it holds.
[[noreturn]] void plang_err_substr_assign(int64_t Len, int64_t N) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: a string of length %" PRId64
                 " was assigned to a substring of length %" PRId64 "\n", Len, N);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.8.3.5: a case index matching no label is an error.
void plang_err_no_case(int64_t V) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: case value %" PRId64 " matches no label\n", V);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.4.3.2: an array index outside the index type's range is an error.
void plang_err_index(int64_t V, int64_t Lo, int64_t Hi) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: array index %" PRId64
                 " out of bounds %" PRId64 "..%" PRId64 "\n", V, Lo, Hi);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.5.3.2: a string component is selected by an index in 1..length(s),
/// so an EP VarStr caller passes Lo=1 and the upper bound reported is the
/// string's length, not its capacity.  A Turbo ShortString caller (issue
/// #643) passes Lo=0 instead -- s[0] is the legal length-byte alias
/// (CGIndexAccess.cpp's ExprIsShortStr arm), so the message's stated lower
/// bound has to say 0, not the EP-only 1, or a genuinely out-of-range
/// ShortString index (e.g. s[-1] or s[cap+1]) reports a range that
/// excludes an index the SAME error's own dialect actually allows.
void plang_err_str_index(int64_t V, int64_t Lo, int64_t Hi) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: string index %" PRId64
                 " out of bounds %" PRId64 "..%" PRId64 "\n", V, Lo, Hi);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.4.2.4: assigning outside a subrange's bounds is an error.
void plang_err_range(int64_t V, int64_t Lo, int64_t Hi) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: value %" PRId64
                 " out of range %" PRId64 "..%" PRId64 "\n", V, Lo, Hi);
    std::exit(PlangRuntimeErrorStatus);
}

/// §6.4.3.2: a string-type assignment copies a fixed number of characters, so
/// the value has to have exactly that many.  Sema settles it when it knows the
/// capacity; it cannot when a discriminant fixes one, and copying the array's
/// length out of a shorter string read past the end of the allocation.
void plang_err_str_length(int64_t Got, int64_t Want) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: a string of length %" PRId64 " cannot fill a "
                 "%" PRId64 "-character string-type\n", Got, Want);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.5.3: new(p, d1..ds) takes the discriminants as expressions, so a
/// value that cannot describe any member of the family is only detectable here.
/// An extent of zero or less would size the allocation from nonsense, and every
/// access to the object afterwards would be outside it.
void plang_err_schema_extent(const char *Name, int64_t Got) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: schema discriminant %s is %" PRId64
                 ", which is not a usable extent\n",
                 Name ? Name : "?", Got);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.3.2: schematic values are only assignment-compatible when they were
/// produced from the schema with the same discriminant tuple.
void plang_err_schema_disc(const char *Name, int64_t Dst, int64_t Src) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: schema discriminant %s differs between the "
                 "target (%" PRId64 ") and the value (%" PRId64 ")\n",
                 Name ? Name : "?", Dst, Src);
    std::exit(PlangRuntimeErrorStatus);
}

/// See the forward declaration above plang_new: the last check before a
/// runtime-computed size reaches calloc, since not every size-computing path
/// upstream of it is guarded.
[[noreturn]] void plang_err_bad_alloc_size(int64_t Requested) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: allocation size %" PRId64 " is not usable\n",
                 Requested);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.6.6.2: abs(x) is x's magnitude, and the one int64_t value this
/// cannot be computed for is minint -- like plang_err_ipow_zero_zero, this
/// fires for exactly one fixed input, so unlike most of the checks above it
/// takes no argument: plang_abs_int (plang_math.cpp) already knows which
/// value it is before it calls in.
[[noreturn]] void plang_err_abs_overflow(void) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: abs(-9223372036854775808) has no "
                 "representable positive result\n");
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.6.6.2: sqr(x) = x*x keeps an integer result for an integer
/// argument, but -- like minint div -1 (plang_err_div_overflow), abs(minint)
/// (plang_err_abs_overflow), and an out-of-range pow (plang_err_ipow_overflow)
/// -- not every such result fits int64_t.  Unlike those three, any X with
/// |X| > 2^31 (roughly) can overflow here, not just one fixed operand pair,
/// so this reports which X it was rather than taking no argument.
[[noreturn]] void plang_err_sqr_overflow(int64_t X) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: sqr(%" PRId64 ") has no representable result\n",
                 X);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.9.3.1 / EP §6.10.3.1: a write TotalWidth is a plain integer
/// expression, so a program can compute one wider than the `int` printf's
/// `%*d`/`%*c` take -- plang_io.cpp's and plang_file.cpp's checkedWidth call
/// in here rather than let a truncating cast silently reinterpret an
/// oversized width as an unrelated, possibly huge one (issue #15).
[[noreturn]] void plang_err_field_width(int64_t W) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: write field width %" PRId64
                 " is too large\n", W);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.7.5.6 / EP §6.7.5.6: it is a dynamic-violation to apply Put (write)
/// to a file not currently generated (opened by rewrite/extend/update, or
/// left positioned that way by seekwrite/seekupdate) or to apply Get (read)
/// to one not currently inspected (opened by reset/extend/update, or left
/// positioned that way by seekread/seekupdate).  The C stream underneath
/// already refuses the mismatched operation -- fwrite/fprintf/fputc/fputs
/// return a failure and fread/fscanf/fgetc set the stream's error indicator
/// -- so plang_file.cpp's readers and writers check for that rather than
/// carry on as though a discarded write or an unfilled read had succeeded
/// (issue #124).
[[noreturn]] void plang_err_file_wrong_mode(const char *Op) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: %s: file is not open in the required mode\n",
                 Op);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.5.6: reset/rewrite/extend/update failing to open the external
/// file they were given (missing file, permission denied, an internal
/// temporary that could not even be created, ...) is a dynamic-violation
/// exactly like the file-wrong-mode check just above -- not an
/// internal-invariant abort()/SIGABRT.  \p Msg is the diagnostic detail
/// plang_file.cpp's caller has already worded for its own open attempt; this
/// only supplies the shared "flush stdout, report, exit" convention every
/// other runtime check here follows (issue #150).
///
/// Msg carries Name -- the external filename plang_file.cpp's reset/rewrite/
/// extend/update was given -- verbatim (see e.g. its "cannot open '%s' for
/// reading" callers).  That filename is a Pascal string *value*, not source
/// text: a program can build it however it likes, including with chr(27) or
/// any other control byte, and it reaches here unexamined.  Printed as-is,
/// such a byte would steer whatever terminal or log is reading plang's
/// stderr -- the same terminal/log-injection hole issue #281 closed for the
/// compiler's OWN diagnostics (a source filename from argv, a locale tag,
/// the -v/-### echo) via escapeControlChars (include/plang/Basic/
/// StringUtil.h). That helper returns a std::string and lives under
/// include/plang/Basic/, which the runtime cannot use: the runtime is linked
/// into generated Pascal programs (see ModuleFinalisers's comment above),
/// and that link carries no C++ standard library (Driver.cpp's link line
/// pulls in only -lm/-lgcc/-lgcc_s/-lc, never -lstdc++), so std::string's
/// operator new/exception machinery would leave every compiled program with
/// unresolved symbols. escapeCC below is a freestanding equivalent -- same
/// threshold, same \xHH escape -- written directly against fputc so it needs
/// no dynamic buffer at all (issue #420, the runtime-side twin of #281).
static void escapeCC(const char *S, std::FILE *Stream) {
    static const char Hex[] = "0123456789abcdef";
    if (!S) return;
    for (const unsigned char *P = reinterpret_cast<const unsigned char *>(S);
         *P; ++P) {
        const unsigned char C = *P;
        if (C < 0x20 || C == 0x7F) {
            std::fputc('\\', Stream);
            std::fputc('x', Stream);
            std::fputc(Hex[C >> 4], Stream);
            std::fputc(Hex[C & 0xF], Stream);
        } else {
            std::fputc(static_cast<char>(C), Stream);
        }
    }
}

[[noreturn]] void plang_err_cannot_open(const char *Msg) {
    std::fflush(stdout);
    std::fputs("plang runtime: ", stderr);
    escapeCC(Msg, stderr);
    std::fputc('\n', stderr);
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.5.2: SeekRead/SeekWrite/SeekUpdate reposition f to component n,
/// measured from the index type's smallest value, but n itself is never
/// range-checked against the file's extent before the seek is attempted --
/// so a value the C library cannot honor (behind the index type's origin,
/// most directly, which computes a negative byte offset) reaches fseek.
/// Unlike a mismatched read/write, which trapOnStreamError catches because
/// the failed C call leaves its own error indicator set, a failed fseek
/// leaves the stream positioned exactly where it already was -- so ignoring
/// the failure does not just skip the seek, it silently redirects whatever
/// read or write comes next onto that unrelated, previously-current
/// component instead. This traps it as the dynamic-violation EP's own
/// pre-assertion calls for, rather than letting the corruption through
/// (issue #233).
[[noreturn]] void plang_err_seek_failed(const char *Op, int64_t N) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: %s(%" PRId64 "): position is not reachable "
                 "in this file\n", Op, N);
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.9.1: the value read for read(f, v)/read(v) with v of an integer or
/// real type is the longest sequence of characters starting at the current
/// position that forms a value of that type -- which means a token that
/// cannot even start one (any character that is not a digit, a sign, or --
/// for a real -- a decimal point) is an error, exactly like every other
/// dynamic-violation this file reports. Before this, a malformed token
/// quietly left the destination variable unchanged: plang_io.cpp's
/// scanNumber-based reader (stdin/readstr) built an empty token and skipped
/// the assignment outright, and plang_file.cpp's fscanf-based reader got a
/// plain match failure (return 0) that nothing distinguished from success.
/// Neither reader had consumed anything either, so a loop driven by eof(f)
/// never advanced past the bad token and so never terminated (issue #236).
[[noreturn]] void plang_err_read_format(const char *Op) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: %s: input does not start with a valid "
                 "number\n", Op ? Op : "read");
    std::exit(PlangRuntimeErrorStatus);
}

/// ISO §6.9.1: the value read for read(f, v)/read(v) with v of an integer
/// type has to be assignment-compatible with it, which an arbitrarily long
/// run of digits is not once it names something outside int64_t. Before
/// this, plang_file.cpp's "%lld"-based fscanf silently clamped such a token
/// (an overflowing scanf numeric conversion is undefined behaviour in C;
/// glibc's happens to clamp to the nearest representable value) and
/// plang_io.cpp's strtoll-based reader computed the same clamped value but
/// never looked at ERANGE to notice -- so both accepted
/// "9223372036854775808" as though it were the largest representable
/// integer instead of reporting it (issue #240). \p Tok is the offending
/// text: the value itself has no int64_t representation to show instead.
[[noreturn]] void plang_err_read_int_range(const char *Op, const char *Tok) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: %s: '%s' is out of range for an integer "
                 "(%" PRId64 "..%" PRId64 ")\n",
                 Op ? Op : "read", Tok ? Tok : "", INT64_MIN, INT64_MAX);
    std::exit(PlangRuntimeErrorStatus);
}

/// TP `Assert(cond[, msg])` (Builtins.def, CGProcCall::emitCallStmt): called
/// only once cond has already tested false, and only when
/// Switch::Assertions was on at the call site -- with it off the whole call
/// compiles to nothing, so this is never even reached from one of those.
/// \p Msg is null when the one-argument form was used.  "Runtime error 227"
/// is Borland/FPC's own numbered run-time error for a failed assertion
/// (confirmed against `fpc -Mtp`, which reports exactly that number and
/// exits 227); plang keeps the number in the message, for a user who
/// recognizes it, but exits PlangRuntimeErrorStatus like every other check
/// in this file rather than 227 itself, so a plang program's exit code still
/// means one consistent thing -- "some plang runtime check failed" --
/// regardless of which one it was.
[[noreturn]] void plang_err_assert_failed(const char *Msg) {
    std::fflush(stdout);
    if (Msg && Msg[0])
        std::fprintf(stderr, "plang runtime: Runtime error 227: Assertion "
                              "failed: %s\n", Msg);
    else
        std::fprintf(stderr, "plang runtime: Runtime error 227: Assertion "
                              "failed\n");
    std::exit(PlangRuntimeErrorStatus);
}

/// EP §6.7.5.6: applying ANY file operation -- read, write, eof, the buffer
/// variable, all 40 of plang_file.cpp's ISO/EP entry points -- to a file
/// variable that is not currently open (F->Fp still null: never set until
/// reset/rewrite/extend/update succeeds) is a dynamic-violation exactly like
/// plang_err_file_wrong_mode/plang_err_cannot_open above, not an
/// internal-invariant abort()/SIGABRT.  Before issue #301, plang_file.cpp's
/// own abortIfClosed -- the single choke point every one of those 40 entry
/// points funnels through -- reported this one itself, directly, with
/// std::abort(): the one dynamic-violation in the whole runtime that had not
/// yet been brought onto the fflush/report/exit(70) convention every other
/// one here already follows.  That was a deliberate, tested call at the
/// time (ISO/EP "program error" status was read as calling for a hard
/// SIGABRT), but issue #301 concluded exit(70) is strictly more robust with
/// no offsetting benefit: exit() flushes EVERY open C stream (this call's
/// own explicit fflush(stdout) only ever covered the one stream this
/// function knows about by name) and still runs any registered atexit
/// handler, where abort() does neither -- and a SIGABRT/core dump buys
/// nothing here, since an unopened file variable is an expected, documented
/// condition a conforming program can trigger, not an internal plang
/// consistency violation a core dump would help debug.  Exported (rather
/// than open-coded in plang_file.cpp) for the same reason every other
/// plang_err_* file check already is: this constant, PlangRuntimeErrorStatus,
/// lives in this file's own anonymous namespace, and every caller outside it
/// reaches the shared "flush stdout, report, exit(70)" convention through a
/// named reporter like this one rather than re-deriving the status value.
[[noreturn]] void plang_err_file_not_open(const char *Op) {
    std::fflush(stdout);
    std::fprintf(stderr, "plang runtime: file not open in '%s'\n", Op);
    std::exit(PlangRuntimeErrorStatus);
}

/// Shared out-of-memory reporter for the runtime's own small internal
/// allocations that are not already covered by an existing plang_err_*
/// (plang_new's own OOM branch, just above in this file, reports a Pascal
/// program's \c new/dispose allocation failure directly since it already
/// has PlangRuntimeErrorStatus in scope; ConfArrStack's and
/// ModuleFinalisers's own OOM branches, further above still, do the same
/// for the same reason).  This one exists for callers OUTSIDE this
/// translation unit -- plang_file.cpp's per-component file buffer
/// (aligned_alloc) being the one today -- that have no direct access to
/// that constant.  \p Context names what could not be allocated, e.g. "a
/// file buffer", so the message stays specific despite the reporter being
/// shared.  Before issue #301 that one call site used std::abort(); the
/// reasoning for exit(PlangRuntimeErrorStatus) instead is the same as
/// plang_err_file_not_open's, just above: exit() flushes every open C
/// stream and runs atexit handlers, abort() does neither, and there is no
/// internal-invariant violation here a core dump would help debug -- just
/// ordinary resource exhaustion.
[[noreturn]] void plang_err_out_of_memory(const char *Context) {
    std::fflush(stdout);
    std::fprintf(stderr, "plang runtime: out of memory for %s\n",
                 Context ? Context : "the runtime");
    std::exit(PlangRuntimeErrorStatus);
}

// ---- -std=turbo run-time error reporting: the plang_tp_* family ----
//
// Every check above shares ONE mechanism: report through plang_err_*, exit
// PlangRuntimeErrorStatus (70) regardless of which check it was.  That is
// wrong for Turbo, where real Turbo Pascal / FPC programs exit with the
// numbered run-time error itself (200 for division by zero, 201 for a range
// check, 215 for arithmetic overflow, 216 for a nil/bad-pointer access, ...)
// and print "Runtime error <n> at $<address>" -- a script driving a
// compiled program (or a person who has used real Turbo Pascal) reads that
// exit status directly, and plang's own shared 70 would say nothing.
//
// This is a PARALLEL family, not a mode flag on the existing one.  This
// project's object files may be linked together from more than one -std=
// (see e.g. the module/program split any -std=turbo program already
// exercises), so "which dialect is this" can never be a single global the
// runtime consults -- each compiled object's own generated code must call
// whichever reporter ITS OWN -std= wants, decided once at codegen time
// (RangeCheckGuards.cpp's isTurbo()/emitTpRunError, CGProcCall.cpp's
// `runerror` arm), never asked of the runtime at the call site.  A shared
// `if (dialect == turbo)` branch inside ONE plang_err_* function would be
// exactly that global mode word, just moved into the runtime instead of
// the compiler -- and would be wrong the instant an ISO object and a Turbo
// object are linked into the same program, since the two would then be
// fighting over one answer to "which dialect is this".
//
// A single entry point, plang_tp_runerror(code), serves every caller: the
// numbered checks above (each already knows its own code -- 200/201/215/216
// -- so it passes that literal straight through) and TP's own
// RunError(code) builtin (CGProcCall.cpp) alike; RunError's no-argument form
// passes 0, Free Pascal's own empirically-confirmed default (`fpc -Mtp`:
// `RunError;` with no pending error reports "Runtime error 0" and exits 0,
// not 216 or any other check's number).
//
// The address in "at $<address>" is __builtin_return_address(0) -- read
// directly here, in the function generated code calls straight into, so it
// is the real return address on the CALLER's own frame: the instruction in
// the Pascal program immediately after the `call` that reached this check,
// a genuine hardware address rather than a placeholder.  It is an
// approximation of "where" in exactly the sense FPC's own first reported
// address is (compare this file's `fpc -Mtp` transcripts above): the return
// site, not necessarily the exact failing instruction, and with no symbol
// name or source line -- plang keeps no unwind tables or debug-info reader
// in the runtime to do better, and a full symbolizing backtrace is more
// machinery than this milestone's "don't over-engineer it" scope calls for.
[[noreturn]] void plang_tp_runerror(int64_t Code) {
    const void *Addr = __builtin_return_address(0);
    // -std=turbo only: ErrorAddr -- set BEFORE the finalizer chain below
    // runs, so a custom ExitProc hooked into that same chain
    // (plang_tp_run_exitproc's own comment, this file's "-std=turbo"
    // section) sees the right value from inside its own call: reading
    // ErrorAddr from an ExitProc is real Turbo Pascal field practice's most
    // common reason to read it at all.
    plang_tp_erroraddr = const_cast<void*>(Addr);
    // EP §6.11.2's finalizers, and -std=turbo's own ExitProc registered
    // alongside them (see plang_tp_run_exitproc's own comment for how) --
    // the same chain plang_halt and emitMain's own end-of-program path
    // already reach, so a runtime error exits through exactly the same
    // cleanup every other way this program can end already does.
    plang_module_finals_run();
    std::fflush(stdout);
    // Issue #660: Code ultimately comes from a Turbo `Integer`-width
    // (plang_tp_inoutres) or a literal a numbered check already knows is
    // in range -- both always fit an int32_t -- but under {$R-} an
    // out-of-bounds write elsewhere in the program can corrupt
    // plang_tp_inoutres into holding an arbitrary 64-bit bit pattern before
    // plang_iocheck ever reads it back through this same path.  Truncating
    // ONCE, here, and using that single truncated value for both the
    // printed message and the process's actual exit status keeps the two
    // in agreement even then -- printing "Runtime error 4294967296" while
    // the OS-visible exit code is really 0 (4294967296 truncates to 0) is
    // the "spurious error at rc=0" mismatch the issue reported; printing
    // the SAME int32 value exit() receives cannot disagree with it, however
    // corrupted Code was.
    const int32_t Status = static_cast<int32_t>(Code);
    std::fprintf(stderr, "Runtime error %" PRId32 " at $%016" PRIxPTR "\n",
                 Status, reinterpret_cast<std::uintptr_t>(Addr));
    std::exit(Status);
}

/// TP `ExitCode: Integer` (Sema::registerBuiltins, -std=turbo only) -- the
/// value emitMain (CodeGenProcs.cpp) returns to the OS when the program
/// block ends normally rather than through Halt (which takes its own exit
/// status as an argument and never reads this).  The FIRST predefined
/// PLANG identifier backed by a mutable runtime global rather than a
/// per-compilation one: a program built from several .pas files
/// (extraInputFiles) compiles each to its own object, and every one that
/// mentions ExitCode has to agree on the SAME storage -- if each object
/// defined its own `plang_tp_exitcode`, the final link would fail on a
/// duplicate symbol.  Defining it exactly once here, and having every
/// compiled Turbo object only DECLARE it (Codegen::Impl::
/// emitPredefinedGlobals, CodeGenProcs.cpp -- an LLVM GlobalVariable built
/// with no initializer), gives the whole program one shared answer no
/// matter how many objects it is linked from.  int16_t, not int64_t: it has
/// to match the LLVM type codegen declares this under exactly (Turbo's
/// Integer is always 16 bits -- LangOptions::defaultIntWidth() -- and
/// ExitCode is only ever registered under Turbo), so both sides agree on
/// how many bytes this occupies without either one guessing at the other's
/// layout.  A later Tier 3 (FileMode/RandSeed/DosError/TextAttr) is
/// expected to reuse this exact mechanism rather than invent another one.
int16_t plang_tp_exitcode = 0;

/// TP `RandSeed: LongInt` (Sema::registerBuiltins, -std=turbo only) -- the
/// current internal state of plang's own pseudo-random generator (see
/// plang_math.cpp's plang_tp_random_real/plang_tp_random_range, which are
/// its only readers/writers besides an explicit program assignment like
/// `RandSeed := 1;`).  Registered, shared, and declared-not-defined by every
/// compiled object exactly the way plang_tp_exitcode is just above -- see
/// that variable's own comment for the whole mechanism, reused rather than
/// reinvented (its own "later Tier 3" remark names RandSeed as the next one
/// to follow it).  uint32_t, not int16_t like ExitCode: real Turbo Pascal's
/// own RandSeed is a LongInt (32 bits), fixed regardless of Integer's own
/// dialect width, so Sema::registerBuiltins gives its Symbol
/// Ctx_.getInt(32, /*Signed=*/true) rather than TyInt, and
/// Codegen::Impl::emitPredefinedGlobals declares a fixed i32 rather than
/// langOpts.defaultIntWidth() -- both sides have to agree on 32 bits the
/// same way ExitCode's two sides agree on Integer's own width.  Unsigned
/// here purely because the generator's own update (a linear-congruential
/// step, plang_math.cpp) is naturally unsigned modular arithmetic; a plang
/// program still reads and assigns it as a signed LongInt, which is only a
/// difference in how the SAME 32 bits are interpreted, not a second,
/// disagreeing width.
uint32_t plang_tp_randseed = 0;

// ---- -std=turbo only: GetMem/FreeMem, HeapError, ExitProc, ErrorAddr,
// ParamCount/ParamStr ----
//
// See plang_tp_exitcode's own comment just above for why every mutable
// predefined-identifier global in this section is defined exactly once
// here and only ever DECLARED by compiled Turbo objects (Codegen::Impl::
// emitPredefinedGlobals, CodeGenProcs.cpp).

/// A settable procedural VALUE (Tier 2's procedural types/values --
/// ClosureAndCallABI.cpp/VarEntry.h's isProcVar substrate, reused here
/// rather than inventing a second function-pointer mechanism; see
/// Sema::registerBuiltins' own comment) the program may assign its own
/// allocation-failure handler to.  nullptr means "no handler installed".
/// See plang_tp_getmem's own comment for the exact contract this is called
/// under, and for why its signature is Int64 -> Int64 rather than real
/// Borland's Word -> Integer.
void *plang_tp_heaperror = nullptr;

/// The settable procedural-value predefined variable (`procedure;`, no
/// arguments, no result) real Turbo Pascal calls as part of its own exit
/// sequence.  See plang_tp_run_exitproc, just below, for how this is hooked
/// into the ALREADY-WORKING plang_module_finals_run chain (issue #242)
/// rather than a second, separately-invoked mechanism.
void *plang_tp_exitproc = nullptr;

/// Turbo's ErrorAddr -- the address the most recent runtime error occurred
/// at, or nullptr (0) if there has not been one yet.  DELIBERATELY
/// SIMPLIFIED, and documented as such rather than either skipped or fully
/// built out: an exact address at EVERY runtime-fault call site would need
/// __builtin_return_address threaded through every plang_err_*/plang_tp_*
/// reporter in this file, a much larger change for a predefined variable
/// real Turbo programs mostly read from inside a custom ExitProc/error
/// handler rather than compare bit-for-bit.  Set at exactly the two places a
/// plang Turbo program's own control flow reports a genuine fault: this
/// function (plang_tp_runerror, both TP's own RunError and every numbered
/// range/overflow/... check that routes through it) and plang_halt for a
/// NONZERO status (an ordinary halt(0) is a successful exit, not an error).
/// Any OTHER runtime fault (e.g. a plang_err_* ISO/EP check, which a Turbo
/// program cannot reach in the first place -- RangeCheckGuards.cpp routes
/// Turbo's own checks through plang_tp_runerror instead, never the
/// plang_err_* family) leaves this untouched.
void *plang_tp_erroraddr = nullptr;

/// Runs ExitProc, if the program has assigned one, and clears it first --
/// the same "pop before call" rule plang_module_finals_run's own loop
/// already follows, so a handler that itself triggers another exit path
/// (calling Halt from inside its own ExitProc, say) cannot run twice or
/// recurse forever.  Codegen::Impl::emitMain registers this, via
/// plang_module_final_push, exactly ONCE, near the very start of a Turbo
/// program's main -- after that one registration, every existing call to
/// plang_module_finals_run (plang_halt above, plang_tp_runerror above, and
/// emitMain's own end-of-program call) already reaches this automatically,
/// with no separate "and also run ExitProc" step needed at any of the three.
///
/// Issue #595: real Turbo Pascal's exit sequence CHAINS -- a handler is
/// free to assign a NEW ExitProc from inside itself (a documented idiom:
/// each handler saves the previous value and installs its own, so several
/// independent units' cleanup routines compose without any one of them
/// knowing about the others ahead of time), and the exit sequence keeps
/// consuming and calling whatever ExitProc currently holds until it reads
/// nil -- confirmed against `fpc -Mtp`.  A single "pop, then call once" (the
/// former body of this function) runs the FIRST handler but never notices a
/// second one it installs before returning.  Looping here reproduces the
/// chain while preserving the exact same "pop before call" no-recursion
/// property on every iteration: each handler is cleared from
/// plang_tp_exitproc before it runs, so one that reassigns ExitProc to
/// itself (or recurses back into a Halt that reaches this function again)
/// still cannot run the SAME handler twice from the two different call
/// frames -- only a genuinely new value written after the clear is ever
/// picked up, by this loop's own next iteration.
void plang_tp_run_exitproc(void) {
    while (plang_tp_exitproc) {
        auto Fn = reinterpret_cast<void (*)(void)>(plang_tp_exitproc);
        plang_tp_exitproc = nullptr;
        Fn();
    }
}

/// GetMem(var P: Pointer; Size: Int64) / FreeMem(P: Pointer[, Size: Int64])
/// (Builtins.def, CGProcCall.cpp) -- a wholly separate pair of runtime entry
/// points from plang_new/plang_dispose, not a flag added to them: ISO 7185/
/// Extended Pascal code must keep aborting the process unconditionally on
/// out-of-memory (plang_new), since ordinary ISO-generated code never checks
/// a `new` result at all, and this project's object files compiled under
/// different -std= may be linked into one program -- so "abort on OOM or
/// not" can never be a runtime-consulted flag (the identical reasoning the
/// "-std=turbo run-time error reporting" section above already establishes
/// for plang_tp_runerror vs the plang_err_* family).
///
/// Size is Int64, not real Borland Turbo Pascal 7's 16-bit Word: confirmed
/// against a local `fpc -Mtp` that modern field practice already widened
/// this (its own rtl/inc/heap.inc declares GetMem's Size as a PtrUInt, far
/// past 65535, even under `-Mtp`) -- HeapError's own Size parameter, below,
/// is kept the same width for the same reason a mismatched pair would be
/// worse than either alone: a handler that cannot see the value GetMem was
/// actually asked for is strictly less useful than one that can.
///
/// Failure contract -- deliberately simplified from real Borland TP7, and
/// documented here rather than silently guessed at: the local `fpc`
/// install's own rtl/inc sources have NO HeapError at all (grepped
/// systemh.inc/heap.inc directly; modern FPC replaced it outright with a
/// TMemoryManager/SetMemoryManager architecture, confirmed by a failed
/// compile of a HeapError-using program under `fpc -Mtp`: "Identifier not
/// found"), so this follows classic Borland TP7 documentation instead, with
/// one deliberate divergence called out below.
///   - No HeapError installed: returns nil, and NEVER aborts.  This is the
///     one deliberate divergence from real Borland (whose actual default is
///     to halt with Runtime error 203) -- chosen so a plang Turbo program
///     can check GetMem's result for nil without installing a handler at
///     all, which is this whole pair's reason for existing as a separate,
///     non-aborting entry point from plang_new in the first place.
///   - HeapError installed, returns 1: returns nil (matches real Borland).
///   - HeapError installed, returns anything else (0, 2, ...): reports
///     Runtime error 203, the numbered "out of memory" error, the same way
///     an actual numbered range/overflow check does (plang_tp_runerror).
///     Real Borland's 0 ("not handled") and 2 ("retry after growing the
///     heap") both collapse to this one outcome: this heap IS the OS
///     allocator, with no separate arena for 2's "retry" to mean anything
///     about.
void *plang_tp_getmem(int64_t Size) {
    // A negative size is a caller bug, not a real allocation failure --
    // HeapError is not consulted for it, matching this whole pair's job of
    // never aborting the process either way.
    if (Size < 0) return nullptr;
    void *P = std::malloc(Size > 0 ? static_cast<std::size_t>(Size) : 1);
    if (P) return P;

    // Called as a raw C function pointer directly from this runtime, NOT
    // through codegen-emitted LLVM IR the way an ordinary procedural-value
    // call (ClosureAndCallABI::emitProcVarCall) is -- which is exactly why
    // HeapError's own signature (Sema::registerBuiltins) is Int64 -> Int64
    // rather than Borland's Word -> Integer: the x86-64 SysV ABI leaves the
    // upper bits of a sub-32-bit return register UNSPECIFIED for the callee
    // to fill in, so reinterpreting this call through a narrower return
    // type could read garbage above the low bits and compare unequal to 1
    // even when the compiled Pascal function really did return it. A full
    // 64-bit return has no such partial-register hazard.
    auto HeapErrorFn = reinterpret_cast<int64_t (*)(int64_t)>(plang_tp_heaperror);
    if (!HeapErrorFn) return nullptr;
    if (HeapErrorFn(Size) == 1) return nullptr;
    plang_tp_runerror(203);
}

/// See plang_tp_getmem's own comment.  Size is accepted -- and, like real
/// Borland/FPC, never checked against what the matching GetMem was actually
/// given -- but otherwise unused: std::free needs no size.  P is a plain
/// value argument, not `var`: confirmed against the local `fpc -Mtp`
/// install's own rtl/inc/heap.inc, whose FreeMem takes `p:pointer` with no
/// `var`, unlike GetMem's own out-parameter P.
void plang_tp_freemem(void *P, int64_t Size) {
    (void)Size;
    std::free(P);
}

/// Codegen::Impl::emitMain (CodeGenProcs.cpp) calls this as the FIRST
/// instruction of every compiled program's own C `main` -- ISO 7185/
/// Extended Pascal/Turbo alike, unconditionally (see that call site's own
/// comment for why the C main signature change and this call are not gated
/// on -std=turbo the way everything else in this section is) -- storing
/// argc/argv in g_argc/g_argv (this file's own top anonymous namespace) for
/// plang_tp_paramcount/plang_tp_paramstr, just below, to read back later,
/// however deep into the program either is actually asked for.
void plang_set_args(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
}

/// ParamCount: the number of command-line arguments, not counting argv[0]
/// itself -- confirmed against `fpc -Mtp`: ParamStr(0) is the running
/// program's own path, and ParamCount does not count it.
int64_t plang_tp_paramcount(void) {
    return g_argc > 0 ? static_cast<int64_t>(g_argc - 1) : 0;
}

/// plang_sstr.cpp's own ShortString constructor from a C string --
/// forward-declared the same way plang_tp_runerror already is in
/// plang_io.cpp/plang_file.cpp, since ParamStr's whole job just below is
/// filling one in from argv's own C strings.
void plang_sstr_from_cstr(void *dst, int64_t cap, const char *src);

/// ParamStr(n): argv[n] as a capacity-255 ShortString (Copy/Concat/
/// StringOfChar's own result capacity -- Builtins.def's own comment on why),
/// or an empty string for n outside 0..ParamCount -- confirmed against
/// `fpc -Mtp`: an out-of-range index is not an error, just an empty result.
void plang_tp_paramstr(int64_t N, void *Dst, int64_t DstCap) {
    const char *S = (N >= 0 && N < g_argc) ? g_argv[N] : "";
    plang_sstr_from_cstr(Dst, DstCap, S);
}

/// TP `FileMode: Byte` (Sema::registerBuiltins' FileMode Symbol, -std=turbo
/// only) -- see that comment for the whole design; this reuses
/// plang_tp_exitcode's mechanism exactly (one shared definition here, every
/// compiled Turbo object only declares it -- Codegen::Impl::
/// emitPredefinedGlobals, CodeGenProcs.cpp).  int16_t for the identical
/// "matches the LLVM type codegen declares this under" reason
/// plang_tp_exitcode's own comment gives (defaultIntWidth() under Turbo is
/// always 16 bits).  Defaults to 2, confirmed against `fpc -Mtp` (a fresh
/// program's own FileMode reads 2 before anything touches it).  Nothing in
/// this compiler reads FileMode back yet to change how Reset opens a file --
/// a later item is expected to.
int16_t plang_tp_filemode = 2;

/// TP `InOutRes: Integer` (Sema::registerBuiltins' InOutRes Symbol,
/// -std=turbo only) -- the hidden global every I/O-performing runtime call is
/// supposed to set on failure instead of aborting the process (this file's
/// own plang_tp_getmem/plang_tp_freemem already established that
/// "non-aborting, Turbo-only" pattern for allocation failure; runtime/
/// plang_file.cpp's tpFileReady, and the `_turbo`-suffixed entry-point
/// siblings it backs, are what do it for file I/O).  Zero means "no error
/// pending" -- Reset/Rewrite/Append/... clear nothing on SUCCESS (matching
/// real Borland/FPC field practice: InOutRes is not reset to 0 by a
/// successful call, only by IOResult itself reading it), so a program that
/// never triggers a failure never sees this become anything but its
/// zero-initialized default.  Declared, shared, and defined exactly once
/// here the same way plang_tp_exitcode/plang_tp_randseed/plang_tp_filemode
/// just above already are -- see plang_tp_exitcode's own comment for the
/// whole "one definition, every compiled Turbo object only declares"
/// mechanism (Sema::registerBuiltins' InOutRes Symbol, CodeGenProcs.cpp's
/// emitPredefinedGlobals) -- reused here rather than reinvented, even
/// though nothing this item ships actually emits Pascal-level IR that reads
/// this global directly (only through plang_tp_ioresult, just below, and
/// runtime/plang_file.cpp's own tpFileReady/plang_eof_file_turbo/
/// plang_eoln_file_turbo): a later {$I+}/InOutRes item needs to read the
/// PENDING value WITHOUT clearing it (exactly what IOResult itself must
/// not do), which a direct load of this global -- already wired as an
/// LLVM-visible predefined identifier by that shared mechanism -- is the
/// natural way to give it, rather than a second non-clearing runtime
/// function invented ahead of the one caller that would actually use it.
///
/// int64_t, DELIBERATELY NOT Borland's 16-bit Word (or even ExitCode's own
/// dialect-width int16_t) -- called out here with the same prominence
/// ExitCode's own width comment gets, so this does not get "corrected" back
/// to 16 bits by analogy with ExitCode/FileMode later.  Two independent
/// reasons, not one: first, InOutRes is read back through IOResult, a
/// Func registered R_Int (Builtins.def) as Turbo's ordinary 16-bit Integer,
/// the same as every other Turbo Func's result -- narrowing THIS storage to
/// 16 bits would buy nothing IOResult's own return-type coercion does not
/// already provide for free, while WIDENING it costs nothing either way.
/// Second, and more binding: plang_tp_posix_to_run_error (runtime/
/// plang_file.cpp) deliberately passes an unmapped errno straight through
/// unchanged (matching real FPC's own `else` fallback) rather than reducing
/// it mod 65536 -- a 16-bit InOutRes could silently wrap an unusual but
/// legitimate large errno into an unrelated, misleadingly-small code, where
/// this width just keeps whatever plang_tp_posix_to_run_error actually
/// computed intact.  Zero-initialized: the language default for a global
/// with no explicit initializer already matches InOutRes's own "no error
/// pending" zero value, so this needs no `= 0` to say so, but see this
/// declaration's own top comment for why zero is not merely a convenient
/// bit pattern here.
int64_t plang_tp_inoutres = 0;

/// TP `IOResult: Integer` (Builtins.def, `Func, TP, 0, 0, R_Int`) -- reads
/// InOutRes and CLEARS IT TO ZERO in the same call, real Turbo Pascal's own
/// read-and-clear contract (confirmed against the local `fpc -Mtp` install's
/// own system.inc-equivalent behavior: a second, immediately following
/// IOResult call always reads 0, even with nothing else run in between).
/// This is the ONE place that read-and-clear happens -- CGExprCore.cpp's
/// bare-identifier arm and CGFuncCall.cpp's explicit-call arm both just call
/// straight in here, neither duplicates the clear itself, so there is a
/// single source of truth for what "reading IOResult" does regardless of
/// which of the two syntactic forms (`x := IOResult;` vs `x := IOResult();`)
/// a program actually wrote.
int64_t plang_tp_ioresult(void) {
    const int64_t Result = plang_tp_inoutres;
    plang_tp_inoutres = 0;
    return Result;
}

/// -std=turbo only: the automatic `{$I+}` check CodeGen (RangeCheckGuards::
/// ioChecksAt, CGProcCall.cpp) emits a call to after a write/writeln/read/
/// readln/Reset/Rewrite/Append/Close statement whose own source location has
/// IOChecks ON.  This is TEXTUAL/POSITIONAL, matching every other switch in
/// CompilerSwitches.def: whether to EMIT the call at all is decided purely at
/// compile time from the STATEMENT's own position, never at run time from
/// which dialect is active or which operation actually ran -- so this
/// function itself has no "is this Turbo" branch of its own to make, the
/// same way plang_tp_runerror above it does not.
///
/// Reads AND CLEARS InOutRes through plang_tp_ioresult itself, rather than
/// duplicating that read-and-clear pair here -- there is exactly one place
/// in this file that clears InOutRes, and this reuses it instead of growing
/// a second, potentially-divergent copy of the same two lines.  A pending
/// error read this way is consumed exactly as if a Pascal-level IOResult had
/// read it: a later explicit `IOResult` call after a checked failure aborted
/// here would see 0, not the code that was just reported and exited on --
/// consistent with the fact that the process is about to exit anyway, and
/// with real Turbo Pascal's own read-and-clear IOResult contract applying no
/// differently when the read happens to be the compiler-inserted kind.
///
/// On a zero read (no error pending) this simply returns -- a no-op, the
/// overwhelmingly common case for every checked I/O statement that actually
/// succeeded.  On a nonzero read it reports through the same
/// plang_tp_runerror(Code) every other Turbo runtime check already routes
/// through (RangeCheckGuards.cpp's emitTpRunError, CGProcCall.cpp's
/// `runerror` arm): prints "Runtime error <Code> at $<addr>", runs the exit
/// chain (plang_module_finals_run), and exits with status Code -- so a
/// checked I/O failure's exit status is always the InOutRes code itself, the
/// same contract every other numbered Turbo runtime check already
/// guarantees.
void plang_iocheck(void) {
    const int64_t Code = plang_tp_ioresult();
    if (Code != 0) plang_tp_runerror(Code);
}

/// -std=turbo only: Input/Output as ordinary, addressable text-file
/// VARIABLES (Sema::registerBuiltins) rather than the ISO/EP `program
/// p(input, output);` heading mechanism's implicit file-parameter Vars
/// (Sema.cpp's own Prog.FileParams loop, unchanged for ISO 7185/Extended
/// Pascal -- see that loop's own comment for how the two are reconciled
/// so a Turbo program that still writes the ISO heading does not double-
/// define either name).  plang_tp_exitcode's own mechanism exactly: one
/// shared PascalFile defined here, and every compiled Turbo object only
/// DECLARES it (Codegen::Impl::emitPredefinedGlobals) -- so `Input`/
/// `Output` name the SAME two file records no matter how many objects a
/// Turbo program is linked from, letting `Assign(Output, 'x'); Rewrite
/// (Output);` in one file and a bare `Writeln` in another still agree on
/// where the write goes.
///
/// Zero-initialized (PascalFile's own in-class default member
/// initializers), exactly like a program-declared `var f: text;` would be
/// before anything ever touches it -- Codegen::Impl::emitMain's Turbo
/// prologue calls plang_bind_std on each, the same runtime entry point
/// the ISO/EP file-parameter mechanism already uses to attach 'input'/
/// 'output' to stdin/stdout, before the program body runs, so ordinary
/// unredirected use already sees them bound to the console by the time
/// any statement in the program body executes.
PascalFile plang_input;
PascalFile plang_output;

} // extern "C"

} // namespace plang
