#include "editor.h"

#include "../import/model_import.h"

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

static LevlObject *SelectedObject(Editor *editor)
{
    if (editor->selectedIndex < 0 || editor->selectedIndex >= editor->document.objectCount) return NULL;
    return &editor->document.objects[editor->selectedIndex];
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
    editor->selectedIndex = (editor->selectedIndex + 1) % editor->document.objectCount;
    SetStatus(editor, "Selected next object");
}

static Vector3 DefaultCreatePosition(const Editor *editor)
{
    Vector3 direction = Vector3Normalize(Vector3Subtract(editor->camera.target, editor->camera.position));
    Vector3 position = Vector3Add(editor->camera.position, Vector3Scale(direction, 8.0f));
    position.x = roundf(position.x * 2.0f) * 0.5f;
    position.y = 0.0f;
    position.z = roundf(position.z * 2.0f) * 0.5f;
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
    editor->selectedIndex = editor->document.objectCount - 1;
    SetStatus(editor, TextFormat("Added %s", LevlObjectTypeName(type)));
}

static void SelectAtMouse(Editor *editor)
{
    if (GetMousePosition().x >= GetScreenWidth() - 360) return;
    Ray ray = GetScreenToWorldRay(GetMousePosition(), editor->camera);
    float nearest = FLT_MAX;
    int found = -1;
    for (int i = 0; i < editor->document.objectCount; ++i) {
        RayCollision hit = GetRayCollisionBox(ray, LevlObjectGetBounds(&editor->document.objects[i]));
        if (hit.hit && hit.distance < nearest) { nearest = hit.distance; found = i; }
    }
    editor->selectedIndex = found;
    if (found >= 0) SetStatus(editor, TextFormat("Selected %s", editor->document.objects[found].name));
}

static void MoveSelected(Editor *editor, Vector3 delta)
{
    LevlObject *object = SelectedObject(editor);
    if (!object) return;
    object->position = Vector3Add(object->position, delta);
    if (object->type == LEVL_OBJECT_RAMP) object->data.ramp.end = Vector3Add(object->data.ramp.end, delta);
    if (object->type == LEVL_OBJECT_PLATFORM && fabsf(delta.y) > 0.001f) {
        object->data.platform.top += delta.y;
        object->position.y = 0.0f;
    }
    editor->document.dirty = true;
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
            if (editor->document.objectCount > 0) editor->selectedIndex = editor->document.objectCount - 1;
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
    else if (object->type == LEVL_OBJECT_MODEL) object->data.model.collidable = !object->data.model.collidable;
    else { SetStatus(editor, "Only boxes and models generate solid colliders"); return; }
    editor->document.dirty = true;
    SetStatus(editor, "Collider toggled");
}

