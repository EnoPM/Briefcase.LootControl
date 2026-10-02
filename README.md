# Briefcase Loot Control

Loot Control adjusts the existing loot distribution on a Windows x64 Deceive Inc. dedicated server. Its default configuration leaves vanilla loot unchanged. You can change how many objects are requested, which objects a room may select, and their relative weights.

## Install

1. Install the [latest BriefcaseNative Windows server release](https://github.com/EnoPM/BriefcaseNative/releases/latest) and stop the server.
2. Download `Briefcase.LootControl-windows-x64-<version>.zip` from the latest release of this repository.
3. Extract it directly into the server's `DeceiveInc/Binaries/Win64` directory. The result must include `ue4ss/Mods/BriefcaseLootControl/dlls/main.dll`.
4. Add `BriefcaseLootControl : 1` to `ue4ss/Mods/mods.txt` if that line is not already present.
5. Start `DeceiveIncServer-Win64-Shipping.exe` with Win64 as its working directory.

Keep your existing `ue4ss/Mods/BriefcaseLootControl/Data/config.json` when upgrading. If you installed an older Briefcase version of this mod, remove that old copy before enabling the UE4SS version.

## Find the vanilla loot values

After a map starts, Loot Control writes `ue4ss/Mods/BriefcaseLootControl/Data/catalog.json`. It also saves a catalog for each visited map in `Data/catalogs`. The catalog lists the object names and spawn points you can use in your configuration. Browse it before making changes; item names must match the game.

## Change the distribution

Stop the server and edit `ue4ss/Mods/BriefcaseLootControl/Data/config.json`. For example, this requests more Bond Piles on each map:

```json
{
  "schemaVersion": 1,
  "enabled": true,
  "objects": {
    "BondPile": {
      "occurrenceFactor": 2.0,
      "maxOnePerRoom": false
    }
  },
  "mapCounts": [
    { "map": "*", "objectType": "BondPile", "count": 8 }
  ],
  "presets": [],
  "points": []
}
```

`map: "*"` matches every map. A map name matches part of the active level path without regard to case. `occurrenceFactor` changes selection frequency; `mapCounts` changes the requested number. Neither guarantees a final spawn count: the game's room budgets and placement rules still apply.

To change the relative chance of objects in a room category, add a preset rule:

```json
"presets": [
  {
    "vault": false,
    "security": 1,
    "pointType": "Default",
    "weights": { "BondPile": 3.0, "CreditPurse": 1.0 }
  }
]
```

Use the catalog to find valid `pointType` and object names. Weights are relative: 3 versus 1 gives the first candidate three times the weight of the second. A `points` rule can replace the complete candidate list at one matching spawn point; use its `path` from the catalog to keep that change narrow.

Save the file and restart the server. Invalid object names or malformed JSON are rejected. Increase counts gradually to keep server performance stable.

## Remove

Stop the server, remove `ue4ss/Mods/BriefcaseLootControl`, delete its line from `ue4ss/Mods/mods.txt`, and restart.
