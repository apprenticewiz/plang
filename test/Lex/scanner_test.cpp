#include "plang/Basic/Diagnostic.h"
#include "plang/Lex/Scanner.h"
#include "plang/Basic/Token.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

using namespace plang;

// ---------------------------------------------------------------------------
// Helper: writes content to a mkstemp file; deletes it on destruction.
// Declare before Scanner so the file exists when the Scanner constructor runs.
// ---------------------------------------------------------------------------
class TempFile {
public:
    explicit TempFile(const std::string &Content) {
        char Tmpl[] = "/tmp/plang_test_XXXXXX";
        int Fd = mkstemp(Tmpl);
        Path = Tmpl;
        write(Fd, Content.data(), Content.size());
        close(Fd);
    }
    ~TempFile() { std::remove(Path.c_str()); }
    const std::string &path() const { return Path; }
private:
    std::string Path;
};

// ---------------------------------------------------------------------------
// Per-test helpers
// ---------------------------------------------------------------------------

// Shared sink that makeScanner() resets before each Scanner construction.
// Error tests check this is non-empty; success tests leave it empty.
static DiagnosticsEngine scanDiags;

// Holds the text of every file the tests scan.  A token carries only a
// position, so resolving it to a line and a column goes through here -- which
// means these tests now exercise SourceManager as well as the Scanner.
static SourceManager scanSM;

/// Where a token is, as a filename, line and column.
static PresumedLoc locOf(const Token &T) { return scanSM.getPresumedLoc(T.Loc); }

static Scanner makeScanner(const std::string &Path) {
    scanDiags.clear();
    return Scanner(scanSM, Path, scanDiags);
}

static Scanner makeScannerEP(const std::string &Path) {
    scanDiags.clear();
    LangOptions Opts;
    Opts.Std = LangOptions::Standard::ISO10206;
    return Scanner(scanSM, Path, scanDiags, Opts);
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

TEST(ScannerLiterals, IntLiteral) {
    TempFile F("42");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "42");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerLiterals, RealLiteral) {
    TempFile F("3.14");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::RealLit);
    EXPECT_EQ(T.Lexeme, "3.14");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

// ISO §6.1.5: unsigned-real = digit-sequence '.' fractional-part [...], and
// fractional-part is a digit-sequence, so the point is part of the number only
// when a digit follows it.  `1.` is the integer and then a point — which is
// what `end.` is made of, and what the closing `.)` of a bracket is made of.
TEST(ScannerLiterals, APointWithNoFractionIsNotPartOfTheNumber) {
    TempFile F("1.");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "1");
    EXPECT_EQ(S.next().Kind, TokenKind::Dot);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

// ISO 7185 §6.1.3 builds an identifier from letters and digits; the underscore
// came in with ISO 10206.  Clause 5.1 h) asks that a use of an extension be
// reportable, and this one went unreported, so a program using it compiled as
// standard Pascal when it was not.
TEST(ScannerLexicalAlternatives, AnUnderscoreIsAnExtension) {
    TempFile F("my_var");
    auto S = makeScanner(F.path());
    (void)S.next();
    ASSERT_FALSE(scanDiags.empty());
    EXPECT_NE(scanDiags[0].Message.find("underscore"), std::string::npos)
        << scanDiags[0].Message;
}

TEST(ScannerLexicalAlternatives, AnUnderscoreIsFineUnderExtendedPascal) {
    TempFile F("my_var");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "my_var");
    EXPECT_TRUE(scanDiags.empty());
}

// ISO §6.1.9: `(.` and `.)` are the alternative representations of `[` and
// `]`, and the two spellings are the same token.
TEST(ScannerLexicalAlternatives, BracketsMayBeWrittenAsParenthesisAndPoint) {
    TempFile F("(.1..3.)");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::LeftBracket);
    EXPECT_EQ(S.next().Kind, TokenKind::IntLit);
    EXPECT_EQ(S.next().Kind, TokenKind::DotDot);
    EXPECT_EQ(S.next().Kind, TokenKind::IntLit);
    EXPECT_EQ(S.next().Kind, TokenKind::RightBracket);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

