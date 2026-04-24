ZGloom Editor
=============

Standalone Win32 / x86 Visual Studio editor prototype for Gloom-compatible maps.

Current editor features:
- top-down 2D map view
- selection + inspector panel
- texture preview panel for the selected zone
- CrM2 texture loading from txts/
- draw modes for walls, monster zones and event triggers
- zone list, zone edit dialog, event editor and texture slot editor
- save / save as / SVG export

Notes:
- Texture preview resolves files from txts/ next to the project or next to the loaded map's parent folder.
- Mouse drawing currently supports walls, monster zones and event triggers.
- Standalone map-object placement can be added next once the dedicated object layer is exposed in the editor data model.
