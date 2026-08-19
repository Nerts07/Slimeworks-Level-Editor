#ifndef SLIMEWORKS_EDITOR_SCENE_DRAW_H
#define SLIMEWORKS_EDITOR_SCENE_DRAW_H

#include "../level/levl.h"

typedef struct EditorModelCache {
    int objectId;
    bool attempted;
    bool loaded;
    Model model;
} EditorModelCache;

typedef enum EditorGizmoMode {
    EDITOR_GIZMO_MOVE,
    EDITOR_GIZMO_ROTATE,
    EDITOR_GIZMO_SCALE
} EditorGizmoMode;

void EditorSceneDraw(LevlDocument *document, int selectedIndex, EditorModelCache *modelCache, int cacheCount,
                     bool showColliders, float gridStep);
void EditorSceneDrawGizmo(const LevlObject *object, EditorGizmoMode mode, int highlightedAxis);
Model *EditorSceneGetModel(EditorModelCache *modelCache, int cacheCount, const LevlObject *object);
void EditorSceneInvalidateModel(EditorModelCache *modelCache, int cacheCount, int objectId);
void EditorSceneUnloadModels(EditorModelCache *modelCache, int cacheCount);

#endif