// A point that closes nothing is still a point, so a field selector followed
// by a parenthesis is not mistaken for a bracket.
TEST(ScannerLexicalAlternatives, AFieldSelectorIsNotABracket) {
    TempFile F("r.x)");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::Identifier);
    EXPECT_EQ(S.next().Kind, TokenKind::Dot);
    EXPECT_EQ(S.next().Kind, TokenKind::Identifier);
    EXPECT_EQ(S.next().Kind, TokenKind::RightParen);
}

// ISO §6.1.8 note 1: a comment may open with '{' and close with '*)', or open
// with '(*' and close with '}'.  Both mixtures were rejected as a mismatch.
TEST(ScannerLexicalAlternatives, EitherTerminatorClosesEitherComment) {
    {
        TempFile F("{ opened with a brace *) 42");
        auto S = makeScanner(F.path());
        EXPECT_EQ(S.next().Kind, TokenKind::IntLit);
    }
    {
        TempFile F("(* opened with a parenthesis } 42");
        auto S = makeScanner(F.path());
        EXPECT_EQ(S.next().Kind, TokenKind::IntLit);
    }
}

// ISO §6.1.9 gives '@' as the alternative for '^', the two "not to be
// distinguished".  Providing it is implementation-defined, and it is provided.
TEST(ScannerLexicalAlternatives, AtSignIsTheArrow) {
    TempFile F("p@ q^");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::Identifier);
    EXPECT_EQ(S.next().Kind, TokenKind::Caret);
    EXPECT_EQ(S.next().Kind, TokenKind::Identifier);
    EXPECT_EQ(S.next().Kind, TokenKind::Caret);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerLiterals, StringLiteral) {
    TempFile F("'hello'");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::StringLit);
    EXPECT_EQ(T.Lexeme, "hello");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerLiterals, StringLiteralEmpty) {
    TempFile F("''");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::StringLit);
    EXPECT_EQ(T.Lexeme, "");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerLiterals, StringLiteralEscapedQuote) {
    TempFile F("'it''s'");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::StringLit);
    EXPECT_EQ(T.Lexeme, "it's");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerLiterals, StringAtEndOfFile) {
    // No newline after closing quote — must not throw.
    TempFile F("'x'");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::StringLit);
    EXPECT_EQ(T.Lexeme, "x");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerLiterals, StringWithSpecialChars) {
    TempFile F("'What the #&*%!'");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::StringLit);
    EXPECT_EQ(T.Lexeme, "What the #&*%!");
}

TEST(ScannerLiterals, LargeIntegerLiteral) {
    // Max 32-bit unsigned int — scanner returns the lexeme without range checking.
    TempFile F("2147483647");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "2147483647");
}

TEST(ScannerLiterals, OversizedIntegerLiteral) {
    // One past max 32-bit — scanner still returns it as IntLit; range validation is the parser's job.
    TempFile F("2147483648");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "2147483648");
}

TEST(ScannerLiterals, VeryLargeIntegerLiteral) {
    TempFile F("99999999999999999");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "99999999999999999");
}

TEST(ScannerLiterals, RealWithLeadingZeros) {
    TempFile F("00000000000000000003.14159265");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::RealLit);
    EXPECT_EQ(T.Lexeme, "00000000000000000003.14159265");
}

TEST(ScannerLiterals, RealWithManyMantissaDigits) {
    // Scanner returns the full lexeme without truncating.
    TempFile F("3.141592653589793238462643");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::RealLit);
    EXPECT_EQ(T.Lexeme, "3.141592653589793238462643");
}

// ISO §6.1.5: a scale-factor makes a real literal, with or without a
// fractional part in front of it.  These used to scan as an integer followed
// by an identifier, so 'r := 1e3' failed to parse.
TEST(ScannerLiterals, ScaleFactorWithoutAFractionalPart) {
    TempFile F("1e3");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::RealLit);
    EXPECT_EQ(T.Lexeme, "1e3");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerLiterals, ScaleFactorSignAndCase) {
    TempFile F("1.5E-2 2.5e+10 6E4");
    auto S = makeScanner(F.path());
    for (const char* Want : {"1.5E-2", "2.5e+10", "6E4"}) {
        Token T = S.next();
        EXPECT_EQ(T.Kind,   TokenKind::RealLit);
        EXPECT_EQ(T.Lexeme, Want);
    }
}

