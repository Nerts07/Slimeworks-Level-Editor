#include "editor_ui.h"

#include <stdio.h>

#define EDITOR_MODE_OBJECT 0
#define EDITOR_MODE_EDIT 1

static const Color UI_BACKGROUND = { 13, 19, 29, 248 };
static const Color UI_PANEL = { 20, 29, 42, 248 };
static const Color UI_PANEL_ALT = { 25, 36, 51, 250 };
static const Color UI_BORDER = { 55, 75, 96, 255 };
static const Color UI_TEXT = { 225, 236, 244, 255 };
static const Color UI_MUTED = { 137, 158, 174, 255 };
static const Color UI_ACCENT = { 104, 224, 176, 255 };
static const Color UI_WARNING = { 255, 188, 86, 255 };
static const Color UI_DANGER = { 255, 103, 107, 255 };

typedef struct UIButton {
    Rectangle bounds;
    const char *label;
    EditorUIAction action;
    bool active;
    bool enabled;
} UIButton;

static Rectangle RightPanel(void)
{
    return (Rectangle){ GetScreenWidth() - EDITOR_UI_RIGHT_WIDTH, 0, EDITOR_UI_RIGHT_WIDTH, GetScreenHeight() };
}

bool EditorUIIsViewportPoint(Vector2 point)
{
    return point.x >= EDITOR_UI_LEFT_WIDTH && point.x < GetScreenWidth() - EDITOR_UI_RIGHT_WIDTH &&
           point.y >= EDITOR_UI_TOP_HEIGHT && point.y < GetScreenHeight() - EDITOR_UI_BOTTOM_HEIGHT;
}

static bool Contains(Rectangle rectangle, Vector2 point)
{
    return CheckCollisionPointRec(point, rectangle);
}

static UIButton Button(float x, float y, float width, float height, const char *label, EditorUIAction action, bool active, bool enabled)
{
    return (UIButton){ .bounds = { x, y, width, height }, .label = label, .action = action, .active = active, .enabled = enabled };
}

static void DrawButton(UIButton button)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = button.enabled && Contains(button.bounds, mouse);
    Color fill = !button.enabled ? (Color){ 19, 26, 36, 255 } :
                 button.active ? (Color){ 42, 104, 88, 255 } :
                 hovered ? (Color){ 40, 58, 76, 255 } : UI_PANEL_ALT;
    Color border = button.active ? UI_ACCENT : (hovered ? (Color){ 119, 160, 177, 255 } : UI_BORDER);
    DrawRectangleRounded(button.bounds, 0.18f, 5, fill);
    DrawRectangleRoundedLines(button.bounds, 0.18f, 5, border);
    int fontSize = button.bounds.height >= 34.0f ? 14 : 12;
    int textWidth = MeasureText(button.label, fontSize);
    Color text = !button.enabled ? (Color){ 73, 88, 101, 255 } : button.active ? UI_ACCENT : UI_TEXT;
    DrawText(button.label, (int)(button.bounds.x + (button.bounds.width - textWidth) * 0.5f),
             (int)(button.bounds.y + (button.bounds.height - fontSize) * 0.5f), fontSize, text);
}

static EditorUIAction HitButtons(const UIButton *buttons, int count, Vector2 mouse)
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return EDITOR_UI_NONE;
    for (int i = 0; i < count; ++i) if (buttons[i].enabled && Contains(buttons[i].bounds, mouse)) return buttons[i].action;
    return EDITOR_UI_NONE;
}

static void Label(const char *text, int x, int y, int size, Color color)
{
    DrawText(text, x, y, size, color);
}

