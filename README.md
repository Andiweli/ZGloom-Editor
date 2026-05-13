# 🧱 ZGloom Editor

<p align="left">
  <img alt="platform" src="https://img.shields.io/badge/platform-Win32%20%2F%20x86-blue?style=flat" />
  <img alt="ide" src="https://img.shields.io/badge/IDE-Visual%20Studio-5C2D91?style=flat&logo=visualstudio&logoColor=white" />

</p>

**Gloom Level Editor** is a standalone Win32 / x86 Visual Studio editor prototype for creating and editing **Gloom-compatible maps**.  
The goal is to provide a practical desktop tool for inspecting, editing and exporting map data while keeping the workflow close to the original Gloom/ZGloom structure.*  

<p align="center">
<img width="800" height="440" alt="image" src="https://github.com/user-attachments/assets/c5f2c34b-3fd1-40c7-b9e6-f45c25f5d427" />
</p>

---

## 🛠️ Core Editing Features

The editor supports:

Loading and saving original Gloom map files
Drawing walls, doors, switches, and event zones
Editing existing wall zones and event trigger lines
Moving and modifying map geometry on a precise grid
8×8 sub-grid snapping for accurate Amiga-style geometry
Support for very thin wall segments, including 1/8-width structures
Correct texture mapping on narrow wall faces
Selection feedback for walls, including visible zone IDs such as Z10, Z11, etc.
Texture selection for walls, doors, switches, and special surfaces
Previewing animated wall/switch textures correctly

## 🔗 Event and Trigger System

The editor can work with Gloom’s event system, including:

Creating and editing event trigger zones
Linking event triggers to walls, doors, or switches
Dedicated support for switch/trigger texture linking
Preserving event command order where needed for Amiga compatibility
Handling doors, switches, and animated trigger behaviour
Keeping switch animation and door activation working correctly in-game

## 👾 Monster and Object Support

The editor supports monster placement and fixes important Amiga runtime requirements:

Reading and writing monster spawn commands
Preserving monster positions
Repairing missing or broken object preload lists
Automatically generating a valid LoadObjects command when needed
Ensuring monsters appear correctly on real Amiga hardware instead of spawning outside the map

## 🧩 Amiga Compatibility Fixes

A major goal of the editor is to save maps that work correctly on a real Amiga with Gloom Deluxe.

It now includes fixes for:

Correct Amiga grid writing
Backface/reversed wall zones
Invisible-but-solid wall problems
Animated wall visibility on real hardware
Preserving original grid coverage where possible
Writing changed/new walls in a way the original engine can render correctly
Keeping ZGloom preview behaviour and real Amiga behaviour aligned as closely as possible

## ✨ Usability Features

The editor also includes several workflow improvements:

Clear status bar guidance
Optimised grid rendering to avoid heavy slowdown
Clearer tool naming such as Wall/Door/Switch
Separate switch-linking workflow via Link Event > Switch/Trigger
Better visual feedback when selecting map elements
Right-side overview area showing selected wall/zone information

---

## 🕹️ About Gloom

**Gloom** is a classic Amiga first-person shooter. This editor is intended as a modern helper tool for working with Gloom-compatible map data in the context of ZGloom-related development.

---

## 🛠️ Project status

This project is still in active prototype state. Features, file formats and editing behaviour may change while the editor evolves.

**Built with AI-assisted development support for faster debugging, map-format analysis, and Amiga compatibility work.*
