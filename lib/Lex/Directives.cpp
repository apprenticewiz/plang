// Directives.cpp -- Turbo `{$...}` compiler directives.
//
// Nothing recognized this syntax at all before this file existed: under
// every dialect, `{$anything}` was just an ordinary brace comment, its
// contents never looked at.  That is still exactly true under ISO 7185 and
// Extended Pascal (skipWhitespaceAndComments in Scanner.cpp gates the whole
// directive path on Opts.turbo()) -- this file only ever runs for
// -std=turbo.
//
// DISPATCH SHAPE
// ---------------
// skipDirective isolates the raw text between '$' and the directive's own
// closing delimiter (the same same-kind-terminator rule skipCommentTurbo
// already uses for an ordinary Turbo comment, since a directive is still a
// comment syntactically) and hands it to dispatchDirective, which splits it
// into a Name (the leading run of letters) and an Argument (everything
// after, trimmed).  dispatchDirective then tries each directive *category*
// in turn -- today just dispatchMessageDirective -- stopping at the first
// one that recognizes Name, and falling back to warn_directive_unknown if
// none does.
//
// Only the message-directive category ({$MESSAGE}/{$INFO}/{$NOTE}/
// {$HINT}/{$WARNING}/{$ERROR}/{$FATAL}) is implemented here.  Three later
// categories share this same dispatch point without it needing to change
// shape at all:
//
//   - Conditional compilation ({$IFDEF}/{$IFNDEF}/{$ENDIF}/{$ELSE}/
//     {$ELSEIF}) adds a dispatchConditionalDirective(Name, Argument, Loc)
//     tried before the unknown-directive fallback.  It needs more than this
//     shape gives it -- skipping dead branches spans many directives, not
//     one -- but recognizing its own directive *names* fits here exactly.
//   - {$I file} includes add a dispatchIncludeDirective the same way.
//   - {$R+}-style switches (letter or long name from CompilerSwitches.def,
//     '+'/'-'/' ON'/' OFF' argument, recorded into a SwitchTable) are a
//     third such handler; switchFromLetter/switchFromLongName already exist
///    for it in SwitchTable.h, just not called from anywhere yet.
//
// WHY NOT AN X-MACRO TABLE HERE
// -------------------------------
// Builtins.def and CompilerSwitches.def both use the `NAME(Id, ...)`
// X-macro idiom because each is consumed from several independent places
// (an enum, a spelling lookup, a dialects/honored-bits check, a defaults
// builder -- CompilerSwitches.def alone feeds five). The message-directive
// table below has exactly one consumer, is seven rows, and is not expected
// to grow (it is Borland/FPC's fixed vocabulary, not this compiler's own).
// A plain array costs nothing extra to maintain and, unlike a macro-hidden
// `diag::Diag` parameter, keeps every `diag::note_directive_message`-style
// token literally present in this .cpp file, which is what
// tools/lint_diagnostics.py's declared-vs-emitted check greps for.

#include "plang/Lex/Scanner.h"

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/StringUtil.h"

#include <cctype>
#include <string>
#include <string_view>

using namespace plang;

namespace {

// Borland Pascal 7 itself had only `{$MESSAGE text}`, unconditionally
// echoed while compiling.  `{$WARNING}`/`{$HINT}`/`{$NOTE}`/`{$INFO}`/
// `{$ERROR}`/`{$FATAL}` are later Borland/FPC additions; this table gives
// each its own plain `{$NAME text}` form -- BP7's own argument syntax --
// rather than FPC's compound `{$MESSAGE <TYPE> text}`, which `fpc -Mtp`
// requires (a bare `{$MESSAGE Hello}` there is rejected as an "illegal
// compiler directive", the first word taken as a required TYPE keyword)
// and BP7 never had.  Both empirically confirmed against `fpc -Mtp` before
// writing this, along with the severities below: {$NOTE} is a Note,
// {$HINT} a Hint (folded to this compiler's Warning -- there is no fourth
// severity), {$WARNING} a Warning, and {$ERROR}/{$FATAL} both an Error.
//
// Real Turbo/FPC distinguish {$ERROR} (report, then keep compiling to the
// end of the file) from {$FATAL} (report, then abort right there,
// confirmed against `fpc -Mtp`) -- but plang's diagnostics are collected
// and only checked at a handful of fixed points (after parsing, after
// Sema, ...), with no immediate-unwind mechanism to hook a mid-scan abort
// into (and the compiler is built -fno-exceptions, so that is not the way
// to add one).  Approximating it by truncating the buffer here was tried:
// it works, but it leaves the Parser mid-construct with no more tokens,
// which reports its own cascade of "expected X, got end of file" -- pure
// noise once the compile has already failed, which it has either way, so
// {$FATAL} gets its own DiagID for a distinguishable message but the same
// report-and-continue handling as {$ERROR}.
struct MessageDirective {
    std::string_view Name; // already lower case; Name is folded to match
    DiagID            Diag; // Argument is reported through this as %0
};

constexpr MessageDirective MessageDirectives[] = {
    {"message", diag::note_directive_message},
    {"info",    diag::note_directive_info},
    {"note",    diag::note_directive_note},
    {"hint",    diag::warn_directive_hint},
    {"warning", diag::warn_directive_warning},
    {"error",   diag::err_directive_error},
    {"fatal",   diag::err_directive_fatal},
};

} // namespace

