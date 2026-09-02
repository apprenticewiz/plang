#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Basic/Token.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace plang {

/// Lexical analyzer for Pascal source files.
/// Reads the entire file into memory on construction, then yields one token at
/// a time via next().  Repeated calls after Eof safely return Eof again.
///
/// All lexical errors are appended to the shared diagnostics vector passed at
/// construction; the Scanner never throws.  On an unrecognized character it
/// emits a diagnostic and returns TokenKind::Error (the Parser skips these).
/// On an unterminated comment or string it emits a diagnostic and resumes
/// scanning from the nearest sensible point.
class Scanner {
public:
    /// Reads Filename into \p SM and scans it.
    /// On failure, reports a diagnostic and leaves the scanner in an empty
    /// state (next() will immediately return Eof).
    explicit Scanner(SourceManager& SM, std::string Filename,
                     DiagnosticsEngine& Diags, LangOptions Opts = {});

    /// Scan an in-memory buffer instead of a file.
    /// \p SourceName is what diagnostics call it (e.g. "<pmi>").
    explicit Scanner(SourceManager& SM, std::string SourceName,
                     std::string Content, DiagnosticsEngine& Diags,
                     LangOptions Opts = {});

    /// Returns the next token from the source, advancing past it.
    /// Skips whitespace, comments, and TokenKind::Error tokens before returning.
    /// Returns TokenKind::Eof once the end of input is reached and on every
    /// subsequent call.
    Token next();

    /// One-shot override of startsExpression(PrevKind)'s allow-list (see
    /// PrevKind's own comment) for the very next call to next(): the parser
    /// calls this immediately before that call, at a point the grammar
    /// itself already guarantees the upcoming token begins an expression
    /// even though the token just consumed -- '=' or 'of' -- is not on
    /// PrevKind's allow-list. Both are genuinely ambiguous by token kind
    /// alone: '=' also introduces a type-definition's type-denoter (`type P
    /// = ^Integer`) and 'of' also introduces `array of`/`set of`/`file of`'s
    /// element type, so the allow-list itself can't simply add them without
    /// breaking those -- only the parser, at the specific production it is
    /// currently in (a const-declaration's value, a case-statement's label
    /// list), knows an expression is what has to follow (issue #600).
    /// Consumed by the very next next() call whether or not that call
    /// actually reads a `^letter` literal, so it can never leak into some
    /// later, unrelated '^'.
    void allowCaretControlCharNext() { ForceExprStartOnce = true; }

    /// The buffer this scanner is reading, for a caller that needs it after
    /// construction but before the scanner itself is moved from (-g's
    /// Codegen::setSourceManager, which needs the main file's FileID).
    /// Invalid if construction failed to open a buffer at all.
    [[nodiscard]] FileID fileID() const { return FID; }

    /// The position-keyed table `{$R+}`-style switch directives have built up
    /// so far, or null if none has been seen -- the same "no directive, no
    /// table" contract LangOptions::Switches documents, since this Scanner's
    /// own `Switches` member below IS that table under construction.  Null for
    /// every ISO 7185 and Extended Pascal scan, and for a Turbo scan that
    /// never wrote a switch directive.
    ///
    /// A caller that wants `Opts.switchOn` to see what THIS scan recorded has
    /// to attach this back onto the LangOptions it hands to Parser/Sema/
    /// Codegen itself: the Scanner is handed its own copy of LangOptions at
    /// construction (see the Opts field below) and mutates only that copy, the
    /// same way every other dialect option already works, so nothing
    /// downstream sees this by just sharing the constructor's LangOptions
    /// argument.  frontendPC1Main calls this once parsing has finished
    /// (Parser::switches(), which forwards to this) and sets Opts.Switches
    /// from it before constructing Sema/Codegen.
    [[nodiscard]] std::shared_ptr<const SwitchTable> switches() const { return Switches; }

private:
    LangOptions          Opts;   // dialect and warning options (owned copy)
    // Owns the text; not owned here.  Non-const (unlike the SourceManager&
    // this Scanner sees no other reason to mutate through) because an
    // {$I file}/{$INCLUDE file} splices a new buffer into THIS same
    // SourceManager mid-scan (openInclude, in Directives.cpp) -- the
    // included file's text has to live somewhere past the directive that
    // named it, and SourceManager's own deque-of-buffers design (see its
    // header comment) is exactly what already lets a Scanner hold a
    // string_view into a buffer that outlives the call that created it.
    SourceManager*       SM;
    FileID               FID;    // the buffer being scanned
    std::string_view     Text;   // that buffer's text, owned by SM
    DiagnosticsEngine&   Diags;  // shared diagnostic sink
    size_t               Pos;    // index of the next character to be consumed

