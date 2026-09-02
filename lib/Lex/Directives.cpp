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
// in turn, stopping at the first one that recognizes Name, and falling back
// to warn_directive_unknown if none does.
//
// The message-directive category ({$MESSAGE}/{$INFO}/{$NOTE}/{$HINT}/
// {$WARNING}/{$ERROR}/{$FATAL}), conditional compilation ({$DEFINE}/
// {$UNDEF}/{$IFDEF}/{$IFNDEF}/{$ELSE}/{$ELSEIF}/{$ENDIF}),
// {$I file}/{$INCLUDE file} source inclusion, {$R+}-style switches
// (CompilerSwitches.def's SWITCH table, letter or long name, recorded
// position-keyed into a SwitchTable -- see dispatchSwitchDirective's own
// comment in Scanner.h), and an accept-and-ignore table for every other real
// Turbo/Borland/FPC directive this milestone does not act on
// (dispatchIgnoredDirective) are all implemented here.  The switch handler's
// letter 'I' already overlaps {$I file}'s own name -- see
// dispatchIncludeDirective's own comment for how the two are told apart.
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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
//
// AdjacentToName, when non-null, is set to whether Argument's very first
// character -- BEFORE the leading-whitespace trim below runs -- immediately
// followed Name with no gap at all.  Only dispatchSwitchDirective's
// one-letter form ever reads it (issue #604): real Turbo/FPC tell `{$R+}`
// (the RangeChecks switch) apart from `{$R +}` or `{$R resourcefile}` (an
// unrelated named directive on the very same letter) purely by whether the
// sign is adjacent, confirmed against fpc's own scanner source
// (tscannerfile.handledirectives in scanner.pas) -- so this has to capture
// that fact before it trims the very whitespace that would otherwise be the
// only evidence of it.
void splitDirectiveBody(std::string_view Body, std::string_view& Name,
                        std::string_view& Argument,
                        bool* AdjacentToName = nullptr) {
    size_t I = 0;
    while (I < Body.size() && std::isalpha(static_cast<unsigned char>(Body[I])))
        ++I;
    Name = Body.substr(0, I);
    Argument = Body.substr(I);
    if (AdjacentToName)
        *AdjacentToName = !Argument.empty() &&
                          !std::isspace(static_cast<unsigned char>(Argument.front()));
    while (!Argument.empty() &&
           std::isspace(static_cast<unsigned char>(Argument.front())))
        Argument.remove_prefix(1);
    while (!Argument.empty() &&
           std::isspace(static_cast<unsigned char>(Argument.back())))
        Argument.remove_suffix(1);
}

} // namespace

