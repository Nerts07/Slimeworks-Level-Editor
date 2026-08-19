#include "editor_navigation.h"

#include <math.h>
#include <raymath.h>

static bool ShiftHeld(void)
{
    return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

static bool ControlHeld(void)
{
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}

static Vector3 DirectionFromAngles(float yaw, float pitch)
{
    return Vector3Normalize((Vector3){ sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) });
}

static void SetAnglesFromDirection(EditorNavigation *navigation, Vector3 direction)
{
    direction = Vector3Normalize(direction);
    navigation->yaw = atan2f(direction.x, direction.z);
    navigation->pitch = asinf(direction.y);
}

static void ClampPitch(EditorNavigation *navigation)
{
    if (navigation->pitch > 1.48f) navigation->pitch = 1.48f;
    if (navigation->pitch < -1.48f) navigation->pitch = -1.48f;
}

static void SyncOrbitCamera(const EditorNavigation *navigation, Camera3D *camera)
{
    Vector3 offset = {
        sinf(navigation->yaw) * cosf(navigation->pitch) * navigation->orbitDistance,
        sinf(navigation->pitch) * navigation->orbitDistance,
        cosf(navigation->yaw) * cosf(navigation->pitch) * navigation->orbitDistance
    };
    camera->position = Vector3Add(navigation->orbitTarget, offset);
    camera->target = navigation->orbitTarget;
}

static Vector3 LerpVector3(Vector3 a, Vector3 b, float amount)
{
    return (Vector3){ Lerp(a.x, b.x, amount), Lerp(a.y, b.y, amount), Lerp(a.z, b.z, amount) };
}

static bool UpdateCameraTransition(EditorNavigation *navigation, Camera3D *camera, float deltaTime)
{
    if (!navigation->transitionActive) return false;
    navigation->transitionElapsed += deltaTime;
    float t = navigation->transitionElapsed / navigation->transitionDuration;
    if (t > 1.0f) t = 1.0f;
    float smooth = t * t * (3.0f - 2.0f * t);
    camera->position = LerpVector3(navigation->transitionStartPosition, navigation->transitionTargetPosition, smooth);
    camera->target = LerpVector3(navigation->transitionStartTarget, navigation->orbitTarget, smooth);
    camera->up = Vector3Normalize(LerpVector3(navigation->transitionStartUp, navigation->transitionTargetUp, smooth));
    camera->fovy = Lerp(navigation->transitionStartFovy, navigation->transitionTargetFovy, smooth);
    if (t >= 1.0f) {
        camera->position = navigation->transitionTargetPosition;
        camera->target = navigation->orbitTarget;
        camera->up = navigation->transitionTargetUp;
        camera->fovy = navigation->transitionTargetFovy;
        navigation->transitionActive = false;
    }
    return true;
}

static bool IsNavigationButtonDown(void)
{
    /* MMB is the exact Blender binding for a mouse. RMB is the practical touchpad equivalent. */
    return IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
}

static bool IsPanGesture(void)
{
    return IsNavigationButtonDown() && ShiftHeld() && !ControlHeld();
}

static bool IsOrbitGesture(void)
{
    return IsNavigationButtonDown() && !ShiftHeld() && !ControlHeld();
}

static bool IsDollyGesture(void)
{
    return IsNavigationButtonDown() && ControlHeld() && !ShiftHeld();
}

static bool IsFreeLookGesture(void)
{
    return IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
}

void EditorNavigationInit(EditorNavigation *navigation, const Camera3D *camera)
{
    *navigation = (EditorNavigation){
#if defined(__APPLE__)
        .profile = EDITOR_NAVIGATION_MACOS,
#else
        .profile = EDITOR_NAVIGATION_WINDOWS,
#endif
        .mode = EDITOR_NAVIGATION_ORBIT,
        .orbitTarget = camera->target
    };
    Vector3 offset = Vector3Subtract(camera->position, camera->target);
    navigation->orbitDistance = Vector3Length(offset);
    navigation->yaw = atan2f(offset.x, offset.z);
    navigation->pitch = asinf(offset.y / navigation->orbitDistance);
}

void EditorNavigationToggleMode(EditorNavigation *navigation, Camera3D *camera)
{
    navigation->transitionActive = false;
    navigation->hasLastView = false;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    if (navigation->mode == EDITOR_NAVIGATION_ORBIT) {
        if (navigation->orthographic) {
            navigation->orthographic = false;
            camera->projection = CAMERA_PERSPECTIVE;
            camera->fovy = 50.0f;
            camera->up = (Vector3){ 0.0f, 1.0f, 0.0f };
        }
        navigation->mode = EDITOR_NAVIGATION_FREE;
        SetAnglesFromDirection(navigation, forward);
        return;
    }
    navigation->mode = EDITOR_NAVIGATION_ORBIT;
    navigation->orbitTarget = Vector3Add(camera->position, Vector3Scale(forward, navigation->orbitDistance));
    SetAnglesFromDirection(navigation, Vector3Scale(forward, -1.0f));
    SyncOrbitCamera(navigation, camera);
}

