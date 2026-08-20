#include "editor.h"

#include "../import/model_import.h"
#include "../ui/editor_ui.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <raymath.h>
#include <stdio.h>
#include <string.h>

static const Color EDITOR_PALETTE[] = {
    { 89, 218, 151, 255 }, { 81, 164, 235, 255 }, { 240, 129, 72, 255 },
    { 229, 94, 167, 255 }, { 155, 110, 229, 255 }, { 238, 211, 80, 255 },
    { 92, 215, 215, 255 }, { 240, 240, 245, 255 }
};

static void SetStatus(Editor *editor, const char *text)
{
    snprintf(editor->status, sizeof(editor->status), "%s", text);
    editor->statusSeconds = 6.0f;
}

static unsigned int SpawnValidationHash(const LevlDocument *document)
{
    unsigned int hash = 2166136261u;
    for (int i = 0; i < document->objectCount; ++i) {
        const LevlObject *object = &document->objects[i];
        if (object->type != LEVL_OBJECT_MARKER && object->type != LEVL_OBJECT_MODEL) continue;
        const unsigned char *bytes = (const unsigned char *)object->name;
        size_t byteCount = object->type == LEVL_OBJECT_MARKER ? sizeof(object->name) + sizeof(object->tag) + sizeof(object->position) :
                           sizeof(object->name) + sizeof(object->tag) + sizeof(object->position) + sizeof(object->data.model);
        for (size_t byte = 0; byte < byteCount; ++byte) hash = (hash ^ bytes[byte]) * 16777619u;
    }
    return hash;
}

static void UpdateSpawnValidation(Editor *editor)
{
    Vector3 blockedSpawn;
    bool blockedByEditableCollision = LevlDocumentPlayerSpawnBlocked(&editor->document, &blockedSpawn, false);
    unsigned int validationHash = SpawnValidationHash(&editor->document);
    if (validationHash != editor->spawnModelValidationHash) {
        editor->spawnModelValidationHash = validationHash;
        editor->spawnModelBlocked = LevlDocumentPlayerSpawnBlocked(&editor->document, &blockedSpawn, true);
    }
    editor->playerSpawnBlocked = blockedByEditableCollision || editor->spawnModelBlocked;
}

static LevlObject *SelectedObject(Editor *editor)
{
    if (editor->selectedIndex < 0 || editor->selectedIndex >= editor->document.objectCount) return NULL;
    return &editor->document.objects[editor->selectedIndex];
}

static void SelectOnly(Editor *editor, int index)
{
    memset(editor->selected, 0, sizeof(editor->selected));
    if (index >= 0 && index < editor->document.objectCount) editor->selected[index] = true;
    editor->selectedIndex = index;
}

static bool IsSelected(const Editor *editor, int index)
{
    return index >= 0 && index < editor->document.objectCount && editor->selected[index];
}

static bool ShiftHeld(void);

static bool HasSelectedAncestor(const Editor *editor, const LevlObject *object)
{
    int parentId = object ? object->parentId : 0;
    while (parentId > 0) {
        for (int i = 0; i < editor->document.objectCount; ++i) {
            if (editor->document.objects[i].id != parentId) continue;
            if (IsSelected(editor, i)) return true;
            parentId = editor->document.objects[i].parentId;
            break;
        }
        if (parentId > 0) {
            bool exists = false;
            for (int i = 0; i < editor->document.objectCount; ++i) if (editor->document.objects[i].id == parentId) { exists = true; break; }
            if (!exists) break;
        }
    }
    return false;
}

static EditorUIState MakeUIState(const Editor *editor)
{
    const LevlObject *object = editor->selectedIndex >= 0 && editor->selectedIndex < editor->document.objectCount ?
                               &editor->document.objects[editor->selectedIndex] : NULL;
    return (EditorUIState){
        .mode = editor->mode, .gizmoMode = editor->gizmoMode, .meshSelectMode = editor->meshEdit.selectMode,
        .selectedObject = object, .snapEnabled = editor->snapEnabled, .showColliders = editor->showColliders,
        .dirty = editor->document.dirty, .playerSpawnBlocked = editor->playerSpawnBlocked,
        .document = &editor->document, .parentingMode = editor->parentingMode,
        .meshSelectionCount = object ? EditorMeshEditSelectionCount(&editor->meshEdit, object) : 0,
        .gridStep = editor->gridStep, .cameraMode = EditorNavigationModeName(&editor->navigation),
        .status = editor->status, .statusSeconds = editor->statusSeconds
    };
}

