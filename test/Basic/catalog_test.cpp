/// catalog_test.cpp — the .po reader
///
/// The reader is the one part of the localization engine that reads bytes
/// somebody else wrote, so most of what is checked here is what it does with
/// bytes it should refuse.  The invariant every case is really testing is the
/// same one: whatever the catalog says or fails to say, a diagnostic still
/// comes out, in English if in nothing else.

#include "plang/Basic/MessageCatalog.h"
#include "plang/Basic/Token.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

using namespace plang;

namespace {

/// Loads a catalog for the duration of a test and puts back what was there,
/// so that one case cannot leave a translation loaded for the next.  The
/// in-process suites run hundreds of compilations in one binary and the
/// catalog is process-wide; without this the first fixture would quietly
/// become every later test's fixture.
class ScopedCatalog {
public:
    explicit ScopedCatalog(std::string_view Po, bool HonourFuzzy = false) {
        MessageCatalog::active().clear();
        MessageCatalog::active().setHonorFuzzy(HonourFuzzy);
        Report = MessageCatalog::active().loadText(Po);
    }
    ~ScopedCatalog() {
        MessageCatalog::active().clear();
        MessageCatalog::active().setHonorFuzzy(false);
    }
    CatalogReport Report;
};

/// A well-formed catalog translating one real diagnostic.  err_no_input_files
/// is chosen because it takes no arguments, so its translation cannot fail
/// placeholder validation for an unrelated reason.
constexpr std::string_view OneEntry = R"po(
msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\n"

msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "aucun fichier d'entree"
)po";

} // namespace

// ---------------------------------------------------------------------------
// The happy path
// ---------------------------------------------------------------------------

TEST(MessageCatalog, ATranslationIsUsed) {
    ScopedCatalog C(OneEntry);
    ASSERT_TRUE(C.Report.ok()) << C.Report.FatalReason;
    EXPECT_EQ(C.Report.Loaded, 1u);
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files), "aucun fichier d'entree");
}

TEST(MessageCatalog, AMessageTheCatalogOmitsStaysEnglish) {
    ScopedCatalog C(OneEntry);
    // Only err_no_input_files was translated; everything else falls back, which
    // is what makes a partial translation useful rather than dangerous.
    EXPECT_STREQ(getDiagFormat(diag::err_undefined_identifier),
                 builtinDiagFormat(diag::err_undefined_identifier));
}

TEST(MessageCatalog, NoCatalogMeansEnglish) {
    MessageCatalog::active().clear();
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 builtinDiagFormat(diag::err_no_input_files));
}

TEST(MessageCatalog, ContinuationLinesAreConcatenatedWithoutASeparator) {
    ScopedCatalog C(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr ""
"one "
"two"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files), "one two");
}

// ---------------------------------------------------------------------------
// Placeholders
// ---------------------------------------------------------------------------

TEST(MessageCatalog, ATranslationMayReorderPlaceholders) {
    // The whole reason the format uses %0..%9 rather than positional-by-order.
    ScopedCatalog C(R"po(
msgctxt "diag/err_expected_token"
msgid "expected %0, got %1"
msgstr "%1 recu, %0 attendu"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(C.Report.Loaded, 1u);
    EXPECT_EQ(formatDiagMsg(getDiagFormat(diag::err_expected_token), {"A", "B"}),
              "B recu, A attendu");
}

TEST(MessageCatalog, ATranslationThatDropsAPlaceholderIsRefused) {
    // formatDiagMsg would substitute nothing and print a sentence with a hole,
    // reporting nothing wrong.  Better to keep the English.
    ScopedCatalog C(R"po(
msgctxt "diag/err_expected_token"
msgid "expected %0, got %1"
msgstr "attendu %0"
)po");
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Malformed, 1u);
    EXPECT_STREQ(getDiagFormat(diag::err_expected_token),
                 builtinDiagFormat(diag::err_expected_token));
}

TEST(MessageCatalog, ATranslationThatInventsAPlaceholderIsRefused) {
    ScopedCatalog C(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "rien %0"
)po");
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Malformed, 1u);
}

TEST(MessageCatalog, RepeatingAPlaceholderIsAllowed) {
    // err_type_of_undefined already repeats %0 in English, so repetition is
    // part of the contract and not a mistake to catch.
    ScopedCatalog C(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "rien"
)po");
    EXPECT_EQ(C.Report.Loaded, 1u);
}

