#ifndef SLIMEWORKS_EDITOR_MESH_EDIT_H
#define SLIMEWORKS_EDITOR_MESH_EDIT_H

#include "../level/levl.h"

#define EDITOR_MESH_MAX_EDGES (LEVL_MESH_MAX_FACES * LEVL_MESH_FACE_MAX_VERTICES)

typedef enum EditorMeshSelectMode {
    EDITOR_MESH_SELECT_VERTEX,
    EDITOR_MESH_SELECT_EDGE,
    EDITOR_MESH_SELECT_FACE
} EditorMeshSelectMode;

typedef struct EditorMeshEdit {
    int objectId;
    EditorMeshSelectMode selectMode;
    bool vertices[LEVL_MESH_MAX_VERTICES];
    bool edges[EDITOR_MESH_MAX_EDGES];
    bool faces[LEVL_MESH_MAX_FACES];
} EditorMeshEdit;

void EditorMeshEditReset(EditorMeshEdit *edit);
void EditorMeshEditBegin(EditorMeshEdit *edit, const LevlObject *object);
void EditorMeshEditSetSelectMode(EditorMeshEdit *edit, EditorMeshSelectMode mode);
bool EditorMeshEditPick(EditorMeshEdit *edit, const LevlObject *object, Camera3D camera, Vector2 mouse, bool append);
void EditorMeshEditSelectAll(EditorMeshEdit *edit, const LevlObject *object);
void EditorMeshEditClear(EditorMeshEdit *edit);
int EditorMeshEditSelectionCount(const EditorMeshEdit *edit, const LevlObject *object);
void EditorMeshEditMove(EditorMeshEdit *edit, LevlObject *object, Vector3 delta);
void EditorMeshEditScale(EditorMeshEdit *edit, LevlObject *object, float factor);
void EditorMeshEditRotateY(EditorMeshEdit *edit, LevlObject *object, float degrees);
void EditorMeshEditCycleFaceColor(EditorMeshEdit *edit, LevlObject *object, const Color *palette, int paletteCount);
void EditorMeshEditDraw(const EditorMeshEdit *edit, const LevlObject *object);
const char *EditorMeshSelectModeName(EditorMeshSelectMode mode);

#endif
