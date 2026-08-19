#ifndef SLIMEWORKS_EDITOR_NAVIGATION_H
#define SLIMEWORKS_EDITOR_NAVIGATION_H

#include <raylib.h>
#include <stdbool.h>

typedef enum EditorNavigationProfile {
    EDITOR_NAVIGATION_MACOS,
    EDITOR_NAVIGATION_WINDOWS
} EditorNavigationProfile;

typedef enum EditorNavigationMode {
    EDITOR_NAVIGATION_ORBIT,
    EDITOR_NAVIGATION_FREE
} EditorNavigationMode;

typedef struct EditorNavigation {
    EditorNavigationProfile profile;
    EditorNavigationMode mode;
    Vector3 orbitTarget;
    float yaw;
    float pitch;
    float orbitDistance;
} EditorNavigation;

void EditorNavigationInit(EditorNavigation *navigation, const Camera3D *camera);
void EditorNavigationToggleMode(EditorNavigation *navigation, Camera3D *camera);
/* Returns true while a camera gesture owns the mouse and selection must be suppressed. */
bool EditorNavigationUpdate(EditorNavigation *navigation, Camera3D *camera, float deltaTime);
const char *EditorNavigationModeName(const EditorNavigation *navigation);
const char *EditorNavigationOrbitHint(const EditorNavigation *navigation);
const char *EditorNavigationFreeHint(const EditorNavigation *navigation);

#endif
