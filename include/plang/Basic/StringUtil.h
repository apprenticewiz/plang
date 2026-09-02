#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace plang {

/// Return a lowercase copy of S.  Pascal identifiers are case-insensitive;
/// use this (or eqCI below) instead of inlining tolower loops everywhere.
[[nodiscard]] inline std::string toLower(std::string_view S) {
    std::string R{S};
    for (auto& C : R)
        C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
    return R;
}

/// Return true if A and B are equal under case-insensitive comparison.
/// Intended for Pascal identifier comparison.
[[nodiscard]] inline bool eqCI(std::string_view A, std::string_view B) {
    if (A.size() != B.size()) return false;
    for (size_t I = 0; I < A.size(); ++I)
        if (std::tolower(static_cast<unsigned char>(A[I])) !=
            std::tolower(static_cast<unsigned char>(B[I])))
            return false;
    return true;
}

/// True if S has the shape of a Pascal identifier: a letter or underscore,
/// followed by zero or more letters, digits, or underscores -- nothing else,
/// front to back.  Shared by lib/Lex/Directives.cpp (validating a `{$DEFINE
/// name}`/`{$UNDEF name}`/`{$IFDEF name}`/`{$IFNDEF name}`/`{$ELSEIF name}`
/// argument) and lib/Frontend/Frontend.cpp (validating -d<name>/-u<name>'s
/// glued-on value) so the two accept exactly the same symbol shapes.
[[nodiscard]] inline bool looksLikeIdentifier(std::string_view S) {
    if (S.empty()) return false;
    const unsigned char First = static_cast<unsigned char>(S.front());
    if (!std::isalpha(First) && First != '_') return false;
    for (const unsigned char C : S)
        if (!std::isalnum(C) && C != '_') return false;
    return true;
}

/// Whether \p C is a UTF-8 continuation byte (10xxxxxx): one that is not the
/// first byte of its character and so does not start a display cell of its
/// own.  A diagnostic's column needs to skip these to land under the right
/// glyph instead of counting raw bytes -- three bytes for one accented
/// letter is still one cell on the screen, and #285 was exactly that: a
/// caret landing cells to the right of its token whenever multi-byte UTF-8
/// text preceded it on the line.  This is a byte-classification, not a
/// decoder on its own -- see utf8SequenceLength below for the decoder that
/// actually validates a sequence rather than just recognizing this shape.
/// Shared between SourceManager (which turns a byte offset into a column
/// number) and DiagnosticPrinter (which draws the caret under that same
/// column), so the two can never disagree about which bytes count.
[[nodiscard]] inline bool isUtf8ContinuationByte(char C) {
    return (static_cast<unsigned char>(C) & 0xC0) == 0x80;
}

/// The length in bytes of the (validated) UTF-8 sequence beginning at
/// \p Text[I]: 1 for an ASCII byte, or the full sequence length (2-4) for a
/// well-formed multi-byte lead byte followed by exactly the continuation
/// bytes it calls for, with no truncation, overlong encoding, or surrogate
/// value.  Anything this cannot validate -- a bare/isolated continuation
/// byte, an invalid lead byte, or a lead byte not followed by enough valid
/// continuation bytes before the text ends -- returns 1, so the caller
/// treats that single malformed byte as its own display cell instead of
/// folding it into a sequence it does not actually belong to.
///
/// Issue #614: isUtf8ContinuationByte alone classifies a byte purely by its
/// own top bits, with no check that it is actually preceded by a valid lead
/// byte, so an isolated continuation byte such as 0x80 was silently folded
/// into whatever came before it -- shifting every column after it one to
/// the left and letting the raw byte reach diagnostic text unescaped. This
/// is a decoder (unlike isUtf8ContinuationByte), so it is what actually
/// tells "part of a real character" apart from "malformed, on its own".
[[nodiscard]] inline unsigned utf8SequenceLength(std::string_view Text, size_t I) {
    const unsigned char C = static_cast<unsigned char>(Text[I]);
    unsigned Len;
    if (C < 0x80) return 1;
    if (C >= 0xC2 && C <= 0xDF)      Len = 2;
    else if (C >= 0xE0 && C <= 0xEF) Len = 3;
    else if (C >= 0xF0 && C <= 0xF4) Len = 4;
    else return 1; // bare continuation byte, or an invalid/overlong lead byte (0x80-0xC1, 0xF5-0xFF)
    if (I + Len > Text.size()) return 1;
    for (size_t K = I + 1; K < I + Len; ++K)
        if (!isUtf8ContinuationByte(Text[K])) return 1;
    const unsigned char C1 = static_cast<unsigned char>(Text[I + 1]);
    if (Len == 3 && C == 0xE0 && C1 < 0xA0) return 1; // overlong 3-byte
    if (Len == 3 && C == 0xED && C1 >= 0xA0) return 1; // UTF-16 surrogate range
    if (Len == 4 && C == 0xF0 && C1 < 0x90) return 1; // overlong 4-byte
    if (Len == 4 && C == 0xF4 && C1 > 0x8F) return 1; // beyond U+10FFFF
    return Len;
}

/// Return a copy of S with every C0 control byte, DEL, and malformed UTF-8
/// byte rendered as a visible \xHH escape instead of passed through as
/// itself; a validated multi-byte UTF-8 character passes through untouched.
///
/// S is text plang did not choose: a filename from argv, a locale tag from
/// $LANG/$LC_MESSAGES/-fdiagnostics-language=, or (issue #614) a diagnostic
/// message that quotes a byte straight out of the source file, such as
/// err_unexpected_char's "unexpected character: '%0'".  All of these reach
/// stderr verbatim otherwise, and an attacker who controls any of them can
/// steer the terminal with a C0 control byte: hide or rewrite what is on
/// screen with an SGR or cursor-motion sequence, or -- with a bare \n --
/// split one diagnostic into what a log viewer reads as two.  This is the
/// same threat MessageCatalog.cpp's .po reader refuses raw control bytes
/// over (see its file comment).  Bytes above 0x7F that form part of a
/// validated UTF-8 sequence (utf8SequenceLength) pass through untouched --
/// they may be part of a legitimate UTF-8 filename or identifier, and
/// mangling them would be punishing the common case -- but a byte above
/// 0x7F that utf8SequenceLength cannot validate (an isolated continuation
/// byte, an invalid lead byte, or a truncated sequence) is exactly the kind
/// of malformed input #614 filed against, and gets the same \xHH treatment
/// a control byte does rather than reaching the terminal raw.
[[nodiscard]] inline std::string escapeControlChars(std::string_view S) {
    static const char Hex[] = "0123456789abcdef";
    std::string R;
    R.reserve(S.size());
    for (size_t I = 0; I < S.size(); ) {
        const unsigned Len = utf8SequenceLength(S, I);
        const unsigned char C = static_cast<unsigned char>(S[I]);
        if (Len > 1) {
            R += S.substr(I, Len);
        } else if (C < 0x20 || C == 0x7F || C >= 0x80) {
            R += '\\'; R += 'x';
            R += Hex[C >> 4]; R += Hex[C & 0xF];
        } else {
            R += static_cast<char>(C);
        }
        I += Len;
    }
    return R;
}

} // namespace plang
