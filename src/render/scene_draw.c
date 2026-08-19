#include "scene_draw.h"

#include <math.h>
#include <raymath.h>
#include <string.h>

static Vector3 LerpVector3(Vector3 a, Vector3 b, float t)
{
    return (Vector3){ Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t) };
}

static Vector3 TransformBoxPoint(const LevlObject *object, Vector3 local)
{
    Matrix rotation = MatrixRotateXYZ(Vector3Scale(object->data.box.rotation, DEG2RAD));
    return Vector3Add(Vector3Transform(local, rotation), object->position);
}

static void DrawSubdividedQuad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color, int subdivisions)
{
    int steps = subdivisions < 1 ? 1 : (subdivisions > 16 ? 16 : subdivisions);
    for (int row = 0; row < steps; ++row) {
        float v0 = (float)row / (float)steps, v1 = (float)(row + 1) / (float)steps;
        Vector3 left0 = LerpVector3(a, d, v0), right0 = LerpVector3(b, c, v0);
        Vector3 left1 = LerpVector3(a, d, v1), right1 = LerpVector3(b, c, v1);
        for (int column = 0; column < steps; ++column) {
            float u0 = (float)column / (float)steps, u1 = (float)(column + 1) / (float)steps;
            Vector3 p0 = LerpVector3(left0, right0, u0), p1 = LerpVector3(left0, right0, u1);
            Vector3 p2 = LerpVector3(left1, right1, u1), p3 = LerpVector3(left1, right1, u0);
            DrawTriangle3D(p0, p1, p2, color);
            DrawTriangle3D(p0, p2, p3, color);
            if (steps > 1) {
                Color edge = Fade(WHITE, 0.18f);
                DrawLine3D(p0, p1, edge); DrawLine3D(p0, p3, edge);
            }
        }
    }
}

static void DrawBox(const LevlObject *object)
{
    const LevlBoxData *data = &object->data.box;
    float hx = data->size.x * 0.5f, hy = data->size.y * 0.5f, hz = data->size.z * 0.5f;
    float bevel = data->bevel;
    float limit = fminf(hx, fminf(hy, hz)) * 0.45f;
    if (bevel < 0.0f) bevel = 0.0f;
    if (bevel > limit) bevel = limit;

    Vector3 faces[6][4] = {
        {{-hx + bevel, hy, -hz + bevel}, {hx - bevel, hy, -hz + bevel}, {hx - bevel, hy, hz - bevel}, {-hx + bevel, hy, hz - bevel}},
        {{-hx + bevel, -hy, hz - bevel}, {hx - bevel, -hy, hz - bevel}, {hx - bevel, -hy, -hz + bevel}, {-hx + bevel, -hy, -hz + bevel}},
        {{-hx + bevel, -hy + bevel, -hz}, {hx - bevel, -hy + bevel, -hz}, {hx - bevel, hy - bevel, -hz}, {-hx + bevel, hy - bevel, -hz}},
        {{hx - bevel, -hy + bevel, hz}, {-hx + bevel, -hy + bevel, hz}, {-hx + bevel, hy - bevel, hz}, {hx - bevel, hy - bevel, hz}},
        {{hx, -hy + bevel, -hz + bevel}, {hx, -hy + bevel, hz - bevel}, {hx, hy - bevel, hz - bevel}, {hx, hy - bevel, -hz + bevel}},
        {{-hx, -hy + bevel, hz - bevel}, {-hx, -hy + bevel, -hz + bevel}, {-hx, hy - bevel, -hz + bevel}, {-hx, hy - bevel, hz - bevel}}
    };
    if (bevel > 0.001f) DrawCubeV(object->position, data->size, (Color){ 19, 38, 45, 255 });
    for (int face = 0; face < 6; ++face) {
        DrawSubdividedQuad(TransformBoxPoint(object, faces[face][0]), TransformBoxPoint(object, faces[face][1]),
                           TransformBoxPoint(object, faces[face][2]), TransformBoxPoint(object, faces[face][3]),
                           data->faceColors[face], data->subdivisions);
    }
}

static void DrawPlatform(const LevlObject *object)
{
    const LevlPlatformData *data = &object->data.platform;
    const float bottom = -2.0f;
    float height = data->top - bottom;
    Vector3 center = { object->position.x, bottom + height * 0.5f, object->position.z };
    Vector3 size = { data->size.x, height, data->size.y };
    DrawCubeV(center, size, data->color);
    DrawCubeWiresV(center, size, Fade(WHITE, 0.14f));
}