static void DrawTopBar(const EditorUIState *state)
{
    DrawRectangle(0, 0, GetScreenWidth(), EDITOR_UI_TOP_HEIGHT, UI_BACKGROUND);
    DrawRectangle(0, EDITOR_UI_TOP_HEIGHT - 2, GetScreenWidth(), 2, UI_ACCENT);
    Label("SW", 18, 12, 24, UI_ACCENT);
    Label("LEVEL WORKBENCH", 54, 11, 15, UI_TEXT);
    Label("3D authoring", 54, 31, 11, UI_MUTED);

    UIButton modeButtons[] = {
        Button(210, 10, 94, 36, "OBJECT", EDITOR_UI_SET_OBJECT_MODE, state->mode == EDITOR_MODE_OBJECT, true),
        Button(310, 10, 94, 36, "EDIT", EDITOR_UI_SET_EDIT_MODE, state->mode == EDITOR_MODE_EDIT, state->selectedObject != NULL)
    };
    for (int i = 0; i < 2; ++i) DrawButton(modeButtons[i]);

    UIButton tools[] = {
        Button(430, 10, 38, 36, "G", EDITOR_UI_SET_MOVE, state->gizmoMode == EDITOR_GIZMO_MOVE, true),
        Button(472, 10, 38, 36, "R", EDITOR_UI_SET_ROTATE, state->gizmoMode == EDITOR_GIZMO_ROTATE, true),
        Button(514, 10, 38, 36, "S", EDITOR_UI_SET_SCALE, state->gizmoMode == EDITOR_GIZMO_SCALE, true),
        Button(568, 10, 82, 36, state->snapEnabled ? "SNAP ON" : "SNAP OFF", EDITOR_UI_TOGGLE_SNAP, state->snapEnabled, true),
        Button(658, 10, 94, 36, state->showColliders ? "COLLIDERS" : "WIREFRAME", EDITOR_UI_TOGGLE_COLLIDERS, state->showColliders, true),
        Button(758, 10, 86, 36, state->cameraMode, EDITOR_UI_TOGGLE_CAMERA, false, true),
        Button(850, 10, 68, 36, "COPY", EDITOR_UI_COPY, false, state->selectedObject != NULL),
        Button(922, 10, 68, 36, "PASTE", EDITOR_UI_PASTE, false, true),
        Button(GetScreenWidth() - 246, 10, 70, 36, "NEW", EDITOR_UI_NEW, false, true),
        Button(GetScreenWidth() - 170, 10, 70, 36, "SAVE", EDITOR_UI_SAVE, false, true),
        Button(GetScreenWidth() - 94, 10, 78, 36, "EXPORT", EDITOR_UI_EXPORT, false, true)
    };
    for (int i = 0; i < (int)(sizeof(tools) / sizeof(tools[0])); ++i) DrawButton(tools[i]);
    if (state->dirty) DrawCircle(GetScreenWidth() - 180, 7, 4, UI_WARNING);
}

static void DrawLeftToolbar(const EditorUIState *state)
{
    DrawRectangle(0, EDITOR_UI_TOP_HEIGHT, EDITOR_UI_LEFT_WIDTH, GetScreenHeight() - EDITOR_UI_TOP_HEIGHT - EDITOR_UI_BOTTOM_HEIGHT, UI_PANEL);
    DrawRectangle(EDITOR_UI_LEFT_WIDTH - 1, EDITOR_UI_TOP_HEIGHT, 1, GetScreenHeight(), UI_BORDER);
    Label("TOOLS", 17, 74, 11, UI_MUTED);
    UIButton buttons[16];
    int count = 0;
    buttons[count++] = Button(12, 92, 80, 32, "BOX", EDITOR_UI_ADD_BOX, false, state->mode == EDITOR_MODE_OBJECT);
    buttons[count++] = Button(12, 130, 80, 32, "MESH", EDITOR_UI_ADD_MESH, false, state->mode == EDITOR_MODE_OBJECT);
    buttons[count++] = Button(12, 168, 80, 32, "PLATFORM", EDITOR_UI_ADD_PLATFORM, false, state->mode == EDITOR_MODE_OBJECT);
    buttons[count++] = Button(12, 206, 80, 32, "RAMP", EDITOR_UI_ADD_RAMP, false, state->mode == EDITOR_MODE_OBJECT);
    buttons[count++] = Button(12, 244, 80, 32, "MARKER", EDITOR_UI_ADD_MARKER, false, state->mode == EDITOR_MODE_OBJECT);
    buttons[count++] = Button(12, 282, 80, 32, "IMPORT", EDITOR_UI_IMPORT_MODEL, false, state->mode == EDITOR_MODE_OBJECT);
    buttons[count++] = Button(12, 320, 80, 32, "EMPTY", EDITOR_UI_ADD_EMPTY, false, state->mode == EDITOR_MODE_OBJECT);
    if (state->mode == EDITOR_MODE_EDIT) {
        Label("SELECT", 17, 340, 11, UI_MUTED);
        buttons[count++] = Button(12, 358, 80, 32, "VERTEX", EDITOR_UI_SELECT_VERTEX, state->meshSelectMode == EDITOR_MESH_SELECT_VERTEX, true);
        buttons[count++] = Button(12, 396, 80, 32, "EDGE", EDITOR_UI_SELECT_EDGE, state->meshSelectMode == EDITOR_MESH_SELECT_EDGE, true);
        buttons[count++] = Button(12, 434, 80, 32, "FACE", EDITOR_UI_SELECT_FACE, state->meshSelectMode == EDITOR_MESH_SELECT_FACE, true);
        buttons[count++] = Button(12, 480, 38, 30, "ALL", EDITOR_UI_SELECT_ALL, false, true);
        buttons[count++] = Button(54, 480, 38, 30, "CLR", EDITOR_UI_CLEAR_SELECTION, false, true);
        buttons[count++] = Button(12, 518, 80, 32, "SUBDIV +", EDITOR_UI_SUBDIVIDE, false, state->meshSelectMode == EDITOR_MESH_SELECT_FACE);
    }
    for (int i = 0; i < count; ++i) DrawButton(buttons[i]);
}

