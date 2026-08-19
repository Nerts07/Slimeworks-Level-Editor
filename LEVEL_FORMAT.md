# Формат `.levl`

`.levl` — текстовый формат уровня версии 1. Он специально простой: его удобно хранить в Git, смотреть diff и при необходимости создавать генератором. Имена, теги и пути внутри файла не должны содержать пробелы.

```text
SLIMEWORKS_LEVL 1
LEVEL factory_level

PLATFORM factory_floor ground 0 0 80 80 0 55 115 102 255
RAMP upper_path ground_ramp 10 0 0 20 3 0 6 117 177 223 255
BOX terminal quest_terminal 0 1.5 0 3 3 3 0 20 0 0.15 2 1
FACES 120 255 181 255 30 80 65 255 48 194 135 255 57 216 150 255 36 160 112 255 42 178 124 255
MODEL crane prop assets/crane.glb 12 0 4 0 0 0 1 1 1 1 4 5 4
MARKER player_start player_spawn 0 0 6
MARKER ore_01 ore_spawn 16 0 9
END
```

## Записи

| Запись | Поля |
| --- | --- |
| `PLATFORM` | `name tag centerX centerZ sizeX sizeZ top r g b a` |
| `RAMP` | `name tag startX startY startZ endX endY endZ width r g b a` |
| `BOX` | `name tag posX posY posZ sizeX sizeY sizeZ rotX rotY rotZ bevel subdivisions collidable` |
| `FACES` | 6 групп `r g b a`, строго после своего `BOX`: top, bottom, front, back, right, left |
| `MODEL` | `name tag assetPath posX posY posZ rotX rotY rotZ scaleX scaleY scaleZ collidable colliderX colliderY colliderZ` |
| `MARKER` | `name tag x y z` |

`collidable` равен `1` для физического препятствия и `0` для декора. Для боксов и моделей игра автоматически использует AABB-коллайдер. Площадки и склоны автоматически участвуют в определении земли и наклона.

## Правила экспорта

`F6` создаёт в проекте игры:

```text
Slimeworks Factory/
└── levels/
    ├── factory.levl
    └── assets/
        └── imported-model.glb
```

Игра ищет `levels/factory.levl` при старте. Если файла нет или он невалиден, запускается встроенный демонстрационный уровень — проект остаётся рабочим даже после чистого клона.

