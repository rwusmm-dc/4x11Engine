# 4x11Engine

A lightweight game engine and editor supporting Direct3D 10 and 11, developed as an experiment in AI-assisted software development.

## Overview

4x11Engine is a complete game development framework featuring:

- **C++ core** with minimal dependencies
- **Direct3D 10/11 support** with runtime backend selection
- **Built-in editor** with scene hierarchy, property inspector, and gizmo manipulation
- **Physically-based physics simulation** with collision detection and response
- **Frustum and occlusion culling** for performance optimization
- **Scripting support** via Lua (4xLang)
- **OBJ model import** and scene serialization (GAF format)
- **Time-of-day sky system** with dynamic lighting
- **Entity Component System** architecture

## Project Structure

```
4xEngine/src/
├── core/
│   ├── ComPtr.h           - Smart COM pointer wrapper
│   ├── CullingSystem.*    - Frustum and occlusion culling
│   ├── FPSCamera.*        - First-person camera controller
│   ├── Window.*           - Win32 window management
│   └── Project.*          - Project management
├── ecs/
│   ├── ECS.h              - Entity Component System
│   └── ECS.cpp            - Entity serialization and scene management
├── d3d10/                 - Direct3D 10 implementation
├── d3d11/                 - Direct3D 11 implementation
│   └── skybox.*           - Dynamic sky system with sun simulation
├── phy/                   - Physics system (AABB collision, impulse resolution)
├── script/                - 4xLang Lua scripting engine
├── io/                    - Archive and file I/O
├── ui/                    - ImGui-based editor interface
│   ├── Overlay.*          - Main editor windows and tools
│   ├── CodeEditor.*       - Syntax-highlighting code editor
│   ├── Gizmo.*            - 3D transformation gizmo
│   └── ProjectManagerUI.* - Project management UI
└── gizmo/                 - Transformation gizmo implementation
```

## Build Instructions

### Windows

1. Install **MinGW-w64** or **Visual Studio** with C++ support
2. Ensure **DirectX SDK** or Windows SDK is installed
3. Build with the provided script:

```bash
build.bat
```

This creates:
- `main.exe` - The editor
- `game.exe` - Standalone game launcher

### Dependencies

- DirectX 10/11 SDK
- ImGui (included)
- Lua 5.1/LuaJIT (included)
- Zstd compression library

## Using the Editor

### Project Management

1. Launch `main.exe` to open the Project Manager
2. Create a new project or open an existing one
3. Projects are stored in `Documents/4xEngine/Projects/`

### Scene Editing

- **Hierarchy Window**: Right-click to create primitives (cube, sphere, capsule, plane, triangle, octagon, snowman), lights, cameras, and scripts. Drag entities to reparent.
- **Properties Window**: Edit transform, physics, colors, light settings, and script paths.
- **Gizmo**: Transform entities using translate (T), rotate (R), and scale (S) modes. Toggle between local (L) and world (W) space.
- **Performance Window**: Monitor FPS, draw calls, CPU/GPU usage, and culling statistics.

### Scripting (4xLang)

Create `.4xs` script files with the following API:

**EntityService**
- `EntityService.findByName(name)` - Find entity by name
- `EntityService.findById(id)` - Find entity by ID
- `EntityService.getAll()` - Get all entities
- `EntityService.create(name)` - Create new entity
- `EntityService.remove(entity)` - Remove entity

**Entity Methods**
- `:getPosition()` / `:setPosition({x,y,z})`
- `:getRotation()` / `:setRotation({x,y,z})`
- `:getScale()` / `:setScale({x,y,z})`
- `:lookAt({x,y,z})`
- `:getName()` / `:setName(name)`
- `:getId()`
- `:getVelocity()` / `:setVelocity({x,y,z})`
- `:getMass()` / `:setMass(mass)`
- `:isLight()`, `:isCamera()`, `:isStatic()`

**LightService**
- `LightService.createPoint(name)`
- `LightService.createDirectional(name)`
- `LightService.setColor(entity, {r,g,b})`
- `LightService.setIntensity(entity, value)`
- `LightService.setRange(entity, value)`

**CameraService**
- `CameraService.getActive()` - Get active camera
- `CameraService.setActive(entity)` - Set active camera
- `CameraService.create(name)` - Create camera

**WorldService**
- `WorldService.setSkyColor({r,g,b})`
- `WorldService.getSkyColor()`
- `WorldService.setSunBrightness(value)`
- `WorldService.getSunBrightness()`

**InputService**
- `Input.key(name)` - Check key state
- `Input.mouseX()`, `Input.mouseY()`
- `Input.mouseButton(index)`
- `Input.mouseDelta()` - Returns dx, dy

**SyncService**
- `SyncService.getDelta()` - Frame delta time
- `SyncService.sleep(ms)`
- `SyncService.getTime()`
- `SyncService.getDiagnostics()` - Engine stats

## Exporting Games

1. Ensure at least one **Camera** entity exists in the scene
2. Select **File > Export** from the editor menu
3. Choose an export folder
4. The exporter creates a standalone `game.exe` with all scene data packaged

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+S | Save scene |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Delete | Delete selected entity |
| W/A/S/D | Move camera |
| Space | Move camera up |
| X | Move camera down |
| Right-click | Lock/unlock mouse for camera control |

## Technical Notes

- **Culling**: Frustum culling with optional D3D11 occlusion queries
- **Physics**: AABB-based collision with impulse resolution, friction, and restitution
- **Serialization**: GAF format supports full entity state including scripts
- **Lighting**: Dynamic sun with time-of-day simulation, point lights with attenuation
- **Scripting**: Each script runs in its own Lua state for isolation

## License

This project is provided as-is for educational and experimental purposes, not ready for real world use yet but can be used for demos, use at your own risk, the project is provided as alpha-state licensed under MIT license.