static bool ShiftHeld(void)
{
    return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

static bool CommandOrControlHeld(void)
{
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
           IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
}

static float SnapValue(float value, float step)
{
    return roundf(value / step) * step;
}

static Vector3 SnapVector(Vector3 value, float step)
{
    return (Vector3){ SnapValue(value.x, step), SnapValue(value.y, step), SnapValue(value.z, step) };
}

static float ClampMin(float value, float minimum)
{
    return value < minimum ? minimum : value;
}

static void MakeIdentifier(char *destination, size_t destinationSize, const char *source)
{
    size_t written = 0;
    for (const char *cursor = source; *cursor && written + 1 < destinationSize; ++cursor) {
        unsigned char c = (unsigned char)*cursor;
        destination[written++] = (isalnum(c) || c == '_') ? (char)c : '_';
    }
    if (written == 0 && destinationSize > 1) destination[written++] = 'x';
    destination[written] = '\0';
}

static void SelectNext(Editor *editor)
{
    if (editor->document.objectCount == 0) { editor->selectedIndex = -1; return; }
    SelectOnly(editor, (editor->selectedIndex + 1) % editor->document.objectCount);
    SetStatus(editor, "Selected next object");
}

static const char *EditorModeName(EditorMode mode)
{
    return mode == EDITOR_MODE_EDIT ? "Edit Mode" : "Object Mode";
}

static void ToggleEditMode(Editor *editor)
{
    LevlObject *object = SelectedObject(editor);
    if (editor->mode == EDITOR_MODE_EDIT) {
        editor->mode = EDITOR_MODE_OBJECT;
        EditorMeshEditReset(&editor->meshEdit);
        SetStatus(editor, "Object Mode");
        return;
    }
    if (!object) { SetStatus(editor, "Select a box or mesh first"); return; }
    if (object->type == LEVL_OBJECT_BOX) {
        LevlObjectConvertBoxToMesh(object);
        editor->document.dirty = true;
        SetStatus(editor, "Box converted to an editable mesh; box-only bevel and edge-loop settings are reset");
    }
    if (object->type != LEVL_OBJECT_MESH) {
        SetStatus(editor, "Edit Mode currently supports box meshes only");
        return;
    }
    editor->mode = EDITOR_MODE_EDIT;
    EditorMeshEditBegin(&editor->meshEdit, &editor->document, object);
    SetStatus(editor, "Edit Mode — 1 vertex, 2 edge, 3 face; LMB select, Shift+LMB add/remove");
}

static Vector3 DefaultCreatePosition(const Editor *editor)
{
    Vector3 direction = Vector3Normalize(Vector3Subtract(editor->camera.target, editor->camera.position));
    Vector3 position = Vector3Add(editor->camera.position, Vector3Scale(direction, 8.0f));
    position.x = roundf(position.x * 2.0f) * 0.5f;
    position.y = 0.0f;
    position.z = roundf(position.z * 2.0f) * 0.5f;
    if (editor->snapEnabled) position = SnapVector(position, editor->gridStep);
    return position;
}

static void AddObject(Editor *editor, LevlObjectType type)
{
    LevlObject *object = LevlDocumentAdd(&editor->document, type);
    if (!object) { SetStatus(editor, "Object limit reached (256)"); return; }
    object->position = DefaultCreatePosition(editor);
    if (type == LEVL_OBJECT_BOX) object->position.y = object->data.box.size.y * 0.5f;
    if (type == LEVL_OBJECT_RAMP) {
        object->data.ramp.end = Vector3Add(object->position, (Vector3){ 7.0f, 2.0f, 0.0f });
    }
    SelectOnly(editor, editor->document.objectCount - 1);
    SetStatus(editor, TextFormat("Added %s", LevlObjectTypeName(type)));
}

static void SelectAtMouse(Editor *editor)
{
    if (!EditorUIIsViewportPoint(GetMousePosition())) return;
    Ray ray = GetScreenToWorldRay(GetMousePosition(), editor->camera);
    float nearest = FLT_MAX;
    int found = -1;
    for (int i = 0; i < editor->document.objectCount; ++i) {
        RayCollision hit = GetRayCollisionBox(ray, LevlObjectGetWorldBounds(&editor->document, &editor->document.objects[i]));
        if (hit.hit && hit.distance < nearest) { nearest = hit.distance; found = i; }
    }
    if (found >= 0 && editor->parentingMode) {
        LevlObject *child = editor->parentingChildIndex >= 0 && editor->parentingChildIndex < editor->document.objectCount ?
                            &editor->document.objects[editor->parentingChildIndex] : NULL;
        LevlObject *parent = &editor->document.objects[found];
        if (child && LevlObjectSetParent(&editor->document, child, parent)) {
            SelectOnly(editor, editor->parentingChildIndex);
            SetStatus(editor, TextFormat("Parented %s to %s", child->name, parent->name));
        } else SetStatus(editor, "Parent must be a different object");
        editor->parentingMode = false;
        return;
    } else if (found >= 0) SetStatus(editor, TextFormat("Selected %s", editor->document.objects[found].name));
    if (found < 0) {
        if (!ShiftHeld()) SelectOnly(editor, -1);
        return;
    }
    if (editor->parentingMode) return;
    if (ShiftHeld()) {
        editor->selected[found] = !editor->selected[found];
        if (editor->selected[found]) editor->selectedIndex = found;
        else if (editor->selectedIndex == found) {
            editor->selectedIndex = -1;
            for (int i = 0; i < editor->document.objectCount; ++i) if (editor->selected[i]) { editor->selectedIndex = i; break; }
        }
    } else SelectOnly(editor, found);
}

static void MoveSelected(Editor *editor, Vector3 delta)
{
    bool moved = false;
    for (int i = 0; i < editor->document.objectCount; ++i) {
        LevlObject *object = &editor->document.objects[i];
        if (!IsSelected(editor, i) || HasSelectedAncestor(editor, object)) continue;
        object->position = Vector3Add(object->position, delta);
        if (object->type == LEVL_OBJECT_RAMP) object->data.ramp.end = Vector3Add(object->data.ramp.end, delta);
        if (object->type == LEVL_OBJECT_PLATFORM && fabsf(delta.y) > 0.001f) {
            object->data.platform.top += delta.y;
            object->position.y = 0.0f;
        }
        if (editor->snapEnabled) {
            object->position = SnapVector(object->position, editor->gridStep);
            if (object->type == LEVL_OBJECT_RAMP) object->data.ramp.end = SnapVector(object->data.ramp.end, editor->gridStep);
            if (object->type == LEVL_OBJECT_PLATFORM) object->data.platform.top = SnapValue(object->data.platform.top, editor->gridStep);
        }
        moved = true;
    }
    if (moved) editor->document.dirty = true;
}

static void DeleteSelected(Editor *editor)
{
    bool removed = false;
    for (int index = editor->document.objectCount - 1; index >= 0; --index) {
        if (!IsSelected(editor, index)) continue;
        removed |= LevlDocumentRemove(&editor->document, index);
    }
    if (removed) {
        SelectOnly(editor, editor->document.objectCount > 0 ? editor->document.objectCount - 1 : -1);
        editor->mode = EDITOR_MODE_OBJECT;
        EditorMeshEditReset(&editor->meshEdit);
        SetStatus(editor, "Selected objects removed");
    }
}

static void ResizeSelected(Editor *editor, float amount)
{
    LevlObject *object = SelectedObject(editor);
    if (!object) return;
    if (object->type == LEVL_OBJECT_BOX) {
        object->data.box.size.x = ClampMin(object->data.box.size.x + amount, 0.25f);
        object->data.box.size.y = ClampMin(object->data.box.size.y + amount, 0.25f);
        object->data.box.size.z = ClampMin(object->data.box.size.z + amount, 0.25f);
    } else if (object->type == LEVL_OBJECT_PLATFORM) {
        object->data.platform.size.x = ClampMin(object->data.platform.size.x + amount * 2.0f, 1.0f);
        object->data.platform.size.y = ClampMin(object->data.platform.size.y + amount * 2.0f, 1.0f);
    } else if (object->type == LEVL_OBJECT_RAMP) {
        object->data.ramp.width = ClampMin(object->data.ramp.width + amount, 0.5f);
    } else if (object->type == LEVL_OBJECT_MODEL) {
        float factor = amount > 0.0f ? 1.1f : 0.9f;
        object->data.model.scale = Vector3Scale(object->data.model.scale, factor);
    } else return;
    editor->document.dirty = true;
}

static void BeginTextInput(Editor *editor, EditorTextMode mode)
{
    LevlObject *object = SelectedObject(editor);
    editor->textMode = mode;
    editor->textBuffer[0] = '\0';
    if (mode == EDITOR_TEXT_NAME && object) snprintf(editor->textBuffer, sizeof(editor->textBuffer), "%s", object->name);
    if (mode == EDITOR_TEXT_TAG && object) snprintf(editor->textBuffer, sizeof(editor->textBuffer), "%s", object->tag);
    SetStatus(editor, mode == EDITOR_TEXT_IMPORT ? "Paste a model path, then press Enter" : "Type text, then press Enter");
}

static void FinishTextInput(Editor *editor)
{
    LevlObject *object = SelectedObject(editor);
    char result[192];
    if (editor->textMode == EDITOR_TEXT_IMPORT) {
        if (editor->textBuffer[0]) {
            ModelImportAdd(&editor->document, editor->textBuffer, result, (int)sizeof(result));
            SetStatus(editor, result);
            if (editor->document.objectCount > 0) SelectOnly(editor, editor->document.objectCount - 1);
        }
    } else if (object && editor->textBuffer[0]) {
        char identifier[LEVL_PATH_LENGTH];
        MakeIdentifier(identifier, sizeof(identifier), editor->textBuffer);
        if (editor->textMode == EDITOR_TEXT_NAME) snprintf(object->name, sizeof(object->name), "%s", identifier);
        if (editor->textMode == EDITOR_TEXT_TAG) snprintf(object->tag, sizeof(object->tag), "%s", identifier);
        editor->document.dirty = true;
        SetStatus(editor, editor->textMode == EDITOR_TEXT_NAME ? "Object name updated" : "Object tag updated");
    }
    editor->textMode = EDITOR_TEXT_NONE;
    editor->textBuffer[0] = '\0';
}

static void UpdateTextInput(Editor *editor)
{
    bool shortcutHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                        IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (shortcutHeld && IsKeyPressed(KEY_V)) {
        const char *clipboard = GetClipboardText();
        if (clipboard) snprintf(editor->textBuffer, sizeof(editor->textBuffer), "%s", clipboard);
    }
    int key = GetCharPressed();
    while (key > 0) {
        bool valid = key >= 32 && key <= 126;
        if (valid && strlen(editor->textBuffer) + 1 < sizeof(editor->textBuffer)) {
            char character = (char)key;
            if (editor->textMode != EDITOR_TEXT_IMPORT && character == ' ') character = '_';
            size_t length = strlen(editor->textBuffer);
            editor->textBuffer[length] = character;
            editor->textBuffer[length + 1] = '\0';
        }
        key = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        size_t length = strlen(editor->textBuffer);
        if (length > 0) editor->textBuffer[length - 1] = '\0';
    }
    if (IsKeyPressed(KEY_ENTER)) FinishTextInput(editor);
    if (IsKeyPressed(KEY_ESCAPE)) { editor->textMode = EDITOR_TEXT_NONE; SetStatus(editor, "Text input cancelled"); }
}

static void CycleFaceColor(Editor *editor)
{
    LevlObject *object = SelectedObject(editor);
    if (!object || object->type != LEVL_OBJECT_BOX) return;
    Color current = object->data.box.faceColors[editor->selectedFace];
    int currentIndex = -1;
    int paletteCount = (int)(sizeof(EDITOR_PALETTE) / sizeof(EDITOR_PALETTE[0]));
    for (int i = 0; i < paletteCount; ++i) {
        if (ColorIsEqual(current, EDITOR_PALETTE[i])) { currentIndex = i; break; }
    }
    object->data.box.faceColors[editor->selectedFace] = EDITOR_PALETTE[(currentIndex + 1) % paletteCount];
    editor->document.dirty = true;
}

static void ToggleCollision(Editor *editor)
{
    LevlObject *object = SelectedObject(editor);
    if (!object) return;
    if (object->type == LEVL_OBJECT_BOX) object->data.box.collidable = !object->data.box.collidable;
    else if (object->type == LEVL_OBJECT_MESH) object->data.mesh.collidable = !object->data.mesh.collidable;
    else if (object->type == LEVL_OBJECT_MODEL) object->data.model.collidable = !object->data.model.collidable;
    else { SetStatus(editor, "Only boxes, meshes and models generate solid colliders"); return; }
    editor->document.dirty = true;
    SetStatus(editor, "Collider toggled");
}

static Vector3 GizmoOrigin(const Editor *editor, const LevlObject *object)
{
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(&editor->document, object, &position, &rotation, &scale);
    if (object->type == LEVL_OBJECT_PLATFORM) {
        Matrix rotationMatrix = MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD));
        Vector3 top = Vector3Multiply((Vector3){ 0, object->data.platform.top - object->position.y, 0 }, scale);
        return Vector3Add(position, Vector3Transform(top, rotationMatrix));
    }
    return position;
}

