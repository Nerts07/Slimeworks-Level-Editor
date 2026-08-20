#include "scene_draw.h"

#include <math.h>
#include <raymath.h>
#include <string.h>

static Vector3 LerpVector3(Vector3 a, Vector3 b, float t)
{
    return (Vector3){ Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t) };
}

static Vector3 TransformPoint(Vector3 local, Vector3 position, Vector3 rotation, Vector3 scale)
{
    Matrix rotationMatrix = MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD));
    return Vector3Add(position, Vector3Transform(Vector3Multiply(local, scale), rotationMatrix));
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

static void DrawBox(const LevlDocument *document, const LevlObject *object)
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
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    if (bevel > 0.001f) DrawCubeV(position, Vector3Multiply(data->size, scale), (Color){ 19, 38, 45, 255 });
    for (int face = 0; face < 6; ++face) {
        DrawSubdividedQuad(TransformPoint(faces[face][0], position, rotation, scale), TransformPoint(faces[face][1], position, rotation, scale),
                           TransformPoint(faces[face][2], position, rotation, scale), TransformPoint(faces[face][3], position, rotation, scale),
                           data->faceColors[face], data->subdivisions);
    }
}

static void DrawEditableMesh(const LevlDocument *document, const LevlObject *object)
{
    const LevlMeshData *mesh = &object->data.mesh;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    for (int faceIndex = 0; faceIndex < mesh->faceCount; ++faceIndex) {
        const LevlMeshFace *face = &mesh->faces[faceIndex];
        if (face->vertexCount < 3 || face->vertexCount > LEVL_MESH_FACE_MAX_VERTICES) continue;
        Vector3 a = TransformPoint(mesh->vertices[face->vertices[0]], position, rotation, scale);
        for (int vertex = 1; vertex + 1 < face->vertexCount; ++vertex) {
            Vector3 b = TransformPoint(mesh->vertices[face->vertices[vertex]], position, rotation, scale);
            Vector3 c = TransformPoint(mesh->vertices[face->vertices[vertex + 1]], position, rotation, scale);
            DrawTriangle3D(a, b, c, face->color);
        }
    }
}

static void DrawMeshCollision(const LevlDocument *document, const LevlObject *object)
{
    const LevlMeshData *mesh = &object->data.mesh;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    for (int faceIndex = 0; faceIndex < mesh->faceCount; ++faceIndex) {
        const LevlMeshFace *face = &mesh->faces[faceIndex];
        if (face->vertexCount < 2) continue;
        for (int vertex = 0; vertex < face->vertexCount; ++vertex) {
            int next = (vertex + 1) % face->vertexCount;
            Vector3 a = TransformPoint(mesh->vertices[face->vertices[vertex]], position, rotation, scale);
            Vector3 b = TransformPoint(mesh->vertices[face->vertices[next]], position, rotation, scale);
            DrawLine3D(a, b, Fade(RED, 0.9f));
        }
    }
}

static Vector3 ModelCollisionVertex(const LevlDocument *document, const LevlObject *object, const Mesh *mesh, int index)
{
    Vector3 local = { mesh->vertices[index * 3], mesh->vertices[index * 3 + 1], mesh->vertices[index * 3 + 2] };
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    Matrix transform = MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD)));
    return Vector3Add(position, Vector3Transform(local, transform));
}

static void DrawModelCollision(const LevlDocument *document, const LevlObject *object, const Model *model)
{
    if (!model || !IsModelValid(*model)) return;
    for (int meshIndex = 0; meshIndex < model->meshCount; ++meshIndex) {
        const Mesh *mesh = &model->meshes[meshIndex];
        for (int triangle = 0; triangle < mesh->triangleCount; ++triangle) {
            int i0 = mesh->indices ? mesh->indices[triangle * 3] : triangle * 3;
            int i1 = mesh->indices ? mesh->indices[triangle * 3 + 1] : triangle * 3 + 1;
            int i2 = mesh->indices ? mesh->indices[triangle * 3 + 2] : triangle * 3 + 2;
            Vector3 a = ModelCollisionVertex(document, object, mesh, i0);
            Vector3 b = ModelCollisionVertex(document, object, mesh, i1);
            Vector3 c = ModelCollisionVertex(document, object, mesh, i2);
            DrawLine3D(a, b, Fade(RED, 0.9f));
            DrawLine3D(b, c, Fade(RED, 0.9f));
            DrawLine3D(c, a, Fade(RED, 0.9f));
        }
    }
}