    // The kind of the last non-Error token next() returned, or Eof before the
    // first one.  The only consumer is the Turbo `^ctrl` control-character
    // literal (see caretLooksLikeControlChar()/startsExpression in
    // Scanner.cpp): `^` is also Caret, used for postfix dereference (`p^`)
    // and for a pointer type's prefix (`type P = ^T`), so a fresh `^` at the
    // top of next() is only read as the start of a new literal when the
    // token just before it is one that can begin an expression -- never
    // Colon/Equal/Of, which is how `type PM = ^Integer` keeps meaning a
    // pointer type instead of being misread as a control-character literal.
    TokenKind            PrevKind = TokenKind::Eof;

    // Set by allowCaretControlCharNext() (see its own comment, above); read
    // and unconditionally cleared alongside PrevKind at the bottom of next(),
    // so it governs exactly one upcoming token.
    bool                 ForceExprStartOnce = false;

    // The position of byte Offset in the buffer.  Line and column are the
    // SourceManager's business, which is why the scanner tracks neither.
    [[nodiscard]] SourceLocation locAt(size_t Offset) const {
        return SM->getLocForOffset(FID, Offset);
    }

    // MS-DOS Ctrl-Z end-of-file truncation (-std=turbo only): see the
    // constructors' own comment in Scanner.cpp, where this used to be a
    // free function local to that file.  Promoted to a static member so
    // openInclude (Directives.cpp) can apply the identical rule to a buffer
    // an {$I file}/{$INCLUDE file} opens mid-scan, the same way each
    // constructor already applies it to the buffer it opens.
    static std::string_view truncateAtCtrlZ(std::string_view Text,
                                            const LangOptions& Opts);

    // Returns the character at Pos+1 without advancing, or '\0' at end of input.
    char peek() const;

    // Advances past whitespace and comments: { } and (* *) in every dialect,
    // plus, under -std=turbo only, a `//` line comment running to the next
    // newline.
    void skipWhitespaceAndComments();

    // Advances past a comment, opened with '{' when \p Braced and with '(*'
    // otherwise.  ISO §6.1.8 lets either terminator close either, so which one
    // opened it decides only how many characters to step over.  An
    // unterminated comment appends a diagnostic and returns where it stopped.
    // Dispatches to skipCommentTurbo instead when Opts.turbo(): Borland's rule
    // is the opposite one (a delimiter must be closed by its own kind), so
    // that path is a full fork rather than a branch threaded through this one.
    void skipComment(bool Braced);
    void skipCommentTurbo(bool Braced);
    void skipBraceComment();
    void skipParenthesisComment();

    // Advances past a -std=turbo `//` line comment (already positioned at the
    // first '/'), stopping before the newline that ends it (or at EOF).
    void skipLineComment();

    // Advances past a Turbo compiler directive -- '{$' or '(*$', a '{' or
    // '(*' immediately followed by '$' with no gap, confirmed by the caller
    // (skipWhitespaceAndComments) before Pos is at the opening delimiter's
    // first character, exactly skipComment's own contract.  Only ever
    // called under Opts.turbo(): ISO 7185 and Extended Pascal have no
    // directives, and a `{$anything}` there stays an ordinary comment,
    // handled by skipComment exactly as before this existed.
    //
    // Closes the same way skipCommentTurbo closes an ordinary Turbo
    // comment -- a same-kind terminator only ('}' for '{', '*)' for '(*')
    // -- since a directive is still a comment syntactically, just one this
    // scanner parses instead of discarding.  On success, hands the text
    // between '$' and the closing delimiter to dispatchDirective(); on an
    // unterminated or mismatched-delimiter directive, reports the same
    // diagnostics skipCommentTurbo would and gives up on it entirely
    // (nothing to dispatch).  Defined in Directives.cpp, not Scanner.cpp:
    // this is genuinely new machinery -- nothing recognized '{$...}' at all
    // before this -- not an extension of the ordinary-comment scanning
    // above it.
    void skipDirective(bool Braced);