static float GizmoSize(const Editor *editor, const LevlObject *object)
{
    BoundingBox bounds = LevlObjectGetWorldBounds(&editor->document, object);
    float sizeX = bounds.max.x - bounds.min.x, sizeY = bounds.max.y - bounds.min.y, sizeZ = bounds.max.z - bounds.min.z;
    float largest = fmaxf(sizeX, fmaxf(sizeY, sizeZ));
    return fmaxf(1.25f, fminf(largest * 0.65f, 4.0f));
}

static float DistanceToSegment(Vector2 point, Vector2 a, Vector2 b)
{
    Vector2 delta = Vector2Subtract(b, a);
    float lengthSquared = Vector2DotProduct(delta, delta);
    if (lengthSquared < 0.0001f) return Vector2Distance(point, a);
    float t = Vector2DotProduct(Vector2Subtract(point, a), delta) / lengthSquared;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return Vector2Distance(point, Vector2Add(a, Vector2Scale(delta, t)));
}

static bool EditGizmoOrigin(const Editor *editor, const LevlObject *object, Vector3 *origin, float *size)
{
    if (!object || !origin || !size) return false;
    *origin = GizmoOrigin(editor, object);
    *size = GizmoSize(editor, object);
    if (editor->mode == EDITOR_MODE_EDIT) {
        if (!EditorMeshEditGetSelectionCenter(&editor->meshEdit, object, origin)) return false;
        *size = fmaxf(0.8f, *size * 0.45f);
    }
    return true;
}

static int PickGizmoAxis(const Editor *editor, Vector3 origin, float size, Vector2 mouse)
{
    Vector2 center = GetWorldToScreen(origin, editor->camera);
    float bestDistance = 14.0f;
    int result = -1;
    if (editor->gizmoMode != EDITOR_GIZMO_ROTATE) {
        const Vector3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int axis = 0; axis < 3; ++axis) {
            Vector2 end = GetWorldToScreen(Vector3Add(origin, Vector3Scale(axes[axis], size)), editor->camera);
            float distance = DistanceToSegment(mouse, center, end);
            if (distance < bestDistance) { bestDistance = distance; result = axis; }
        }
        return result;
    }

    for (int axis = 0; axis < 3; ++axis) {
        Vector2 previous = { 0 };
        bool hasPrevious = false;
        for (int segment = 0; segment <= 48; ++segment) {
            float angle = (float)segment / 48.0f * 2.0f * PI;
            Vector3 local = axis == 0 ? (Vector3){ 0, cosf(angle) * size, sinf(angle) * size } :
                            (axis == 1 ? (Vector3){ cosf(angle) * size, 0, sinf(angle) * size } :
                                         (Vector3){ cosf(angle) * size, sinf(angle) * size, 0 });
            Vector2 current = GetWorldToScreen(Vector3Add(origin, local), editor->camera);
            if (hasPrevious) {
                float distance = DistanceToSegment(mouse, previous, current);
                if (distance < bestDistance) { bestDistance = distance; result = axis; }
            }
            previous = current;
            hasPrevious = true;
        }
    }
    return result;
}

static void BeginGizmoDrag(Editor *editor, int axis, Vector3 origin, float size)
{
    LevlObject *object = SelectedObject(editor);
    if (!object || axis < 0) return;
    EditorGizmoDrag *drag = &editor->gizmoDrag;
    *drag = (EditorGizmoDrag){ .active = true, .axis = axis, .startMouse = GetMousePosition(),
        .startPosition = object->position, .startBoxSize = object->type == LEVL_OBJECT_BOX ? object->data.box.size : (Vector3){ 1, 1, 1 },
        .startRotation = object->type == LEVL_OBJECT_BOX ? object->data.box.rotation :
                         (object->type == LEVL_OBJECT_MODEL ? object->data.model.rotation : (Vector3){ 0 }),
        .startModelScale = object->type == LEVL_OBJECT_MODEL ? object->data.model.scale : (Vector3){ 1, 1, 1 },
        .startPlatformSize = object->type == LEVL_OBJECT_PLATFORM ? object->data.platform.size : (Vector2){ 1, 1 },
        .startRampEnd = object->type == LEVL_OBJECT_RAMP ? object->data.ramp.end : (Vector3){ 0 },
        .startPlatformTop = object->type == LEVL_OBJECT_PLATFORM ? object->data.platform.top : 0.0f,
        .startRampWidth = object->type == LEVL_OBJECT_RAMP ? object->data.ramp.width : 1.0f,
        .startGizmoOrigin = origin, .startGizmoSize = size };
    if (editor->mode == EDITOR_MODE_EDIT) {
        drag->startMeshVertexCount = object->data.mesh.vertexCount;
        memcpy(drag->startMeshVertices, object->data.mesh.vertices,
               (size_t)drag->startMeshVertexCount * sizeof(drag->startMeshVertices[0]));
    }
    Vector2 center = GetWorldToScreen(origin, editor->camera);
    drag->startAngle = atan2f(drag->startMouse.y - center.y, drag->startMouse.x - center.x);
}

static void ApplyGizmoPosition(Editor *editor, LevlObject *object, Vector3 position)
{
    Vector3 delta = Vector3Subtract(position, editor->gizmoDrag.startPosition);
    object->position = position;
    if (object->type == LEVL_OBJECT_RAMP) object->data.ramp.end = Vector3Add(editor->gizmoDrag.startRampEnd, delta);
    if (object->type == LEVL_OBJECT_PLATFORM) {
        object->data.platform.top = editor->gizmoDrag.startPlatformTop + delta.y;
        object->position.y = 0.0f;
    }
}

static void UpdateGizmoDrag(Editor *editor)
{
    if (!editor->gizmoDrag.active) return;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { editor->gizmoDrag.active = false; return; }
    LevlObject *object = SelectedObject(editor);
    if (!object) { editor->gizmoDrag.active = false; return; }
    const Vector3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    int axis = editor->gizmoDrag.axis;
    float gizmoSize = editor->gizmoDrag.startGizmoSize;
    Vector3 origin = editor->gizmoDrag.startGizmoOrigin;
    Vector2 center = GetWorldToScreen(origin, editor->camera);

    if (editor->gizmoMode == EDITOR_GIZMO_ROTATE) {
        Vector2 mouse = GetMousePosition();
        float angle = (atan2f(mouse.y - center.y, mouse.x - center.x) - editor->gizmoDrag.startAngle) * RAD2DEG;
        if (editor->snapEnabled) angle = SnapValue(angle, 15.0f);
        if (editor->mode == EDITOR_MODE_EDIT) {
            memcpy(object->data.mesh.vertices, editor->gizmoDrag.startMeshVertices,
                   (size_t)editor->gizmoDrag.startMeshVertexCount * sizeof(object->data.mesh.vertices[0]));
            EditorMeshEditRotate(&editor->meshEdit, object, axes[axis], angle);
        }
        if (object->type == LEVL_OBJECT_BOX) object->data.box.rotation = editor->gizmoDrag.startRotation;
        if (object->type == LEVL_OBJECT_MODEL) object->data.model.rotation = editor->gizmoDrag.startRotation;
        if (object->type == LEVL_OBJECT_BOX) ((float *)&object->data.box.rotation)[axis] += angle;
        if (object->type == LEVL_OBJECT_MODEL) ((float *)&object->data.model.rotation)[axis] += angle;
        editor->document.dirty = true;
        return;
    }

    Vector2 screenEnd = GetWorldToScreen(Vector3Add(origin, Vector3Scale(axes[axis], gizmoSize)), editor->camera);
    Vector2 screenAxis = Vector2Subtract(screenEnd, center);
    float length = Vector2Length(screenAxis);
    if (length < 2.0f) return;
    float pixelsAlongAxis = Vector2DotProduct(Vector2Subtract(GetMousePosition(), editor->gizmoDrag.startMouse), Vector2Scale(screenAxis, 1.0f / length));
    float worldAmount = pixelsAlongAxis / length * gizmoSize;
    if (editor->gizmoMode == EDITOR_GIZMO_MOVE) {
        Vector3 position = Vector3Add(editor->gizmoDrag.startPosition, Vector3Scale(axes[axis], worldAmount));
        if (editor->mode == EDITOR_MODE_EDIT) {
            position = Vector3Add(editor->gizmoDrag.startGizmoOrigin, Vector3Scale(axes[axis], worldAmount));
            if (editor->snapEnabled) position = SnapVector(position, editor->gridStep);
            memcpy(object->data.mesh.vertices, editor->gizmoDrag.startMeshVertices,
                   (size_t)editor->gizmoDrag.startMeshVertexCount * sizeof(object->data.mesh.vertices[0]));
            EditorMeshEditMove(&editor->meshEdit, object, Vector3Subtract(position, editor->gizmoDrag.startGizmoOrigin));
            if (editor->snapEnabled) EditorMeshEditSnapSelection(&editor->meshEdit, object, editor->gridStep);
        } else {
            if (editor->snapEnabled) position = SnapVector(position, editor->gridStep);
            ApplyGizmoPosition(editor, object, position);
        }
    } else {
        float factor = fmaxf(0.05f, 1.0f + worldAmount / gizmoSize);
        if (editor->mode == EDITOR_MODE_EDIT) {
            memcpy(object->data.mesh.vertices, editor->gizmoDrag.startMeshVertices,
                   (size_t)editor->gizmoDrag.startMeshVertexCount * sizeof(object->data.mesh.vertices[0]));
            EditorMeshEditScaleAxis(&editor->meshEdit, object, axis, factor);
        } else if (object->type == LEVL_OBJECT_BOX) {
            object->data.box.size = editor->gizmoDrag.startBoxSize;
            ((float *)&object->data.box.size)[axis] = ClampMin(((float *)&editor->gizmoDrag.startBoxSize)[axis] * factor, 0.1f);
            if (editor->snapEnabled) ((float *)&object->data.box.size)[axis] = ClampMin(SnapValue(((float *)&object->data.box.size)[axis], editor->gridStep), editor->gridStep);
        } else if (object->type == LEVL_OBJECT_MODEL) {
            object->data.model.scale = editor->gizmoDrag.startModelScale;
            ((float *)&object->data.model.scale)[axis] = ClampMin(((float *)&editor->gizmoDrag.startModelScale)[axis] * factor, 0.05f);
        } else if (object->type == LEVL_OBJECT_PLATFORM && axis != 1) {
            object->data.platform.size = editor->gizmoDrag.startPlatformSize;
            float *component = axis == 0 ? &object->data.platform.size.x : &object->data.platform.size.y;
            float start = axis == 0 ? editor->gizmoDrag.startPlatformSize.x : editor->gizmoDrag.startPlatformSize.y;
            *component = ClampMin(start * factor, editor->snapEnabled ? editor->gridStep : 0.25f);
            if (editor->snapEnabled) *component = ClampMin(SnapValue(*component, editor->gridStep), editor->gridStep);
        } else if (object->type == LEVL_OBJECT_RAMP) object->data.ramp.width = ClampMin(editor->gizmoDrag.startRampWidth * factor, 0.25f);
    }
    editor->document.dirty = true;
}