TEST(ScannerLiterals, AnEWithNoDigitsIsNotPartOfTheNumber) {
    // Nothing that follows may be swallowed into a malformed literal: the
    // exponent is only taken when a digit really follows the e and its sign.
    TempFile F("1e");
    auto S = makeScanner(F.path());
    Token A = S.next();
    Token B = S.next();
    EXPECT_EQ(A.Kind,   TokenKind::IntLit);
    EXPECT_EQ(A.Lexeme, "1");
    EXPECT_EQ(B.Kind,   TokenKind::Identifier);
    EXPECT_EQ(B.Lexeme, "e");
}

TEST(ScannerLiterals, ARangeIsStillTwoBoundsAndADotDot) {
    TempFile F("1..5");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::IntLit);
    EXPECT_EQ(S.next().Kind, TokenKind::DotDot);
    EXPECT_EQ(S.next().Kind, TokenKind::IntLit);
}

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

TEST(ScannerIdentifiers, Simple) {
    TempFile F("myVar");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "myVar");
}

TEST(ScannerIdentifiers, WithUnderscore) {
    TempFile F("my_var");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "my_var");
}

TEST(ScannerIdentifiers, WithDigits) {
    TempFile F("var1");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "var1");
}

TEST(ScannerIdentifiers, PreservesCase) {
    TempFile F("MyIdentifier");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "MyIdentifier");
}

TEST(ScannerIdentifiers, LongIdentifier) {
    TempFile F("thisisanunusuallylongidentifierbutitislegal");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "thisisanunusuallylongidentifierbutitislegal");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

// ---------------------------------------------------------------------------
// Keywords — one test per keyword, plus case-insensitivity
// ---------------------------------------------------------------------------

TEST(ScannerKeywords, And)       { TempFile F("and");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::And);       }
TEST(ScannerKeywords, Array)     { TempFile F("array");     auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Array);     }
TEST(ScannerKeywords, Begin)     { TempFile F("begin");     auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Begin);     }
TEST(ScannerKeywords, Boolean)   { TempFile F("boolean");   auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Boolean);   }
TEST(ScannerKeywords, Char)      { TempFile F("char");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Char);      }
TEST(ScannerKeywords, Const)     { TempFile F("const");     auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Const);     }
TEST(ScannerKeywords, Div)       { TempFile F("div");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Div);       }
TEST(ScannerKeywords, Do)        { TempFile F("do");        auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Do);        }
TEST(ScannerKeywords, Downto)    { TempFile F("downto");    auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Downto);    }
TEST(ScannerKeywords, Else)      { TempFile F("else");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Else);      }
TEST(ScannerKeywords, End)       { TempFile F("end");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::End);       }
TEST(ScannerKeywords, False)     { TempFile F("false");     auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::False);     }
TEST(ScannerKeywords, File)      { TempFile F("file");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::File);      }
TEST(ScannerKeywords, For)       { TempFile F("for");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::For);       }
TEST(ScannerKeywords, Forward)   { TempFile F("forward");   auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Forward);   }
TEST(ScannerKeywords, Function)  { TempFile F("function");  auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Function);  }
TEST(ScannerKeywords, Goto)      { TempFile F("goto");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Goto);      }
TEST(ScannerKeywords, If)        { TempFile F("if");        auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::If);        }
TEST(ScannerKeywords, In)        { TempFile F("in");        auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::In);        }
TEST(ScannerKeywords, Integer)   { TempFile F("integer");   auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Integer);   }
TEST(ScannerKeywords, Mod)       { TempFile F("mod");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Mod);       }
TEST(ScannerKeywords, Label)     { TempFile F("label");     auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Label);     }
TEST(ScannerKeywords, Nil)       { TempFile F("nil");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Nil);       }
TEST(ScannerKeywords, Not)       { TempFile F("not");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Not);       }
TEST(ScannerKeywords, Of)        { TempFile F("of");        auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Of);        }
TEST(ScannerKeywords, Packed)    { TempFile F("packed");    auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Packed);    }
TEST(ScannerKeywords, Or)        { TempFile F("or");        auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Or);        }
TEST(ScannerKeywords, Procedure) { TempFile F("procedure"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Procedure); }
TEST(ScannerKeywords, Program)   { TempFile F("program");   auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Program);   }
TEST(ScannerKeywords, Real)      { TempFile F("real");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Real);      }
TEST(ScannerKeywords, Record)    { TempFile F("record");    auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Record);    }
TEST(ScannerKeywords, Set)       { TempFile F("set");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Set);       }
TEST(ScannerKeywords, Repeat)    { TempFile F("repeat");    auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Repeat);    }
TEST(ScannerKeywords, String)    { TempFile F("string");    auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::String);    }
TEST(ScannerKeywords, Then)      { TempFile F("then");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Then);      }
TEST(ScannerKeywords, To)        { TempFile F("to");        auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::To);        }
TEST(ScannerKeywords, True)      { TempFile F("true");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::True);      }
TEST(ScannerKeywords, Type)      { TempFile F("type");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Type);      }
TEST(ScannerKeywords, Until)     { TempFile F("until");     auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Until);     }
TEST(ScannerKeywords, Var)       { TempFile F("var");       auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Var);       }
TEST(ScannerKeywords, While)     { TempFile F("while");     auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::While);     }
TEST(ScannerKeywords, With)      { TempFile F("with");      auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::With);      }

