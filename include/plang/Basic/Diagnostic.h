#pragma once

#include "plang/Basic/SourceLocation.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plang {

// ---------------------------------------------------------------------------
// DiagID — diagnostic identifier enum
//
// Generated from DiagnosticMessages.def — the single source of truth.
// Add new diagnostics there, not here.
// ---------------------------------------------------------------------------

enum class DiagID {
    none = 0,
#define DIAG(ID, LEVEL, MSG) ID,
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
};

// ---------------------------------------------------------------------------
// diag:: — short-name aliases for DiagID values (like Clang's diag::err_foo)
//
// Defined immediately after DiagID so they are in scope for the rest of the
// file and for all translation units that include Diagnostic.h.
//
// Usage:  error(tok, diag::err_undefined_identifier, {name});
// ---------------------------------------------------------------------------

namespace diag {
inline constexpr ::plang::DiagID none = ::plang::DiagID::none;
#define DIAG(ID, LEVEL, MSG) inline constexpr ::plang::DiagID ID = ::plang::DiagID::ID;
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
} // namespace diag

// ---------------------------------------------------------------------------
// DiagSeverity — severity of a diagnostic (locale-independent)
// ---------------------------------------------------------------------------

/// Severity of a compiler diagnostic.  Used both by the DiagInfo catalog and
/// by the Diagnostic struct — there is only one severity enum in the compiler.
enum class DiagSeverity { Error, Warning, Info };

// ---------------------------------------------------------------------------
// getDiagSeverity — severity for a diagnostic ID
//
// Generated from DiagnosticMessages.def via the Level column.
// ---------------------------------------------------------------------------

inline DiagSeverity getDiagSeverity(DiagID ID) {
    static const DiagSeverity Table[] = {
        DiagSeverity::Error, // none
#define DIAG(ID, LEVEL, MSG) DiagSeverity::LEVEL,
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
    };
    auto Idx = static_cast<size_t>(ID);
    return Idx < (sizeof(Table) / sizeof(Table[0])) ? Table[Idx]
                                                     : DiagSeverity::Error;
}

// ---------------------------------------------------------------------------
// Warning names — the spelling a warning answers to on the command line
//
// A warning's -W name is its DiagID with the "warn_" prefix dropped and the
// underscores written as hyphens, so warn_label_unreachable is turned off by
// -Wno-label-unreachable.  Deriving the name means there is no second list to
// keep in step with the .def files.
// ---------------------------------------------------------------------------

/// The DiagID enumerator spelling, e.g. "warn_label_unreachable".
inline std::string_view getDiagSpelling(DiagID ID) {
    static const char* const Table[] = {
        "none",
#define DIAG(ID, LEVEL, MSG) #ID,
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
    };
    auto Idx = static_cast<size_t>(ID);
    return Idx < (sizeof(Table) / sizeof(Table[0])) ? Table[Idx] : "none";
}

/// The -W name of a warning; empty for a diagnostic that is not one.
inline std::string getWarningName(DiagID ID) {
    if (getDiagSeverity(ID) != DiagSeverity::Warning) return {};
    std::string_view S = getDiagSpelling(ID);
    if (!S.starts_with("warn_")) return {};
    std::string Name{S.substr(5)};
    for (char& C : Name)
        if (C == '_') C = '-';
    return Name;
}

/// The warning a -W name refers to, or diag::none if no warning has that name.
inline DiagID getWarningNamed(std::string_view Name) {
#define DIAG(ID, LEVEL, MSG)                                                   \
    if (getWarningName(DiagID::ID) == Name) return DiagID::ID;
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
    return DiagID::none;
}

/// Calls F(name) once for every warning the compiler can emit, in .def order.
template <typename F> void forEachWarningName(F&& Fn) {
#define DIAG(ID, LEVEL, MSG)                                                   \
    if (std::string N = getWarningName(DiagID::ID); !N.empty()) Fn(N);
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
}

// ---------------------------------------------------------------------------
// getDiagFormat — message template for a diagnostic ID
//
// INTERNATIONALISATION
// ---------------------
// Defined in lib/Basic/MessageCatalog.cpp: reads a .po translation chosen
// at run time (-fdiagnostics-language, else LC_ALL/LC_MESSAGES/LANG) and
// falls back to the compiled-in English (lib/Basic/BuiltinCatalog.cpp,
// always available, never missing or truncated) for anything the
// translation doesn't cover. See po/README.md to add or update one.
// ---------------------------------------------------------------------------

/// Returns the message template for a diagnostic ID: a translation if one
/// is loaded and covers it, the compiled-in English otherwise.
/// Defined in lib/Basic/MessageCatalog.cpp.
const char* getDiagFormat(DiagID ID);

// ---------------------------------------------------------------------------
// DiagInfo — descriptor for a single diagnostic (level + message template)
// ---------------------------------------------------------------------------

