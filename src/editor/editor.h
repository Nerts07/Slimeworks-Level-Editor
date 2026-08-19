#ifndef SLIMEWORKS_EDITOR_H
#define SLIMEWORKS_EDITOR_H

#include "../level/levl.h"
#include "../navigation/editor_navigation.h"
#include "../render/scene_draw.h"

typedef enum EditorTextMode {
    EDITOR_TEXT_NONE,
    EDITOR_TEXT_NAME,
    EDITOR_TEXT_TAG,
    EDITOR_TEXT_IMPORT
} EditorTextMode;

typedef struct EditorGizmoDrag {
    bool active;
    int axis;
    Vector2 startMouse;
    Vector3 startPosition;
    Vector3 startBoxSize;
    Vector3 startRotation;
    Vector3 startModelScale;
    Vector2 startPlatformSize;
    Vector3 startRampEnd;
    float startPlatformTop;
    float startRampWidth;
    float startAngle;
} EditorGizmoDrag;

typedef struct Editor {
    LevlDocument document;
    Camera3D camera;
    EditorNavigation navigation;
    int selectedIndex;
    int selectedFace;
    EditorGizmoMode gizmoMode;
    int gizmoHighlightedAxis;
    EditorGizmoDrag gizmoDrag;
    bool snapEnabled;
    float gridStep;
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