TEST(ScannerKeywords, CaseInsensitiveUpper) {
    TempFile F("PROGRAM");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::Program);
}

TEST(ScannerKeywords, CaseInsensitiveMixed) {
    TempFile F("BeGiN");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::Begin);
}

// ---------------------------------------------------------------------------
// Operators and delimiters
// ---------------------------------------------------------------------------

TEST(ScannerOperators, Caret)  { TempFile F("^"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Caret);  }
TEST(ScannerOperators, Plus)   { TempFile F("+"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Plus);   }
TEST(ScannerOperators, Minus)  { TempFile F("-"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Minus);  }
TEST(ScannerOperators, Times)  { TempFile F("*"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Times);  }
TEST(ScannerOperators, Divide) { TempFile F("/"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Divide); }
TEST(ScannerOperators, Equal)  { TempFile F("="); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Equal);  }
TEST(ScannerOperators, Comma)  { TempFile F(","); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Comma);  }
TEST(ScannerOperators, Semicolon)    { TempFile F(";"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::Semicolon);    }
TEST(ScannerOperators, LeftParen)    { TempFile F("("); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::LeftParen);    }
TEST(ScannerOperators, RightParen)   { TempFile F(")"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::RightParen);   }
TEST(ScannerOperators, LeftBracket)  { TempFile F("["); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::LeftBracket);  }
TEST(ScannerOperators, RightBracket) { TempFile F("]"); auto S = makeScanner(F.path()); EXPECT_EQ(S.next().Kind, TokenKind::RightBracket); }

TEST(ScannerOperators, Assign) {
    TempFile F(":=");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Assign);
    EXPECT_EQ(T.Lexeme, ":=");
}

TEST(ScannerOperators, Colon) {
    TempFile F(":");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Colon);
    EXPECT_EQ(T.Lexeme, ":");
}

TEST(ScannerOperators, NotEqual) {
    TempFile F("<>");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::NotEqual);
    EXPECT_EQ(T.Lexeme, "<>");
}

TEST(ScannerOperators, LessThanOrEqual) {
    TempFile F("<=");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::LessThanOrEqual);
    EXPECT_EQ(T.Lexeme, "<=");
}

TEST(ScannerOperators, LessThan) {
    TempFile F("<");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::LessThan);
    EXPECT_EQ(T.Lexeme, "<");
}

TEST(ScannerOperators, GreaterThanOrEqual) {
    TempFile F(">=");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::GreaterThanOrEqual);
    EXPECT_EQ(T.Lexeme, ">=");
}

TEST(ScannerOperators, GreaterThan) {
    TempFile F(">");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::GreaterThan);
    EXPECT_EQ(T.Lexeme, ">");
}

TEST(ScannerOperators, DotDot) {
    TempFile F("..");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::DotDot);
    EXPECT_EQ(T.Lexeme, "..");
}

TEST(ScannerOperators, Dot) {
    TempFile F(".");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Dot);
    EXPECT_EQ(T.Lexeme, ".");
}

// ---------------------------------------------------------------------------
// Operators at end-of-file (bounds-check regression tests)
// ---------------------------------------------------------------------------

TEST(ScannerOperators, LessThanAtEof) {
    TempFile F("<");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::LessThan);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerOperators, GreaterThanAtEof) {
    TempFile F(">");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::GreaterThan);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerOperators, ColonAtEof) {
    TempFile F(":");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::Colon);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerOperators, DotAtEof) {
    TempFile F(".");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::Dot);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

