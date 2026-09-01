# Character Equipment Editor

The Character Equipment Editor creates engine-owned `.3dgequipment` catalogs for
weapons, armor, staffs, shields, props, and other socketed character items. Open it
from **Panels > Animation & Characters > Character Equipment Editor**, or
double-click an equipment set in Content.

## Author an equipment set

1. Create the required sockets in the Character Editor and save the Character.
2. Open the Character Equipment Editor and select that Character under
   **Compatible Character**.
3. Click **Add Item** and give the item a unique name.
4. Set an **Equipment Slot** such as `RightHand`, `Head`, `Back`, or `Armor`.
5. Select a Character Socket. Missing sockets are highlighted immediately.
6. Choose an engine static Mesh and optional Material.
7. Optionally select an equip Sound and particle/effect asset.
8. Adjust the item's position, rotation, and scale relative to the socket.
9. Add comma-separated search tags and save the equipment set.

Only one item can occupy a slot at runtime. Equipping a new `RightHand` item removes
the previous `RightHand` equipment while permanent attachments and equipment in
other slots remain intact.

## Native C++ scripts

```cpp
if (EquipCharacterItem(
        "Content/GameAssets/Equipment/WizardGear.3dgequipment", "OakStaff")) {
    // The mesh is attached to its authored socket and its equip sound plays.
}

UnequipCharacterSlot("RightHand");
const std::string current = EquippedCharacterItem("RightHand");
```

## Lua scripts

```lua
EquipCharacterItem(
    "Content/GameAssets/Equipment/WizardGear.3dgequipment", "OakStaff")

local current = EquippedCharacterItem("RightHand")
UnequipCharacterSlot("RightHand")
```

Runtime equipping uses the active engine asset cache, resolves the named character
socket, combines the socket and item offsets, loads the selected material, and adds
the model as a normal animated attachment. Equip sound playback is automatic. The
effect reference remains part of the item definition for attack/equip scripts that
spawn the chosen effect at the same named socket.
