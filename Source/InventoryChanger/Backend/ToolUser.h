#pragma once

#include <optional>
#include <string_view>

#include <InventoryChanger/Inventory/Item.h>
#include <InventoryChanger/StaticData.h>
#include <InventoryChanger/GameItems/Lookup.h>

#include "Response.h"

namespace inventory_changer::backend
{

class BackendSimulator;

class ToolUser {
public:
    std::optional<Response> applySticker(BackendSimulator& backend, std::list<inventory::Item_v2>::iterator item, std::list<inventory::Item_v2>::const_iterator sticker, std::uint8_t slot);
    void activateOperationPass(BackendSimulator& backend, std::list<inventory::Item_v2>::const_iterator item);
    std::optional<Response> activateViewerPass(BackendSimulator& backend, std::list<inventory::Item_v2>::const_iterator item);
    std::optional<Response> wearSticker(BackendSimulator& backend, std::list<inventory::Item_v2>::iterator item, std::uint8_t slot);
    std::optional<Response> addNameTag(BackendSimulator& backend, std::list<inventory::Item_v2>::iterator item, std::list<inventory::Item_v2>::const_iterator nameTagItem, std::string_view nameTag);
    std::optional<Response> removeNameTag(BackendSimulator& backend, std::list<inventory::Item_v2>::iterator item);
    std::optional<Response> openContainer(BackendSimulator& backend, std::list<inventory::Item_v2>::const_iterator item, std::optional<std::list<inventory::Item_v2>::const_iterator> key);

};

}