static void SnapToView(EditorNavigation *navigation, Camera3D *camera, EditorNavigationView view)
{
    bool flip = navigation->hasLastView && navigation->orthographic && navigation->lastView == view;
    float distance = navigation->orbitDistance;
    Vector3 offset = { 0.0f, 0.0f, view == EDITOR_VIEW_BACK ? -distance : distance };
    Vector3 targetUp = { 0.0f, 1.0f, 0.0f };
    if (view == EDITOR_VIEW_FRONT) offset = (Vector3){ 0.0f, 0.0f, flip ? -distance : distance };
    if (view == EDITOR_VIEW_BACK) offset = (Vector3){ 0.0f, 0.0f, flip ? distance : -distance };
    if (view == EDITOR_VIEW_RIGHT) offset = (Vector3){ flip ? -distance : distance, 0.0f, 0.0f };
    if (view == EDITOR_VIEW_LEFT) offset = (Vector3){ flip ? distance : -distance, 0.0f, 0.0f };
    if (view == EDITOR_VIEW_TOP) {
        offset = (Vector3){ 0.0f, flip ? -distance : distance, 0.0f };
        targetUp = (Vector3){ 0.0f, 0.0f, -1.0f };
    }
    if (view == EDITOR_VIEW_BOTTOM) {
        offset = (Vector3){ 0.0f, flip ? distance : -distance, 0.0f };
        targetUp = (Vector3){ 0.0f, 0.0f, 1.0f };
    }
    Vector3 targetPosition = Vector3Add(camera->target, offset);
    navigation->mode = EDITOR_NAVIGATION_ORBIT;
    navigation->orthographic = true;
    navigation->orbitTarget = camera->target;
    SetAnglesFromDirection(navigation, offset);
    navigation->hasLastView = true;
    navigation->lastView = view;
    navigation->lastViewFlipped = flip;
    navigation->transitionActive = true;
    navigation->transitionElapsed = 0.0f;
    navigation->transitionDuration = 0.16f;
    navigation->transitionStartPosition = camera->position;
    navigation->transitionStartTarget = camera->target;
    navigation->transitionStartUp = camera->up;
    navigation->transitionStartFovy = camera->fovy;
    navigation->transitionTargetPosition = targetPosition;
    navigation->transitionTargetUp = targetUp;
    navigation->transitionTargetFovy = fmaxf(8.0f, navigation->orbitDistance * 0.75f);
    camera->projection = CAMERA_ORTHOGRAPHIC;
}

bool EditorNavigationHandleViewShortcuts(EditorNavigation *navigation, Camera3D *camera)
{
    if (!ControlHeld()) return false;
    if (IsKeyPressed(KEY_ONE)) { SnapToView(navigation, camera, EDITOR_VIEW_FRONT); return true; }
    if (IsKeyPressed(KEY_TWO)) { SnapToView(navigation, camera, EDITOR_VIEW_BACK); return true; }
    if (IsKeyPressed(KEY_THREE)) { SnapToView(navigation, camera, EDITOR_VIEW_RIGHT); return true; }
    if (IsKeyPressed(KEY_FOUR)) { SnapToView(navigation, camera, EDITOR_VIEW_LEFT); return true; }
    if (IsKeyPressed(KEY_FIVE)) { SnapToView(navigation, camera, EDITOR_VIEW_TOP); return true; }
    if (IsKeyPressed(KEY_SIX)) { SnapToView(navigation, camera, EDITOR_VIEW_BOTTOM); return true; }
    if (IsKeyPressed(KEY_SEVEN)) { SnapToView(navigation, camera, EDITOR_VIEW_TOP); return true; }
    return false;
}