void Scanner::skipDirective(bool Braced) {
    const size_t CommentStart = Pos;
    Pos += Braced ? 2 : 3; // past '{$' or '(*$'
    const size_t BodyStart = Pos;
    bool SawOtherCloser = false;

    while (Pos < Text.size()) {
        const size_t Here = Pos;
        const char   C    = Text[Pos++];
        if (Braced) {
            if (C == '}') {
                dispatchDirective(Text.substr(BodyStart, Here - BodyStart),
                                  locAt(BodyStart));
                return;
            }
            if (C == '*' && Pos < Text.size() && Text[Pos] == ')') {
                SawOtherCloser = true;
                ++Pos;
            }
        } else {
            if (C == '*' && Pos < Text.size() && Text[Pos] == ')') {
                ++Pos; // consume ')'
                dispatchDirective(Text.substr(BodyStart, Here - BodyStart),
                                  locAt(BodyStart));
                return;
            }
            if (C == '}') SawOtherCloser = true;
        }
    }

    // Ran off the end with no closer of the right kind -- the same two
    // diagnostics skipCommentTurbo reports for an ordinary Turbo comment in
    // the same situation, since a directive closes exactly the way one
    // does.  Nothing to dispatch either way.
    if (SawOtherCloser) {
        const std::string_view Opener = Braced ? "{" : "(*";
        const std::string_view Closer = Braced ? "}" : "*)";
        emitError(locAt(CommentStart), diag::err_comment_delim_mismatch,
                  {Opener, Closer});
    } else {
        emitError(locAt(CommentStart), diag::err_unterminated_comment);
    }
}

void Scanner::dispatchDirective(std::string_view Body, SourceLocation Loc) {
    size_t I = 0;
    while (I < Body.size() && std::isalpha(static_cast<unsigned char>(Body[I])))
        ++I;
    const std::string_view Name = Body.substr(0, I);
    std::string_view       Argument = Body.substr(I);
    while (!Argument.empty() &&
           std::isspace(static_cast<unsigned char>(Argument.front())))
        Argument.remove_prefix(1);
    while (!Argument.empty() &&
           std::isspace(static_cast<unsigned char>(Argument.back())))
        Argument.remove_suffix(1);

    if (dispatchMessageDirective(Name, Argument, Loc)) return;

    // Cluster B's later items (conditional compilation, {$I file},
    // {$R+}-style switches -- see this file's header comment) each add
    // their own "try this category" call above this line.  None of them
    // exist yet, so every directive name but the seven message-directive
    // ones reaches here.  Reported rather than silently ignored or treated
    // as a plain comment: a `{$R+}` that does nothing and says nothing is
    // worse than one that says so.
    emitError(Loc, diag::warn_directive_unknown, {Name});
}

bool Scanner::dispatchMessageDirective(std::string_view Name,
                                       std::string_view Argument,
                                       SourceLocation Loc) {
    const std::string Folded = toLower(Name);
    for (const auto& D : MessageDirectives) {
        if (Folded != D.Name) continue;
        emitError(Loc, D.Diag, {Argument});
        return true;
    }
    return false;
}