static void DrawInspectorPanel(const EditorUIState *state)
{
    Rectangle panel = RightPanel();
    DrawRectangleRec(panel, UI_PANEL);
    DrawRectangle(panel.x, EDITOR_UI_TOP_HEIGHT, 3, panel.height - EDITOR_UI_TOP_HEIGHT, UI_ACCENT);
    Label("INSPECTOR", (int)panel.x + 18, 76, 12, UI_MUTED);
    if (!state->selectedObject) {
        Label("Nothing selected", (int)panel.x + 18, 112, 20, UI_TEXT);
        Label("Select an object in the viewport", (int)panel.x + 18, 142, 13, UI_MUTED);
        Label("or create one from the toolbar", (int)panel.x + 18, 162, 13, UI_MUTED);
        return;
    }
    const LevlObject *object = state->selectedObject;
    Label(object->name, (int)panel.x + 18, 106, 20, UI_TEXT);
    Label(TextFormat("%s  ·  #%d", LevlObjectTypeName(object->type), object->id), (int)panel.x + 18, 132, 13, UI_ACCENT);
    Label(object->parentId > 0 ? TextFormat("Parent object  #%d", object->parentId) : "Parent object  —", (int)panel.x + 190, 132, 12, UI_MUTED);
    DrawLine((int)panel.x + 18, 156, (int)panel.x + panel.width - 18, 156, UI_BORDER);
    Label("TRANSFORM", (int)panel.x + 18, 174, 11, UI_MUTED);
    Label(TextFormat("Position  %.2f   %.2f   %.2f", object->position.x, object->position.y, object->position.z), (int)panel.x + 18, 195, 13, UI_TEXT);
    Label(TextFormat("Mode  %s   ·   Grid %.3g", state->cameraMode, state->gridStep), (int)panel.x + 18, 216, 13, UI_MUTED);

    UIButton buttons[14];
    int count = 0;
    int y = 246;
    if (object->type == LEVL_OBJECT_MESH) {
        buttons[count++] = Button(panel.x + 18, y, 156, 34, state->mode == EDITOR_MODE_EDIT ? "EXIT EDIT MODE" : "EDIT MESH", EDITOR_UI_TOGGLE_EDIT, state->mode == EDITOR_MODE_EDIT, true);
        buttons[count++] = Button(panel.x + 184, y, 140, 34, "TOGGLE COLLISION", EDITOR_UI_TOGGLE_COLLISION, object->data.mesh.collidable, true);
        y += 44;
        Label(TextFormat("%d vertices  ·  %d faces", object->data.mesh.vertexCount, object->data.mesh.faceCount), (int)panel.x + 18, y, 13, UI_MUTED);
        y += 24;
    } else if (object->type == LEVL_OBJECT_MODEL) {
        buttons[count++] = Button(panel.x + 18, y, 156, 34, "APPLY SCALE", EDITOR_UI_APPLY_SCALE, false, true);
        buttons[count++] = Button(panel.x + 184, y, 140, 34, "TOGGLE COLLISION", EDITOR_UI_TOGGLE_COLLISION, object->data.model.collidable, true);
        y += 44;
        Label(object->data.model.assetPath, (int)panel.x + 18, y, 12, UI_MUTED);
        y += 24;
    } else if (object->type == LEVL_OBJECT_BOX) {
        buttons[count++] = Button(panel.x + 18, y, 156, 34, "CONVERT TO MESH", EDITOR_UI_TOGGLE_EDIT, false, true);
        buttons[count++] = Button(panel.x + 184, y, 140, 34, "TOGGLE COLLISION", EDITOR_UI_TOGGLE_COLLISION, object->data.box.collidable, true);
        y += 44;
        buttons[count++] = Button(panel.x + 18, y, 74, 30, "BEVEL -", EDITOR_UI_BOX_BEVEL_DOWN, false, true);
        buttons[count++] = Button(panel.x + 98, y, 74, 30, "BEVEL +", EDITOR_UI_BOX_BEVEL_UP, false, true);
        buttons[count++] = Button(panel.x + 184, y, 64, 30, "SUB -", EDITOR_UI_BOX_SUBDIVISION_DOWN, false, true);
        buttons[count++] = Button(panel.x + 254, y, 70, 30, "SUB +", EDITOR_UI_BOX_SUBDIVISION_UP, false, true);
        y += 40;
        Label(TextFormat("Bevel %.2f  ·  subdivisions %d", object->data.box.bevel, object->data.box.subdivisions), (int)panel.x + 18, y, 13, UI_MUTED);
        y += 24;
        buttons[count++] = Button(panel.x + 18, y, 112, 30, "FACE COLOR", EDITOR_UI_FACE_COLOR, false, true);
        y += 40;
    }
    buttons[count++] = Button(panel.x + 18, y, 96, 30, "RENAME", EDITOR_UI_RENAME, false, true);
    buttons[count++] = Button(panel.x + 122, y, 96, 30, "SET TAG", EDITOR_UI_SET_TAG, false, true);
    buttons[count++] = Button(panel.x + 226, y, 98, 30, "DELETE", EDITOR_UI_DELETE, false, true);
    for (int i = 0; i < count; ++i) DrawButton(buttons[i]);
    y += 56;
    DrawButton(Button(panel.x + 18, y, 146, 30, object->parentId > 0 ? "CLEAR PARENT" : "SET PARENT",
                      object->parentId > 0 ? EDITOR_UI_CLEAR_PARENT : EDITOR_UI_SET_PARENT, false, true));
    if (state->parentingMode) Label("Choose an Empty in hierarchy", (int)panel.x + 178, y + 9, 12, UI_WARNING);
    y += 44;
    Label("HIERARCHY", (int)panel.x + 18, y, 11, UI_MUTED);
    if (state->document) {
        int rowY = y + 22;
        for (int index = 0; index < state->document->objectCount && rowY < GetScreenHeight() - 48; ++index) {
            const LevlObject *entry = &state->document->objects[index];
            int depth = 0;
            int parentId = entry->parentId;
            while (parentId > 0 && depth < 8) {
                bool found = false;
                for (int candidate = 0; candidate < state->document->objectCount; ++candidate) {
                    if (state->document->objects[candidate].id == parentId) {
                        parentId = state->document->objects[candidate].parentId;
                        found = true;
                        break;
                    }
                }
                if (!found) break;
                depth++;
            }
            Color rowColor = entry == object ? UI_ACCENT : (entry->type == LEVL_OBJECT_EMPTY ? UI_WARNING : UI_TEXT);
            Label(TextFormat("%s%s", depth > 0 ? "  └ " : "• ", entry->name), (int)panel.x + 18 + depth * 12, rowY, 12, rowColor);
            rowY += 20;
        }
    }
    if (state->playerSpawnBlocked) {
        DrawRectangleRounded((Rectangle){ panel.x + 18, y, panel.width - 36, 58 }, 0.12f, 4, (Color){ 87, 34, 43, 255 });
        Label("PLAYER SPAWN BLOCKED", (int)panel.x + 30, y + 11, 13, UI_DANGER);
        Label("Move player_spawn outside collision", (int)panel.x + 30, y + 33, 12, UI_TEXT);
    }
}