static void DrawRamp(const LevlObject *object)
{
    const LevlRampData *data = &object->data.ramp;
    Vector2 delta = { data->end.x - object->position.x, data->end.z - object->position.z };
    float length = Vector2Length(delta);
    if (length < 0.01f) return;
    Vector2 direction = Vector2Scale(delta, 1.0f / length);
    Vector2 side = { -direction.y * data->width * 0.5f, direction.x * data->width * 0.5f };
    float bottom = fminf(object->position.y, data->end.y) - 0.3f;
    Vector3 a = { object->position.x + side.x, object->position.y, object->position.z + side.y };
    Vector3 b = { object->position.x - side.x, object->position.y, object->position.z - side.y };
    Vector3 c = { data->end.x - side.x, data->end.y, data->end.z - side.y };
    Vector3 d = { data->end.x + side.x, data->end.y, data->end.z + side.y };
    Vector3 ab = { a.x, bottom, a.z }, bb = { b.x, bottom, b.z }, cb = { c.x, bottom, c.z }, db = { d.x, bottom, d.z };
    DrawTriangle3D(a, d, c, data->color); DrawTriangle3D(a, c, b, data->color);
    DrawTriangle3D(ab, bb, cb, Fade(data->color, 0.75f)); DrawTriangle3D(ab, cb, db, Fade(data->color, 0.75f));
    DrawTriangle3D(a, ab, bb, Fade(data->color, 0.85f)); DrawTriangle3D(a, bb, b, Fade(data->color, 0.85f));
    DrawTriangle3D(d, c, cb, Fade(data->color, 0.85f)); DrawTriangle3D(d, cb, db, Fade(data->color, 0.85f));
    DrawLine3D(a, d, WHITE); DrawLine3D(b, c, WHITE);
}

static void DrawMarker(const LevlObject *object)
{
    Color markerColor = strcmp(object->tag, "player_spawn") == 0 ? LIME : YELLOW;
    DrawSphere(object->position, 0.26f, markerColor);
    DrawCylinder((Vector3){ object->position.x, object->position.y + 0.7f, object->position.z }, 0.035f, 0.035f, 1.1f, 6, markerColor);
    DrawSphere((Vector3){ object->position.x, object->position.y + 1.25f, object->position.z }, 0.11f, WHITE);
}

Model *EditorSceneGetModel(EditorModelCache *modelCache, int cacheCount, const LevlObject *object)
{
    for (int i = 0; i < cacheCount; ++i) {
        EditorModelCache *entry = &modelCache[i];
        if (entry->objectId != object->id) continue;
        return entry->loaded ? &entry->model : NULL;
    }
    for (int i = 0; i < cacheCount; ++i) {
        EditorModelCache *entry = &modelCache[i];
        if (entry->attempted) continue;
        entry->attempted = true;
        entry->objectId = object->id;
        entry->model = LoadModel(object->data.model.assetPath);
        entry->loaded = IsModelValid(entry->model);
        return entry->loaded ? &entry->model : NULL;
    }
    return NULL;
}

static void DrawModelObject(EditorModelCache *modelCache, int cacheCount, const LevlObject *object)
{
    Model *cached = EditorSceneGetModel(modelCache, cacheCount, object);
    if (!cached) {
        DrawCubeV(object->position, object->data.model.collisionSize, MAROON);
        return;
    }
    Model model = *cached;
    Matrix rotation = MatrixRotateXYZ(Vector3Scale(object->data.model.rotation, DEG2RAD));
    model.transform = MatrixMultiply(MatrixScale(object->data.model.scale.x, object->data.model.scale.y, object->data.model.scale.z), rotation);
    DrawModel(model, object->position, 1.0f, WHITE);
}

static Vector3 GizmoOrigin(const LevlObject *object)
{
    if (object->type == LEVL_OBJECT_PLATFORM) return (Vector3){ object->position.x, object->data.platform.top, object->position.z };
    return object->position;
}

static float GizmoSize(const LevlObject *object)
{
    BoundingBox bounds = LevlObjectGetBounds(object);
    float x = bounds.max.x - bounds.min.x, y = bounds.max.y - bounds.min.y, z = bounds.max.z - bounds.min.z;
    float maximum = fmaxf(x, fmaxf(y, z));
    return fmaxf(1.25f, fminf(maximum * 0.65f, 4.0f));
}