// ---------------------------------------------------------------------------
// Range operator disambiguation
// ---------------------------------------------------------------------------

TEST(ScannerOperators, RangeOperator) {
    TempFile F("1..10");
    auto S = makeScanner(F.path());
    Token A = S.next();
    Token B = S.next();
    Token C = S.next();
    EXPECT_EQ(A.Kind,   TokenKind::IntLit);
    EXPECT_EQ(A.Lexeme, "1");
    EXPECT_EQ(B.Kind,   TokenKind::DotDot);
    EXPECT_EQ(C.Kind,   TokenKind::IntLit);
    EXPECT_EQ(C.Lexeme, "10");
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------

TEST(ScannerComments, BraceComment) {
    TempFile F("{ this is a comment } x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "x");
}

TEST(ScannerComments, ParenStarComment) {
    TempFile F("(* this is a comment *) x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "x");
}

TEST(ScannerComments, MultilineBraceComment) {
    TempFile F("{\nline2\n} x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "x");
    EXPECT_EQ(locOf(T).Line,   3u);
}

TEST(ScannerComments, BraceCommentDoesNotNest) {
    // The (* inside a { } comment is ignored.
    TempFile F("{ (* not nested } x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "x");
}

TEST(ScannerComments, UnterminatedBraceComment) {
    TempFile F("{ oops");
    auto S = makeScanner(F.path());
    S.next(); // triggers skipBraceComment which emits the error
    EXPECT_FALSE(scanDiags.empty());
}

TEST(ScannerComments, UnterminatedParenStarComment) {
    TempFile F("(* oops");
    auto S = makeScanner(F.path());
    S.next(); // triggers skipParenthesisComment which emits the error
    EXPECT_FALSE(scanDiags.empty());
}

TEST(ScannerComments, EmptyParenStarComment) {
    // (**) is (*  followed immediately by *) — valid empty comment.
    TempFile F("(**) x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "x");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerComments, ParenStarCommentWithStarsInBody) {
    // (***) — the middle * is in the comment body; only the final *) closes.
    TempFile F("(***) x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "x");
}

TEST(ScannerComments, ParenStarCommentManyStars) {
    // (******) — four interior stars before the closing *).
    TempFile F("(******) x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "x");
}

TEST(ScannerComments, ParenStarCommentCloseInsideBody) {
    // (*)*) — the ) after (* is part of the body; *) at the end is the real close.
    // Must consume the entire five-char sequence as one comment.
    TempFile F("(*)*)x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "x");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerComments, ComplexStarCommentSequence) {
    // From scantst.pas line 9: six identifiers separated by increasingly
    // star-heavy paren comments, ending with the tricky (*)*) form.
    TempFile F("(**) u (***) v (****) w (*****) x (******) y (*)*) z");
    auto S = makeScanner(F.path());

    for (const char *Id : {"u", "v", "w", "x", "y", "z"}) {
        Token T = S.next();
        EXPECT_EQ(T.Kind,   TokenKind::Identifier) << "expected identifier '" << Id << "'";
        EXPECT_EQ(T.Lexeme, Id);
    }
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST(ScannerErrors, UnterminatedString) {
    TempFile F("'oops");
    auto S = makeScanner(F.path());
    S.next(); // emits error, returns partial StringLit
    EXPECT_FALSE(scanDiags.empty());
}

TEST(ScannerErrors, StringWithNewline) {
    TempFile F("'oops\n'");
    auto S = makeScanner(F.path());
    S.next(); // emits error, returns partial StringLit
    EXPECT_FALSE(scanDiags.empty());
}

TEST(ScannerErrors, UnexpectedCharacter) {
    // '@' was the example here until §6.1.9 made it a token; a backquote is in
    // no representation of anything.
    TempFile F("`");
    auto S = makeScanner(F.path());
    // next() skips the Error token internally and returns Eof; error is in diags.
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
    EXPECT_FALSE(scanDiags.empty());
}

TEST(ScannerErrors, FileNotFound) {
    // File-open failure emits a diagnostic and leaves the scanner in an empty state.
    auto S = makeScanner("/nonexistent/path/plang_test.pas");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
    EXPECT_FALSE(scanDiags.empty());
}

// ---------------------------------------------------------------------------
// EOF behavior
// ---------------------------------------------------------------------------

TEST(ScannerEof, EmptyFile) {
    TempFile F("");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerEof, EofAfterToken) {
    TempFile F("x");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::Identifier);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerEof, RepeatedEofCalls) {
    TempFile F("x");
    auto S = makeScanner(F.path());
    S.next(); // identifier
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

// ---------------------------------------------------------------------------
// Source location tracking
// ---------------------------------------------------------------------------

TEST(ScannerLocation, FirstTokenColumn) {
    TempFile F("  foo");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(locOf(T).Line,   1u);
    EXPECT_EQ(locOf(T).Column, 3u);
}

TEST(ScannerLocation, MultilineTracking) {
    TempFile F("x\ny");
    auto S = makeScanner(F.path());
    Token A = S.next();
    Token B = S.next();
    EXPECT_EQ(locOf(A).Line,   1u);
    EXPECT_EQ(locOf(A).Column, 1u);
    EXPECT_EQ(locOf(B).Line,   2u);
    EXPECT_EQ(locOf(B).Column, 1u);
}

TEST(ScannerLocation, ColumnAfterComment) {
    // "{ c } x" — x begins at column 7.
    TempFile F("{ c } x");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(locOf(T).Line,   1u);
    EXPECT_EQ(locOf(T).Column, 7u);
}

// ---------------------------------------------------------------------------
// Integration: full token stream for a small Pascal fragment
// ---------------------------------------------------------------------------

TEST(ScannerIntegration, SmallProgram) {
    TempFile F(
        "program test;\n"
        "var x : integer;\n"
        "begin\n"
        "  x := 42\n"
        "end.\n"
    );
    auto S = makeScanner(F.path());

    struct Expect { TokenKind Kind; std::string Lexeme; };
    std::vector<Expect> Expected = {
        {TokenKind::Program,    "program"},
        {TokenKind::Identifier, "test"},
        {TokenKind::Semicolon,  ";"},
        {TokenKind::Var,        "var"},
        {TokenKind::Identifier, "x"},
        {TokenKind::Colon,      ":"},
        {TokenKind::Integer,    "integer"},
        {TokenKind::Semicolon,  ";"},
        {TokenKind::Begin,      "begin"},
        {TokenKind::Identifier, "x"},
        {TokenKind::Assign,     ":="},
        {TokenKind::IntLit,     "42"},
        {TokenKind::End,        "end"},
        {TokenKind::Dot,        "."},
        {TokenKind::Eof,        ""},
    };

    for (const auto &E : Expected) {
        Token T = S.next();
        EXPECT_EQ(T.Kind,   E.Kind)   << "unexpected kind for lexeme '" << E.Lexeme << "'";
        EXPECT_EQ(T.Lexeme, E.Lexeme) << "unexpected lexeme for kind " << static_cast<int>(E.Kind);
    }
}

// ---------------------------------------------------------------------------
// Extended Pascal (ISO 10206) — Tier 1
// ---------------------------------------------------------------------------

// --- EP reserved words: recognized as keywords in iso10206 mode ------------

TEST(ScannerEP, EPKeywordsRecognized) {
    // Spot-check a representative set of EP-only reserved words.
    struct Expect { const char* Src; TokenKind Kind; };
    static const Expect Cases[] = {
        {"and_then",   TokenKind::AndThen},
        {"or_else",    TokenKind::OrElse},
        {"otherwise",  TokenKind::Otherwise},
        {"module",     TokenKind::Module},
        {"import",     TokenKind::Import},
        {"export",     TokenKind::Export},
        {"only",       TokenKind::Only},
        {"qualified",  TokenKind::Qualified},
        {"restricted", TokenKind::Restricted},
        {"bindable",   TokenKind::Bindable},
        {"protected",  TokenKind::Protected},
        {"value",      TokenKind::Value},
        {"pow",        TokenKind::Pow},
    };
    for (const auto &C : Cases) {
        TempFile F(C.Src);
        auto S = makeScannerEP(F.path());
        Token T = S.next();
        EXPECT_EQ(T.Kind, C.Kind) << "EP keyword '" << C.Src << "' not recognized";
    }
}

TEST(ScannerEP, EPKeywordsAreIdentifiersIn7185) {
    // In iso7185 mode the same words must be plain identifiers.
    const char* Words[] = {
        "and_then", "or_else", "otherwise", "module", "import", "export",
        "only", "qualified", "restricted", "bindable", "protected", "value", "pow",
    };
    for (const char* W : Words) {
        TempFile F(W);
        auto S = makeScanner(F.path());   // default = iso7185
        Token T = S.next();
        EXPECT_EQ(T.Kind, TokenKind::Identifier)
            << "'" << W << "' should be an identifier in iso7185 mode";
    }
}

TEST(ScannerEP, EPKeywordsCaseInsensitive) {
    TempFile F("AND_THEN Or_Else OTHERWISE Module");
    auto S = makeScannerEP(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::AndThen);
    EXPECT_EQ(S.next().Kind, TokenKind::OrElse);
    EXPECT_EQ(S.next().Kind, TokenKind::Otherwise);
    EXPECT_EQ(S.next().Kind, TokenKind::Module);
}

// --- Nondecimal integer literals (EP §6.1.7) --------------------------------

TEST(ScannerEP, NondecimalHex) {
    TempFile F("16#ff");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "255");
}

TEST(ScannerEP, NondecimalOctal) {
    TempFile F("8#377");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "255");
}

TEST(ScannerEP, NondecimalBinary) {
    TempFile F("2#11111111");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "255");
}

TEST(ScannerEP, NondecimalBase10) {
    // base 10 with # notation should equal the plain decimal value
    TempFile F("10#42");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "42");
}

TEST(ScannerEP, NondecimalBase36) {
    // z in base 36 = 35
    TempFile F("36#z");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "35");
}

TEST(ScannerEP, NondecimalHexUpperCase) {
    TempFile F("16#FF");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "255");
}

TEST(ScannerEP, NondecimalNotRecognizedIn7185) {
    // In iso7185 mode, "16" is an IntLit and "#ff" starts with '#' (error token).
    TempFile F("16#ff");
    auto S = makeScanner(F.path());   // iso7185
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "16");
    // '#' is not a valid Pascal symbol; scanner emits an Error token (skipped)
    // and then 'ff' becomes an Identifier.
    Token T2 = S.next();
    EXPECT_EQ(T2.Kind, TokenKind::Identifier);
    EXPECT_EQ(T2.Lexeme, "ff");
}

TEST(ScannerEP, NondecimalBadBase) {
    TempFile F("1#0");   // base 1 is invalid
    auto S = makeScannerEP(F.path());
    (void)S.next();   // consume whatever comes out
    EXPECT_FALSE(scanDiags.empty()) << "expected error for base-1 literal";
}

TEST(ScannerEP, NondecimalDigitOutOfRange) {
    TempFile F("2#2");   // digit '2' invalid for base 2
    auto S = makeScannerEP(F.path());
    (void)S.next();
    EXPECT_FALSE(scanDiags.empty()) << "expected error for out-of-range digit";
}

TEST(ScannerEP, NondecimalLiteralOverflow) {
    // Issue #14: base#digits was accumulated into int64_t with no overflow
    // check, silently wrapping instead of being rejected like an
    // out-of-range decimal literal is.
    //
    // next() never hands an Error token back to a caller (see the "Skip
    // Error tokens" comment above) -- it swallows it and keeps scanning, so
    // like NondecimalBadBase/NondecimalDigitOutOfRange above, this checks
    // scanDiags rather than the returned token's kind.
    TempFile F("36#ZZZZZZZZZZZZZZZZ");
    auto S = makeScannerEP(F.path());
    (void)S.next();
    ASSERT_FALSE(scanDiags.empty())
        << "expected error for out-of-range nondecimal literal";
    EXPECT_NE(scanDiags[0].Message.find("out of range"), std::string::npos)
        << scanDiags[0].Message;
}

TEST(ScannerEP, NondecimalLiteralAtInt64Max) {
    // 16#7FFFFFFFFFFFFFFF == INT64_MAX exactly: the largest literal the
    // overflow check must still accept, so the check itself isn't off by one.
    TempFile F("16#7FFFFFFFFFFFFFFF");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::IntLit);
    EXPECT_EQ(T.Lexeme, "9223372036854775807");
    EXPECT_TRUE(scanDiags.empty());
}

TEST(ScannerEP, NondecimalLiteralOneAboveInt64Max) {
    // 16#8000000000000000 == INT64_MAX + 1: the smallest literal the
    // overflow check must reject, the other side of the same boundary.
    TempFile F("16#8000000000000000");
    auto S = makeScannerEP(F.path());
    (void)S.next();
    EXPECT_FALSE(scanDiags.empty()) << "expected error for INT64_MAX + 1";
}

// --- Underscore in identifiers ----------------------------------------------

TEST(ScannerEP, UnderscoreInIdentifierEP) {
    TempFile F("my_var");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "my_var");
}

TEST(ScannerEP, UnderscoreInIdentifier7185) {
    // Underscore is accepted in iso7185 mode too (scanner-level permissiveness).
    TempFile F("my_var");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "my_var");
}

