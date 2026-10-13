#include "Config.h"

#include <cassert>
#include <string>
#include <vector>
#include <sstream>
#include <utility>

#include <inicpp.h> // NOLINT: this is not a C system header.

#include "Library/Logger/Logger.h"

#include "Utility/Streams/FileInputStream.h"
#include "Utility/Streams/FileOutputStream.h"
#include "Utility/MapAccess.h"
#include "Utility/String/Format.h"
#include "Utility/String/Wrap.h"

void Config::load(const Path &path) {
    FileInputStream stream(path); // Will throw if file doesn't exist.
    load(&stream);
}

void Config::save(const Path &path) const {
    FileOutputStream stream(path);
    save(&stream);
}

/**
 * First line of the config files that write the entries left at their default as comments. Older files wrote every
 * entry, so an entry at its default in them doesn't say that the player chose it.
 */
static constexpr std::string_view CONFIG_HEADER = "; Entries that are commented out are at their default value.";

void Config::load(InputStream *stream) {
    std::string text = stream->readAll();
    if (text.starts_with("\xEF\xBB\xBF"))
        text.erase(0, 3); // UTF-8 byte order mark, editors on Windows like to add one.
    bool commentedDefaults = text.starts_with(CONFIG_HEADER);
    std::istringstream stdStream(std::move(text));

    ini::IniFile ini;
    ini.setCommentChar(';'); // Use ini comment char, not '#'.
    ini.decode(stdStream); // This can throw.

    for (const auto &[sectionName, iniSection] : ini) {
        if (ConfigSection *section = this->section(sectionName)) {
            for (const auto &[entryName, iniValue] : iniSection) {
                if (AnyConfigEntry *entry = section->entry(entryName)) {
                    try {
                        entry->setString(iniValue.as<std::string_view>());
                        if (!commentedDefaults && entry->string() == entry->defaultString())
                            entry->setExplicit(false);
                    } catch (const std::exception &e) {
                        MM_WARNING("Could not load config entry '[{}]/{}': {}. Value unchanged.",
                                   sectionName, entryName, e.what());
                    }
                }
            }
        }
    }
}

void Config::save(OutputStream *stream) const {
    // ini::IniFile doesn't support comments so we just write things out ourselves.
    stream->write(fmt::format("{}\n\n", CONFIG_HEADER));
    for (ConfigSection *section : sections()) {
        stream->write(fmt::format("[{}]\n", section->name()));

        for (AnyConfigEntry *entry : section->entries()) {
            if (!entry->description().empty())
                for (const std::string &line : wrapText(entry->description(), 78))
                    stream->write(fmt::format("; {}\n", line));
            stream->write(fmt::format("; Default is '{}'.\n", entry->defaultString()));
            stream->write(fmt::format("{}{} = {}\n", entry->isExplicit() ? "" : "; ", entry->name(), entry->string()));
            stream->write("\n");
        }

        stream->write("\n");
    }
}

void Config::reset() {
    for (ConfigSection *section : sections())
        for (AnyConfigEntry *entry : section->entries())
            entry->reset();
}

void Config::registerSection(ConfigSection *section) {
    assert(section);
    assert(!_sectionByName.contains(section->name()));

    _sectionByName.emplace(section->name(), section);
}

ConfigSection *Config::section(std::string_view name) const {
    return valueOr(_sectionByName, name, nullptr);
}

std::vector<ConfigSection *> Config::sections() const {
    std::vector<ConfigSection *> result;
    for (const auto &[_, section] : _sectionByName)
        result.push_back(section);
    return result;
}