    // Splits a directive's body (already isolated by skipDirective: the raw
    // text between '$' and the closing delimiter) into a name -- the
    // longest run of letters at the front, folded for lookup the same way
    // an identifier is -- and an argument -- everything after it, trimmed
    // of leading/trailing whitespace but otherwise passed through verbatim.
    // Dispatches by name to each category in turn -- dispatchMessageDirective,
    // dispatchConditionalDirective, dispatchIncludeDirective,
    // dispatchSwitchDirective, dispatchIgnoredDirective, in that order, each
    // in the same (Name, Argument, Loc) -> bool "handled?" shape -- and
    // reports an unrecognized name rather than silently ignoring it or
    // treating it as a plain comment.
    void dispatchDirective(std::string_view Body, SourceLocation Loc);

    // The {$MESSAGE}/{$INFO}/{$NOTE}/{$HINT}/{$WARNING}/{$ERROR}/{$FATAL}
    // family: Name (already folded to lower case is not assumed -- this
    // folds it itself) looked up in a small table pairing each keyword with
    // the DiagID its Argument is reported through as %0.  Returns false,
    // having done nothing, when Name names none of the seven -- the cue for
    // dispatchDirective to try the next category, or give up.
    //
    // A real Turbo Pascal compiler's {$FATAL} aborts the compilation right
    // there instead of {$ERROR}'s report-and-keep-going (confirmed against
    // `fpc -Mtp`); plang approximates this only as far as its diagnostic
    // model allows -- {$FATAL} gets its own DiagID so its message reads
    // distinctly, but scanning continues, same as {$ERROR}.  Both still
    // fail the compilation once Sema/Frontend check hasErrors(); stopping
    // the scanner mid-buffer was tried and produces nothing but a cascade
    // of unrelated "expected X, got end of file" parser noise once that has
    // already happened, since plang has no immediate-unwind mechanism to
    // hook a real abort into (and is built -fno-exceptions besides).
    bool dispatchMessageDirective(std::string_view Name, std::string_view Argument,
                                  SourceLocation Loc);

    // ---- Conditional compilation: {$DEFINE}/{$UNDEF}/{$IFDEF}/{$IFNDEF}/
    // {$ELSE}/{$ELSEIF}/{$ENDIF} (lib/Lex/Directives.cpp) -------------------

    // One entry per {$IFDEF}/{$IFNDEF} whose matching {$ENDIF} has not yet
    // been scanned.  Pushed by dispatchConditionalDirective when the opening
    // directive is dispatched (whether or not its own branch turns out to be
    // live); popped when the matching {$ENDIF} is reached, either directly
    // in dispatchConditionalDirective (ordinary/live scanning) or inside
    // skipToNextConditionalMarker (skipping dead source).  A nested
    // {$IFDEF}/{$IFNDEF} encountered *while* skipping dead source for some
    // other frame is never pushed here at all -- see
    // skipToNextConditionalMarker's own comment for why.
    struct CondFrame {
        // Becomes true the moment any branch of this {$IFDEF}/{$ELSEIF}/
        // {$ELSE} chain is live (including the opening {$IFDEF}/{$IFNDEF}
        // itself, if its own condition holds).  Once true, no later
        // {$ELSEIF} in the same chain is ever evaluated -- only checked for
        // directive-syntax validity -- since only one branch of a chain can
        // ever run, no matter what a later one tests.
        bool AnyBranchTaken;
        // Becomes true once {$ELSE} has been seen for this chain, so a
        // further {$ELSEIF} or a second {$ELSE} is reported rather than
        // silently accepted.
        bool SeenElse = false;
        // Where the opening {$IFDEF}/{$IFNDEF} is, for
        // err_directive_unterminated_conditional if end of file is reached
        // with this frame still open.
        SourceLocation OpenLoc;
        // "IFDEF" or "IFNDEF", exactly as spelled at OpenLoc (case
        // preserved), reported as %0 of that same diagnostic.
        std::string OpenName;
    };
    std::vector<CondFrame> CondStack;