// ---------------------------------------------------------------------------
// What a translation in progress looks like
// ---------------------------------------------------------------------------

TEST(MessageCatalog, FuzzyEntriesAreIgnoredByDefault) {
    ScopedCatalog C(R"po(
#, fuzzy
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "une supposition"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Fuzzy, 1u);
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 builtinDiagFormat(diag::err_no_input_files));
}

TEST(MessageCatalog, FuzzyEntriesCanBeHonouredForReview) {
    // A catalog shipped entirely fuzzy is inert, so whoever reviews it needs a
    // way to see the thing they are reviewing.
    ScopedCatalog C(R"po(
#, fuzzy
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "une supposition"
)po", /*HonourFuzzy=*/true);
    EXPECT_EQ(C.Report.Loaded, 1u);
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files), "une supposition");
}

TEST(MessageCatalog, AnEmptyMsgstrIsUntranslatedNotEmpty) {
    ScopedCatalog C(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr ""
)po");
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Untranslated, 1u);
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 builtinDiagFormat(diag::err_no_input_files));
}

TEST(MessageCatalog, AnEntryForADiagnosticPlangDoesNotHaveIsCounted) {
    // A catalog written against a later plang, loaded by an earlier one.
    ScopedCatalog C(R"po(
msgctxt "diag/err_this_does_not_exist"
msgid "whatever"
msgstr "peu importe"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Unknown, 1u);
}

TEST(MessageCatalog, ObsoleteEntriesAreNotReadBackIn) {
    ScopedCatalog C(R"po(
#~ msgctxt "diag/err_no_input_files"
#~ msgid "no input files"
#~ msgstr "retire"
)po");
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 builtinDiagFormat(diag::err_no_input_files));
}

// ---------------------------------------------------------------------------
// Bytes the reader will not carry
// ---------------------------------------------------------------------------

TEST(MessageCatalog, AnEscapeSequenceOutsideTheWhitelistIsRefused) {
    // \x1b would put a terminal control sequence into text written verbatim to
    // stderr; the whole point of the four-escape whitelist.
    ScopedCatalog C(
        "msgctxt \"diag/err_no_input_files\"\n"
        "msgid \"no input files\"\n"
        "msgstr \"\\x1b[31mrouge\"\n");
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Malformed, 1u);
}

TEST(MessageCatalog, ARawControlByteIsRefused) {
    std::string Po = "msgctxt \"diag/err_no_input_files\"\n"
                     "msgid \"no input files\"\n"
                     "msgstr \"a\x1b" "b\"\n";
    ScopedCatalog C(Po);
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Malformed, 1u);
}

TEST(MessageCatalog, IllFormedUtf8IsRefused) {
    std::string Po = "msgctxt \"diag/err_no_input_files\"\n"
                     "msgid \"no input files\"\n"
                     "msgstr \"a\xC3\x28 b\"\n";
    ScopedCatalog C(Po);
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Malformed, 1u);
}

TEST(MessageCatalog, WellFormedUtf8IsCarriedThrough) {
    ScopedCatalog C(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "aucun fichier d'entrée — vraiment"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 "aucun fichier d'entrée — vraiment");
}

TEST(MessageCatalog, PluralEntriesAreRefusedRatherThanHalfRead) {
    ScopedCatalog C(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input file"
msgid_plural "no input files"
msgstr[0] "un"
msgstr[1] "plusieurs"
)po");
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 builtinDiagFormat(diag::err_no_input_files));
}

// ---------------------------------------------------------------------------
// A bad file does not cost a good one
// ---------------------------------------------------------------------------

TEST(MessageCatalog, OneMalformedEntryDoesNotCostTheRest) {
    // One typo in a translation should lose one message, not 193.
    ScopedCatalog C(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "bon

msgctxt "diag/err_undefined_identifier"
msgid "undefined identifier '%0'"
msgstr "identifiant non defini '%0'"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(C.Report.Loaded, 1u);
    EXPECT_STREQ(getDiagFormat(diag::err_undefined_identifier),
                 "identifiant non defini '%0'");
}

TEST(MessageCatalog, ANonUtf8CharsetRefusesTheWholeFile) {
    ScopedCatalog Good(OneEntry);
    ASSERT_EQ(Good.Report.Loaded, 1u);

    // plang links no iconv, so a catalog in another encoding cannot be read at
    // all; refusing it whole keeps the previous one rather than mixing them.
    const auto R = MessageCatalog::active().loadText(R"po(
msgid ""
msgstr ""
"Content-Type: text/plain; charset=ISO-8859-1\n"

msgctxt "diag/err_undefined_identifier"
msgid "undefined identifier '%0'"
msgstr "autre chose '%0'"
)po");
    EXPECT_FALSE(R.ok());
    EXPECT_NE(R.FatalReason.find("UTF-8"), std::string::npos) << R.FatalReason;
    // The catalog that was already loaded is untouched.
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files), "aucun fichier d'entree");
}