void Scanner::skipDirective(bool Braced) {
    // Both diagnosed positions below are computed up front, while FID still
    // names the buffer this directive actually opened in: a directive body
    // that runs off the end of an included buffer may, per the loop below,
    // pop back through however many includes it takes to keep looking for a
    // closer (issue #656 -- {$I file} splices its text in "as if pasted",
    // so a directive opened before the splice point and closed after it is
    // one continuous directive, not two unrelated fragments), and by the
    // time that happens FID no longer names this buffer at all.  locAt()
    // always reads the CURRENT FID, so calling it now, before the loop can
    // ever pop anything, is what keeps either diagnostic pointing at where
    // the directive actually opened rather than wherever scanning happened
    // to end up.
    const size_t         CommentStart = Pos;
    const SourceLocation OpenLoc      = locAt(CommentStart);
    Pos += Braced ? 2 : 3; // past '{$' or '(*$'
    const size_t         BodyStart = Pos;
    const SourceLocation BodyLoc   = locAt(BodyStart);
    bool SawOtherCloser = false;

    // Stays empty, and is never consulted, unless the directive's closer
    // turns out to be in a DIFFERENT buffer than BodyStart -- the
    // overwhelmingly common case (a directive that opens and closes in the
    // same file) is still one plain Text.substr() view with no copy at all.
    // Once a buffer boundary is crossed, though, BodyStart and the eventual
    // closer are offsets into two buffers with no shared address space, so
    // there is no single string_view that could name both ends; this is
    // built up piece by piece instead, one segment per buffer the directive
    // body passes through.
    std::string SpanningBody;
    bool        Spanning     = false;
    size_t      SegmentStart = BodyStart;

    for (;;) {
        if (Pos >= Text.size()) {
            // Mirrors skipToNextConditionalMarker's own EOF handling
            // (issue #651's own comment there): an {$I file} splice means
            // running out of THIS buffer mid-directive is not necessarily
            // the directive's own end, only this buffer's -- if an outer
            // file is waiting to resume, its text is exactly what would
            // have followed here had the include been pasted by hand, so
            // popInclude and keep scanning for the real closer there
            // instead of reporting a spurious unterminated-comment (issue
            // #656).
            SpanningBody.append(Text.substr(SegmentStart));
            Spanning = true;
            if (popInclude()) {
                SegmentStart = Pos;
                continue;
            }
            break;
        }
        const size_t Here = Pos;
        const char   C    = Text[Pos++];
        if (Braced) {
            if (C == '}') {
                if (Spanning) {
                    SpanningBody.append(Text.substr(SegmentStart, Here - SegmentStart));
                    dispatchDirective(SpanningBody, BodyLoc);
                } else {
                    dispatchDirective(Text.substr(BodyStart, Here - BodyStart), BodyLoc);
                }
                return;
            }
            if (C == '*' && Pos < Text.size() && Text[Pos] == ')') {
                SawOtherCloser = true;
                ++Pos;
            }
        } else {
            if (C == '*' && Pos < Text.size() && Text[Pos] == ')') {
                ++Pos; // consume ')'
                if (Spanning) {
                    SpanningBody.append(Text.substr(SegmentStart, Here - SegmentStart));
                    dispatchDirective(SpanningBody, BodyLoc);
                } else {
                    dispatchDirective(Text.substr(BodyStart, Here - BodyStart), BodyLoc);
                }
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
        emitError(OpenLoc, diag::err_comment_delim_mismatch, {Opener, Closer});
    } else {
        emitError(OpenLoc, diag::err_unterminated_comment);
    }
}

void Scanner::dispatchDirective(std::string_view Body, SourceLocation Loc) {
    // The comma-separated multi-switch form, `{$R+,I-}`, is tried before
    // Body is even split into one Name/Argument pair: unlike every other
    // category here, it is not one directive but several, each its own
    // one-letter switch spec strung together with no directive-level
    // grammar of its own to hang a Name off of (issue #658) -- see
    // dispatchMultiSwitchDirective's own comment for why this has to run
    // first, and why it costs nothing when Body is not this form at all.
    if (dispatchMultiSwitchDirective(Body, Loc)) return;

    std::string_view Name, Argument;
    bool Adjacent = false;
    splitDirectiveBody(Body, Name, Argument, &Adjacent);

    if (dispatchMessageDirective(Name, Argument, Loc)) return;
    if (dispatchConditionalDirective(Name, Argument, Loc)) return;
    if (dispatchIncludeDirective(Name, Argument, Loc)) return;
    if (dispatchSwitchDirective(Name, Argument, Loc, Adjacent)) return;
    if (dispatchIgnoredDirective(Name, Loc)) return;

    // Every category above has had its turn, including the accept-and-ignore
    // table -- what reaches here is a directive plang genuinely does not
    // recognize at all.  Reported rather than silently ignored or treated as
    // a plain comment: a `{$R+}` that does nothing and says nothing is worse
    // than one that says so.
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
            // Mirrors next()'s own EOF handling (Scanner.cpp): an {$I file}/
            // {$INCLUDE file} splices the included text in "as if" it had
            // been pasted at the directive itself (Scanner.h's own promise
            // for live scanning), so a dead branch opened inside an include
            // whose own matching {$ENDIF} is back in the includer -- past
            // the {$I} that opened this buffer -- must keep skipping in the
            // parent buffer once the included one runs out, not treat the
            // included buffer's own end as Frame's end (issue #651). Only
            // once there is no parent left to pop back to (popInclude
            // returns false) is this genuinely the end of the token stream,
            // exactly as it already was before this file ever had includes.
            if (popInclude()) continue;
            emitError(Frame.OpenLoc, diag::err_directive_unterminated_conditional,
                      {Frame.OpenName});
            CondStack.pop_back();
            return;
        }

        // A single-quoted string literal's own content is never inspected
        // for a {$/(*$-looking substring (issue #644): 's := 'dead {$ENDIF}
        // text';' must not end Frame's branch on the string's own text, the
        // same way live scanning never looks inside a string for one
        // either. Bounded to a single line, the same as a real string
        // literal (scanQuotedFragment): a `'` with no closing `'` before the
        // next newline is not treated as a real string here, so this
        // quietly gives up and lets the raw byte scan continue from the
        // SAME opening quote instead of risking a false match against some
        // unrelated later quote -- see skipToNextConditionalMarker's own
        // comment (Scanner.h) for why this never diagnoses anything either
        // way.
        if (Text[Pos] == '\'') {
            size_t Look = Pos + 1;
            bool   Closed = false;
            while (Look < Text.size() && Text[Look] != '\n') {
                if (Text[Look] == '\'') {
                    // Doubled '' escape (a literal quote): the string keeps
                    // going, exactly like scanQuotedFragment's own rule.
                    if (Look + 1 < Text.size() && Text[Look + 1] == '\'') {
                        Look += 2;
                        continue;
                    }
                    Closed = true;
                    ++Look;
                    break;
                }
                ++Look;
            }
            if (Closed) {
                Pos = Look; // skip the whole 'literal', quotes included
                continue;
            }
            ++Pos; // no closing quote on this line: not a real string here
            continue;
        }

        // A `//` line comment's own content gets the identical treatment
        // (issue #644's other repro shape, `{$IFDEF X} // {$ENDIF}`).
        // Unlike a string literal this always succeeds: a line comment has
        // no closing delimiter to fail to find, only the next newline or
        // end of file, both of which raw-skipping already stops at safely.
        if (Opts.turbo() && Text[Pos] == '/' && Pos + 1 < Text.size() &&
                Text[Pos + 1] == '/') {
            while (Pos < Text.size() && Text[Pos] != '\n')
                ++Pos;
            continue;
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

    // {$I+}/{$I-}: the IOChecks switch, not this directive -- dispatchDirective
    // tries dispatchSwitchDirective right after this function, which is what
    // actually records it.  Its long name is "iochecks", never "include", so
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

    // Then the process's current working directory -- confirmed against
    // `fpc -Mtp` (issue #657): a NESTED {$I} (one reached from inside an
    // already-included file, so CurDir above is that file's own directory,
    // not the project root the invocation actually ran from) still resolves
    // a project-root-relative path fpc accepts, because fpc's own include
    // search always tries the compiler's cwd as one of its fixed steps,
    // independent of which file is doing the including.  Skipped when cwd
    // itself cannot be determined (Ec set) rather than treated as a hard
    // error -- one failed candidate among several, exactly like a
    // -Fi<dir> that does not exist either.
    std::error_code Ec;
    const fs::path Cwd = fs::current_path(Ec);
    if (!Ec)
        if (const fs::path Candidate = Cwd / FP; isReadableFile(Candidate))
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

    // A switch table keys entirely on raw offset, and *NewID's whole buffer
    // sits at a *later* stretch of the shared coordinate space than
    // wherever in the includer this {$I} stood (SourceManager lays buffers
    // out in the order they are opened, not the order their text is read) --
    // so without an explicit point here, a query for a location early in the
    // new buffer, before its own first switch directive (if it has one at
    // all), would search backwards through raw offsets and could land on
    // some earlier-included, higher-numbered buffer's own last point instead
    // of the includer's actually-current state.  Only when Switches is
    // already real: an include that never sees a switch directive, in a file
    // that never sees one either, must cost nothing and change nothing (see
    // switches()'s own null-means-none contract).
    if (Switches) Switches->record(SM->getLocForOffset(FID, 0), CurrentSwitchState);
}

bool Scanner::popInclude() {
    if (IncludeStack.empty()) return false;
    const IncludeFrame& F = IncludeStack.back();
    FID  = F.FID;
    Text = F.Text;
    Pos  = F.Pos;
    IncludeStack.pop_back();
    OpenIncludePaths.pop_back();

    // The mirror of openInclude's own boundary point: {$I file} splices the
    // included text in "as if" it had been typed at the directive itself, so
    // a switch the include changed carries into whatever comes after it in
    // the includer, exactly as it would if the file's contents had been
    // pasted there by hand.  Without this, the includer's own resume offset
    // -- always numerically SMALLER than anything in the buffer just
    // finished, for the same reason openInclude's point had to be added --
    // would search backwards past the whole include and find only the
    // includer's OWN last point from before it, silently reverting any
    // change the include just made the moment control returns.
    if (Switches) Switches->record(SM->getLocForOffset(FID, Pos), CurrentSwitchState);
    return true;
}

// ---------------------------------------------------------------------------
// {$R+}-style switches: CompilerSwitches.def's SWITCH table
// ---------------------------------------------------------------------------
//
// Real Turbo/FPC give the letter form and the long-name form different
// argument grammars, confirmed against fpc's own scanner source
// (tscannerfile.handledirectives in scanner.pas) rather than assumed:
//
//   - The letter form is tried FIRST, before any directive name is even
//     looked up by word, and ONLY when the name is exactly one character
//     immediately followed by '+' or '-' -- no space, and no 'ON'/'OFF'.
//     Anything else and the letter falls through to be looked up as a named
//     directive instead, which is how `{$R+}` (RangeChecks) and
//     `{$R resourcefile}` (a Windows-resource directive plang does not
//     implement) coexist on the very same letter in real Borland/FPC: R, D,
//     F, L and M are each a switch AND an unrelated named directive that
//     takes a real argument, told apart purely by whether '+'/'-' comes
//     right after.  plang's own SWITCH table never puts two meanings on one
//     letter, so this matters here only for staying honest about what a
//     letter that fails to parse as a switch falls through to (see below).
//   - The long-name form accepts either the same bare '+'/'-', or, with a
//     space before it, the word 'ON' or 'OFF'.  splitDirectiveBody already
//     trims the space away by the time Argument reaches here, so both
//     collapse to comparing the same trimmed text.
//
// A single directive body is one Name and one Argument in this codebase's
// existing shape (dispatchMessageDirective, dispatchConditionalDirective and
// dispatchIncludeDirective all assume it too) -- real FPC additionally lets
// several switches share one `{$R+,I-}` comment via a comma; that form is
// recognized separately, by dispatchMultiSwitchDirective, tried before Body
// is even split into a Name/Argument pair (see its own comment, issue #658).
//
// The letter form's adjacency requirement (this section's header comment)
// is enforced by the caller: Adjacent is dispatchDirective's own
// splitDirectiveBody() result for whether Argument started immediately
// after Name with no intervening whitespace (issue #604) -- only consulted
// when Name is a single letter, since the long-name form's own grammar
// (bare '+'/'-', or a SPACE then 'ON'/'OFF') never depended on adjacency in
// the first place, and splitDirectiveBody's trim already collapses `{$RANGECHECKS
// +}` and `{$RANGECHECKS+}` to the identical Argument either way.
bool Scanner::dispatchSwitchDirective(std::string_view Name,
                                      std::string_view Argument,
                                      SourceLocation Loc,
                                      bool Adjacent) {
    std::optional<Switch> Sw;
    std::optional<bool>   On;

    if (Name.size() == 1) {
        // Not adjacent -- `{$R +}`, `{$R resourcefile}`, anything with a
        // gap -- is never this switch, no matter what Argument holds: real
        // Turbo/FPC give the bare-adjacent spelling alone to the switch, and
        // leave every other shape on this letter to whatever OTHER, unrelated
        // directive it might be (a Windows resource file for 'R', an object
        // file for 'L', ...), most of which land in dispatchIgnoredDirective
        // or the unknown-directive fallback instead -- see this section's
        // header comment.
        if (Adjacent) {
            Sw = switchFromLetter(Name[0]);
            if (Sw) {
                if (Argument == "+")      On = true;
                else if (Argument == "-") On = false;
                // Anything else (a filename, 'ON'/'OFF' with no '+'/'-') is
                // not this switch in real Turbo/FPC either -- Sw stays set
                // but On does not, and the function returns false below,
                // same as if Name had never matched a switch at all.
            }
        }
    } else {
        Sw = switchFromLongName(toLower(Name));
        if (Sw) {
            if (Argument == "+")      On = true;
            else if (Argument == "-") On = false;
            else {
                const std::string Folded = toLower(Argument);
                if (Folded == "on")       On = true;
                else if (Folded == "off") On = false;
            }
        }
    }
    if (!Sw || !On) return false;

    applySwitch(*Sw, *On, Loc);
    return true;
}

// Records Sw = On at Loc into Switches/CurrentSwitchState, lazily building
// the table the first time any switch directive is actually recognized --
// see Switches' own comment in Scanner.h for why null has to mean exactly
// "never touched" rather than "off".  Shared by dispatchSwitchDirective (one
// switch per directive) and dispatchMultiSwitchDirective (several switches
// sharing one `{$R+,I-}` comma-separated directive, issue #658): both
// dispatch to the exact same underlying state, so a comma form and its
// single-switch equivalents combine identically either way.
void Scanner::applySwitch(Switch Sw, bool On, SourceLocation Loc) {
    if (!Switches) {
        Switches = std::make_shared<SwitchTable>();
        CurrentSwitchState = Opts.defaultSwitches();
    }
    CurrentSwitchState.set(Sw, On);
    Switches->record(Loc, CurrentSwitchState);
}

// The comma-separated multi-switch form, `{$R+,I-}` (issue #658): real
// Turbo/FPC let several one-letter switches share a single directive
// comment this way, each comma-separated element its own adjacent
// Letter+Sign pair -- the exact same adjacency rule the single-switch
// letter form enforces (this section's header comment, issue #604), just
// applied once per element instead of once per directive.  Body is the
// RAW, unsplit directive text (not yet even a Name/Argument pair; there is
// no single Name for a directive that is really several), so this has to
// run before splitDirectiveBody() does, and has to fail (return false)
// cleanly and cheaply for every directive that is not this form -- which is
// most of them -- so ordinary single-directive dispatch is not slowed down
// by it.
//
// Deliberately strict: every comma-separated element must parse as exactly
// two characters, an ASCII letter immediately followed by '+' or '-', with
// only whitespace tolerated around the commas themselves (`{$R+, I-}`) --
// never inside an element (`{$R +,I-}` is rejected, same as `{$R +}` alone
// is under issue #604's adjacency rule). One unparseable element fails the
// WHOLE directive -- nothing is applied, and Body falls through to ordinary
// single-directive dispatch, which will report it unknown -- rather than
// applying a partial prefix and silently discarding the rest.
bool Scanner::dispatchMultiSwitchDirective(std::string_view Body, SourceLocation Loc) {
    if (Body.find(',') == std::string_view::npos) return false;

    std::vector<std::pair<Switch, bool>> Parsed;
    size_t Start = 0;
    while (Start <= Body.size()) {
        const size_t Comma = Body.find(',', Start);
        std::string_view Elem = Body.substr(
            Start, Comma == std::string_view::npos ? std::string_view::npos
                                                    : Comma - Start);
        while (!Elem.empty() && std::isspace(static_cast<unsigned char>(Elem.front())))
            Elem.remove_prefix(1);
        while (!Elem.empty() && std::isspace(static_cast<unsigned char>(Elem.back())))
            Elem.remove_suffix(1);

        if (Elem.size() != 2) return false; // not Letter+Sign, adjacent
        const char Sign = Elem[1];
        if (Sign != '+' && Sign != '-') return false;
        const std::optional<Switch> Sw = switchFromLetter(Elem[0]); // case insensitive
        if (!Sw) return false;
        Parsed.emplace_back(*Sw, Sign == '+');

        if (Comma == std::string_view::npos) break;
        Start = Comma + 1;
    }
    // Fewer than two elements is not genuinely a comma form at all (an empty
    // trailing element after a stray comma, say) -- fall through and let
    // ordinary single-directive dispatch give its own honest answer instead
    // of this one claiming a directive it was never really written to mean.
    if (Parsed.size() < 2) return false;

    for (const auto& [Sw, On] : Parsed) applySwitch(Sw, On, Loc);
    return true;
}

// ---------------------------------------------------------------------------
// Accept-and-ignore: every other real Turbo/Borland/FPC directive this
// milestone does not act on
// ---------------------------------------------------------------------------
//
// A plain array, not an X-macro table: like the message-directive table
// above, it has exactly one consumer and is not expected to grow much (it is
// Borland/FPC's own fixed vocabulary of directives outside plang's scope, not
// this compiler's own design surface) -- see this file's header comment for
// why that is the deciding factor here, same as it was there.
//
// The single letters are DOS/Windows/386-target concerns this project's own
// target (a native LLVM-backed Linux/macOS compiler, see README.md) has no
// analogue for -- data alignment (A), debug/description info (D),
// coprocessor emulation (E), far calls (F), 286 instructions / imported data
// (G), object linking (L), memory sizing (M), numeric coprocessor (N),
// overlays (O), reference/browser info (Y), 8086 smart callbacks (K), and
// Pentium-safe FDIV (U).  The long names are newer Delphi/FPC directives
// with the same "real, but nothing plang's own target needs it to mean"
// shape: application type and codepage metadata for a produced executable
// (APPTYPE, CODEPAGE), record/field layout hints CodeGen does not implement
// (PACKRECORDS, ALIGN), linker behavior (SMARTLINK), an FPC-specific warning
// vocabulary distinct from plang's own -W flags (WARN), and IDE code-folding
// markers with no effect on compilation at all in ANY compiler
// (REGION/ENDREGION -- included as a pair so a program that opens one is not
// told plang has never heard of the one that closes it).
namespace {
constexpr std::string_view IgnoredDirectiveNames[] = {
    "a", "d", "e", "f", "g", "l", "m", "n", "o", "y", "k", "u",
    "apptype", "codepage", "packrecords", "align", "smartlink", "warn",
    "region", "endregion",
};
} // namespace

bool Scanner::dispatchIgnoredDirective(std::string_view Name, SourceLocation Loc) {
    const std::string Folded = toLower(Name);
    for (const std::string_view Known : IgnoredDirectiveNames) {
        if (Folded != Known) continue;
        emitError(Loc, diag::warn_directive_ignored, {Name});
        return true;
    }
    return false;
}