static void ApplySelectedScale(Editor *editor)
{
    LevlObject *object = SelectedObject(editor);
    if (!object) return;
    if (object->type != LEVL_OBJECT_MODEL) {
        SetStatus(editor, "Primitive dimensions are already applied; use them directly for texture-safe geometry");
        return;
    }
    Model *model = EditorSceneGetModel(editor->modelCache, LEVL_MAX_OBJECTS, object);
    char result[192];
    if (ModelImportApplyScale(object, model, result, (int)sizeof(result))) {
        EditorSceneInvalidateModel(editor->modelCache, LEVL_MAX_OBJECTS, object->id);
        editor->document.dirty = true;
    }
    SetStatus(editor, result);
}

static void AdjustGridStep(Editor *editor, int direction)
{
    static const float steps[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    int index = 0;
    for (int i = 0; i < (int)(sizeof(steps) / sizeof(steps[0])); ++i) {
        if (fabsf(steps[i] - editor->gridStep) < 0.01f) { index = i; break; }
    }
    index += direction;
    if (index < 0) index = 0;
    if (index >= (int)(sizeof(steps) / sizeof(steps[0]))) index = (int)(sizeof(steps) / sizeof(steps[0])) - 1;
    editor->gridStep = steps[index];
    SetStatus(editor, TextFormat("Grid step: %.3g", editor->gridStep));
}

static void SetGizmoMode(Editor *editor, EditorGizmoMode mode)
{
    editor->gizmoMode = mode;
    editor->gizmoHighlightedAxis = -1;
    SetStatus(editor, mode == EDITOR_GIZMO_MOVE ? "Move gizmo — drag a coloured axis" :
                      (mode == EDITOR_GIZMO_ROTATE ? "Rotate gizmo — drag a coloured ring" : "Scale gizmo — drag a coloured axis"));
}

static void UpdateMeshEditControls(Editor *editor)
{
    LevlObject *object = SelectedObject(editor);
    if (!object || object->type != LEVL_OBJECT_MESH || editor->meshEdit.objectId != object->id) {
        editor->mode = EDITOR_MODE_OBJECT;
        EditorMeshEditReset(&editor->meshEdit);
        SetStatus(editor, "Edit Mode closed because the active mesh changed");
        return;
    }
    if (IsKeyPressed(KEY_TAB)) { ToggleEditMode(editor); return; }
    if (IsKeyPressed(KEY_ONE)) { EditorMeshEditSetSelectMode(&editor->meshEdit, EDITOR_MESH_SELECT_VERTEX); SetStatus(editor, "Vertex select mode"); }
    if (IsKeyPressed(KEY_TWO)) { EditorMeshEditSetSelectMode(&editor->meshEdit, EDITOR_MESH_SELECT_EDGE); SetStatus(editor, "Edge select mode"); }
    if (IsKeyPressed(KEY_THREE)) { EditorMeshEditSetSelectMode(&editor->meshEdit, EDITOR_MESH_SELECT_FACE); SetStatus(editor, "Face select mode"); }
    if ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_A)) { EditorMeshEditClear(&editor->meshEdit); SetStatus(editor, "Mesh selection cleared"); }
    else if (IsKeyPressed(KEY_A) && !ShiftHeld()) { EditorMeshEditSelectAll(&editor->meshEdit, object); SetStatus(editor, "Selected all mesh elements"); }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        bool append = ShiftHeld();
        if (EditorMeshEditPick(&editor->meshEdit, object, editor->camera, GetMousePosition(), append))
            SetStatus(editor, append ? "Mesh selection updated" : "Mesh element selected");
    }

    float step = editor->snapEnabled ? editor->gridStep : (ShiftHeld() ? 1.0f : 0.2f);
    Vector3 movement = { 0 };
    if (IsKeyDown(KEY_LEFT)) movement.x -= step;
    if (IsKeyDown(KEY_RIGHT)) movement.x += step;
    if (IsKeyDown(KEY_UP)) movement.z -= step;
    if (IsKeyDown(KEY_DOWN)) movement.z += step;
    if (IsKeyDown(KEY_Q)) movement.y += step;
    if (IsKeyDown(KEY_E)) movement.y -= step;
    if (Vector3Length(movement) > 0.0f) {
        EditorMeshEditMove(&editor->meshEdit, object, movement);
        if (editor->snapEnabled) EditorMeshEditSnapSelection(&editor->meshEdit, object, editor->gridStep);
        editor->document.dirty = true;
    }
    if (IsKeyPressed(KEY_LEFT_BRACKET)) { EditorMeshEditScale(&editor->meshEdit, object, 0.9f); editor->document.dirty = true; }
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) { EditorMeshEditScale(&editor->meshEdit, object, 1.1f); editor->document.dirty = true; }
    if (IsKeyPressed(KEY_COMMA)) { EditorMeshEditRotate(&editor->meshEdit, object, (Vector3){ 0, 1, 0 }, editor->snapEnabled ? -15.0f : -5.0f); editor->document.dirty = true; }
    if (IsKeyPressed(KEY_PERIOD)) { EditorMeshEditRotate(&editor->meshEdit, object, (Vector3){ 0, 1, 0 }, editor->snapEnabled ? 15.0f : 5.0f); editor->document.dirty = true; }
    if (IsKeyPressed(KEY_C) && editor->meshEdit.selectMode == EDITOR_MESH_SELECT_FACE) {
        EditorMeshEditCycleFaceColor(&editor->meshEdit, object, EDITOR_PALETTE, (int)(sizeof(EDITOR_PALETTE) / sizeof(EDITOR_PALETTE[0])));
        editor->document.dirty = true;
    }
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
        if (EditorMeshEditSubdivideFaces(&editor->meshEdit, object)) {
            editor->document.dirty = true;
            SetStatus(editor, "Selected faces subdivided");
        } else SetStatus(editor, "Select one or more faces before subdivision (or mesh limit reached)");
    }
}

static void CopySelectedObject(Editor *editor);
static void PasteObject(Editor *editor);