struct DiagInfo {
    DiagID       ID;
    DiagSeverity Severity;
    const char*  Format; ///< %0..%9 positional, reorderable across languages
};

inline DiagInfo getDiagInfo(DiagID ID) {
    return { ID, getDiagSeverity(ID), getDiagFormat(ID) };
}

// ---------------------------------------------------------------------------
// formatDiagMsg — substitute %0..%9 in a format string with args
// ---------------------------------------------------------------------------

inline std::string formatDiagMsg(std::string_view Fmt,
                                  std::initializer_list<std::string_view> Args) {
    std::string Out;
    Out.reserve(Fmt.size() + 32);
    const std::string_view* Arr = std::data(Args);
    const size_t N = Args.size();
    for (size_t I = 0; I < Fmt.size(); ) {
        if (Fmt[I] == '%' && I + 1 < Fmt.size()) {
            char Next = Fmt[I + 1];
            if (Next >= '0' && Next <= '9') {
                size_t Idx = static_cast<size_t>(Next - '0');
                if (Idx < N) Out += Arr[Idx];
                I += 2;
                continue;
            }
        }
        Out += Fmt[I++];
    }
    return Out;
}

// ---------------------------------------------------------------------------
// Diagnostic — a single compiler message (scanner, parser, or sema)
// ---------------------------------------------------------------------------

struct Diagnostic {
    DiagSeverity   Severity;
    SourceLocation Loc;
    std::string    Message;
    DiagID         ID{diag::none};
};

/// The word in front of every message, translated if the catalog has it.
/// Defined in lib/Basic/MessageCatalog.cpp; declared here because
/// severityLabel below is what prints it and MessageCatalog.h includes this
/// header, so the dependency can only go this way.
const char* localizedSeverityLabel(DiagSeverity Sev);

/// The label a severity is printed under, and the color it is printed in.
///
/// The label is translated with the messages: a French diagnostic reading
/// "error: <French>" would be half the job, and the half a reader sees first.
/// The color is not: an SGR sequence is not language.
[[nodiscard]] inline std::string severityLabel(DiagSeverity Sev, bool UseColor) {
    const std::string Label = localizedSeverityLabel(Sev);
    if (!UseColor) return Label;
    std::string_view On;
    switch (Sev) {
        case DiagSeverity::Error:   On = "\033[1;31m"; break;
        case DiagSeverity::Warning: On = "\033[1;33m"; break;
        case DiagSeverity::Info:    On = "\033[1;32m"; break;
    }
    return std::string(On) + Label + "\033[0m";
}

// ---------------------------------------------------------------------------
// DiagnosticOptions — what gets reported, and how severely
// ---------------------------------------------------------------------------

/// The policy half of diagnostics: which ones are emitted and at what level.
///
/// Kept apart from LangOptions because none of it is a property of the
/// language.  Whether a program is Extended Pascal changes what it means;
/// whether -Wno-unused-variable was given does not.
struct DiagnosticOptions {
    /// -w: report no warnings at all.  Overrides everything else about them.
    bool SuppressWarnings = false;
    /// -Werror: report warnings as errors, so that they fail the compilation.
    bool WarningsAsErrors = false;
    /// Warnings turned off by -Wno-<name>, held by name so that this header
    /// need not know the catalog.  Every warning is on by default, and
    /// -W<name> undoes an earlier -Wno-<name> by removing it from here.
    std::vector<std::string> DisabledWarnings;
    /// Stop after this many errors; 0 means no limit.
    unsigned ErrorLimit = 0;
};

// ---------------------------------------------------------------------------
// Color — one decision, made in one place
// ---------------------------------------------------------------------------

/// What the command line asked for about color.
enum class ColorDiagnostics {
    Auto,   ///< color if stderr is a terminal (the default)
    Always, ///< -fcolor-diagnostics
    Never,  ///< -fno-color-diagnostics
};

/// The choice \p Arg expresses, or Auto if it is not one of the color options.
[[nodiscard]] inline ColorDiagnostics colorDiagnosticsArg(std::string_view Arg) {
    if (Arg == "-fcolor-diagnostics")    return ColorDiagnostics::Always;
    if (Arg == "-fno-color-diagnostics") return ColorDiagnostics::Never;
    return ColorDiagnostics::Auto;
}

/// Whether to colorize, given the choice and whether stderr is a terminal.
///
/// The driver and the front end are separate processes and both print
/// diagnostics, so both have to answer this.  They used to answer it
/// independently and by different means — the driver through
/// llvm::sys::Process::StandardErrIsDisplayed, the front end through
/// isatty(STDERR_FILENO) — with no way for either to be told otherwise.
[[nodiscard]] inline bool useColor(ColorDiagnostics C, bool StderrIsTerminal) {
    switch (C) {
    case ColorDiagnostics::Always: return true;
    case ColorDiagnostics::Never:  return false;
    case ColorDiagnostics::Auto:   break;
    }
    return StderrIsTerminal;
}

