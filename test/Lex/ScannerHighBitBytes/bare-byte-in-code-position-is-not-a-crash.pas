(*
Issue #221: several call sites in Scanner.cpp handed a plain `char` straight
to a <cctype> function (std::isalpha/isdigit/isalnum/tolower) without first
going through `static_cast<unsigned char>`, as :109's isspace call and :245's
isdigit(peek()) call already did.  On the common platform where `char` is
signed, a source byte >= 0x80 becomes a negative `int` at the call --
undefined behavior per the C standard, which requires the argument to be
representable as unsigned char or EOF.

The byte below is 'e' with an acute accent (U+00E9), UTF-8-encoded as two
bytes (0xC3 0xA9).  The scanner classifies one raw byte at a time, not one
Unicode codepoint at a time, so each byte of this ordinary UTF-8 character
independently reaches std::isalpha/std::isdigit in Scanner::next() (the
dispatch that decides whether to start an identifier, a number, or fall
through to scanSymbol) -- no need for a byte that is invalid UTF-8 on its
own.

This is silent on this project's actual target platforms today: plang never
calls setlocale() (see MessageCatalog.h), so <cctype> only ever runs under
the "C" locale, whose classification is standardized to be ASCII-only -- a
guarantee every conforming C library provides, not just glibc.  That makes
both bytes classify as "not a letter, not a digit" whether the fix is
present or not, so this file's own token stream cannot show a difference
either side of the fix (confirmed empirically against both a pre-fix and a
post-fix build, under a plain and an ASan+UBSan build alike: byte-identical
stdout, no sanitizer report, in all four).  The bug is real UB per the
standard regardless -- glibc's ctype table happens to pad the negative
index range to agree with its positive one, but that is glibc's own
implementation choice, not something the standard promises (a smaller/
hardened libc, e.g. musl, is not obliged to do the same, and the negative
range would then read genuinely unrelated table data rather than 0). Fixed
by casting to unsigned char at every such call site, matching the pattern
:109 and :245 already used.

This file's own scenario: neither byte is a letter or a digit under the
scanner's ASCII-only classification, so scanSymbol() reports each one as an
unexpected character and both Error tokens are elided (Scanner::next()
skips them, same as the existing unexpected-character.pas backtick case) --
only Eof survives in the dumped stream.  The point of this file is not the
Eof-only output (unremarkable on its own) but that the run below completes
at all, deterministically, without a crash or a sanitizer report.
*)

(*
RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

é

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Eof
*)