    // The symbols currently defined for `{$IFDEF}`/`{$IFNDEF}`/`{$ELSEIF}`.
    // Seeded from Opts.Defines at construction (folded through toLower again
    // regardless of whether Opts.Defines already was, since nothing enforces
    // that on every caller) and then mutated in place by `{$DEFINE}`/
    // `{$UNDEF}` as they are scanned.  See LangOptions::Defines's own
    // comment for why this lives only here, as plain mutable Scanner state,
    // rather than in a SwitchTable-style position-indexed table: nothing
    // downstream of the Scanner ever asks "was X defined at location L".
    std::set<std::string> CurrentDefines;

    // Tries the conditional-compilation directive family against Name,
    // called from dispatchDirective before the unknown-directive fallback,
    // in dispatchMessageDirective's own (Name, Argument, Loc) -> bool shape.
    // Unlike that one, this can consume far more source than the directive
    // it was handed: a false {$IFDEF}/{$IFNDEF}, or reaching {$ELSE}/
    // {$ELSEIF} after a branch that was live, hands off to
    // skipToNextConditionalMarker, which advances Pos raw through however
    // much dead source separates here from the next branch this chain can
    // still take, or its {$ENDIF}.
    bool dispatchConditionalDirective(std::string_view Name, std::string_view Argument,
                                      SourceLocation Loc);

    // Raw-skips forward from Pos -- already positioned just past the
    // directive that closed off the branch being left, a false {$IFDEF}/
    // {$IFNDEF} or an {$ELSE}/{$ELSEIF} reached after the branch before it
    // was live -- through however much source belongs to Frame's dead
    // remainder.  Frame must be CondStack.back() at the time of the call;
    // this never pushes to CondStack itself (a nested {$IFDEF}/{$IFNDEF}
    // found while skipping is tracked with a plain local depth counter, not
    // a new frame), so the reference stays valid for the whole call.
    //
    // Stops one of three ways: a live {$ELSEIF}/{$ELSE} is found (Frame
    // stays on CondStack, Pos left just after that directive's own closer,
    // ordinary scanning resumes there); Frame's own {$ENDIF} is found (Frame
    // is popped, Pos left just after it); or end of file is reached first,
    // including a directive that never finds its own closing delimiter --
    // reports err_directive_unterminated_conditional at Frame.OpenLoc, pops
    // Frame, and leaves Pos at end of file.
    //
    // No directive found while skipping -- {$DEFINE}, {$MESSAGE}, an
    // unknown name, anything but {$IFDEF}/{$IFNDEF}/{$ELSE}/{$ELSEIF}/
    // {$ENDIF} -- is ever dispatched, or even inspected past its own Name:
    // real Turbo/FPC never evaluate a directive inside a branch that was
    // never taken, and this does not either.  The same is true of a plain
    // syntax error nested in the dead source (an unterminated string, a
    // mismatched comment delimiter, an unexpected character): none of it is
    // ever tokenized, so none of it is ever diagnosed.
    //
    // A single-quoted string literal and a `//` line comment ARE each
    // recognized as opaque spans while raw-skipping (issue #644): a
    // {$/(*$-looking substring INSIDE one of those -- 's := 'dead {$ENDIF}
    // text';' or '// {$ENDIF}' -- must not be mistaken for one of Frame's
    // own markers, the same way it already isn't for live source (next()'s
    // own dispatch never looks inside a string or line comment for one
    // either). This recognition never diagnoses anything, matching the
    // paragraph above: an apparent string with no closing quote before the
    // next newline is not treated as a real string here at all (silently --
    // the malformed-content contract still holds), and the raw byte scan
    // simply continues from its own opening quote as if this recognition
    // did not exist. A `{`/`(*`-opened ordinary (non-`$`) comment is
    // deliberately NOT given the same treatment: unlike a string or a `//`
    // comment, its own extent cannot always be told apart from "this is
    // genuinely unterminated, and the very next {$ENDIF}-looking text is a
    // REAL marker" without diagnosing the difference, which would break the
    // no-diagnostic contract above (confirmed against
    // dead-conditional-branch-malformed-content-produces-no-diagnostic.pas's
    // own deliberately-unterminated `{` comment, whose following {$ENDIF}
    // that test requires still be found).
    void skipToNextConditionalMarker(CondFrame& Frame);