static void DrawBottomBar(const EditorUIState *state)
{
    int y = GetScreenHeight() - EDITOR_UI_BOTTOM_HEIGHT;
    DrawRectangle(0, y, GetScreenWidth(), EDITOR_UI_BOTTOM_HEIGHT, UI_BACKGROUND);
    DrawLine(0, y, GetScreenWidth(), y, UI_BORDER);
    Color statusColor = state->playerSpawnBlocked ? UI_DANGER : (state->dirty ? UI_WARNING : UI_ACCENT);
    DrawCircle(16, y + 17, 5, statusColor);
    Label(state->statusSeconds > 0.0f ? state->status : "Ready", 30, y + 9, 13, UI_TEXT);
    Label("LMB select  ·  Shift+LMB multi-select  ·  Shift+RMB pan  ·  Wheel zoom", 420, y + 9, 12, UI_MUTED);
    Label(state->mode == EDITOR_MODE_EDIT ? "TAB Object Mode" : "TAB Edit Mode", GetScreenWidth() - 150, y + 9, 12, UI_ACCENT);
}

void EditorUIDraw(const EditorUIState *state)
{
    if (!state) return;
    DrawTopBar(state);
    DrawLeftToolbar(state);
    DrawInspectorPanel(state);
    DrawBottomBar(state);
}

