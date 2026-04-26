# 🧱 Gloom Level Editor [WiP]

<p align="left">
  <img alt="status" src="https://img.shields.io/badge/status-prototype-orange?style=flat" />
  <img alt="platform" src="https://img.shields.io/badge/platform-Win32%20%2F%20x86-blue?style=flat" />
  <img alt="ide" src="https://img.shields.io/badge/IDE-Visual%20Studio-5C2D91?style=flat&logo=visualstudio&logoColor=white" />
</p>

**Gloom Level Editor** is a standalone Win32 / x86 Visual Studio editor prototype for creating and editing **Gloom-compatible maps**.

The goal is to provide a practical desktop tool for inspecting, editing and exporting map data while keeping the workflow close to the original Gloom/ZGloom structure.

<img width="1264" height="811" alt="gloomed" src="https://github.com/user-attachments/assets/9b1809c6-6a3f-4ee6-bdb8-3ebf4d0e817a" />

---

## ✨ Current editor features

| Area | Features |
| --- | --- |
| 🗺️ **Map view** | Top-down 2D map view with selection support |
| 🔍 **Inspector** | Selection and inspector panel for editing map elements |
| 🎨 **Textures** | Texture preview panel for the selected zone |
| 📦 **CrM2 support** | CrM2 texture loading from `txts/` |
| 🧱 **Drawing tools** | Draw modes for walls, monster zones and event triggers |
| 🧩 **Zone tools** | Zone list and zone edit dialog |
| ⚙️ **Events** | Event editor and texture slot editor |
| 💾 **Export** | Save, Save As and SVG export |

---

## 🖱️ Editing workflow

The editor currently focuses on a clean **2D editing workflow**:

- select existing map elements
- inspect and adjust zone data
- preview assigned textures
- draw walls, monster zones and event triggers
- save edited maps or export the layout as SVG

---

## 📁 Texture loading notes

Texture previews resolve files from the following locations:

1. `txts/` next to the editor project
2. `txts/` next to the loaded map's parent folder

This keeps the editor flexible while still matching the expected Gloom-style data layout.

---

## 🚧 Prototype notes

- Mouse drawing currently supports **walls**, **monster zones** and **event triggers**.
- Standalone map-object placement can be added next once the dedicated object layer is exposed in the editor data model.
- The editor is currently a **Win32 / x86 Visual Studio prototype**.

---

## 🕹️ About Gloom

**Gloom** is a classic Amiga first-person shooter. This editor is intended as a modern helper tool for working with Gloom-compatible map data in the context of ZGloom-related development.

---

## 🛠️ Project status

This project is still in active prototype state. Features, file formats and editing behaviour may change while the editor evolves.