// EP §6.1.3's grammar -- identifier = letter { [ underscore ]
// ( letter | digit ) } . -- interleaves each optional underscore with a
// mandatory following letter-or-digit, so it can never produce a leading,
// trailing, or doubled underscore; the clause's own NOTE says this
// explicitly.  scanIdentifierOrKeyword tracked only whether the run
// contained an underscore ANYWHERE, so under -std=iso10206 (where
// underscores are allowed at all) a leading, trailing, or doubled one was
// accepted completely silently.

TEST(ScannerEP, LeadingUnderscoreIsRejectedUnderExtendedPascal) {
    TempFile F("_foo");
    auto S = makeScannerEP(F.path());
    (void)S.next();
    ASSERT_FALSE(scanDiags.empty());
    EXPECT_NE(scanDiags[0].Message.find("begin or end"), std::string::npos)
        << scanDiags[0].Message;
}

TEST(ScannerEP, TrailingUnderscoreIsRejectedUnderExtendedPascal) {
    TempFile F("foo_");
    auto S = makeScannerEP(F.path());
    (void)S.next();
    ASSERT_FALSE(scanDiags.empty());
    EXPECT_NE(scanDiags[0].Message.find("begin or end"), std::string::npos)
        << scanDiags[0].Message;
}

TEST(ScannerEP, DoubledUnderscoreIsRejectedUnderExtendedPascal) {
    TempFile F("foo__bar");
    auto S = makeScannerEP(F.path());
    (void)S.next();
    ASSERT_FALSE(scanDiags.empty());
    EXPECT_NE(scanDiags[0].Message.find("adjacent"), std::string::npos)
        << scanDiags[0].Message;
}