static bool UpdateSharedShortcuts(Editor *editor)
{
    if (CommandOrControlHeld()) {
        if (IsKeyPressed(KEY_C)) { CopySelectedObject(editor); return true; }
        if (IsKeyPressed(KEY_V)) { PasteObject(editor); return true; }
        if (IsKeyPressed(KEY_P)) {
            if (SelectedObject(editor)) {
                editor->parentingMode = true;
                editor->parentingChildIndex = editor->selectedIndex;
                SetStatus(editor, "Select an Empty object as parent");
            }
            return true;
        }
        if (editor->mode == EDITOR_MODE_OBJECT && IsKeyPressed(KEY_TAB)) { SelectNext(editor); return true; }
        if (IsKeyPressed(KEY_S)) {
            char result[192];
            LevlDocumentSave(&editor->document, "level.levl", result, (int)sizeof(result));
            SetStatus(editor, result);
            return true;
        }
        if (IsKeyPressed(KEY_O)) {
            char result[192];
            if (LevlDocumentLoad(&editor->document, "level.levl", result, (int)sizeof(result))) {
                EditorSceneUnloadModels(editor->modelCache, LEVL_MAX_OBJECTS);
                memset(editor->modelCache, 0, sizeof(editor->modelCache));
                SelectOnly(editor, editor->document.objectCount ? 0 : -1);
                editor->mode = EDITOR_MODE_OBJECT;
                EditorMeshEditReset(&editor->meshEdit);
            }
            SetStatus(editor, result);
            return true;
        }
        if (editor->mode == EDITOR_MODE_OBJECT && IsKeyPressed(KEY_A)) { ApplySelectedScale(editor); return true; }
        return false;
    }
    if (ShiftHeld() && IsKeyPressed(KEY_RIGHT_BRACKET)) {
        EditorNavigationToggleMode(&editor->navigation, &editor->camera);
        SetStatus(editor, editor->navigation.mode == EDITOR_NAVIGATION_FREE ? "Free camera mode enabled" : "Orbit camera mode enabled");
        return true;
    }
    if (ShiftHeld() && IsKeyPressed(KEY_S)) { editor->snapEnabled = !editor->snapEnabled; SetStatus(editor, editor->snapEnabled ? "Grid snapping enabled" : "Grid snapping disabled"); return true; }
    if (ShiftHeld() && IsKeyPressed(KEY_COMMA)) { AdjustGridStep(editor, -1); return true; }
    if (ShiftHeld() && IsKeyPressed(KEY_PERIOD)) { AdjustGridStep(editor, 1); return true; }
    if (IsKeyPressed(KEY_F6)) {
        char result[192];
        LevlDocumentExportToGame(&editor->document, "../Slimeworks Factory/levels/factory.levl", result, (int)sizeof(result));
        SetStatus(editor, result);
        return true;
    }
    if (IsKeyPressed(KEY_G)) { SetGizmoMode(editor, EDITOR_GIZMO_MOVE); return false; }
    if (IsKeyPressed(KEY_R) && !ShiftHeld()) { SetGizmoMode(editor, EDITOR_GIZMO_ROTATE); return false; }
    if (IsKeyPressed(KEY_S) && !ShiftHeld()) { SetGizmoMode(editor, EDITOR_GIZMO_SCALE); return false; }
    return false;
}

static LevlObject MakeClipboardObject(const Editor *editor, const LevlObject *object)
{
    LevlObject copy = *object;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(&editor->document, object, &position, &rotation, &scale);
    copy.parentId = 0;
    copy.position = position;
    if (copy.type == LEVL_OBJECT_BOX) {
        copy.data.box.rotation = rotation;
        copy.data.box.size = Vector3Multiply(copy.data.box.size, scale);
    } else if (copy.type == LEVL_OBJECT_MESH) {
        Matrix transform = MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD)));
        for (int vertex = 0; vertex < copy.data.mesh.vertexCount; ++vertex)
            copy.data.mesh.vertices[vertex] = Vector3Transform(copy.data.mesh.vertices[vertex], transform);
    } else if (copy.type == LEVL_OBJECT_MODEL) {
        copy.data.model.rotation = rotation;
        copy.data.model.scale = scale;
    } else if (copy.type == LEVL_OBJECT_EMPTY) {
        copy.data.empty.rotation = rotation;
        copy.data.empty.scale = scale;
    } else if (copy.type == LEVL_OBJECT_PLATFORM) {
        copy.data.platform.size = Vector2Multiply(copy.data.platform.size, (Vector2){ scale.x, scale.z });
        copy.data.platform.top = position.y + (copy.data.platform.top - object->position.y) * scale.y;
    } else if (copy.type == LEVL_OBJECT_RAMP) {
        Matrix transform = MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD)));
        copy.data.ramp.end = Vector3Add(position, Vector3Transform(Vector3Subtract(copy.data.ramp.end, object->position), transform));
        copy.data.ramp.width *= fmaxf(scale.x, scale.z);
    } else if (copy.type == LEVL_OBJECT_MARKER) {
        copy.position = position;
    }
    return copy;
}

static void CopySelectedObject(Editor *editor)
{
    LevlObject *object = SelectedObject(editor);
    if (!object) return;
    LevlObject copy = MakeClipboardObject(editor, object);
    char clipboard[sizeof(LevlObject) * 2 + 64];
    int offset = snprintf(clipboard, sizeof(clipboard), "SLIMEWORKS_OBJECT_V2 %zu ", sizeof(copy));
    if (offset < 0 || (size_t)offset + sizeof(copy) * 2 + 1 > sizeof(clipboard)) {
        SetStatus(editor, "Clipboard object is too large");
        return;
    }
    const unsigned char *bytes = (const unsigned char *)&copy;
    for (size_t byte = 0; byte < sizeof(copy); ++byte)
        snprintf(clipboard + offset + byte * 2, 3, "%02X", bytes[byte]);
    SetClipboardText(clipboard);
    SetStatus(editor, TextFormat("Copied %s", object->name));
}

static void PasteObject(Editor *editor)
{
    const char *clipboard = GetClipboardText();
    if (!clipboard) {
        SetStatus(editor, "Clipboard is empty");
        return;
    }
    size_t byteCount = 0;
    int offset = 0;
    if (sscanf(clipboard, "SLIMEWORKS_OBJECT_V2 %zu %n", &byteCount, &offset) == 1 && byteCount == sizeof(LevlObject)) {
        size_t encodedLength = strlen(clipboard + offset);
        if (encodedLength >= byteCount * 2) {
            LevlObject copy = { 0 };
            unsigned char *bytes = (unsigned char *)&copy;
            bool valid = true;
            for (size_t byte = 0; byte < byteCount; ++byte) {
                int high = isdigit((unsigned char)clipboard[offset + byte * 2]) ? clipboard[offset + byte * 2] - '0' : toupper((unsigned char)clipboard[offset + byte * 2]) - 'A' + 10;
                int low = isdigit((unsigned char)clipboard[offset + byte * 2 + 1]) ? clipboard[offset + byte * 2 + 1] - '0' : toupper((unsigned char)clipboard[offset + byte * 2 + 1]) - 'A' + 10;
                if (high < 0 || high > 15 || low < 0 || low > 15) { valid = false; break; }
                bytes[byte] = (unsigned char)((high << 4) | low);
            }
            if (valid && copy.type >= LEVL_OBJECT_BOX && copy.type <= LEVL_OBJECT_EMPTY) {
                LevlObject *object = LevlDocumentAdd(&editor->document, copy.type);
                if (!object) { SetStatus(editor, "Object limit reached (256)"); return; }
                int newId = object->id;
                *object = copy;
                object->id = newId;
                object->parentId = 0;
                snprintf(object->name, sizeof(object->name), "%s_copy", copy.name);
                SelectOnly(editor, editor->document.objectCount - 1);
                SetStatus(editor, TextFormat("Pasted %s", LevlObjectTypeName(object->type)));
                return;
            }
        }
    }
    if (strncmp(clipboard, "SLIMEWORKS_OBJECT ", 19) != 0) {
        SetStatus(editor, "Clipboard does not contain a supported Slimeworks object");
        return;
    }
    LevlObject *object = NULL;
    char type[16], name[LEVL_NAME_LENGTH], tag[LEVL_TAG_LENGTH], asset[LEVL_PATH_LENGTH];
    float px, py, pz, rx, ry, rz, sx, sy, sz, cx, cy, cz;
    int collidable;
    if (sscanf(clipboard, "SLIMEWORKS_OBJECT %15s %31s %31s %255s %f %f %f %f %f %f %f %f %f %d %f %f %f",
               type, name, tag, asset, &px, &py, &pz, &rx, &ry, &rz, &sx, &sy, &sz, &collidable,
               &cx, &cy, &cz) == 17 && strcmp(type, "MODEL") == 0) {
        object = LevlDocumentAdd(&editor->document, LEVL_OBJECT_MODEL);
        if (!object) { SetStatus(editor, "Object limit reached (256)"); return; }
        snprintf(object->name, sizeof(object->name), "%s_copy", name);
        snprintf(object->tag, sizeof(object->tag), "%s", tag);
        snprintf(object->data.model.assetPath, sizeof(object->data.model.assetPath), "%s", asset);
        object->position = (Vector3){ px + 1.0f, py, pz + 1.0f };
        object->data.model.rotation = (Vector3){ rx, ry, rz };
        object->data.model.scale = (Vector3){ sx, sy, sz };
        object->data.model.collidable = collidable != 0;
        object->data.model.collisionSize = (Vector3){ cx, cy, cz };
        SelectOnly(editor, editor->document.objectCount - 1);
        SetStatus(editor, "Pasted model");
        return;
    }
    if (sscanf(clipboard, "SLIMEWORKS_OBJECT %15s %31s %31s %f %f %f %f %f %f %f %f %f",
               type, name, tag, &px, &py, &pz, &rx, &ry, &rz, &sx, &sy, &sz) == 12 && strcmp(type, "EMPTY") == 0) {
        object = LevlDocumentAdd(&editor->document, LEVL_OBJECT_EMPTY);
        if (!object) { SetStatus(editor, "Object limit reached (256)"); return; }
        snprintf(object->name, sizeof(object->name), "%s_copy", name);
        snprintf(object->tag, sizeof(object->tag), "%s", tag);
        object->position = (Vector3){ px + 1.0f, py, pz + 1.0f };
        object->data.empty.rotation = (Vector3){ rx, ry, rz };
        object->data.empty.scale = (Vector3){ sx, sy, sz };
        SelectOnly(editor, editor->document.objectCount - 1);
        SetStatus(editor, "Pasted Empty");
        return;
    }
    SetStatus(editor, "Clipboard data is invalid");
}