static void DrawPlatform(const LevlDocument *document, const LevlObject *object)
{
    const LevlPlatformData *data = &object->data.platform;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    const float bottom = -2.0f - object->position.y;
    const float top = data->top - object->position.y;
    float hx = data->size.x * 0.5f, hz = data->size.y * 0.5f;
    Vector3 corners[8] = {
        {-hx, bottom, -hz}, {hx, bottom, -hz}, {hx, bottom, hz}, {-hx, bottom, hz},
        {-hx, top, -hz}, {hx, top, -hz}, {hx, top, hz}, {-hx, top, hz}
    };
    const int faces[6][4] = {{4, 5, 6, 7}, {0, 3, 2, 1}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    for (int face = 0; face < 6; ++face) {
        Vector3 a = TransformPoint(corners[faces[face][0]], position, rotation, scale);
        Vector3 b = TransformPoint(corners[faces[face][1]], position, rotation, scale);
        Vector3 c = TransformPoint(corners[faces[face][2]], position, rotation, scale);
        Vector3 d = TransformPoint(corners[faces[face][3]], position, rotation, scale);
        DrawTriangle3D(a, b, c, data->color); DrawTriangle3D(a, c, d, data->color);
        DrawLine3D(a, b, Fade(WHITE, 0.14f)); DrawLine3D(b, c, Fade(WHITE, 0.14f));
    }
}

static void DrawRamp(const LevlDocument *document, const LevlObject *object)
{
    const LevlRampData *data = &object->data.ramp;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    Vector3 localEnd = Vector3Subtract(data->end, object->position);
    Vector2 delta = { localEnd.x, localEnd.z };
    float length = Vector2Length(delta);
    if (length < 0.01f) return;
    Vector2 direction = Vector2Scale(delta, 1.0f / length);
    Vector2 side = { -direction.y * data->width * 0.5f, direction.x * data->width * 0.5f };
    float bottom = fminf(0.0f, localEnd.y) - 0.3f;
    Vector3 a = { side.x, 0.0f, side.y };
    Vector3 b = { -side.x, 0.0f, -side.y };
    Vector3 c = { localEnd.x - side.x, localEnd.y, localEnd.z - side.y };
    Vector3 d = { localEnd.x + side.x, localEnd.y, localEnd.z + side.y };
    Vector3 ab = { a.x, bottom, a.z }, bb = { b.x, bottom, b.z }, cb = { c.x, bottom, c.z }, db = { d.x, bottom, d.z };
    a = TransformPoint(a, position, rotation, scale); b = TransformPoint(b, position, rotation, scale);
    c = TransformPoint(c, position, rotation, scale); d = TransformPoint(d, position, rotation, scale);
    ab = TransformPoint(ab, position, rotation, scale); bb = TransformPoint(bb, position, rotation, scale);
    cb = TransformPoint(cb, position, rotation, scale); db = TransformPoint(db, position, rotation, scale);
    DrawTriangle3D(a, d, c, data->color); DrawTriangle3D(a, c, b, data->color);
    DrawTriangle3D(ab, bb, cb, Fade(data->color, 0.75f)); DrawTriangle3D(ab, cb, db, Fade(data->color, 0.75f));
    DrawTriangle3D(a, ab, bb, Fade(data->color, 0.85f)); DrawTriangle3D(a, bb, b, Fade(data->color, 0.85f));
    DrawTriangle3D(d, c, cb, Fade(data->color, 0.85f)); DrawTriangle3D(d, cb, db, Fade(data->color, 0.85f));
    DrawLine3D(a, d, WHITE); DrawLine3D(b, c, WHITE);
}

static void DrawMarker(const LevlDocument *document, const LevlObject *object, bool playerSpawnBlocked)
{
    bool blocked = strcmp(object->tag, "player_spawn") == 0 && playerSpawnBlocked;
    Color markerColor = strcmp(object->tag, "player_spawn") == 0 ? (blocked ? RED : LIME) : YELLOW;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    float radius = fmaxf(0.08f, (scale.x + scale.y + scale.z) * 0.26f / 3.0f);
    DrawSphere(position, radius, markerColor);
    Vector3 stem = TransformPoint((Vector3){ 0, 0.7f, 0 }, position, rotation, scale);
    Vector3 top = TransformPoint((Vector3){ 0, 1.25f, 0 }, position, rotation, scale);
    DrawCylinder(stem, 0.035f * scale.x, 0.035f * scale.x, 1.1f * scale.y, 6, markerColor);
    DrawSphere(top, 0.11f * scale.x, WHITE);
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

static void DrawModelObject(const LevlDocument *document, EditorModelCache *modelCache, int cacheCount, const LevlObject *object)
{
    Model *cached = EditorSceneGetModel(modelCache, cacheCount, object);
    if (!cached) {
        Vector3 position, rotation, scale;
        LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
        DrawCubeV(position, Vector3Multiply(object->data.model.collisionSize, scale), MAROON);
        return;
    }
    Model model = *cached;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    model.transform = MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD)));
    DrawModel(model, position, 1.0f, WHITE);
}

static void DrawEmpty(const LevlDocument *document, const LevlObject *object)
{
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    float size = fmaxf(0.18f, scale.x * 0.28f);
    DrawSphere(position, size, Fade(YELLOW, 0.85f));
    DrawLine3D(position, Vector3Add(position, (Vector3){ size * 2.5f, 0, 0 }), RED);
    DrawLine3D(position, Vector3Add(position, (Vector3){ 0, size * 2.5f, 0 }), GREEN);
    DrawLine3D(position, Vector3Add(position, (Vector3){ 0, 0, size * 2.5f }), BLUE);
    (void)rotation;
}

