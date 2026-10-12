#include "TexturePackManifest.h"

#include "Library/Json/Json.h"
#include "Library/Serialization/EnumSerialization.h"

MM_DEFINE_ENUM_SERIALIZATION_FUNCTIONS(TexturePackColors, CASE_INSENSITIVE, {
    {TEXTURE_PACK_COLORS_RAW, "raw"},
    {TEXTURE_PACK_COLORS_FINAL, "final"}
})

MM_DEFINE_JSON_LEXICAL_SERIALIZATION_FUNCTIONS(TexturePackColors)

MM_DEFINE_JSON_STRUCT_SERIALIZATION_FUNCTIONS(TexturePackEntry, (
    (crc32, "crc32")
))

MM_DEFINE_JSON_STRUCT_SERIALIZATION_FUNCTIONS(TexturePackManifest, (
    (format, "format"),
    (colors, "colors"),
    (bitmaps, "bitmaps"),
    (icons, "icons")
))