static void UpdateEditorControls(Editor *editor)
{
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) UpdateCamera(&editor->camera, CAMERA_FREE);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) SelectAtMouse(editor);
    if (IsKeyPressed(KEY_TAB)) SelectNext(editor);

    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        if (IsKeyPressed(KEY_S)) {
            char result[192]; LevlDocumentSave(&editor->document, "level.levl", result, (int)sizeof(result)); SetStatus(editor, result);
        }
        if (IsKeyPressed(KEY_O)) {
            char result[192];
            if (LevlDocumentLoad(&editor->document, "level.levl", result, (int)sizeof(result))) {
                EditorSceneUnloadModels(editor->modelCache, LEVL_MAX_OBJECTS);
                memset(editor->modelCache, 0, sizeof(editor->modelCache));
                editor->selectedIndex = editor->document.objectCount ? 0 : -1;
            }
            SetStatus(editor, result);
        }
        return;
    }

    if (IsKeyPressed(KEY_F1)) {
        EditorSceneUnloadModels(editor->modelCache, LEVL_MAX_OBJECTS);
        memset(editor->modelCache, 0, sizeof(editor->modelCache));
        LevlDocumentNew(&editor->document);
        editor->selectedIndex = 0;
        SetStatus(editor, "New starter level created");
    }
    if (IsKeyPressed(KEY_F6)) {
        char result[192];
        LevlDocumentExportToGame(&editor->document, "../Slimeworks Factory/levels/factory.levl", result, (int)sizeof(result));
        SetStatus(editor, result);
    }
    if (IsKeyPressed(KEY_A)) AddObject(editor, LEVL_OBJECT_BOX);
    if (IsKeyPressed(KEY_P)) AddObject(editor, LEVL_OBJECT_PLATFORM);
    if (IsKeyPressed(KEY_R)) AddObject(editor, LEVL_OBJECT_RAMP);
    if (IsKeyPressed(KEY_K)) AddObject(editor, LEVL_OBJECT_MARKER);
    if (IsKeyPressed(KEY_I)) BeginTextInput(editor, EDITOR_TEXT_IMPORT);
    if (IsKeyPressed(KEY_F2)) BeginTextInput(editor, EDITOR_TEXT_NAME);
    if (IsKeyPressed(KEY_T)) BeginTextInput(editor, EDITOR_TEXT_TAG);
    if (IsKeyPressed(KEY_H)) editor->showColliders = !editor->showColliders;
    if (IsKeyPressed(KEY_DELETE)) {
        if (LevlDocumentRemove(&editor->document, editor->selectedIndex)) {
            if (editor->selectedIndex >= editor->document.objectCount) editor->selectedIndex = editor->document.objectCount - 1;
            SetStatus(editor, "Object removed");
        }
    }

    LevlObject *object = SelectedObject(editor);
    if (!object) return;
    float step = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? 1.0f : 0.2f;
    Vector3 movement = { 0 };
    if (IsKeyDown(KEY_LEFT)) movement.x -= step;
    if (IsKeyDown(KEY_RIGHT)) movement.x += step;
    if (IsKeyDown(KEY_UP)) movement.z -= step;
    if (IsKeyDown(KEY_DOWN)) movement.z += step;
    if (IsKeyDown(KEY_Q)) movement.y += step;
    if (IsKeyDown(KEY_E)) movement.y -= step;
    if (Vector3Length(movement) > 0.0f) MoveSelected(editor, movement);
    if (IsKeyPressed(KEY_LEFT_BRACKET)) ResizeSelected(editor, -0.25f);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) ResizeSelected(editor, 0.25f);
    if (IsKeyPressed(KEY_COMMA)) { if (object->type == LEVL_OBJECT_BOX) object->data.box.rotation.y -= 15.0f; if (object->type == LEVL_OBJECT_MODEL) object->data.model.rotation.y -= 15.0f; editor->document.dirty = true; }
    if (IsKeyPressed(KEY_PERIOD)) { if (object->type == LEVL_OBJECT_BOX) object->data.box.rotation.y += 15.0f; if (object->type == LEVL_OBJECT_MODEL) object->data.model.rotation.y += 15.0f; editor->document.dirty = true; }
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
        } else if (object->type == LEVL_OBJECT_MODEL) {
            DrawTextLine(TextFormat("Asset: %s", object->data.model.assetPath), x, &y, 14, LIGHTGRAY);
            DrawTextLine(object->data.model.collidable ? "Collision: generated from bounds" : "Collision: disabled", x, &y, 15, object->data.model.collidable ? LIME : ORANGE);
        } else if (object->type == LEVL_OBJECT_MARKER) {
            DrawTextLine("Invisible in game; available by tag", x, &y, 15, YELLOW);
        }
    } else DrawTextLine("Nothing selected", x, &y, 18, LIGHTGRAY);

    y += 12;
    DrawTextLine("CREATE", x, &y, 16, (Color){ 124, 255, 187, 255 });
    DrawTextLine("A box | P platform | R ramp | K marker", x, &y, 14, LIGHTGRAY);
    DrawTextLine("I import OBJ/GLB/GLTF/IQM/VOX/FBX", x, &y, 14, LIGHTGRAY);
    DrawTextLine("EDIT", x, &y, 16, (Color){ 124, 255, 187, 255 });
    DrawTextLine("Click/Tab select | arrows move | Q/E vertical", x, &y, 14, LIGHTGRAY);
    DrawTextLine("[ ] resize | , . rotate | F2 rename | T tag", x, &y, 14, LIGHTGRAY);
    DrawTextLine("Box: F face, C colour, B/N bevel, +/- edge loops", x, &y, 14, LIGHTGRAY);
    DrawTextLine("O collider | H show colliders | Del delete", x, &y, 14, LIGHTGRAY);
    DrawTextLine("SAVE / EXPORT", x, &y, 16, (Color){ 124, 255, 187, 255 });
    DrawTextLine("Ctrl+S save | Ctrl+O load | F6 export to game", x, &y, 14, LIGHTGRAY);
    DrawTextLine("Hold right mouse + WASD to fly camera", x, &y, 14, LIGHTGRAY);
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
}

void EditorInit(Editor *editor)
{
    *editor = (Editor){ .selectedIndex = 0, .showColliders = true };
    InitWindow(1440, 900, "Slimeworks Level Editor");
    SetTargetFPS(60);
    editor->camera = (Camera3D){
        .position = { 24.0f, 19.0f, 24.0f }, .target = { 0.0f, 0.0f, 0.0f },
        .up = { 0.0f, 1.0f, 0.0f }, .fovy = 50.0f, .projection = CAMERA_PERSPECTIVE
    };
    LevlDocumentNew(&editor->document);
    SetStatus(editor, "Starter level ready — F6 exports it to the game");
}

void EditorRun(Editor *editor)
{
    while (!WindowShouldClose()) {
        float deltaTime = fminf(GetFrameTime(), 0.05f);
        if (editor->statusSeconds > 0.0f) editor->statusSeconds -= deltaTime;
        if (editor->textMode != EDITOR_TEXT_NONE) UpdateTextInput(editor);
        else UpdateEditorControls(editor);

        BeginDrawing();
        ClearBackground((Color){ 36, 62, 83, 255 });
        BeginMode3D(editor->camera);
        EditorSceneDraw(&editor->document, editor->selectedIndex, editor->modelCache, LEVL_MAX_OBJECTS, editor->showColliders);
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