EditorUIAction EditorUIHandleMouse(const EditorUIState *state, Vector2 mouse, int *targetIndex)
{
    if (!state || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return EDITOR_UI_NONE;
    Rectangle panel = RightPanel();
    if (mouse.x >= panel.x) {
        UIButton buttons[14];
        int count = 0;
        const LevlObject *object = state->selectedObject;
        int y = 246;
        if (object && object->type == LEVL_OBJECT_MESH) {
            buttons[count++] = Button(panel.x + 18, y, 156, 34, "", EDITOR_UI_TOGGLE_EDIT, false, true);
            buttons[count++] = Button(panel.x + 184, y, 140, 34, "", EDITOR_UI_TOGGLE_COLLISION, false, true);
            y += 44 + 24;
        } else if (object && object->type == LEVL_OBJECT_MODEL) {
            buttons[count++] = Button(panel.x + 18, y, 156, 34, "", EDITOR_UI_APPLY_SCALE, false, true);
            buttons[count++] = Button(panel.x + 184, y, 140, 34, "", EDITOR_UI_TOGGLE_COLLISION, false, true);
            y += 44 + 24;
        } else if (object && object->type == LEVL_OBJECT_BOX) {
            buttons[count++] = Button(panel.x + 18, y, 156, 34, "", EDITOR_UI_TOGGLE_EDIT, false, true);
            buttons[count++] = Button(panel.x + 184, y, 140, 34, "", EDITOR_UI_TOGGLE_COLLISION, false, true);
            y += 44;
            buttons[count++] = Button(panel.x + 18, y, 74, 30, "", EDITOR_UI_BOX_BEVEL_DOWN, false, true);
            buttons[count++] = Button(panel.x + 98, y, 74, 30, "", EDITOR_UI_BOX_BEVEL_UP, false, true);
            buttons[count++] = Button(panel.x + 184, y, 64, 30, "", EDITOR_UI_BOX_SUBDIVISION_DOWN, false, true);
            buttons[count++] = Button(panel.x + 254, y, 70, 30, "", EDITOR_UI_BOX_SUBDIVISION_UP, false, true);
            y += 40;
            y += 24;
            buttons[count++] = Button(panel.x + 18, y, 112, 30, "", EDITOR_UI_FACE_COLOR, false, true);
            y += 40;
        }
        buttons[count++] = Button(panel.x + 18, y, 96, 30, "", EDITOR_UI_RENAME, false, object != NULL);
        buttons[count++] = Button(panel.x + 122, y, 96, 30, "", EDITOR_UI_SET_TAG, false, object != NULL);
        buttons[count++] = Button(panel.x + 226, y, 98, 30, "", EDITOR_UI_DELETE, false, object != NULL);
        EditorUIAction action = HitButtons(buttons, count, mouse);
        if (action != EDITOR_UI_NONE) return action;
        y += 56;
        UIButton parentButton = Button(panel.x + 18, y, 146, 30, "", object && object->parentId > 0 ? EDITOR_UI_CLEAR_PARENT : EDITOR_UI_SET_PARENT, false, object != NULL);
        action = HitButtons(&parentButton, 1, mouse);
        if (action != EDITOR_UI_NONE) return action;
        if (state->document && mouse.y >= y + 44) {
            int rowY = y + 66;
            for (int index = 0; index < state->document->objectCount; ++index, rowY += 20) {
                if (rowY > GetScreenHeight() - 48) break;
                if (mouse.y >= rowY - 3 && mouse.y <= rowY + 16) {
                    if (targetIndex) *targetIndex = index;
                    return EDITOR_UI_SELECT_OBJECT;
                }
            }
        }
        return EDITOR_UI_NONE;
    }
    if (mouse.x < EDITOR_UI_LEFT_WIDTH && mouse.y >= EDITOR_UI_TOP_HEIGHT) {
        UIButton buttons[16];
        int count = 0;
        buttons[count++] = Button(12, 92, 80, 32, "", EDITOR_UI_ADD_BOX, false, state->mode == EDITOR_MODE_OBJECT);
        buttons[count++] = Button(12, 130, 80, 32, "", EDITOR_UI_ADD_MESH, false, state->mode == EDITOR_MODE_OBJECT);
        buttons[count++] = Button(12, 168, 80, 32, "", EDITOR_UI_ADD_PLATFORM, false, state->mode == EDITOR_MODE_OBJECT);
        buttons[count++] = Button(12, 206, 80, 32, "", EDITOR_UI_ADD_RAMP, false, state->mode == EDITOR_MODE_OBJECT);
        buttons[count++] = Button(12, 244, 80, 32, "", EDITOR_UI_ADD_MARKER, false, state->mode == EDITOR_MODE_OBJECT);
        buttons[count++] = Button(12, 282, 80, 32, "", EDITOR_UI_IMPORT_MODEL, false, state->mode == EDITOR_MODE_OBJECT);
        buttons[count++] = Button(12, 320, 80, 32, "", EDITOR_UI_ADD_EMPTY, false, state->mode == EDITOR_MODE_OBJECT);
        if (state->mode == EDITOR_MODE_EDIT) {
            buttons[count++] = Button(12, 358, 80, 32, "", EDITOR_UI_SELECT_VERTEX, false, true);
            buttons[count++] = Button(12, 396, 80, 32, "", EDITOR_UI_SELECT_EDGE, false, true);
            buttons[count++] = Button(12, 434, 80, 32, "", EDITOR_UI_SELECT_FACE, false, true);
            buttons[count++] = Button(12, 480, 38, 30, "", EDITOR_UI_SELECT_ALL, false, true);
            buttons[count++] = Button(54, 480, 38, 30, "", EDITOR_UI_CLEAR_SELECTION, false, true);
            buttons[count++] = Button(12, 518, 80, 32, "", EDITOR_UI_SUBDIVIDE, false, state->meshSelectMode == EDITOR_MESH_SELECT_FACE);
        }
        return HitButtons(buttons, count, mouse);
    }
    if (mouse.y < EDITOR_UI_TOP_HEIGHT) {
        UIButton buttons[] = {
            Button(210, 10, 94, 36, "", EDITOR_UI_SET_OBJECT_MODE, false, true), Button(310, 10, 94, 36, "", EDITOR_UI_SET_EDIT_MODE, false, state->selectedObject != NULL),
            Button(430, 10, 38, 36, "", EDITOR_UI_SET_MOVE, false, true), Button(472, 10, 38, 36, "", EDITOR_UI_SET_ROTATE, false, true),
            Button(514, 10, 38, 36, "", EDITOR_UI_SET_SCALE, false, true), Button(568, 10, 82, 36, "", EDITOR_UI_TOGGLE_SNAP, false, true),
            Button(658, 10, 94, 36, "", EDITOR_UI_TOGGLE_COLLIDERS, false, true), Button(758, 10, 86, 36, "", EDITOR_UI_TOGGLE_CAMERA, false, true), Button(850, 10, 68, 36, "", EDITOR_UI_COPY, false, state->selectedObject != NULL), Button(922, 10, 68, 36, "", EDITOR_UI_PASTE, false, true), Button(GetScreenWidth() - 246, 10, 70, 36, "", EDITOR_UI_NEW, false, true),
            Button(GetScreenWidth() - 170, 10, 70, 36, "", EDITOR_UI_SAVE, false, true), Button(GetScreenWidth() - 94, 10, 78, 36, "", EDITOR_UI_EXPORT, false, true)
        };
        return HitButtons(buttons, (int)(sizeof(buttons) / sizeof(buttons[0])), mouse);
    }
    return EDITOR_UI_NONE;
}
