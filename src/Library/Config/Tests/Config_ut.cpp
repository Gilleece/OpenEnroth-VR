#include "Testing/Unit/UnitTest.h"

#include "Library/Config/Config.h"
#include "Library/Config/ConfigSection.h"
#include "Library/Config/ConfigEntry.h"
#include "Library/Logger/Logger.h"
#include "Library/Logger/LogSink.h"

#include "Utility/Memory/Blob.h"
#include "Utility/Streams/BlobOutputStream.h"
#include "Utility/Streams/MemoryInputStream.h"

// Minimal concrete Config subclass for testing.
struct TestConfig : Config {
    struct TestSection : ConfigSection {
        explicit TestSection(Config *config) : ConfigSection(config, "test") {}

        ConfigEntry<int> value = {this, "value", 42, "Test integer entry."};
    } test{this};
};

// Null log sink that discards all messages, counting the warnings and errors.
class NullLogSink : public LogSink {
 public:
    virtual void write(const LogCategory &, LogLevel level, std::string_view) override {
        if (level >= LOG_WARNING)
            warnings++;
    }

    int warnings = 0;
};

// Fixture that initializes the global logger singleton required by Config::load.
class ConfigTest : public testing::Test {
 protected:
    NullLogSink _sink;
    Logger _logger{LOG_TRACE, &_sink};
};

UNIT_TEST_FIXTURE(ConfigTest, InvalidValueIsIgnoredAndPreviousValueKept) {
    // Loading an invalid value should log a warning and leave the entry unchanged.
    TestConfig config;
    config.test.value.setValue(99);

    std::string_view ini = "[test]\nvalue = not_a_number\n";
    MemoryInputStream stream(ini.data(), ini.size());
    EXPECT_NO_THROW(config.load(&stream));
    EXPECT_EQ(config.test.value.value(), 99); // Previous value preserved, not default.
}

UNIT_TEST_FIXTURE(ConfigTest, ValidValueIsLoaded) {
    TestConfig config;

    std::string_view ini = "[test]\nvalue = 7\n";
    MemoryInputStream stream(ini.data(), ini.size());
    EXPECT_NO_THROW(config.load(&stream));
    EXPECT_EQ(config.test.value.value(), 7);
}

UNIT_TEST_FIXTURE(ConfigTest, ByteOrderMark) {
    // An ini that an editor saved with a UTF-8 byte order mark failed to load, the mark read as a field outside a section.
    TestConfig config;

    std::string_view ini = "\xEF\xBB\xBF[test]\nvalue = 7\n";
    MemoryInputStream stream(ini.data(), ini.size());
    EXPECT_NO_THROW(config.load(&stream));
    EXPECT_EQ(config.test.value.value(), 7);
}

UNIT_TEST_FIXTURE(ConfigTest, FieldWithoutSection) {
    // A field before any section is still an error, with a byte order mark or without one.
    for (std::string_view ini : {std::string_view("value = 7\n[test]\n"), std::string_view("\xEF\xBB\xBFvalue = 7\n[test]\n")}) {
        TestConfig config;
        MemoryInputStream stream(ini.data(), ini.size());
        EXPECT_THROW(config.load(&stream), std::exception);
        EXPECT_EQ(config.test.value.value(), 42);
    }
}

UNIT_TEST_FIXTURE(ConfigTest, ExplicitDefaultIsSaved) {
    // A value at its default that the player set is written out, one that nobody set is written as a comment.
    for (bool set : {false, true}) {
        TestConfig config;
        if (set)
            config.test.value.setValue(42);
        Blob saved;
        BlobOutputStream out(&saved);
        config.save(&out);
        out.close();
        EXPECT_EQ(saved.str().find("\n; value = 42\n") != std::string_view::npos, !set);
        EXPECT_EQ(saved.str().find("\nvalue = 42\n") != std::string_view::npos, set);

        TestConfig loaded;
        MemoryInputStream in(saved.data(), saved.size());
        loaded.load(&in);
        EXPECT_EQ(loaded.test.value.isExplicit(), set);
    }
}

UNIT_TEST_FIXTURE(ConfigTest, UnsetValueIsSavedAsDefault) {
    // A value that nobody set, like one a preset filled in, is written as its default, as the file's header says.
    TestConfig config;
    config.test.value.setValue(7);
    config.test.value.setExplicit(false);
    Blob saved;
    BlobOutputStream out(&saved);
    config.save(&out);
    out.close();
    EXPECT_NE(saved.str().find("\n; value = 42\n"), std::string_view::npos);
    EXPECT_EQ(saved.str().find("value = 7"), std::string_view::npos);
}

UNIT_TEST_FIXTURE(ConfigTest, OlderFileDefaultIsNotExplicit) {
    // Files written before defaults were commented out have every entry, so one at its default wasn't chosen.
    TestConfig config;
    std::string_view ini = "[test]\nvalue = 42\n";
    MemoryInputStream stream(ini.data(), ini.size());
    config.load(&stream);
    EXPECT_FALSE(config.test.value.isExplicit());

    TestConfig changed;
    std::string_view changedIni = "[test]\nvalue = 7\n";
    MemoryInputStream changedStream(changedIni.data(), changedIni.size());
    changed.load(&changedStream);
    EXPECT_TRUE(changed.test.value.isExplicit());
}

UNIT_TEST_FIXTURE(ConfigTest, UnknownEntryIsSkippedQuietly) {
    // An entry the config doesn't have, like a removed setting left in an old ini, is skipped without a warning.
    TestConfig config;

    std::string_view ini = "[test]\nvalue = 7\nremoved_setting = true\n";
    MemoryInputStream stream(ini.data(), ini.size());
    EXPECT_NO_THROW(config.load(&stream));
    EXPECT_EQ(config.test.value.value(), 7);
    EXPECT_EQ(_sink.warnings, 0);
}
