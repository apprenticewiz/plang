/// plang_sys.cpp — Pascal system routines (C++23)
///
/// halt, new, and dispose — the only places generated Pascal programs touch
/// process control and the heap.

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstdlib>

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
} // namespace

extern "C" {

/// EP §6.11.2: run every registered module finaliser, most recently
/// initialized module first (defined below, once module registration has
/// been introduced) -- forward-declared here because plang_halt, just
/// below, is the other way a program can terminate and has to reach the
/// same finalisers that falling off the end of the program block already
/// does (CodeGenProcs.cpp's emitMain).
void plang_module_finals_run(void);

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
    plang_module_finals_run();
    std::fflush(stdout);
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
            std::fputs("plang runtime: out of memory\n", stderr);
            std::abort();
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

/// EP §6.5.3.2: a string component is selected by an index in 1..length(s), so
/// the upper bound reported here is the string's length, not its capacity.
void plang_err_str_index(int64_t V, int64_t Len) {
    std::fflush(stdout);
    std::fprintf(stderr,
                 "plang runtime: string index %" PRId64
                 " out of bounds 1..%" PRId64 "\n", V, Len);
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
[[noreturn]] void plang_err_cannot_open(const char *Msg) {
    std::fflush(stdout);
    std::fprintf(stderr, "plang runtime: %s\n", Msg);
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

} // extern "C"

} // namespace plang