    // Scans a directive body the same way skipDirective does -- same opener
    // width, same same-kind-terminator rule -- for
    // skipToNextConditionalMarker's use inside dead source: never
    // dispatches, and never diagnoses a missing terminator (an unterminated
    // or mismatched directive found while skipping dead source is exactly
    // the "genuine syntax error nobody will ever compile" this whole
    // mechanism exists not to report). On success returns the raw body text
    // with Pos left just past the closer; on failure -- ran off the end of
    // the buffer first -- returns std::nullopt with Pos left at Text.size().
    // Already positioned at the opening delimiter's first character, same
    // contract as skipDirective.
    std::optional<std::string_view> rawDirectiveBody(bool Braced);

    // Called from next() right before it would return Eof: reports
    // err_directive_unterminated_conditional for every frame still on
    // CondStack (a live {$IFDEF}/{$IFNDEF} whose own {$ENDIF} was never
    // reached because the file simply ended) and clears CondStack, so a
    // second next() call after Eof -- which next()'s own contract
    // guarantees stays Eof -- does not report the same thing twice.  A
    // no-op, as it must be for ISO 7185/Extended Pascal, whenever CondStack
    // is already empty, which it always is under those dialects.
    void reportUnterminatedConditionals();

    // ---- {$I file}/{$INCLUDE file} (lib/Lex/Directives.cpp) ---------------
    //
    // Splices the named file's contents into the token stream right where
    // the directive stands, "as if" it had been written there directly --
    // this directive's own field name in real Turbo Pascal.  That "as if"
    // is taken literally: unlike CondStack/CurrentDefines above, nothing
    // about conditional-compilation state is saved or restored around an
    // include, so a {$IFDEF} opened in one file and closed by an {$ENDIF}
    // in another (or a {$DEFINE} that carries across the boundary either
    // way) behaves exactly as it would if the included text had been
    // pasted in by hand. What genuinely is per-file -- which buffer is
    // being read, and where in it -- is saved and restored, in the same
    // (FID, Text, Pos) triple every other scanning function already
    // threads through, one IncludeFrame per file currently spliced in.

    // One entry per include still open, in nesting order: IncludeStack.back()
    // is the file that most recently did an {$I}/{$INCLUDE}, i.e. the one to
    // resume once the current (innermost) buffer runs out.  Pushed by
    // openInclude right before switching FID/Text/Pos onto the included
    // buffer; popped by popInclude, called from next() in place of
    // returning Eof whenever this is non-empty.
    struct IncludeFrame {
        FileID            FID;
        std::string_view  Text;
        size_t            Pos;
    };
    std::vector<IncludeFrame> IncludeStack;

    // The on-disk identity (SM->addFile's own Path, canonicalized when
    // possible -- see openInclude) of every file currently open: the main
    // file, pushed once at construction, plus one entry per frame on
    // IncludeStack, in the same order.  Invariant:
    // OpenIncludePaths.size() == IncludeStack.size() + 1 whenever the main
    // file was opened from disk (the Scanner(SourceManager&, std::string,
    // ...) constructor); the in-memory-buffer constructor pushes nothing,
    // since it names no real file to protect a self-include against -- see
    // that constructor's own comment in Scanner.cpp.  What
    // dispatchIncludeDirective checks a resolved candidate against before
    // ever opening it: a match means A includes A, or A includes B
    // includes ... includes A, and is reported instead of recursed into.
    std::vector<std::string> OpenIncludePaths;

    // The on-disk identity Path is recorded under in OpenIncludePaths: Path
    // canonicalized (symlinks resolved, "."/".." segments collapsed) when
    // that succeeds, or Path itself unchanged if it doesn't -- e.g. a
    // transient race between the existence check that already confirmed
    // Path names a real file and this call.  A same-file comparison that
    // occasionally misses two different spellings of the same path because
    // of a race is still strictly better than one that cannot be computed
    // at all whenever canonicalization has a bad moment.  Used both by the
    // file-path constructor (Scanner.cpp, for the main file) and by
    // openInclude (Directives.cpp, for each included file), which is why
    // this is declared here rather than file-local to either .cpp.
    static std::string canonicalIdentity(const std::string& Path);