static Vector3 GizmoOrigin(const LevlDocument *document, const LevlObject *object)
{
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    if (object->type == LEVL_OBJECT_PLATFORM) {
        Matrix rotationMatrix = MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD));
        Vector3 top = Vector3Multiply((Vector3){ 0, object->data.platform.top - object->position.y, 0 }, scale);
        return Vector3Add(position, Vector3Transform(top, rotationMatrix));
    }
    return position;
}

static float GizmoSize(const LevlDocument *document, const LevlObject *object)
{
    BoundingBox bounds = LevlObjectGetWorldBounds(document, object);
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

void EditorSceneDrawGizmoAt(Vector3 origin, float size, EditorGizmoMode mode, int highlightedAxis)
{
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

void EditorSceneDrawGizmo(const LevlDocument *document, const LevlObject *object, EditorGizmoMode mode, int highlightedAxis)
{
    if (!object) return;
    EditorSceneDrawGizmoAt(GizmoOrigin(document, object), GizmoSize(document, object), mode, highlightedAxis);
}

static float SnapToGrid(float value, float step)
{
    return roundf(value / step) * step;
}

static void DrawInfiniteGrid(const Camera3D *camera, bool orthographic, float gridStep)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 right, up, center;
    float radius;
    if (orthographic) {
        right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
        up = Vector3Normalize(Vector3CrossProduct(right, forward));
        float alongRight = SnapToGrid(Vector3DotProduct(camera->target, right), gridStep);
        float alongUp = SnapToGrid(Vector3DotProduct(camera->target, up), gridStep);
        float depth = Vector3DotProduct(camera->target, forward) + 0.02f;
        center = Vector3Add(Vector3Add(Vector3Scale(right, alongRight), Vector3Scale(up, alongUp)), Vector3Scale(forward, depth));
        radius = fmaxf(30.0f, camera->fovy * 1.7f);
    } else {
        right = (Vector3){ 1.0f, 0.0f, 0.0f };
        up = (Vector3){ 0.0f, 0.0f, 1.0f };
        center = (Vector3){ SnapToGrid(camera->target.x, gridStep), 0.0f, SnapToGrid(camera->target.z, gridStep) };
        radius = fmaxf(70.0f, Vector3Distance(camera->position, camera->target) * 3.0f);
    }
    int lineCount = (int)ceilf(radius / gridStep);
    if (lineCount < 24) lineCount = 24;
    Color minor = (Color){ 143, 187, 205, 32 };
    Color major = (Color){ 159, 219, 226, 62 };
    for (int line = -lineCount; line <= lineCount; ++line) {
        float offset = line * gridStep;
        Color color = line % 10 == 0 ? major : minor;
        Vector3 a = Vector3Add(center, Vector3Scale(up, offset));
        DrawLine3D(Vector3Subtract(a, Vector3Scale(right, radius)), Vector3Add(a, Vector3Scale(right, radius)), color);
        Vector3 b = Vector3Add(center, Vector3Scale(right, offset));
        DrawLine3D(Vector3Subtract(b, Vector3Scale(up, radius)), Vector3Add(b, Vector3Scale(up, radius)), color);
    }
}

void EditorSceneDraw(LevlDocument *document, int selectedIndex, const bool *selection, EditorModelCache *modelCache, int cacheCount,
                     bool showColliders, bool playerSpawnBlocked, const Camera3D *camera, bool orthographicGrid, float gridStep)
{
    DrawInfiniteGrid(camera, orthographicGrid, gridStep);
    for (int i = 0; i < document->objectCount; ++i) {
        LevlObject *object = &document->objects[i];
        switch (object->type) {
            case LEVL_OBJECT_BOX: DrawBox(document, object); break;
            case LEVL_OBJECT_MESH: DrawEditableMesh(document, object); break;
            case LEVL_OBJECT_PLATFORM: DrawPlatform(document, object); break;
            case LEVL_OBJECT_RAMP: DrawRamp(document, object); break;
            case LEVL_OBJECT_MODEL: DrawModelObject(document, modelCache, cacheCount, object); break;
            case LEVL_OBJECT_MARKER: DrawMarker(document, object, playerSpawnBlocked); break;
            case LEVL_OBJECT_EMPTY: DrawEmpty(document, object); break;
        }
        BoundingBox bounds = LevlObjectGetWorldBounds(document, object);
        if (showColliders && object->type == LEVL_OBJECT_MESH) DrawMeshCollision(document, object);
        else if (showColliders && object->type == LEVL_OBJECT_MODEL) {
            Model *model = EditorSceneGetModel(modelCache, cacheCount, object);
            if (model) DrawModelCollision(document, object, model);
            else DrawBoundingBox(bounds, Fade(RED, 0.4f));
        } else if (showColliders && object->type == LEVL_OBJECT_BOX) DrawBoundingBox(bounds, Fade(RED, 0.4f));
        if ((selection && selection[i]) || (!selection && i == selectedIndex)) DrawBoundingBox(bounds, YELLOW);
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
