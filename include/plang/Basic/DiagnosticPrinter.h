#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Basic/StringUtil.h"

#include <algorithm>
#include <format>
#include <string>

namespace plang {

/// Renders diagnostics as text, in the form gcc and clang use:
///
///     prog.pas:3:11: error: expected expression, got ';'
///       writeln(;
///               ^
///
/// The source line and the caret need the text of the file, which is why this
/// takes a SourceManager and why Diagnostic itself cannot format anything: all
/// a diagnostic holds is a four-byte position.
///
/// \p Prefix stands in for the file, line and column when a diagnostic has no
/// place in the source, which every diagnostic from the driver does:
///
///     plang: error: no input files
///
/// clang does the same, through TextDiagnosticPrinter::setPrefix.  Without it
/// such a diagnostic would begin with the bare severity, and "error: no input
/// files" does not say who is unable to proceed.
class DiagnosticPrinter {
public:
    DiagnosticPrinter(const SourceManager& SM, bool UseColor,
                      bool ShowCarets = true, std::string_view Prefix = {})
        : SM(&SM), UseColor(UseColor), ShowCarets(ShowCarets), Prefix(Prefix) {}

    /// The whole diagnostic, source line and caret included, without a
    /// trailing newline.
    [[nodiscard]] std::string print(const Diagnostic& D) const {
        std::string Out = printHeadline(D);
        if (!ShowCarets) return Out;
        if (std::string Caret = printSnippet(D.Loc); !Caret.empty())
            Out += "\n" + Caret;
        return Out;
    }

    /// Just the "file:line:col: severity: message" line.
    [[nodiscard]] std::string printHeadline(const Diagnostic& D) const {
        const std::string Sev = severityLabel(D.Severity, UseColor);
        const PresumedLoc P   = SM->getPresumedLoc(D.Loc);
        // D.Message can itself embed attacker-controlled text (an
        // identifier lexed from source, an argv-supplied filename quoted
        // into "no such file or directory", ...), so it goes through
        // escapeControlChars the same as a filename does -- otherwise a
        // raw control byte reaches stderr from inside the message body
        // instead of just the "file:line:col:" prefix in front of it.
        const std::string Msg = escapeControlChars(D.Message);
        if (!P.isValid()) {
            if (Prefix.empty()) return std::format("{}: {}", Sev, Msg);
            return std::format("{}: {}: {}", Prefix, Sev, Msg);
        }
        // P.Filename is whatever the command line named -- not text plang
        // wrote -- so it goes through escapeControlChars before it reaches
        // stderr: see that function's comment for why.
        return std::format("{}:{}:{}: {}: {}", escapeControlChars(P.Filename),
                           P.Line, P.Column, Sev, Msg);
    }

    /// The offending source line with a caret under it, or empty if there is
    /// no line to show.
    [[nodiscard]] std::string printSnippet(SourceLocation Loc) const {
        const PresumedLoc P = SM->getPresumedLoc(Loc);
        if (!P.isValid()) return {};
        const std::string_view Line = SM->getLineText(Loc);
        if (Line.empty()) return {};

        // Build the *escaped* display line and the caret's indent in one
        // pass over Line, cell by cell, the same way the UTF-8-aware indent
        // computation this replaces already did (#285): P.Column counts
        // display cells, not bytes, so a UTF-8 continuation byte belongs to
        // the same cell as the lead byte before it and contributes nothing
        // of its own to either string, or a multi-byte character earlier on
        // the line would push things past where P.Column says they belong.
        //
        // Line is source text, not text plang wrote, so -- like a filename
        // or a diagnostic message body -- a raw control byte in it must not
        // reach stderr as itself (#303): it goes through the same escaping
        // as escapeControlChars, turned into a 4-character \xHH sequence.
        // Doing that naively (escape the whole line, then reuse the old
        // byte-counting indent loop) breaks caret alignment, since a cell
        // that used to contribute one character to the line now
        // contributes four; instead each cell's *escaped* width is what
        // gets echoed into Indent, so Indent and DisplayLine's prefix
        // before the caret always have the same length.  Tab is the one
        // control byte kept unescaped: it is ordinary, common source
        // indentation, not a threat, and the existing "a tab in the text
        // has to stay a tab in the indent" trick -- keeping the terminal's
        // own tab stops doing the alignment -- only works if the tab
        // reaching the terminal is still a tab.
        std::string DisplayLine;
        DisplayLine.reserve(Line.size());
        std::string Indent;
        Indent.reserve(P.Column);
        unsigned Col      = 1;
        size_t   CaretPos = std::string::npos;
        for (size_t I = 0; I < Line.size(); ++I) {
            const unsigned char C = static_cast<unsigned char>(Line[I]);
            if (isUtf8ContinuationByte(Line[I])) {
                DisplayLine += Line[I];
                continue;
            }
            if (Col == P.Column) CaretPos = DisplayLine.size();
            if ((C < 0x20 && C != '\t') || C == 0x7F) {
                static const char Hex[] = "0123456789abcdef";
                DisplayLine += '\\';
                DisplayLine += 'x';
                DisplayLine += Hex[C >> 4];
                DisplayLine += Hex[C & 0xF];
                if (Col < P.Column) Indent += "    ";
            } else {
                DisplayLine += Line[I];
                if (Col < P.Column) Indent += (Line[I] == '\t') ? '\t' : ' ';
            }
            ++Col;
        }
        // P.Column can name a cell past the end of the line (an error at
        // "end of line"); Indent has then already been built out to the
        // full (escaped) line length, so CaretPos matches it here too.
        if (CaretPos == std::string::npos) CaretPos = DisplayLine.size();

        // Long lines get a window around the error column instead of being
        // quoted in full: an N-byte line otherwise produces an ~N-byte
        // diagnostic, unbounded by anything the user did wrong on any
        // *other* line.  kMaxDisplayWidth is chosen to sit far above any
        // realistic Pascal source line -- the existing caret-alignment
        // tests all quote lines under 50 columns -- so ordinary programs
        // never see a truncated snippet.
        constexpr size_t kMaxDisplayWidth = 200;
        constexpr size_t kContextRadius   = 80;
        if (DisplayLine.size() > kMaxDisplayWidth) {
            const size_t Start = CaretPos > kContextRadius ? CaretPos - kContextRadius : 0;
            const size_t End   = std::min(DisplayLine.size(), CaretPos + kContextRadius);
            const std::string_view Prefix = Start > 0 ? "..." : "";
            const std::string_view Suffix = End < DisplayLine.size() ? "..." : "";
            // Indent's length is always exactly CaretPos (see above), so
            // slicing it at the same Start the window uses keeps it lined
            // up under the truncated DisplayLine's caret cell.
            Indent = std::string(Prefix.size(), ' ') + Indent.substr(Start);
            DisplayLine =
                std::string(Prefix) + DisplayLine.substr(Start, End - Start) + std::string(Suffix);
        }

        std::string Caret = UseColor ? "\033[1;32m^\033[0m" : "^";
        return DisplayLine + "\n" + Indent + Caret;
    }

private:
    const SourceManager* SM;
    bool                 UseColor;
    bool                 ShowCarets;
    std::string          Prefix;
};

} // namespace plang
