#include "SpritePalettes.h"

#include <map>
#include <set>
#include <string>

#include "Engine/Graphics/Image.h"
#include "Engine/Graphics/Sprites.h"
#include "Engine/Objects/Monsters.h"
#include "Engine/Seasons.h"
#include "Engine/Tables/DecorationTable.h"
#include "Engine/Tables/ObjectTable.h"

#include "Utility/String/Ascii.h"

static std::set<int> referencedFrames() {
    std::set<int> result;
    for (const MonsterDesc &desc : pMonsterList->monsters)
        for (const std::string &name : desc.spriteNames)
            if (!name.empty())
                if (int id = pSpriteFrameTable->FastFindSprite(name))
                    result.insert(id);

    for (const DecorationData &desc : pDecorationTable->decorations) {
        if (!desc.uSpriteID)
            continue;
        for (int month = 0; month < 12; month++)
            result.insert(spriteIdForSeason(desc.uSpriteID, month));
    }

    for (const ObjectData &desc : pObjectTable->pObjects)
        if (!desc.NoSprite() && desc.uSpriteID)
            result.insert(desc.uSpriteID);
    return result;
}

std::map<std::string, std::set<int>> collectSpritePalettes() {
    std::map<std::string, std::set<int>> result;
    for (int id : referencedFrames()) {
        pSpriteFrameTable->InitializeSprite(id); // Resolves the directional image names of the whole frame sequence.
        for (int i = id; i < pSpriteFrameTable->pSpriteSFrames.size(); i++) {
            const SpriteFrame &frame = pSpriteFrameTable->pSpriteSFrames[i];
            for (const Sprite *sprite : frame.sprites)
                if (sprite && sprite->texture)
                    result[ascii::toLower(sprite->texture->name())].insert(frame.paletteId);
            if (!(frame.flags & SPRITE_FRAME_HAS_MORE))
                break;
        }
    }
    return result;
}
