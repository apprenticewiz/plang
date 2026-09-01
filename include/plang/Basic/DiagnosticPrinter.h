#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Basic/StringUtil.h"

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

        // Indent the caret with the line's own leading whitespace so that it
        // lands under the right character however the source is indented; a
        // tab in the text has to stay a tab in the indent.  P.Column counts
        // display cells, not bytes (#285, see SourceManager::getPresumedLoc),
        // so this has to walk Line the same way: a UTF-8 continuation byte
        // belongs to the same cell as the lead byte before it and gets no
        // indent character of its own, or a multi-byte character earlier on
        // the line would push the caret further right than P.Column says.
        std::string Indent;
        Indent.reserve(P.Column);
        unsigned Col = 1;
        for (size_t I = 0; I < Line.size() && Col < P.Column; ++I) {
            if (isUtf8ContinuationByte(Line[I])) continue;
            Indent += (Line[I] == '\t') ? '\t' : ' ';
            ++Col;
        }

        std::string Caret = UseColor ? "\033[1;32m^\033[0m" : "^";
        return std::string(Line) + "\n" + Indent + Caret;
    }

private:
    const SourceManager* SM;
    bool                 UseColor;
    bool                 ShowCarets;
    std::string          Prefix;
};

} // namespace plang
