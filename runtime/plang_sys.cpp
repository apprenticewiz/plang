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
} // namespace

extern "C" {

/// Flush stdout then terminate (EP §6.7.5.7 \c halt).  The standard's halt takes
/// no argument; \p Status carries the common extension halt(n), and is zero for
/// a bare halt.
void plang_halt(int64_t Status) {
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

} // extern "C"

} // namespace plang