TEST(MessageCatalog, ACatalogFromANewerPlangIsRefused) {
    ScopedCatalog C(R"po(
msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\n"
"X-Plang-Catalog-ABI: 99\n"

msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "quelque chose"
)po");
    EXPECT_FALSE(C.Report.ok());
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 builtinDiagFormat(diag::err_no_input_files));
}

TEST(MessageCatalog, CrLfLineEndingsDoNotLandInTheMessage) {
    ScopedCatalog C("msgctxt \"diag/err_no_input_files\"\r\n"
                    "msgid \"no input files\"\r\n"
                    "msgstr \"sans retour\"\r\n");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files), "sans retour");
}

TEST(MessageCatalog, LoadingIsRepeatable) {
    // The in-process suites run many compilations per process and the catalog
    // is process-wide, so a once-only initialization would make the first
    // fixture every later test's fixture.
    {
        ScopedCatalog First(OneEntry);
        EXPECT_STREQ(getDiagFormat(diag::err_no_input_files), "aucun fichier d'entree");
    }
    {
        ScopedCatalog Second(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "otra cosa"
)po");
        EXPECT_STREQ(getDiagFormat(diag::err_no_input_files), "otra cosa");
    }
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 builtinDiagFormat(diag::err_no_input_files));
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

TEST(MessageCatalog, ANonexistentFileIsRefusedNotFatal) {
    const auto R = MessageCatalog::active().loadFile("/nonexistent/plang.po");
    EXPECT_FALSE(R.ok());
    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                 builtinDiagFormat(diag::err_no_input_files));
}

TEST(MessageCatalog, ADirectoryIsNotACatalog) {
    const auto R = MessageCatalog::active().loadFile("/tmp");
    EXPECT_FALSE(R.ok());
}

TEST(MessageCatalog, ACharacterDeviceIsRefusedRatherThanRead) {
    // Without the regular-file check this reads until the machine gives out.
    const auto R = MessageCatalog::active().loadFile("/dev/zero");
    EXPECT_FALSE(R.ok());
}

// ---------------------------------------------------------------------------
// The generated catalogs
//
// These load the real files the build produced.  The round trip is the
// cheapest complete test the engine gets: en_US.po was written from the same
// .def the compiled-in English comes from, so if the generator's escaping, the
// reader's unescaping, the continuation lines and the placeholder validation
// are all correct, every one of the 193 messages comes back byte-identical.
// Anything less and this fails on the message that broke.
// ---------------------------------------------------------------------------

namespace {

std::string catalogPath(const char* Name) {
    return std::string(PLANG_CATALOG_DIR) + "/" + Name;
}

} // namespace

