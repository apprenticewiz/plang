#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Basic/Token.h"

#include <initializer_list>
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

    /// The buffer this scanner is reading, for a caller that needs it after
    /// construction but before the scanner itself is moved from (-g's
    /// Codegen::setSourceManager, which needs the main file's FileID).
    /// Invalid if construction failed to open a buffer at all.
    [[nodiscard]] FileID fileID() const { return FID; }

private:
    LangOptions          Opts;   // dialect and warning options (owned copy)
    const SourceManager* SM;     // owns the text; not owned here
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

    // The position of byte Offset in the buffer.  Line and column are the
    // SourceManager's business, which is why the scanner tracks neither.
    [[nodiscard]] SourceLocation locAt(size_t Offset) const {
        return SM->getLocForOffset(FID, Offset);
    }

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
    // Dispatches by name to the one category implemented so far
    // (dispatchMessageDirective); an unrecognized name is reported rather
    // than silently ignored or treated as a plain comment.
    //
    // This is the extension point Cluster B's later items plug into: a
    // conditional-compilation handler ({$IFDEF}/{$IFNDEF}/{$ENDIF}/{$ELSE}/
    // {$ELSEIF}) -- now dispatchConditionalDirective, below -- an {$I file}
    // include handler, and a {$R+}-style switch handler (CompilerSwitches.def's
    // SwitchTable already exists and is only waiting for this) each add
    // their own "try this category" call here, in dispatchMessageDirective's
    // own shape -- (Name, Argument, Loc) -> bool handled -- tried in turn
    // before the final warn_directive_unknown fallback.  The latter two do
    // not exist yet.
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
