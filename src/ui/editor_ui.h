#ifndef SLIMEWORKS_EDITOR_UI_H
#define SLIMEWORKS_EDITOR_UI_H

#include <raylib.h>
#include <stdbool.h>

#include "../level/levl.h"
#include "../mesh/mesh_edit.h"
#include "../render/scene_draw.h"

#define EDITOR_UI_LEFT_WIDTH 104
#define EDITOR_UI_RIGHT_WIDTH 360
#define EDITOR_UI_TOP_HEIGHT 58
#define EDITOR_UI_BOTTOM_HEIGHT 34

typedef enum EditorUIAction {
    EDITOR_UI_NONE,
    EDITOR_UI_NEW,
    EDITOR_UI_SAVE,
    EDITOR_UI_EXPORT,
    EDITOR_UI_COPY,
    EDITOR_UI_PASTE,
    EDITOR_UI_SET_OBJECT_MODE,
    EDITOR_UI_SET_EDIT_MODE,
    EDITOR_UI_TOGGLE_EDIT,
    EDITOR_UI_SET_MOVE,
    EDITOR_UI_SET_ROTATE,
    EDITOR_UI_SET_SCALE,
    EDITOR_UI_TOGGLE_SNAP,
    EDITOR_UI_TOGGLE_CAMERA,
    EDITOR_UI_TOGGLE_COLLIDERS,
    EDITOR_UI_ADD_BOX,
    EDITOR_UI_ADD_MESH,
    EDITOR_UI_ADD_PLATFORM,
    EDITOR_UI_ADD_RAMP,
    EDITOR_UI_ADD_MARKER,
    EDITOR_UI_ADD_EMPTY,
    EDITOR_UI_IMPORT_MODEL,
    EDITOR_UI_SELECT_VERTEX,
    EDITOR_UI_SELECT_EDGE,
    EDITOR_UI_SELECT_FACE,
    EDITOR_UI_SELECT_ALL,
    EDITOR_UI_CLEAR_SELECTION,
    EDITOR_UI_SUBDIVIDE,
    EDITOR_UI_TOGGLE_COLLISION,
    EDITOR_UI_APPLY_SCALE,
    EDITOR_UI_RENAME,
    EDITOR_UI_SET_TAG,
    EDITOR_UI_BOX_BEVEL_UP,
    EDITOR_UI_BOX_BEVEL_DOWN,
    EDITOR_UI_BOX_SUBDIVISION_UP,
    EDITOR_UI_BOX_SUBDIVISION_DOWN,
    EDITOR_UI_FACE_COLOR,
    EDITOR_UI_DELETE,
    EDITOR_UI_SELECT_OBJECT,
    EDITOR_UI_SET_PARENT,
    EDITOR_UI_CLEAR_PARENT
} EditorUIAction;

typedef struct EditorUIState {
    int mode;
    EditorGizmoMode gizmoMode;
    EditorMeshSelectMode meshSelectMode;
    const LevlObject *selectedObject;
    const LevlDocument *document;
    bool snapEnabled;
    bool showColliders;
    bool dirty;
    bool playerSpawnBlocked;
    int meshSelectionCount;
    float gridStep;
    const char *cameraMode;
    const char *status;
    float statusSeconds;
    bool parentingMode;
} EditorUIState;

EditorUIAction EditorUIHandleMouse(const EditorUIState *state, Vector2 mouse, int *targetIndex);
bool EditorUIIsViewportPoint(Vector2 point);
void EditorUIDraw(const EditorUIState *state);

#endif
