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
// The message-directive category ({$MESSAGE}/{$INFO}/{$NOTE}/{$HINT}/
// {$WARNING}/{$ERROR}/{$FATAL}) and conditional compilation ({$DEFINE}/
// {$UNDEF}/{$IFDEF}/{$IFNDEF}/{$ELSE}/{$ELSEIF}/{$ENDIF}) are both
// implemented here.  Two later categories can share this same dispatch
// point without it needing to change shape at all:
//
//   - {$I file} includes add a dispatchIncludeDirective the same way.
//   - {$R+}-style switches (letter or long name from CompilerSwitches.def,
//     '+'/'-'/' ON'/' OFF' argument, recorded into a SwitchTable) are a
//     second such handler; switchFromLetter/switchFromLongName already exist
///    for it in SwitchTable.h, just not called from anywhere yet.
//
// Conditional compilation is the one category that does not fit
// dispatchDirective's plain "recognize Name, act, return" shape: a false
// {$IFDEF}/{$IFNDEF} branch, or reaching {$ELSE}/{$ELSEIF} after a branch
// that already ran, has to skip everything up to its next relevant marker --
// which can span any number of tokens, comments, and other directives, none
// of which may be dispatched or diagnosed along the way (see
// skipToNextConditionalMarker's own comment in Scanner.h).  That skip is
// still reached from dispatchConditionalDirective, in exactly the same
// (Name, Argument, Loc) -> bool shape dispatchMessageDirective uses; it just
// does much more work before returning.
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
#include <optional>
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

// Splits a directive's raw body into a Name -- the leading run of letters --
// and an Argument -- everything after it, trimmed of leading/trailing
// whitespace but otherwise passed through verbatim.  Shared by
// dispatchDirective (called once per live directive) and
// skipToNextConditionalMarker (which parses each directive it meets while
// raw-skipping dead source the same way, without ever routing through
// dispatchDirective itself -- that would dispatch it for real).
void splitDirectiveBody(std::string_view Body, std::string_view& Name,
                        std::string_view& Argument) {
    size_t I = 0;
    while (I < Body.size() && std::isalpha(static_cast<unsigned char>(Body[I])))
        ++I;
    Name = Body.substr(0, I);
    Argument = Body.substr(I);
    while (!Argument.empty() &&
           std::isspace(static_cast<unsigned char>(Argument.front())))
        Argument.remove_prefix(1);
    while (!Argument.empty() &&
           std::isspace(static_cast<unsigned char>(Argument.back())))
        Argument.remove_suffix(1);
}

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
    std::string_view Name, Argument;
    splitDirectiveBody(Body, Name, Argument);

    if (dispatchMessageDirective(Name, Argument, Loc)) return;
    if (dispatchConditionalDirective(Name, Argument, Loc)) return;

    // Cluster B's remaining items ({$I file}, {$R+}-style switches -- see
    // this file's header comment) each add their own "try this category"
    // call above this line.  Neither exists yet, so every directive name but
    // the message-directive and conditional-compilation ones reaches here.
    // Reported rather than silently ignored or treated as a plain comment: a
    // `{$R+}` that does nothing and says nothing is worse than one that says
    // so.
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

// ---------------------------------------------------------------------------
// Conditional compilation: {$DEFINE}/{$UNDEF}/{$IFDEF}/{$IFNDEF}/{$ELSE}/
// {$ELSEIF}/{$ENDIF}
// ---------------------------------------------------------------------------
//
// {$DEFINE}/{$UNDEF} just mutate CurrentDefines and return -- see its own
// comment in Scanner.h for why that alone, with no SwitchTable-style
// position-indexed table, is enough.
//
// {$IFDEF}/{$IFNDEF} push a CondFrame and, if the condition fails, hand off
// to skipToNextConditionalMarker to find wherever this chain's next live
// branch (or its {$ENDIF}) actually is.  {$ELSE}/{$ELSEIF} reached here --
// through ordinary, live scanning -- mean the branch just finished WAS live,
// so this chain has already taken a branch and nothing after this point can
// ever be live again; they hand off to the same skip function purely to
// find the matching {$ENDIF} (still validating directive-syntax errors --
// duplicate {$ELSE}, {$ELSEIF} after {$ELSE} -- along the way). {$ENDIF}
// just pops.

bool Scanner::dispatchConditionalDirective(std::string_view Name,
                                           std::string_view Argument,
                                           SourceLocation Loc) {
    const std::string Folded = toLower(Name);

    if (Folded == "define" || Folded == "undef") {
        if (!looksLikeIdentifier(Argument)) {
            emitError(Loc, diag::err_directive_expects_symbol, {Name});
            return true;
        }
        const std::string Symbol = toLower(Argument);
        if (Folded == "define") CurrentDefines.insert(Symbol);
        else                    CurrentDefines.erase(Symbol);
        return true;
    }

    if (Folded == "ifdef" || Folded == "ifndef") {
        bool Cond;
        if (!looksLikeIdentifier(Argument)) {
            emitError(Loc, diag::err_directive_expects_symbol, {Name});
            // No real symbol to test either way; treat the branch as not
            // satisfied so {$ELSE}/{$ENDIF} below still balance correctly
            // rather than cascading into an unmatched-directive error too.
            Cond = false;
        } else {
            const bool Defined = CurrentDefines.count(toLower(Argument)) != 0;
            Cond = (Folded == "ifdef") ? Defined : !Defined;
        }
        CondStack.push_back(CondFrame{Cond, /*SeenElse=*/false, Loc, std::string(Name)});
        if (!Cond) skipToNextConditionalMarker(CondStack.back());
        return true;
    }

    if (Folded == "else" || Folded == "elseif") {
        if (CondStack.empty()) {
            emitError(Loc, diag::err_directive_no_matching_ifdef, {Name});
            return true;
        }
        CondFrame& Top = CondStack.back();
        if (Top.SeenElse) {
            emitError(Loc, diag::err_directive_else_already_seen, {Name});
            return true;
        }
        if (Folded == "else") Top.SeenElse = true;
        skipToNextConditionalMarker(Top);
        return true;
    }

    if (Folded == "endif") {
        if (CondStack.empty()) {
            emitError(Loc, diag::err_directive_no_matching_ifdef, {Name});
            return true;
        }
        CondStack.pop_back();
        return true;
    }

    return false;
}