static void ApplyUIAction(Editor *editor, EditorUIAction action, int targetIndex)
{
    LevlObject *object = SelectedObject(editor);
    char result[192];
    switch (action) {
        case EDITOR_UI_NEW:
            EditorSceneUnloadModels(editor->modelCache, LEVL_MAX_OBJECTS);
            memset(editor->modelCache, 0, sizeof(editor->modelCache));
            LevlDocumentNew(&editor->document);
            SelectOnly(editor, 0);
            editor->mode = EDITOR_MODE_OBJECT;
            EditorMeshEditReset(&editor->meshEdit);
            SetStatus(editor, "New starter level created");
            break;
        case EDITOR_UI_SAVE:
            LevlDocumentSave(&editor->document, "level.levl", result, (int)sizeof(result));
            SetStatus(editor, result);
            break;
        case EDITOR_UI_EXPORT:
            LevlDocumentExportToGame(&editor->document, "../Slimeworks Factory/levels/factory.levl", result, (int)sizeof(result));
            SetStatus(editor, result);
            break;
        case EDITOR_UI_COPY: CopySelectedObject(editor); break;
        case EDITOR_UI_PASTE: PasteObject(editor); break;
        case EDITOR_UI_SET_OBJECT_MODE:
            if (editor->mode == EDITOR_MODE_EDIT) ToggleEditMode(editor);
            break;
        case EDITOR_UI_SET_EDIT_MODE:
            if (editor->mode == EDITOR_MODE_OBJECT) ToggleEditMode(editor);
            break;
        case EDITOR_UI_TOGGLE_EDIT: ToggleEditMode(editor); break;
        case EDITOR_UI_SET_MOVE: SetGizmoMode(editor, EDITOR_GIZMO_MOVE); break;
        case EDITOR_UI_SET_ROTATE: SetGizmoMode(editor, EDITOR_GIZMO_ROTATE); break;
        case EDITOR_UI_SET_SCALE: SetGizmoMode(editor, EDITOR_GIZMO_SCALE); break;
        case EDITOR_UI_TOGGLE_SNAP:
            editor->snapEnabled = !editor->snapEnabled;
            SetStatus(editor, editor->snapEnabled ? "Grid snapping enabled" : "Grid snapping disabled");
            break;
        case EDITOR_UI_TOGGLE_CAMERA:
            EditorNavigationToggleMode(&editor->navigation, &editor->camera);
            SetStatus(editor, editor->navigation.mode == EDITOR_NAVIGATION_FREE ? "Free camera mode enabled" : "Orbit camera mode enabled");
            break;
        case EDITOR_UI_TOGGLE_COLLIDERS:
            editor->showColliders = !editor->showColliders;
            SetStatus(editor, editor->showColliders ? "Collision overlays enabled" : "Collision overlays hidden");
            break;
        case EDITOR_UI_ADD_BOX: AddObject(editor, LEVL_OBJECT_BOX); break;
        case EDITOR_UI_ADD_MESH: AddObject(editor, LEVL_OBJECT_MESH); break;
        case EDITOR_UI_ADD_PLATFORM: AddObject(editor, LEVL_OBJECT_PLATFORM); break;
        case EDITOR_UI_ADD_RAMP: AddObject(editor, LEVL_OBJECT_RAMP); break;
        case EDITOR_UI_ADD_MARKER: AddObject(editor, LEVL_OBJECT_MARKER); break;
        case EDITOR_UI_ADD_EMPTY: AddObject(editor, LEVL_OBJECT_EMPTY); break;
        case EDITOR_UI_IMPORT_MODEL: BeginTextInput(editor, EDITOR_TEXT_IMPORT); break;
        case EDITOR_UI_SELECT_VERTEX: EditorMeshEditSetSelectMode(&editor->meshEdit, EDITOR_MESH_SELECT_VERTEX); SetStatus(editor, "Vertex select mode"); break;
        case EDITOR_UI_SELECT_EDGE: EditorMeshEditSetSelectMode(&editor->meshEdit, EDITOR_MESH_SELECT_EDGE); SetStatus(editor, "Edge select mode"); break;
        case EDITOR_UI_SELECT_FACE: EditorMeshEditSetSelectMode(&editor->meshEdit, EDITOR_MESH_SELECT_FACE); SetStatus(editor, "Face select mode"); break;
        case EDITOR_UI_SELECT_ALL:
            if (object) EditorMeshEditSelectAll(&editor->meshEdit, object);
            SetStatus(editor, "Selected all mesh elements");
            break;
        case EDITOR_UI_CLEAR_SELECTION: EditorMeshEditClear(&editor->meshEdit); SetStatus(editor, "Mesh selection cleared"); break;
        case EDITOR_UI_SUBDIVIDE:
            if (object && EditorMeshEditSubdivideFaces(&editor->meshEdit, object)) {
                editor->document.dirty = true;
                SetStatus(editor, "Selected faces subdivided");
            } else SetStatus(editor, "Select faces before subdivision");
            break;
        case EDITOR_UI_SNAP_MESH:
            if (object && object->type == LEVL_OBJECT_MESH) {
                EditorMeshEditSnapAllVertices(&editor->document, object, editor->gridStep);
                editor->document.dirty = true;
                SetStatus(editor, TextFormat("Snapped all vertices to %.3g grid", editor->gridStep));
            }
            break;
        case EDITOR_UI_TOGGLE_COLLISION: ToggleCollision(editor); break;
        case EDITOR_UI_APPLY_SCALE: ApplySelectedScale(editor); break;
        case EDITOR_UI_FACE_COLOR: CycleFaceColor(editor); break;
        case EDITOR_UI_RENAME: BeginTextInput(editor, EDITOR_TEXT_NAME); break;
        case EDITOR_UI_SET_TAG: BeginTextInput(editor, EDITOR_TEXT_TAG); break;
        case EDITOR_UI_SET_PARENT:
            if (object) {
                editor->parentingMode = true;
                editor->parentingChildIndex = editor->selectedIndex;
                SetStatus(editor, "Select any object in the hierarchy as parent");
            }
            break;
        case EDITOR_UI_CLEAR_PARENT:
            if (object && LevlObjectSetParent(&editor->document, object, NULL)) SetStatus(editor, "Parent cleared");
            break;
        case EDITOR_UI_SELECT_OBJECT:
            if (targetIndex < 0 || targetIndex >= editor->document.objectCount) break;
            if (editor->parentingMode) {
                LevlObject *child = editor->parentingChildIndex >= 0 && editor->parentingChildIndex < editor->document.objectCount ?
                                    &editor->document.objects[editor->parentingChildIndex] : NULL;
                LevlObject *parent = &editor->document.objects[targetIndex];
                if (child && LevlObjectSetParent(&editor->document, child, parent)) {
                    SelectOnly(editor, editor->parentingChildIndex);
                    SetStatus(editor, TextFormat("Parented %s to %s", child->name, parent->name));
                } else SetStatus(editor, "Parent must be a different object");
                editor->parentingMode = false;
            } else {
                if (editor->mode == EDITOR_MODE_EDIT) {
                    if (editor->document.objects[targetIndex].type == LEVL_OBJECT_MESH) {
                        EditorMeshEditBegin(&editor->meshEdit, &editor->document, &editor->document.objects[targetIndex]);
                    } else {
                        ToggleEditMode(editor);
                    }
                }
                SelectOnly(editor, targetIndex);
                SetStatus(editor, TextFormat("Selected %s", editor->document.objects[targetIndex].name));
            }
            break;
        case EDITOR_UI_BOX_BEVEL_UP:
            if (object && object->type == LEVL_OBJECT_BOX) { object->data.box.bevel += 0.05f; editor->document.dirty = true; SetStatus(editor, "Bevel increased"); }
            break;
        case EDITOR_UI_BOX_BEVEL_DOWN:
            if (object && object->type == LEVL_OBJECT_BOX) { object->data.box.bevel = ClampMin(object->data.box.bevel - 0.05f, 0.0f); editor->document.dirty = true; SetStatus(editor, "Bevel decreased"); }
            break;
        case EDITOR_UI_BOX_SUBDIVISION_UP:
            if (object && object->type == LEVL_OBJECT_BOX && object->data.box.subdivisions < 16) { object->data.box.subdivisions++; editor->document.dirty = true; SetStatus(editor, "Subdivision increased"); }
            break;
        case EDITOR_UI_BOX_SUBDIVISION_DOWN:
            if (object && object->type == LEVL_OBJECT_BOX && object->data.box.subdivisions > 1) { object->data.box.subdivisions--; editor->document.dirty = true; SetStatus(editor, "Subdivision decreased"); }
            break;
        case EDITOR_UI_DELETE:
            DeleteSelected(editor);
            break;
        default: break;
    }
}

