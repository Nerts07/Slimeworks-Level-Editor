#ifndef SLIMEWORKS_EDITOR_H
#define SLIMEWORKS_EDITOR_H

#include "../level/levl.h"
#include "../render/scene_draw.h"

typedef enum EditorTextMode {
    EDITOR_TEXT_NONE,
    EDITOR_TEXT_NAME,
    EDITOR_TEXT_TAG,
    EDITOR_TEXT_IMPORT
} EditorTextMode;

typedef struct Editor {
    LevlDocument document;
    Camera3D camera;
    int selectedIndex;
    int selectedFace;
    bool showColliders;
    EditorTextMode textMode;
    char textBuffer[512];
    char status[192];
    float statusSeconds;
    EditorModelCache modelCache[LEVL_MAX_OBJECTS];
} Editor;

void EditorInit(Editor *editor);
void EditorRun(Editor *editor);
void EditorShutdown(Editor *editor);

#endif