    // Tries the {$I}/{$INCLUDE} directive family against Name, called from
    // dispatchDirective in dispatchMessageDirective's own (Name, Argument,
    // Loc) -> bool shape.  Returns false, having done nothing, for every
    // Name but "i"/"include" folded -- OR for "i" with an Argument of
    // exactly '+' or '-', which is the IOChecks switch instead (its long
    // name is "iochecks", never "include", so only the one-letter spelling
    // needs this guard): dispatchDirective tries dispatchSwitchDirective
    // right after this one, so leaving {$I+}/{$I-} unclaimed here is what
    // lets them reach it and be recorded as the real switch toggle they are,
    // rather than being misread as an attempt to include a file literally
    // named "+" or "-".
    //
    // A recognized Argument is unquoted (a single layer of surrounding '
    // ... ', if present, stripped -- confirmed against `fpc -Mtp`, which
    // accepts both {$I foo.inc} and {$I 'foo.inc'}) and, if still empty,
    // reported as err_directive_include_expects_filename; otherwise handed
    // to openInclude.
    bool dispatchIncludeDirective(std::string_view Name, std::string_view Argument,
                                  SourceLocation Loc);

    // Resolves Filename (already unquoted) to a readable regular file's
    // path, or nullopt if none is found.  Search order: if Filename is
    // already absolute, tried as-is with no search at all; otherwise THIS
    // scanner's currently active buffer's own directory first (SM's Name
    // for FID, not the outermost file -- a nested include resolves
    // relative to its own immediate parent), then Opts.IncludeSearchPaths
    // (-Fi<dir>) in the order given -- the same "try each candidate, first
    // hit wins" shape Sema::resolveImports already uses for
    // Opts.ModuleSearchPaths/.pmi.
    [[nodiscard]] std::optional<std::string>
    resolveIncludePath(std::string_view Filename) const;

    // Resolves Filename via resolveIncludePath and, on success, switches
    // this Scanner onto it: pushes an IncludeFrame capturing exactly where
    // the including file's own scan currently stands, pushes the new
    // file's identity onto OpenIncludePaths, then sets FID/Text/Pos to the
    // start of the new buffer (added to *SM via addFile, the same call the
    // file-path constructor makes for the main file).  Reports
    // err_directive_include_not_found if Filename cannot be resolved or
    // opened at all, or err_directive_include_cycle if resolving it would
    // reopen a file already somewhere on OpenIncludePaths -- neither case
    // pushes anything, so scanning simply continues in the current buffer
    // right after the directive, same as any other directive that ends up
    // doing nothing.
    void openInclude(std::string_view Filename, SourceLocation Loc);

    // Called from next() in place of returning Eof whenever IncludeStack is
    // non-empty: pops the innermost frame, restoring FID/Text/Pos to
    // exactly where the including file's own scan left off, and pops
    // OpenIncludePaths to match. Returns false, having left everything
    // alone, when IncludeStack is already empty -- next()'s cue that this
    // really is the end of the whole token stream.
    bool popInclude();

    // ---- {$R+}-style switches (lib/Lex/Directives.cpp) ---------------------
    //
    // CompilerSwitches.def's SWITCH table, letter or long name, '+'/'-' (both
    // spellings) or ' ON'/' OFF' (long name only) argument -- see
    // dispatchSwitchDirective's own comment for exactly which combination is
    // accepted and why, confirmed against fpc -Mtp's own scanner source
    // rather than assumed.  Recorded into Switches/CurrentSwitchState below,
    // which is what switches() above hands back to a caller once scanning is
    // done.

    // The table under construction, or null until the first switch directive
    // this scan actually recognizes -- see switches()'s own comment for why
    // null has to mean exactly that.  Lazily allocated rather than built
    // eagerly in every Turbo constructor: a Turbo file that never writes a
    // switch directive (the common case) then costs nothing beyond the one
    // null check switchOn already makes.
    std::shared_ptr<SwitchTable> Switches;

    // The state Switches was last recorded at, i.e. what the NEXT record()
    // call starts from -- record() only ever hears about the one switch that
    // just changed, not the other thirteen, so this is what lets `{$R+}`
    // then `{$Q-}` combine into one CompilerState with both bits set rather
    // than each directive's own record() call overwriting the other's.
    // Meaningless (never read) while Switches is null; seeded from
    // Opts.defaultSwitches() the moment Switches is first allocated.
    CompilerState CurrentSwitchState;