static void UpdateEditorControls(Editor *editor, float deltaTime)
{
    EditorUIState uiState = MakeUIState(editor);
    int targetIndex = -1;
    EditorUIAction uiAction = EditorUIHandleMouse(&uiState, GetMousePosition(), &targetIndex);
    if (uiAction != EDITOR_UI_NONE) { ApplyUIAction(editor, uiAction, targetIndex); return; }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !EditorUIIsViewportPoint(GetMousePosition())) return;
    if (EditorNavigationUpdate(&editor->navigation, &editor->camera, deltaTime)) return;
    if (EditorNavigationHandleViewShortcuts(&editor->navigation, &editor->camera)) {
        SetStatus(editor, "Orthographic view snapped; orbit to return to perspective");
        return;
    }
    if (UpdateSharedShortcuts(editor)) return;

    if (editor->gizmoDrag.active) { UpdateGizmoDrag(editor); return; }
    LevlObject *object = SelectedObject(editor);
    Vector3 gizmoOrigin = { 0 };
    float gizmoSize = 0.0f;
    bool hasGizmo = EditGizmoOrigin(editor, object, &gizmoOrigin, &gizmoSize);
    editor->gizmoHighlightedAxis = hasGizmo ? PickGizmoAxis(editor, gizmoOrigin, gizmoSize, GetMousePosition()) : -1;
    if (editor->mode == EDITOR_MODE_EDIT) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && editor->gizmoHighlightedAxis >= 0) {
            BeginGizmoDrag(editor, editor->gizmoHighlightedAxis, gizmoOrigin, gizmoSize);
            return;
        }
        UpdateMeshEditControls(editor);
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (editor->gizmoHighlightedAxis >= 0) BeginGizmoDrag(editor, editor->gizmoHighlightedAxis, gizmoOrigin, gizmoSize);
        else SelectAtMouse(editor);
    }
    if (IsKeyPressed(KEY_TAB) && !CommandOrControlHeld()) { ToggleEditMode(editor); return; }

    if (IsKeyPressed(KEY_F1)) {
        EditorSceneUnloadModels(editor->modelCache, LEVL_MAX_OBJECTS);
        memset(editor->modelCache, 0, sizeof(editor->modelCache));
        LevlDocumentNew(&editor->document);
        SelectOnly(editor, 0);
        editor->mode = EDITOR_MODE_OBJECT;
        EditorMeshEditReset(&editor->meshEdit);
        SetStatus(editor, "New starter level created");
    }
    if (IsKeyPressed(KEY_A) && !ShiftHeld()) AddObject(editor, LEVL_OBJECT_BOX);
    if (ShiftHeld() && IsKeyPressed(KEY_A)) AddObject(editor, LEVL_OBJECT_MESH);
    if (IsKeyPressed(KEY_P)) AddObject(editor, LEVL_OBJECT_PLATFORM);
    if (ShiftHeld() && IsKeyPressed(KEY_R)) AddObject(editor, LEVL_OBJECT_RAMP);
    if (IsKeyPressed(KEY_K)) AddObject(editor, LEVL_OBJECT_MARKER);
    if (IsKeyPressed(KEY_I)) BeginTextInput(editor, EDITOR_TEXT_IMPORT);
    if (IsKeyPressed(KEY_F2)) BeginTextInput(editor, EDITOR_TEXT_NAME);
    if (IsKeyPressed(KEY_T)) BeginTextInput(editor, EDITOR_TEXT_TAG);
    if (IsKeyPressed(KEY_H)) editor->showColliders = !editor->showColliders;
    if (IsKeyPressed(KEY_DELETE)) {
        DeleteSelected(editor);
    }

    object = SelectedObject(editor);
    if (!object) return;
    float step = editor->snapEnabled ? editor->gridStep : (ShiftHeld() ? 1.0f : 0.2f);
    Vector3 movement = { 0 };
    if (IsKeyDown(KEY_LEFT)) movement.x -= step;
    if (IsKeyDown(KEY_RIGHT)) movement.x += step;
    if (IsKeyDown(KEY_UP)) movement.z -= step;
    if (IsKeyDown(KEY_DOWN)) movement.z += step;
    if (IsKeyDown(KEY_Q)) movement.y += step;
    if (IsKeyDown(KEY_E)) movement.y -= step;
    if (Vector3Length(movement) > 0.0f) MoveSelected(editor, movement);
    if (IsKeyPressed(KEY_LEFT_BRACKET)) ResizeSelected(editor, editor->snapEnabled ? -editor->gridStep : -0.25f);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) ResizeSelected(editor, editor->snapEnabled ? editor->gridStep : 0.25f);
    if (IsKeyPressed(KEY_COMMA)) { if (object->type == LEVL_OBJECT_BOX) object->data.box.rotation.y -= editor->snapEnabled ? 15.0f : 5.0f; if (object->type == LEVL_OBJECT_MODEL) object->data.model.rotation.y -= editor->snapEnabled ? 15.0f : 5.0f; editor->document.dirty = true; }
    if (IsKeyPressed(KEY_PERIOD)) { if (object->type == LEVL_OBJECT_BOX) object->data.box.rotation.y += editor->snapEnabled ? 15.0f : 5.0f; if (object->type == LEVL_OBJECT_MODEL) object->data.model.rotation.y += editor->snapEnabled ? 15.0f : 5.0f; editor->document.dirty = true; }
    if (object->type == LEVL_OBJECT_BOX) {
        if (IsKeyPressed(KEY_F)) editor->selectedFace = (editor->selectedFace + 1) % 6;
        if (IsKeyPressed(KEY_C)) CycleFaceColor(editor);
        if (IsKeyPressed(KEY_B)) { object->data.box.bevel += 0.05f; editor->document.dirty = true; }
        if (IsKeyPressed(KEY_N)) { object->data.box.bevel = ClampMin(object->data.box.bevel - 0.05f, 0.0f); editor->document.dirty = true; }
        if (IsKeyPressed(KEY_EQUAL)) { if (object->data.box.subdivisions < 16) object->data.box.subdivisions++; editor->document.dirty = true; }
        if (IsKeyPressed(KEY_MINUS)) { if (object->data.box.subdivisions > 1) object->data.box.subdivisions--; editor->document.dirty = true; }
    }
    if (IsKeyPressed(KEY_O)) ToggleCollision(editor);
}

static void DrawTextLine(const char *text, int x, int *y, int fontSize, Color color)
{
    DrawText(text, x, *y, fontSize, color);
    *y += fontSize + 6;
}

