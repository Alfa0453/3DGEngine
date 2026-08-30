# Inventory and Item Editor

The Inventory and Item Editor creates engine-owned `.3dgitem` assets and configures starting inventories for players, characters, containers, pickups, and merchants.

## Create an item

1. Open **Panels > World & Gameplay > Inventory and Item Editor**.
2. Choose **New Item**, then set its internal and display names, description, localization key, type, stack size, value, and weight.
3. For equipment, choose a slot such as Main Hand, Chest, or Accessory. Unique items are automatically limited to one per stack.
4. Select engine-owned icon, world mesh, pickup prefab, ability, animation, audio, and particle assets from the searchable fields.
5. Add statistics such as Damage, Armor, Healing, or Mana, plus tags used by scripts and UI filters.
6. Consumables can emit one or more use effects with an event name and magnitude.
7. Save the item. Double-clicking the `.3dgitem` in Content reopens it.

## Configure a starting inventory

Use the **Inventory Setup and Debug** tab to configure slot and weight limits and preview stacking. Set the desired starting count, select the player, NPC, chest, or other owner in the scene, then choose **Add to Selected Inventory**. Starting contents and equipped state persist with the scene and packaged game.

## Native C++ scripts

```cpp
AddItem("GameAssets/Items/HealthPotion.3dgitem", 3);

if (HasItem("HealthPotion"))
    UseItem("HealthPotion");

EquipItem("WizardSword");
UnequipItemSlot(1); // Main Hand
```

Use `ItemCount`, `InventoryWeight`, `RemoveItem`, `SaveInventory`, and `LoadInventory` for HUD, pickup, merchant, checkpoint, and save-game logic. Inventory events describe additions, removals, uses, equipment changes, rejection, and authored item effects.

## Lua scripts

```lua
function PickupPotion()
    Engine.AddItem("GameAssets/Items/HealthPotion.3dgitem", 1)
end

function DrinkPotion()
    if Engine.HasItem("HealthPotion", 1) then
        Engine.UseItem("HealthPotion")
    end
end
```

Equipment slot values are: 0 None, 1 Main Hand, 2 Off Hand, 3 Head, 4 Chest, 5 Hands, 6 Legs, 7 Feet, and 8 Accessory.