TEST(ScannerEP, AMedialSingleUnderscoreIsStillFineUnderExtendedPascal) {
    TempFile F("foo_bar");
    auto S = makeScannerEP(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::Identifier);
    EXPECT_EQ(T.Lexeme, "foo_bar");
    EXPECT_TRUE(scanDiags.empty());
}

// ---------------------------------------------------------------------------
// Tier 2 scanner: new two-character tokens
// ---------------------------------------------------------------------------

TEST(ScannerTier2, StarStarToken) {
    TempFile F("**");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::StarStar);
    EXPECT_EQ(T.Lexeme, "**");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerTier2, StarStarDoesNotConsumeThird) {
    // "***" should be StarStar then Times
    TempFile F("***");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::StarStar);
    EXPECT_EQ(S.next().Kind, TokenKind::Times);
}

TEST(ScannerTier2, SymDiffToken) {
    TempFile F("><");
    auto S = makeScanner(F.path());
    Token T = S.next();
    EXPECT_EQ(T.Kind,   TokenKind::SymDiff);
    EXPECT_EQ(T.Lexeme, "><");
    EXPECT_EQ(S.next().Kind, TokenKind::Eof);
}

TEST(ScannerTier2, SymDiffDistinctFromGT) {
    // '>' not followed by '<' is GreaterThan
    TempFile F("> <");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::GreaterThan);
    EXPECT_EQ(S.next().Kind, TokenKind::LessThan);
}

TEST(ScannerTier2, StarStarInExpression) {
    TempFile F("2.0 ** 8.0");
    auto S = makeScanner(F.path());
    EXPECT_EQ(S.next().Kind, TokenKind::RealLit);
    EXPECT_EQ(S.next().Kind, TokenKind::StarStar);
    EXPECT_EQ(S.next().Kind, TokenKind::RealLit);
}