static void DrawInspector(const Editor *editor)
{
    int width = GetScreenWidth(), height = GetScreenHeight();
    int panelX = width - 360;
    DrawRectangle(panelX, 0, 360, height, (Color){ 17, 26, 39, 242 });
    DrawRectangle(panelX, 0, 5, height, (Color){ 79, 231, 169, 255 });
    int x = panelX + 20, y = 20;
    DrawTextLine("SLIMEWORKS LEVEL EDITOR", x, &y, 19, (Color){ 124, 255, 187, 255 });
    DrawTextLine(editor->document.dirty ? "Unsaved changes" : "Saved", x, &y, 14, editor->document.dirty ? ORANGE : LIGHTGRAY);
    DrawTextLine(TextFormat("Grid %.3g | snap %s", editor->gridStep, editor->snapEnabled ? "on" : "off"), x, &y, 14, editor->snapEnabled ? LIME : LIGHTGRAY);
    DrawTextLine(TextFormat("Camera: %s", EditorNavigationModeName(&editor->navigation)), x, &y, 14, LIGHTGRAY);
    DrawTextLine(TextFormat("Mode: %s", EditorModeName(editor->mode)), x, &y, 14, editor->mode == EDITOR_MODE_EDIT ? YELLOW : LIGHTGRAY);
    if (editor->playerSpawnBlocked)
        DrawTextLine("ERROR: player_spawn inside collision", x, &y, 14, RED);
    y += 8;
    const LevlObject *object = editor->selectedIndex >= 0 && editor->selectedIndex < editor->document.objectCount ? &editor->document.objects[editor->selectedIndex] : NULL;
    if (object) {
        DrawTextLine(TextFormat("Type: %s", LevlObjectTypeName(object->type)), x, &y, 18, WHITE);
        DrawTextLine(TextFormat("Name: %s", object->name), x, &y, 16, LIGHTGRAY);
        DrawTextLine(TextFormat("Tag: %s", object->tag), x, &y, 16, LIGHTGRAY);
        DrawTextLine(TextFormat("Position: %.1f, %.1f, %.1f", object->position.x, object->position.y, object->position.z), x, &y, 15, LIGHTGRAY);
        if (object->type == LEVL_OBJECT_BOX) {
            DrawTextLine(TextFormat("Face: %d/6 | bevel %.2f | edges %d", editor->selectedFace + 1, object->data.box.bevel, object->data.box.subdivisions), x, &y, 15, LIGHTGRAY);
            DrawTextLine(object->data.box.collidable ? "Collision: generated" : "Collision: disabled", x, &y, 15, object->data.box.collidable ? LIME : ORANGE);
            int swatchX = x;
            for (int face = 0; face < 6; ++face) {
                Color color = object->data.box.faceColors[face];
                DrawRectangle(swatchX, y, 34, 22, color);
                if (face == editor->selectedFace) DrawRectangleLines(swatchX - 2, y - 2, 38, 26, YELLOW);
                swatchX += 42;
            }
            y += 32;
        } else if (object->type == LEVL_OBJECT_MESH) {
            LevlMeshEdge edges[EDITOR_MESH_MAX_EDGES];
            int edgeCount = LevlMeshBuildEdges(&object->data.mesh, edges, EDITOR_MESH_MAX_EDGES);
            DrawTextLine(TextFormat("Mesh: %d vertices | %d edges | %d faces", object->data.mesh.vertexCount, edgeCount, object->data.mesh.faceCount), x, &y, 15, LIGHTGRAY);
            if (editor->mode == EDITOR_MODE_EDIT) {
                DrawTextLine(TextFormat("Select: %s | %d selected", EditorMeshSelectModeName(editor->meshEdit.selectMode),
                                        EditorMeshEditSelectionCount(&editor->meshEdit, object)), x, &y, 15, YELLOW);
            }
            DrawTextLine(object->data.mesh.collidable ? "Collision: generated from mesh polygons" : "Collision: disabled", x, &y, 15,
                         object->data.mesh.collidable ? LIME : ORANGE);
        } else if (object->type == LEVL_OBJECT_MODEL) {
            DrawTextLine(TextFormat("Asset: %s", object->data.model.assetPath), x, &y, 14, LIGHTGRAY);
            DrawTextLine(object->data.model.collidable ? "Collision: generated from model polygons" : "Collision: disabled", x, &y, 15, object->data.model.collidable ? LIME : ORANGE);
        } else if (object->type == LEVL_OBJECT_MARKER) {
            DrawTextLine("Invisible in game; available by tag", x, &y, 15, YELLOW);
        }
    } else DrawTextLine("Nothing selected", x, &y, 18, LIGHTGRAY);

    y += 12;
    DrawTextLine("CREATE", x, &y, 16, (Color){ 124, 255, 187, 255 });
    DrawTextLine("A box | P platform | Shift+R ramp | K marker", x, &y, 14, LIGHTGRAY);
    DrawTextLine("I import OBJ/GLB/GLTF/IQM/VOX/FBX", x, &y, 14, LIGHTGRAY);
    DrawTextLine("EDIT", x, &y, 16, (Color){ 124, 255, 187, 255 });
    DrawTextLine("G/R/S gizmo | drag coloured axis/ring", x, &y, 14, LIGHTGRAY);
    DrawTextLine("Click select | Tab Edit Mode | Ctrl+Tab next object", x, &y, 14, LIGHTGRAY);
    DrawTextLine("[ ] resize | , . rotate | Ctrl+A apply scale", x, &y, 14, LIGHTGRAY);
    DrawTextLine("F2 rename | T tag | Shift+S snap | Shift+,/. grid", x, &y, 14, LIGHTGRAY);
    DrawTextLine("Box: F face, C colour, B/N bevel, +/- edge loops", x, &y, 14, LIGHTGRAY);
    DrawTextLine("Edit: 1 vertex, 2 edge, 3 face | Shift+LMB multi-select", x, &y, 14, LIGHTGRAY);
    DrawTextLine("Edit move: arrows/Q/E | [ ] scale | , . rotate | + subdivide faces", x, &y, 14, LIGHTGRAY);
    DrawTextLine("O collider | H show colliders | Del delete", x, &y, 14, LIGHTGRAY);
    DrawTextLine("SAVE / EXPORT", x, &y, 16, (Color){ 124, 255, 187, 255 });
    DrawTextLine("Ctrl+S save | Ctrl+O load | F6 export to game", x, &y, 14, LIGHTGRAY);
    DrawTextLine(EditorNavigationOrbitHint(&editor->navigation), x, &y, 14, LIGHTGRAY);
    DrawTextLine("Wheel/two-finger scroll zoom | Shift+] free/orbit", x, &y, 14, LIGHTGRAY);
    DrawTextLine(EditorNavigationFreeHint(&editor->navigation), x, &y, 14, LIGHTGRAY);
    DrawTextLine(EditorNavigationViewHint(&editor->navigation), x, &y, 14, LIGHTGRAY);
    DrawTextLine("Orthographic grid faces camera; orbit returns perspective", x, &y, 14, LIGHTGRAY);
}

static void DrawOverlay(const Editor *editor)
{
    DrawRectangle(0, 0, GetScreenWidth() - 360, 42, (Color){ 10, 16, 25, 215 });
    DrawText("Level workspace — F6 exports directly into Slimeworks Factory", 16, 12, 17, RAYWHITE);
    if (editor->statusSeconds > 0.0f) DrawText(editor->status, 16, 54, 18, YELLOW);
    if (editor->textMode != EDITOR_TEXT_NONE) {
        int width = GetScreenWidth() - 360;
        DrawRectangle(28, GetScreenHeight() - 92, width - 56, 58, (Color){ 11, 18, 28, 245 });
        DrawRectangleLines(28, GetScreenHeight() - 92, width - 56, 58, (Color){ 124, 255, 187, 255 });
        const char *prompt = editor->textMode == EDITOR_TEXT_IMPORT ? "Model path" : (editor->textMode == EDITOR_TEXT_NAME ? "Object name" : "Object tag");
        DrawText(TextFormat("%s: %s_", prompt, editor->textBuffer), 44, GetScreenHeight() - 72, 20, WHITE);
    }
    DrawInspector(editor);
    EditorUIState state = MakeUIState(editor);
    EditorUIDraw(&state);
}

void EditorInit(Editor *editor)
{
    *editor = (Editor){ .selectedIndex = 0, .mode = EDITOR_MODE_OBJECT, .showColliders = true,
                        .gizmoMode = EDITOR_GIZMO_MOVE, .gizmoHighlightedAxis = -1, .snapEnabled = true, .gridStep = 1.0f };
    InitWindow(1280, 720, "Slimeworks Level Editor");
    SetTargetFPS(60);
    editor->camera = (Camera3D){
        .position = { 24.0f, 19.0f, 24.0f }, .target = { 0.0f, 0.0f, 0.0f },
        .up = { 0.0f, 1.0f, 0.0f }, .fovy = 50.0f, .projection = CAMERA_PERSPECTIVE
    };
    EditorNavigationInit(&editor->navigation, &editor->camera);
    EditorMeshEditReset(&editor->meshEdit);
    LevlDocumentNew(&editor->document);
    SelectOnly(editor, 0);
    SetStatus(editor, "Starter level ready — F6 exports it to the game");
}

void EditorRun(Editor *editor)
{
    while (!WindowShouldClose()) {
        float deltaTime = fminf(GetFrameTime(), 0.05f);
        if (editor->statusSeconds > 0.0f) editor->statusSeconds -= deltaTime;
        if (editor->textMode != EDITOR_TEXT_NONE) UpdateTextInput(editor);
        else UpdateEditorControls(editor, deltaTime);
        UpdateSpawnValidation(editor);

        BeginDrawing();
        ClearBackground((Color){ 36, 62, 83, 255 });
        BeginMode3D(editor->camera);
        EditorSceneDraw(&editor->document, editor->selectedIndex, editor->selected, editor->modelCache, LEVL_MAX_OBJECTS,
                        editor->showColliders, editor->playerSpawnBlocked, &editor->camera,
                        EditorNavigationIsOrthographic(&editor->navigation), editor->gridStep);
        LevlObject *selected = SelectedObject(editor);
        if (editor->mode == EDITOR_MODE_EDIT && selected) {
            EditorMeshEditDraw(&editor->meshEdit, selected);
            Vector3 gizmoOrigin = { 0 };
            float gizmoSize = 0.0f;
            if (EditGizmoOrigin(editor, selected, &gizmoOrigin, &gizmoSize))
                EditorSceneDrawGizmoAt(gizmoOrigin, gizmoSize, editor->gizmoMode, editor->gizmoHighlightedAxis);
        } else if (selected) EditorSceneDrawGizmo(&editor->document, selected, editor->gizmoMode, editor->gizmoHighlightedAxis);
        EndMode3D();
        DrawOverlay(editor);
        EndDrawing();
    }
}

void EditorShutdown(Editor *editor)
{
    EditorSceneUnloadModels(editor->modelCache, LEVL_MAX_OBJECTS);
    CloseWindow();
}
