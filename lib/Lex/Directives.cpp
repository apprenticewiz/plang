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
// {$WARNING}/{$ERROR}/{$FATAL}), conditional compilation ({$DEFINE}/
// {$UNDEF}/{$IFDEF}/{$IFNDEF}/{$ELSE}/{$ELSEIF}/{$ENDIF}), and
// {$I file}/{$INCLUDE file} source inclusion are all implemented here.  One
// later category can share this same dispatch point without it needing to
// change shape at all:
//
//   - {$R+}-style switches (letter or long name from CompilerSwitches.def,
//     '+'/'-'/' ON'/' OFF' argument, recorded into a SwitchTable) are a
//     second such handler; switchFromLetter/switchFromLongName already exist
///    for it in SwitchTable.h, just not called from anywhere yet.  Its
//     letter 'I' already overlaps {$I file}'s own name -- see
//     dispatchIncludeDirective's own comment for how the two are told apart.
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
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

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
    if (dispatchIncludeDirective(Name, Argument, Loc)) return;

    // Cluster B's one remaining item ({$R+}-style switches -- see this
    // file's header comment) adds its own "try this category" call above
    // this line.  It does not exist yet, so every directive name but the
    // message-directive, conditional-compilation, and include ones reaches
    // here -- including {$I+}/{$I-}, which dispatchIncludeDirective
    // deliberately leaves unhandled (see its own comment).  Reported
    // rather than silently ignored or treated as a plain comment: a
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

// ---------------------------------------------------------------------------
// {$I file}/{$INCLUDE file}: Turbo source inclusion
// ---------------------------------------------------------------------------
//
// Most of the design is already documented where each piece is declared in
// Scanner.h (IncludeFrame/IncludeStack/OpenIncludePaths/canonicalIdentity/
// dispatchIncludeDirective/resolveIncludePath/openInclude/popInclude) and in
// next()'s own comment in Scanner.cpp for how popInclude fits into the
// ordinary Eof check.  This section is just the bodies.

namespace {

// True when Path names a file that can actually be read as ordinary text --
// not a directory, not a dangling symlink, not something that simply does
// not exist.  is_regular_file follows symlinks itself, so a symlink to a
// real file passes and one to nothing (or to a directory) does not.
bool isReadableFile(const std::filesystem::path& Path) {
    std::error_code Ec;
    return std::filesystem::is_regular_file(Path, Ec) && !Ec;
}

} // namespace

std::string Scanner::canonicalIdentity(const std::string& Path) {
    std::error_code Ec;
    const std::filesystem::path Canon = std::filesystem::canonical(Path, Ec);
    return Ec ? Path : Canon.string();
}

bool Scanner::dispatchIncludeDirective(std::string_view Name,
                                       std::string_view Argument,
                                       SourceLocation Loc) {
    const std::string Folded = toLower(Name);
    if (Folded != "i" && Folded != "include") return false;

    // {$I+}/{$I-}: the (separately tracked, not yet dispatched from
    // anywhere -- CompilerSwitches.def's own later task) IOChecks switch,
    // not this directive.  Its long name is "iochecks", never "include", so
    // only the one-letter spelling can collide, and only for exactly these
    // two arguments -- see this function's own comment in Scanner.h.
    if (Folded == "i" && (Argument == "+" || Argument == "-")) return false;

    // `fpc -Mtp` accepts both {$I foo.inc} and {$I 'foo.inc'} (confirmed
    // empirically); matched here by stripping one layer of surrounding
    // quotes, if present, before anything else sees Filename.  No escape
    // handling inside the quotes -- a quoted include filename containing a
    // literal quote is not a case real Turbo/FPC programs exercise, and
    // this project's own Pascal string-literal escaping is a wholly
    // separate grammar this directive argument is not one of.
    std::string_view Filename = Argument;
    if (Filename.size() >= 2 && Filename.front() == '\'' && Filename.back() == '\'')
        Filename = Filename.substr(1, Filename.size() - 2);

    if (Filename.empty()) {
        emitError(Loc, diag::err_directive_include_expects_filename, {Name});
        return true;
    }

    openInclude(Filename, Loc);
    return true;
}

std::optional<std::string> Scanner::resolveIncludePath(std::string_view Filename) const {
    namespace fs = std::filesystem;
    const fs::path FP{std::string(Filename)};

    if (FP.is_absolute())
        return isReadableFile(FP) ? std::optional<std::string>(FP.string()) : std::nullopt;

    // THIS scanner's currently active buffer's own directory, not the
    // outermost file's: SM->getBufferName(FID) always names whichever
    // buffer is presently being scanned, whether that is the main file or
    // one an earlier {$I} already spliced in, so a nested include resolves
    // relative to its own immediate parent -- most C-like #include
    // conventions' rule, and the one this directive's own Scanner.h comment
    // documents.
    const fs::path CurDir = fs::path(std::string(SM->getBufferName(FID))).parent_path();
    if (const fs::path Candidate = CurDir / FP; isReadableFile(Candidate))
        return Candidate.string();

    // Then -Fi<dir>, in the order given -- the same "try each candidate,
    // first hit wins" shape Sema::resolveImports already uses for
    // Opts.ModuleSearchPaths/.pmi.
    for (const std::string& Dir : Opts.IncludeSearchPaths)
        if (const fs::path Candidate = fs::path(Dir) / FP; isReadableFile(Candidate))
            return Candidate.string();

    return std::nullopt;
}

void Scanner::openInclude(std::string_view Filename, SourceLocation Loc) {
    const std::optional<std::string> Resolved = resolveIncludePath(Filename);
    if (!Resolved) {
        emitError(Loc, diag::err_directive_include_not_found, {Filename});
        return;
    }

    const std::string Identity = canonicalIdentity(*Resolved);
    bool AlreadyOpen = false;
    for (const std::string& Open : OpenIncludePaths)
        if (Open == Identity) { AlreadyOpen = true; break; }
    if (AlreadyOpen) {
        // A includes A (Identity matches the main file's own entry, or an
        // include still open further up the stack), or A includes B
        // includes ... includes A -- either way, opening it for real would
        // recurse into the exact same directive again the moment the new
        // buffer reached it, forever.  Reported instead, and nothing is
        // pushed: scanning simply continues in the current buffer right
        // after this directive.
        emitError(Loc, diag::err_directive_include_cycle, {Filename});
        return;
    }

    const std::optional<FileID> NewID = SM->addFile(*Resolved);
    if (!NewID) {
        // resolveIncludePath just confirmed *Resolved names a readable
        // regular file, so reaching here means addFile itself failed
        // anyway: a race (removed, or its permissions changed, between the
        // two calls) or SourceManager's own coordinate space has no room
        // left for it (wouldOverflow).  Neither is common enough to need a
        // diagnostic distinct from "could not be included".
        emitError(Loc, diag::err_directive_include_not_found, {Filename});
        return;
    }

    // Everything above only ever reads Scanner state; this is the one
    // place that changes it, and only once every failure path above has
    // already returned -- so a failed include leaves FID/Text/Pos, and
    // both stacks, exactly as they were.
    IncludeStack.push_back(IncludeFrame{FID, Text, Pos});
    OpenIncludePaths.push_back(Identity);
    FID  = *NewID;
    Text = truncateAtCtrlZ(SM->getBufferData(FID), Opts);
    Pos  = 0;
}

bool Scanner::popInclude() {
    if (IncludeStack.empty()) return false;
    const IncludeFrame& F = IncludeStack.back();
    FID  = F.FID;
    Text = F.Text;
    Pos  = F.Pos;
    IncludeStack.pop_back();
    OpenIncludePaths.pop_back();
    return true;
}