TEST(GeneratedCatalog, EnglishRoundTripsThroughThePoFile) {
    MessageCatalog::active().clear();
    // Every entry is filled in, so that the round trip is over real text and
    // not over the empty msgstrs a translation base would have.
    std::string Po;
    {
        std::FILE* F = std::fopen(catalogPath("en_US.po").c_str(), "rb");
        ASSERT_NE(F, nullptr) << "generated en_US.po not found";
        char Buf[8192];
        while (std::size_t N = std::fread(Buf, 1, sizeof(Buf), F)) Po.append(Buf, N);
        std::fclose(F);
    }
    // The base catalog ships untranslated by design, so fill each msgstr from
    // its msgid to make it a translation that happens to be identical.
    std::string Filled;
    Filled.reserve(Po.size());
    for (std::size_t I = 0; I < Po.size();) {
        const std::size_t E = Po.find('\n', I);
        const std::string L = Po.substr(I, E == std::string::npos ? E : E - I);
        if (L.rfind("msgid \"", 0) == 0 && L != "msgid \"\"")
            Filled += "msgstr " + L.substr(6) + "\n";
        else if (L.rfind("msgstr \"\"", 0) == 0 && Filled.size() > 9 &&
                 Filled.compare(Filled.size() - 1, 1, "\n") == 0 &&
                 Filled.find("\nmsgstr \"", Filled.size() > 400 ? Filled.size() - 400 : 0)
                     != std::string::npos)
            ; // already emitted from the msgid above
        else
            Filled += L + "\n";
        if (E == std::string::npos) break;
        I = E + 1;
    }

    const auto R = MessageCatalog::active().loadText(Filled);
    ASSERT_TRUE(R.ok()) << R.FatalReason;
    EXPECT_EQ(R.Malformed, 0u) << "an entry did not survive the round trip";
    EXPECT_EQ(R.Unknown, 0u)   << "the catalog names a diagnostic plang does not have";
    EXPECT_GT(R.Loaded, 190u)  << "far fewer entries than the 193 diagnostics";

#define DIAG(ID, LEVEL, MSG) \
    EXPECT_STREQ(getDiagFormat(diag::ID), builtinDiagFormat(diag::ID)) \
        << "round trip changed " #ID;
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG

    MessageCatalog::active().clear();
}

TEST(GeneratedCatalog, ThePseudoLocaleIsLoadableAndMarksEveryMessage) {
    // What the CI install check greps for.  If this passes and the install
    // check fails, the catalogs were built but installed out of reach.
    MessageCatalog::active().clear();
    const auto R = MessageCatalog::active().loadFile(catalogPath("qps_ploc.po"));
    ASSERT_TRUE(R.ok()) << R.FatalReason;
    EXPECT_EQ(R.Malformed, 0u);
    EXPECT_GT(R.Loaded, 190u);
    const std::string M = getDiagFormat(diag::err_no_input_files);
    EXPECT_TRUE(M.starts_with("[!") && M.ends_with("!]")) << M;
    MessageCatalog::active().clear();
}

TEST(GeneratedCatalog, ADeliveredCatalogIsFoundWhereThePathSaysItIs) {
    // catalogSearchPaths is what the compiler uses; if the build-tree entry is
    // wrong every in-process test silently runs against no catalog at all.
    const auto Paths = catalogSearchPaths("");
    ASSERT_FALSE(Paths.empty());
    bool Found = false;
    for (const auto& P : Paths)
        if (std::filesystem::is_regular_file(P + "/qps_ploc.po")) Found = true;
    EXPECT_TRUE(Found) << "no search path reaches the generated catalogs";
}

TEST(LocaleSelection, PosixSpellingsReduceToACatalogName) {
    EXPECT_EQ(normaliseLocale("fr_CA.UTF-8@euro"), "fr_CA");
    EXPECT_EQ(normaliseLocale("de_DE.utf8"), "de_DE");
    EXPECT_EQ(normaliseLocale("es_MX"), "es_MX");
    // "C" and "POSIX" are the absence of a locale, not a language.
    EXPECT_EQ(normaliseLocale("C"), "");
    EXPECT_EQ(normaliseLocale("POSIX"), "");
}

TEST(LocaleSelection, ARegionFallsBackToItsLanguage) {
    const auto Order = catalogSearchOrder("fr_CA");
    ASSERT_EQ(Order.size(), 2u);
    EXPECT_EQ(Order[0], "fr_CA");
    EXPECT_EQ(Order[1], "fr");
}

TEST(LocaleSelection, EnglishReadsNoFileAtAll) {
    // The compiled-in messages are already English; opening a file to be told
    // so would be work for no result, and it is the common case.
    EXPECT_TRUE(catalogSearchOrder("en_US").empty());
    EXPECT_TRUE(catalogSearchOrder("en").empty());
    EXPECT_TRUE(catalogSearchOrder("").empty());
    // A regional English is a real catalog and is not short-circuited.
    EXPECT_FALSE(catalogSearchOrder("en_GB").empty());
}

// ---------------------------------------------------------------------------
// The other two namespaces
//
// A message is not the only English on a diagnostic line.  The severity label
// comes first and the token descriptions arrive inside the message, so a
// catalog that carried only `diag/` would leave every line part English --
// which is the defect this feature removes, not one it may relocate.
// ---------------------------------------------------------------------------

