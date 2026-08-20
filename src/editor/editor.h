#ifndef SLIMEWORKS_EDITOR_H
#define SLIMEWORKS_EDITOR_H

#include "../level/levl.h"
#include "../navigation/editor_navigation.h"
#include "../render/scene_draw.h"
#include "../mesh/mesh_edit.h"

typedef enum EditorMode {
    EDITOR_MODE_OBJECT,
    EDITOR_MODE_EDIT
} EditorMode;

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
    Vector3 startSlimeVolumeSize;
    Vector3 startRotation;
    Vector3 startModelScale;
    Vector2 startPlatformSize;
    Vector3 startRampEnd;
    float startPlatformTop;
    float startRampWidth;
    float startAngle;
    Vector3 startMeshVertices[LEVL_MESH_MAX_VERTICES];
    int startMeshVertexCount;
    Vector3 startGizmoOrigin;
    float startGizmoSize;
} EditorGizmoDrag;

typedef struct Editor {
    LevlDocument document;
    Camera3D camera;
    EditorNavigation navigation;
    int selectedIndex;
    bool selected[LEVL_MAX_OBJECTS];
    int selectedFace;
    EditorMode mode;
    EditorMeshEdit meshEdit;
    EditorGizmoMode gizmoMode;
    int gizmoHighlightedAxis;
    EditorGizmoDrag gizmoDrag;
    bool snapEnabled;
    float gridStep;
    bool showColliders;
    bool playerSpawnBlocked;
    unsigned int spawnModelValidationHash;
    bool spawnModelBlocked;
    bool parentingMode;
    int parentingChildIndex;
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