bool EditorNavigationUpdate(EditorNavigation *navigation, Camera3D *camera, float deltaTime)
{
    if (UpdateCameraTransition(navigation, camera, deltaTime)) return true;
    float wheel = GetMouseWheelMove();
    if (navigation->mode == EDITOR_NAVIGATION_ORBIT) {
        bool pan = IsPanGesture();
        bool orbit = IsOrbitGesture();
        bool dolly = IsDollyGesture();
        if (pan || orbit || dolly) {
            Vector2 mouseDelta = GetMouseDelta();
            if (pan) {
                Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
                Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
                Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
                float panSpeed = navigation->orbitDistance * 0.0018f;
                navigation->orbitTarget = Vector3Add(navigation->orbitTarget, Vector3Scale(right, -mouseDelta.x * panSpeed));
                navigation->orbitTarget = Vector3Add(navigation->orbitTarget, Vector3Scale(up, mouseDelta.y * panSpeed));
            } else if (orbit) {
                if (navigation->orthographic && Vector2Length(mouseDelta) > 0.01f) {
                    navigation->orthographic = false;
                    navigation->hasLastView = false;
                    camera->projection = CAMERA_PERSPECTIVE;
                    camera->fovy = 50.0f;
                    camera->up = (Vector3){ 0.0f, 1.0f, 0.0f };
                }
                navigation->yaw -= mouseDelta.x * 0.008f;
                navigation->pitch += mouseDelta.y * 0.008f;
                ClampPitch(navigation);
            } else {
                float zoom = mouseDelta.y * 0.015f;
                if (navigation->orthographic) {
                    camera->fovy *= expf(zoom);
                    if (camera->fovy < 2.0f) camera->fovy = 2.0f;
                    if (camera->fovy > 160.0f) camera->fovy = 160.0f;
                } else {
                    navigation->orbitDistance *= expf(zoom);
                    if (navigation->orbitDistance < 1.5f) navigation->orbitDistance = 1.5f;
                    if (navigation->orbitDistance > 250.0f) navigation->orbitDistance = 250.0f;
                }
            }
        }
        if (fabsf(wheel) > 0.001f) {
            if (navigation->orthographic) {
                camera->fovy *= powf(0.84f, wheel);
                if (camera->fovy < 2.0f) camera->fovy = 2.0f;
                if (camera->fovy > 160.0f) camera->fovy = 160.0f;
            } else {
                navigation->orbitDistance *= powf(0.84f, wheel);
                if (navigation->orbitDistance < 1.5f) navigation->orbitDistance = 1.5f;
                if (navigation->orbitDistance > 250.0f) navigation->orbitDistance = 250.0f;
            }
        }
        SyncOrbitCamera(navigation, camera);
        return pan || orbit || dolly;
    }

    bool freeLook = IsFreeLookGesture();
    Vector3 direction = DirectionFromAngles(navigation->yaw, navigation->pitch);
    if (freeLook) {
        Vector2 mouseDelta = GetMouseDelta();
        navigation->yaw -= mouseDelta.x * 0.008f;
        navigation->pitch += mouseDelta.y * 0.008f;
        ClampPitch(navigation);
        direction = DirectionFromAngles(navigation->yaw, navigation->pitch);
        Vector3 flatForward = Vector3Normalize((Vector3){ direction.x, 0.0f, direction.z });
        Vector3 right = Vector3Normalize(Vector3CrossProduct(flatForward, camera->up));
        float speed = ShiftHeld() ? 16.0f : 7.0f;
        Vector3 movement = { 0 };
        if (IsKeyDown(KEY_W)) movement = Vector3Add(movement, flatForward);
        if (IsKeyDown(KEY_S)) movement = Vector3Subtract(movement, flatForward);
        if (IsKeyDown(KEY_D)) movement = Vector3Add(movement, right);
        if (IsKeyDown(KEY_A)) movement = Vector3Subtract(movement, right);
        if (IsKeyDown(KEY_E)) movement.y += 1.0f;
        if (IsKeyDown(KEY_Q)) movement.y -= 1.0f;
        if (Vector3Length(movement) > 0.001f) camera->position = Vector3Add(camera->position,
            Vector3Scale(Vector3Normalize(movement), speed * deltaTime));
    }
    if (fabsf(wheel) > 0.001f) camera->position = Vector3Add(camera->position, Vector3Scale(direction, wheel * 1.6f));
    camera->target = Vector3Add(camera->position, direction);
    return freeLook;
}

bool EditorNavigationIsOrthographic(const EditorNavigation *navigation)
{
    return navigation->orthographic;
}

const char *EditorNavigationModeName(const EditorNavigation *navigation)
{
    return navigation->mode == EDITOR_NAVIGATION_ORBIT ? "orbit" : "free";
}

const char *EditorNavigationOrbitHint(const EditorNavigation *navigation)
{
    return navigation->profile == EDITOR_NAVIGATION_MACOS ?
        "Mouse: MMB orbit, Shift+MMB pan, Ctrl+MMB dolly" :
        "Mouse: MMB orbit, Shift+MMB pan, Ctrl+MMB dolly";
}

const char *EditorNavigationFreeHint(const EditorNavigation *navigation)
{
    return navigation->profile == EDITOR_NAVIGATION_MACOS ?
        "Trackpad: RMB orbit, Shift+RMB pan, Ctrl+RMB dolly" :
        "Trackpad: RMB orbit, Shift+RMB pan, Ctrl+RMB dolly";
}

const char *EditorNavigationViewHint(const EditorNavigation *navigation)
{
    (void)navigation;
    return "Ortho: Ctrl+1/2 front/back, 3/4 right/left, 5/6 top/bottom (7 = top)";
}