// ---------------------------------------------------------------------------
// DiagnosticsEngine — the one route a diagnostic takes to the user
// ---------------------------------------------------------------------------

/// Collects diagnostics and decides which of them are reported.
///
/// Every diagnostic the compiler produces goes through report(), which is the
/// only place the -w, -Werror and -Wno-<name> policy is applied.  Before this
/// existed each phase pushed onto a shared vector and only Sema consulted the
/// options, so a warning raised while scanning or parsing would have ignored
/// all three.
class DiagnosticsEngine {
public:
    DiagnosticsEngine() = default;
    explicit DiagnosticsEngine(DiagnosticOptions O) : Opts(std::move(O)) {}

    void setOptions(DiagnosticOptions O) { Opts = std::move(O); }
    [[nodiscard]] const DiagnosticOptions& options() const { return Opts; }

    /// How a diagnostic would be reported once policy has had its say.
    /// Ignored means it is not reported at all.
    enum class Level { Ignored, Note, Warning, Error };

    /// Apply -w, -Wno-<name> and -Werror to a diagnostic's catalog severity.
    [[nodiscard]] Level levelOf(DiagID ID) const {
        switch (getDiagSeverity(ID)) {
        case DiagSeverity::Error:
            return Level::Error;
        case DiagSeverity::Info:
            return Level::Note;
        case DiagSeverity::Warning:
            break;
        }
        if (Opts.SuppressWarnings) return Level::Ignored;
        if (std::string Name = getWarningName(ID); !Name.empty())
            for (const auto& D : Opts.DisabledWarnings)
                if (D == Name) return Level::Ignored;
        return Opts.WarningsAsErrors ? Level::Error : Level::Warning;
    }

    /// Report a cataloged diagnostic.  Returns false if policy discarded it.
    bool report(SourceLocation Loc, DiagID ID,
                std::initializer_list<std::string_view> Args = {}) {
        const Level L = levelOf(ID);
        if (L == Level::Ignored) return false;
        return emit(toSeverity(L), Loc, formatDiagMsg(getDiagFormat(ID), Args), ID);
    }

    /// Report a diagnostic whose text is already in hand.
    ///
    /// Used by the handful of call sites that have no catalog entry.  A warning
    /// arriving this way has no -W name and so cannot be turned off by one, but
    /// -w and -Werror still apply.
    bool report(SourceLocation Loc, DiagSeverity Sev, std::string Message) {
        if (Sev == DiagSeverity::Warning) {
            if (Opts.SuppressWarnings) return false;
            if (Opts.WarningsAsErrors) Sev = DiagSeverity::Error;
        }
        return emit(Sev, Loc, std::move(Message), diag::none);
    }

    [[nodiscard]] bool     hasErrors()   const { return NumErrors > 0; }
    [[nodiscard]] unsigned numErrors()   const { return NumErrors; }
    [[nodiscard]] unsigned numWarnings() const { return NumWarnings; }

    /// True once -ferror-limit errors have been reported.
    [[nodiscard]] bool errorLimitReached() const {
        return Opts.ErrorLimit != 0 && NumErrors >= Opts.ErrorLimit;
    }

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const { return Diags; }

    // Enough of a vector's interface for the places that only want to look.
    [[nodiscard]] bool   empty() const { return Diags.empty(); }
    [[nodiscard]] size_t size()  const { return Diags.size(); }
    [[nodiscard]] const Diagnostic& operator[](size_t I) const { return Diags[I]; }
    [[nodiscard]] auto begin() const { return Diags.begin(); }
    [[nodiscard]] auto end()   const { return Diags.end(); }

    /// Discard every diagnostic and reset the counts, keeping the policy.
    void clear() {
        Diags.clear();
        NumErrors = NumWarnings = 0;
    }

private:
    static DiagSeverity toSeverity(Level L) {
        switch (L) {
        case Level::Error:   return DiagSeverity::Error;
        case Level::Warning: return DiagSeverity::Warning;
        default:             return DiagSeverity::Info;
        }
    }

    bool emit(DiagSeverity Sev, SourceLocation Loc, std::string Message,
              DiagID ID) {
        // Past the limit, keep counting errors but stop repeating them.
        if (Sev == DiagSeverity::Error && errorLimitReached()) {
            ++NumErrors;
            return false;
        }
        if (Sev == DiagSeverity::Error)        ++NumErrors;
        else if (Sev == DiagSeverity::Warning) ++NumWarnings;
        Diags.push_back({ .Severity = Sev,
                          .Loc      = Loc,
                          .Message  = std::move(Message),
                          .ID       = ID });
        return true;
    }

    DiagnosticOptions       Opts;
    std::vector<Diagnostic> Diags;
    unsigned                NumErrors{0};
    unsigned                NumWarnings{0};
};

} // namespace plang