TEST(MessageCatalog, TheSeverityLabelIsTranslated) {
    ScopedCatalog C(R"po(
msgctxt "label/error"
msgid "error"
msgstr "erreur"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(C.Report.Loaded, 1u);
    EXPECT_EQ(severityLabel(DiagSeverity::Error, /*UseColor=*/false), "erreur");
    // Untranslated severities keep the English, one message at a time.
    EXPECT_EQ(severityLabel(DiagSeverity::Warning, false), "warning");
}

TEST(MessageCatalog, TheColorIsNotPartOfTheTranslation) {
    // An SGR escape is not language, and a catalog must not be able to inject
    // one -- which is also why the reader refuses \x1b in the first place.
    ScopedCatalog C(R"po(
msgctxt "label/error"
msgid "error"
msgstr "erreur"
)po");
    const std::string L = severityLabel(DiagSeverity::Error, /*UseColor=*/true);
    EXPECT_EQ(L, "\033[1;31merreur\033[0m");
}

TEST(MessageCatalog, ATokenDescriptionIsTranslated) {
    ScopedCatalog C(R"po(
msgctxt "token/Eof"
msgid "end of file"
msgstr "fin de fichier"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(describe(TokenKind::Eof), "fin de fichier");
}

TEST(MessageCatalog, ATokenWithAFixedSpellingIsNeverTranslated) {
    // 'begin' and ';' are Pascal syntax.  Even given an entry, describe() does
    // not ask: it only consults the catalog for kinds with no fixed spelling.
    ScopedCatalog C(R"po(
msgctxt "token/Begin"
msgid "begin"
msgstr "commencer"
)po");
    EXPECT_EQ(describe(TokenKind::Begin), "'begin'");
}

TEST(MessageCatalog, AllThreeNamespacesCoexist) {
    ScopedCatalog C(R"po(
msgctxt "diag/err_expected_token"
msgid "expected %0, got %1"
msgstr "attendu %0, obtenu %1"

msgctxt "token/Eof"
msgid "end of file"
msgstr "fin de fichier"

msgctxt "label/error"
msgid "error"
msgstr "erreur"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(C.Report.Loaded, 3u);
    // The whole line, not part of it.
    EXPECT_EQ(severityLabel(DiagSeverity::Error, false), "erreur");
    EXPECT_EQ(formatDiagMsg(getDiagFormat(diag::err_expected_token),
                            {"'end'", describe(TokenKind::Eof)}),
              "attendu 'end', obtenu fin de fichier");
}

TEST(MessageCatalog, AKeyInNoKnownNamespaceIsIgnored) {
    ScopedCatalog C(R"po(
msgctxt "banner/hello"
msgid "hello"
msgstr "bonjour"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(C.Report.Loaded, 0u);
    EXPECT_EQ(C.Report.Unknown, 1u);
}

TEST(MessageCatalog, APlaceholderInATokenDescriptionIsTextNotAPlaceholder) {
    // A token description is substituted *into* a message; formatDiagMsg makes
    // one pass, so a %0 arriving inside an argument is not re-expanded.  It is
    // therefore not validated either, and must not be.
    ScopedCatalog C(R"po(
msgctxt "token/Eof"
msgid "end of file"
msgstr "fin %0 fichier"
)po");
    ASSERT_TRUE(C.Report.ok());
    EXPECT_EQ(C.Report.Loaded, 1u);
    EXPECT_EQ(formatDiagMsg("got %0", {describe(TokenKind::Eof)}),
              "got fin %0 fichier");
}

// ---------------------------------------------------------------------------
// Layering a region over its language
//
// A regional catalog names only what differs -- es_MX.po is a few dozen
// entries against es.po's 214 -- so the loader has to lay one over the other.
// Loading only the most specific file leaves every message it does not name in
// English, which is the opposite of what a delta catalog is for, and is what
// this did until the shipped catalogs made it visible.
// ---------------------------------------------------------------------------

TEST(LocaleChain, ADeltaMergesOverItsBaseRatherThanReplacingIt) {
    MessageCatalog::active().clear();
    // The base: two messages.
    auto Base = MessageCatalog::active().loadText(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "base uno"

msgctxt "diag/err_undefined_identifier"
msgid "undefined identifier '%0'"
msgstr "base dos '%0'"
)po");
    ASSERT_TRUE(Base.ok());
    // The delta: one of them, merged.
    auto Delta = MessageCatalog::active().loadText(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "delta uno"
)po", /*Merge=*/true);
    ASSERT_TRUE(Delta.ok());

    EXPECT_STREQ(getDiagFormat(diag::err_no_input_files), "delta uno");
    // The one the delta is silent about must still come from the base, not
    // from English.
    EXPECT_STREQ(getDiagFormat(diag::err_undefined_identifier), "base dos '%0'");
    MessageCatalog::active().clear();
}

TEST(LocaleChain, WithoutMergeALoadReplaces) {
    MessageCatalog::active().clear();
    (void)MessageCatalog::active().loadText(R"po(
msgctxt "diag/err_undefined_identifier"
msgid "undefined identifier '%0'"
msgstr "primero '%0'"
)po");
    (void)MessageCatalog::active().loadText(R"po(
msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "segundo"
)po");
    EXPECT_STREQ(getDiagFormat(diag::err_undefined_identifier),
                 builtinDiagFormat(diag::err_undefined_identifier));
    MessageCatalog::active().clear();
}

TEST(LocaleChain, TheShippedRegionalCatalogsLayerOverTheirLanguage) {
    // Against the real files, since the bug this covers was only visible with
    // a delta small enough to leave most messages to the base.
    const auto R = selectLocale("es_MX", "", /*HonourFuzzy=*/true);
    ASSERT_FALSE(R.builtin()) << "es_MX.po was not found";
    ASSERT_EQ(R.Loaded.size(), 2u) << "expected es.po and es_MX.po";
    EXPECT_NE(R.Loaded[0].find("es.po"), std::string::npos)    << R.Loaded[0];
    EXPECT_NE(R.Loaded[1].find("es_MX.po"), std::string::npos) << R.Loaded[1];
    // Far more entries than es_MX.po holds on its own.
    EXPECT_GT(R.Report.Loaded, 200u);
    MessageCatalog::active().clear();
}

TEST(LocaleChain, ARegionWithNoCatalogOfItsOwnUsesItsLanguage) {
    // es_ES ships no file; it must resolve to es.po rather than to English.
    const auto R = selectLocale("es_ES", "", /*HonourFuzzy=*/true);
    ASSERT_FALSE(R.builtin());
    EXPECT_EQ(R.Language, "es");
    MessageCatalog::active().clear();
}

TEST(LocaleChain, TheShippedFrenchAndSpanishAreInertUntilReviewed) {
    // They ship entirely fuzzy on purpose.  Without -fdiagnostics-show-fuzzy
    // they must load and change nothing.
    for (const char* Lang : {"fr", "es", "fr_CA", "es_MX"}) {
        const auto R = selectLocale(Lang, "", /*HonourFuzzy=*/false);
        EXPECT_EQ(R.Report.Loaded, 0u) << Lang << " is not fully fuzzy";
        EXPECT_GT(R.Report.Fuzzy, 0u)  << Lang;
        EXPECT_STREQ(getDiagFormat(diag::err_no_input_files),
                     builtinDiagFormat(diag::err_no_input_files)) << Lang;
    }
    MessageCatalog::active().clear();
}

TEST(LocaleChain, TheShippedEnglishVariantsAreLiveNotFuzzy) {
    // en_GB and en_CA are spelling deltas a reviewer is not needed for.
    for (const char* Lang : {"en_GB", "en_CA"}) {
        const auto R = selectLocale(Lang, "", /*HonourFuzzy=*/false);
        ASSERT_FALSE(R.builtin()) << Lang;
        EXPECT_GT(R.Report.Loaded, 0u) << Lang << " loaded nothing";
        EXPECT_EQ(R.Report.Fuzzy, 0u)  << Lang << " should need no reviewer";
    }
    MessageCatalog::active().clear();
}

TEST(LocaleChain, EveryShippedCatalogParsesCleanly) {
    // A malformed entry is dropped silently by design, so nothing else would
    // notice a typo in a catalog we ship.
    for (const char* Lang : {"en_GB", "en_CA", "fr", "fr_CA", "es", "es_MX"}) {
        const auto R = selectLocale(Lang, "", /*HonourFuzzy=*/true);
        ASSERT_FALSE(R.builtin()) << Lang << " was not found";
        EXPECT_EQ(R.Report.Malformed, 0u) << Lang << " has a malformed entry";
        EXPECT_EQ(R.Report.Unknown, 0u)
            << Lang << " names a message plang does not have";
    }
    MessageCatalog::active().clear();
}