    // Tries the switch-directive family against Name, called from
    // dispatchDirective in dispatchMessageDirective's own (Name, Argument,
    // Loc) -> bool shape, after dispatchIncludeDirective (whose own comment
    // explains the one letter, 'I', both this and it can mean).  Recognizes
    // Name as either a switch's Letter (exactly one character, case
    // insensitive) or its LongName (folded the same way every other
    // directive name is); a Name that matches neither, or an Argument that
    // does not parse for the spelling actually used, is left unclaimed --
    // returns false, doing nothing -- rather than diagnosed, since a
    // matching Letter with an unrecognized Argument is real, unimplemented
    // Borland/FPC syntax on the very same character (`{$R resourcefile}`,
    // `{$L object.o}`, ...) at least as often as it is a typo, and
    // warn_directive_unknown is the honest answer to both.
    bool dispatchSwitchDirective(std::string_view Name, std::string_view Argument,
                                 SourceLocation Loc);

    // ---- Accept-and-ignore directives (lib/Lex/Directives.cpp) -------------
    //
    // Every other real Turbo/Borland/FPC compiler directive this milestone
    // does not act on: DOS/Windows/386-target concerns (data alignment,
    // object linking, memory sizing, emulation, calling convention,
    // overlays, smart-linking safety, ...) this project's own target (a
    // native LLVM-backed Linux/macOS compiler) has no analogue for, plus a
    // handful of modern Delphi/FPC-only directives with the same shape.
    // Tried last, right before the unknown-directive fallback: recognized by
    // name (case insensitive, whatever its own argument grammar happens to
    // be -- unlike a switch's Argument, which is inspected, an ignored
    // directive's is not even looked at) and reported through
    // warn_directive_ignored rather than either silently doing nothing (a
    // user has no way to tell that from plang mis-scanning past it) or
    // warn_directive_unknown (which says "plang has never heard of this,"
    // untrue of a real directive this project has simply chosen not to
    // implement).
    bool dispatchIgnoredDirective(std::string_view Name, SourceLocation Loc);

    Token scanIdentifierOrKeyword(size_t TokenStart);
    Token scanNumber(size_t TokenStart);

    // Scans a single-quoted string, or, under -std=turbo, a run of one or
    // more adjacent string / #code / ^ctrl fragments glued into a single
    // StringLit -- 'AB'#13#10'CD' is one 6-character token, not four.  On an
    // unterminated or newline-spanning quoted fragment, appends a diagnostic
    // and returns the partial content accumulated so far as a StringLit.
    Token scanString(size_t TokenStart);

    // The three fragment kinds scanString glues together.  Each appends its
    // decoded character(s) to Lexeme and leaves Pos just past what it
    // consumed.  scanQuotedFragment/scanControlCodeFragment return false,
    // having already emitted a diagnostic, on malformed input (an
    // unterminated quote, or a '#' with no digits or a value that overflows
    // a Char) -- scanCaretFragment cannot fail, since it is only ever called
    // once caretLooksLikeControlChar() has confirmed the shape.
    bool scanQuotedFragment(std::string& Lexeme);
    bool scanControlCodeFragment(std::string& Lexeme);
    void scanCaretFragment(std::string& Lexeme);

    // True when the scanner is sitting on a '^' immediately followed (no gap)
    // by a letter -- the shape of a Turbo `^ctrl` control-character literal.
    // A shape check only: it says nothing about whether this position is
    // where a literal is actually wanted (see PrevKind's comment), which is
    // why mid-glue callers may use this alone but next()'s fresh-token
    // dispatch additionally consults startsExpression(PrevKind).
    [[nodiscard]] bool caretLooksLikeControlChar() const;

    // Turbo `$FF`-style hexadecimal integer literal: '$' followed by one or
    // more hex digits.  Not EP's `16#FF` nondecimal-base literal (that one is
    // handled inside scanNumber, gated on extendedPascal(), and shares no
    // code with this): different dialect, different leading character.  On
    // no digits or overflow, emits a diagnostic and returns TokenKind::Error.
    Token scanHexLiteral(size_t TokenStart);

    // Scans a one- or two-character operator / delimiter.
    // Returns TokenKind::Error (with the bad character as the lexeme) for any
    // character not recognized as a valid Pascal symbol.
    Token scanSymbol(size_t TokenStart);

    void emitError(SourceLocation Loc, std::string Msg);
    void emitError(SourceLocation Loc, DiagID ID,
                   std::initializer_list<std::string_view> Args = {});

    Token make(TokenKind Kind, std::string Lexeme, size_t TokenStart);
};

} // namespace plang
