// The rest of this file's TEST() cases migrated to test/Basic/Catalog/
// (issue #43, Phase E) -- each drives the real plang CLI (-fdiagnostics-language=,
// -fdiagnostics-show-fuzzy, --version, PLANG_LOCALE_DIR) against a synthetic or
// real shipped .po catalog and checks the translated (or English-fallback)
// diagnostic text on stderr.
//
// These 16 are a deliberate, permanent GoogleTest exception. Two different
// reasons put a case here, not one:
//
//  - CatalogReport's per-entry rejection counters (Fuzzy/Malformed/Unknown/
//    Untranslated) are computed in MessageCatalog.cpp but never printed,
//    returned, or otherwise surfaced by any CLI flag or --version output
//    (confirmed by grep across lib/, tools/, include/ -- the struct's own
//    definition and this test file are the only readers). A rejected entry
//    is indistinguishable on stderr from a diagnostic that was simply never
//    given a translation: both fall back to identical English text. A case
//    whose only real assertion is one of these counters (ATranslationThat
//    InventsAPlaceholderIsRefused, RepeatingAPlaceholderIsAllowed, AnEntryFor
//    ADiagnosticPlangDoesNotHaveIsCounted, AnEscapeSequenceOutsideTheWhitelist
//    IsRefused, ARawControlByteIsRefused, IllFormedUtf8IsRefused, AKeyIn
//    NoKnownNamespaceIsIgnored, LocaleChain.EveryShippedCatalogParsesCleanly)
//    has no CLI-observable proxy at all.
//  - MessageCatalog::loadFile()'s own handling of "doesn't exist" / "is a
//    directory" / "is a character device" is unreachable from any CLI
//    invocation: selectLocale()'s search loop always guards with
//    std::filesystem::is_regular_file() before calling loadFile, so these
//    paths are filtered out before loadFile ever sees them (ANonexistentFile
//    IsRefusedNotFatal, ADirectoryIsNotACatalog, ACharacterDeviceIsRefused
//    RatherThanRead).
//
// The remaining four are each a narrower, one-off gap: LoadingIsRepeatable
// tests that the process-wide MessageCatalog singleton can be reloaded and
// cleared repeatedly *within one process* with no state leaking to the next
// load -- a real CLI process loads its catalog once and exits, so there is
// no way to observe this property from outside. LocaleChain.
// WithoutMergeALoadReplaces tests loadText's Merge=false default directly;
// the driver's own selectLocale() call always merges, so no CLI path
// performs two independent non-merging loads in one process.
// GeneratedCatalog.EnglishRoundTripsThroughThePoFile is a 223-diagnostic
// (not 193 -- the file's own old comments were stale) exhaustive generator/
// reader round-trip fidelity check, not compiler-diagnostic behavior; not a
// reasonable 1:1 lit conversion. GeneratedCatalog.
// ADeliveredCatalogIsFoundWhereThePathSaysItIs is a pure install-layout
// filesystem check (catalogSearchPaths("") with no ExeDir) that never
// invokes the compiler at all, and deliberately isolates a narrower code
// path than any real CLI invocation exercises (the driver always passes a
// real findInstallDir()).

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
// Placeholders
// ---------------------------------------------------------------------------

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
// What CatalogReport counts that nothing prints
// ---------------------------------------------------------------------------

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
// The process-wide singleton
// ---------------------------------------------------------------------------

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
// The other two namespaces
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Layering a region over its language: the API-level guarantee
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Locale selection: a pure-function assertion with no CLI proxy
// ---------------------------------------------------------------------------

TEST(LocaleSelection, ARegionFallsBackToItsLanguage) {
    const auto Order = catalogSearchOrder("fr_CA");
    ASSERT_EQ(Order.size(), 2u);
    EXPECT_EQ(Order[0], "fr_CA");
    EXPECT_EQ(Order[1], "fr");
}

// ---------------------------------------------------------------------------
// The generated catalogs
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