static Color AxisColor(int axis, int highlightedAxis)
{
    if (axis == highlightedAxis) return YELLOW;
    return axis == 0 ? RED : (axis == 1 ? GREEN : BLUE);
}

static void DrawRotationRing(Vector3 origin, float radius, int axis, Color color)
{
    const int segments = 48;
    for (int segment = 0; segment < segments; ++segment) {
        float a = (float)segment / (float)segments * 2.0f * PI;
        float b = (float)(segment + 1) / (float)segments * 2.0f * PI;
        Vector3 p0, p1;
        if (axis == 0) { p0 = (Vector3){ 0, cosf(a) * radius, sinf(a) * radius }; p1 = (Vector3){ 0, cosf(b) * radius, sinf(b) * radius }; }
        else if (axis == 1) { p0 = (Vector3){ cosf(a) * radius, 0, sinf(a) * radius }; p1 = (Vector3){ cosf(b) * radius, 0, sinf(b) * radius }; }
        else { p0 = (Vector3){ cosf(a) * radius, sinf(a) * radius, 0 }; p1 = (Vector3){ cosf(b) * radius, sinf(b) * radius, 0 }; }
        DrawLine3D(Vector3Add(origin, p0), Vector3Add(origin, p1), color);
    }
}

void EditorSceneDrawGizmo(const LevlObject *object, EditorGizmoMode mode, int highlightedAxis)
{
    if (!object) return;
    Vector3 origin = GizmoOrigin(object);
    float size = GizmoSize(object);
    if (mode == EDITOR_GIZMO_ROTATE) {
        for (int axis = 0; axis < 3; ++axis) DrawRotationRing(origin, size, axis, AxisColor(axis, highlightedAxis));
        DrawSphere(origin, 0.09f, WHITE);
        return;
    }
    const Vector3 directions[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int axis = 0; axis < 3; ++axis) {
        Vector3 end = Vector3Add(origin, Vector3Scale(directions[axis], size));
        Color color = AxisColor(axis, highlightedAxis);
        DrawCylinderEx(origin, end, 0.035f, 0.025f, 8, color);
        if (mode == EDITOR_GIZMO_MOVE) DrawSphere(end, 0.13f, color);
        else DrawCubeV(end, (Vector3){ 0.22f, 0.22f, 0.22f }, color);
    }
    DrawSphere(origin, 0.08f, WHITE);
}

void EditorSceneDraw(LevlDocument *document, int selectedIndex, EditorModelCache *modelCache, int cacheCount,
                     bool showColliders, float gridStep)
{
    int gridSlices = gridStep < 0.5f ? 80 : 160;
    DrawGrid(gridSlices, gridStep);
    for (int i = 0; i < document->objectCount; ++i) {
        LevlObject *object = &document->objects[i];
        switch (object->type) {
            case LEVL_OBJECT_BOX: DrawBox(object); break;
            case LEVL_OBJECT_PLATFORM: DrawPlatform(object); break;
            case LEVL_OBJECT_RAMP: DrawRamp(object); break;
            case LEVL_OBJECT_MODEL: DrawModelObject(modelCache, cacheCount, object); break;
            case LEVL_OBJECT_MARKER: DrawMarker(object); break;
        }
        BoundingBox bounds = LevlObjectGetBounds(object);
        if (showColliders && (object->type == LEVL_OBJECT_BOX || object->type == LEVL_OBJECT_MODEL)) DrawBoundingBox(bounds, Fade(RED, 0.4f));
        if (i == selectedIndex) DrawBoundingBox(bounds, YELLOW);
    }
}

void EditorSceneInvalidateModel(EditorModelCache *modelCache, int cacheCount, int objectId)
{
    for (int i = 0; i < cacheCount; ++i) {
        if (modelCache[i].objectId != objectId) continue;
        if (modelCache[i].loaded) UnloadModel(modelCache[i].model);
        modelCache[i] = (EditorModelCache){ 0 };
        return;
    }
}

void EditorSceneUnloadModels(EditorModelCache *modelCache, int cacheCount)
{
    for (int i = 0; i < cacheCount; ++i) if (modelCache[i].loaded) UnloadModel(modelCache[i].model);
}
