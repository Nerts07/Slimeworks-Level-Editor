#ifndef SLIMEWORKS_EDITOR_SCENE_DRAW_H
#define SLIMEWORKS_EDITOR_SCENE_DRAW_H

#include "../level/levl.h"

typedef struct EditorModelCache {
    int objectId;
    bool attempted;
    bool loaded;
    Model model;
} EditorModelCache;

void EditorSceneDraw(LevlDocument *document, int selectedIndex, EditorModelCache *modelCache, int cacheCount, bool showColliders);
void EditorSceneUnloadModels(EditorModelCache *modelCache, int cacheCount);

#endif