std::optional<std::string_view> Scanner::rawDirectiveBody(bool Braced) {
    Pos += Braced ? 2 : 3; // past '{$' or '(*$', same as skipDirective
    const size_t BodyStart = Pos;

    while (Pos < Text.size()) {
        const size_t Here = Pos;
        const char   C    = Text[Pos++];
        if (Braced) {
            if (C == '}') return Text.substr(BodyStart, Here - BodyStart);
        } else {
            if (C == '*' && Pos < Text.size() && Text[Pos] == ')') {
                ++Pos; // consume ')'
                return Text.substr(BodyStart, Here - BodyStart);
            }
        }
    }
    return std::nullopt; // ran off the end; Pos is already Text.size()
}

void Scanner::skipToNextConditionalMarker(CondFrame& Frame) {
    unsigned Depth = 0; // nested {$IFDEF}/{$IFNDEF}...{$ENDIF} pairs found
                        // while skipping, none of which are Frame's own

    for (;;) {
        if (Pos >= Text.size()) {
            emitError(Frame.OpenLoc, diag::err_directive_unterminated_conditional,
                      {Frame.OpenName});
            CondStack.pop_back();
            return;
        }

        bool Braced;
        if (Text[Pos] == '{' && Pos + 1 < Text.size() && Text[Pos + 1] == '$') {
            Braced = true;
        } else if (Text[Pos] == '(' && Pos + 2 < Text.size() &&
                   Text[Pos + 1] == '*' && Text[Pos + 2] == '$') {
            Braced = false;
        } else {
            ++Pos; // ordinary byte of dead source: not inspected at all
            continue;
        }

        // BodyStart mirrors skipDirective's own: right after the opener, so
        // a diagnostic about THIS directive points at the same place a live
        // one would.
        const size_t BodyStart = Pos + (Braced ? 2 : 3);
        const std::optional<std::string_view> Body = rawDirectiveBody(Braced);
        if (!Body) {
            // A directive found while skipping never closes -- e.g. dead
            // source containing an unterminated `{$SOMETHING`.  Not
            // diagnosed on its own (see this function's Scanner.h comment):
            // from here there is no way to tell where the dead region was
            // even meant to end, so this reads as Frame itself never having
            // been closed, which is the one true thing about it.
            emitError(Frame.OpenLoc, diag::err_directive_unterminated_conditional,
                      {Frame.OpenName});
            CondStack.pop_back();
            return;
        }

        std::string_view Name, Argument;
        splitDirectiveBody(*Body, Name, Argument);
        const std::string Folded = toLower(Name);
        const SourceLocation DirLoc = locAt(BodyStart);

        if (Folded == "ifdef" || Folded == "ifndef") {
            ++Depth;
            continue;
        }
        if (Folded == "endif") {
            if (Depth > 0) { --Depth; continue; }
            CondStack.pop_back();
            return;
        }
        if (Depth > 0) continue; // some other nested directive: not Frame's

        if (Folded == "else") {
            if (Frame.SeenElse) {
                emitError(DirLoc, diag::err_directive_else_already_seen, {Name});
                continue;
            }
            Frame.SeenElse = true;
            if (!Frame.AnyBranchTaken) {
                Frame.AnyBranchTaken = true;
                return; // live now; resume ordinary scanning right here
            }
            continue; // already taken a branch earlier; keep looking for ENDIF
        }
        if (Folded == "elseif") {
            if (Frame.SeenElse) {
                emitError(DirLoc, diag::err_directive_else_already_seen, {Name});
                continue;
            }
            if (!Frame.AnyBranchTaken) {
                if (!looksLikeIdentifier(Argument)) {
                    emitError(DirLoc, diag::err_directive_expects_symbol, {Name});
                } else if (CurrentDefines.count(toLower(Argument)) != 0) {
                    Frame.AnyBranchTaken = true;
                    return; // live now
                }
            }
            continue;
        }

        // Anything else -- {$DEFINE}, {$MESSAGE}, an unknown name, anything
        // but the five conditional-compilation keywords -- found while
        // skipping dead source is never dispatched or even looked at past
        // its own Name: real Turbo/FPC never evaluate a directive inside a
        // branch that was never taken, and this does not either.
    }
}

void Scanner::reportUnterminatedConditionals() {
    // A live {$IFDEF}/{$IFNDEF} (or one that skipToNextConditionalMarker
    // left live via a satisfied {$ELSEIF}/{$ELSE}) whose own {$ENDIF} was
    // never reached: ordinary scanning just ran off the end of the file
    // with the frame still open, the one case skipToNextConditionalMarker
    // itself cannot catch, since it is never called for a branch that
    // stayed live.
    for (const CondFrame& F : CondStack)
        emitError(F.OpenLoc, diag::err_directive_unterminated_conditional,
                  {F.OpenName});
    CondStack.clear();
}
