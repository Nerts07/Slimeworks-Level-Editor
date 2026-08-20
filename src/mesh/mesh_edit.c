#include "mesh_edit.h"

#include <float.h>
#include <math.h>
#include <raymath.h>
#include <string.h>

static float DistanceToSegment(Vector2 point, Vector2 a, Vector2 b)
{
    Vector2 delta = Vector2Subtract(b, a);
    float lengthSquared = Vector2DotProduct(delta, delta);
    if (lengthSquared < 0.0001f) return Vector2Distance(point, a);
    float t = Vector2DotProduct(Vector2Subtract(point, a), delta) / lengthSquared;
    t = Clamp(t, 0.0f, 1.0f);
    return Vector2Distance(point, Vector2Add(a, Vector2Scale(delta, t)));
}

static bool IsEditingObject(const EditorMeshEdit *edit, const LevlObject *object)
{
    return edit && object && object->type == LEVL_OBJECT_MESH && edit->objectId == object->id;
}

static Vector3 VertexWorld(const LevlObject *object, int index)
{
    return Vector3Add(object->position, object->data.mesh.vertices[index]);
}

void EditorMeshEditReset(EditorMeshEdit *edit)
{
    *edit = (EditorMeshEdit){ .objectId = -1, .selectMode = EDITOR_MESH_SELECT_VERTEX };
}

void EditorMeshEditBegin(EditorMeshEdit *edit, const LevlObject *object)
{
    EditorMeshEditReset(edit);
    if (object && object->type == LEVL_OBJECT_MESH) edit->objectId = object->id;
}

void EditorMeshEditSetSelectMode(EditorMeshEdit *edit, EditorMeshSelectMode mode)
{
    if (!edit) return;
    edit->selectMode = mode;
}

void EditorMeshEditClear(EditorMeshEdit *edit)
{
    if (!edit) return;
    memset(edit->vertices, 0, sizeof(edit->vertices));
    memset(edit->edges, 0, sizeof(edit->edges));
    memset(edit->faces, 0, sizeof(edit->faces));
}

static void SelectSingle(EditorMeshEdit *edit, int index, bool append)
{
    if (!append) EditorMeshEditClear(edit);
    bool *selected = edit->selectMode == EDITOR_MESH_SELECT_VERTEX ? edit->vertices :
                     (edit->selectMode == EDITOR_MESH_SELECT_EDGE ? edit->edges : edit->faces);
    selected[index] = append ? !selected[index] : true;
}

bool EditorMeshEditPick(EditorMeshEdit *edit, const LevlObject *object, Camera3D camera, Vector2 mouse, bool append)
{
    if (!IsEditingObject(edit, object)) return false;
    const LevlMeshData *mesh = &object->data.mesh;
    int picked = -1;
    if (edit->selectMode == EDITOR_MESH_SELECT_VERTEX) {
        float best = 13.0f;
        for (int i = 0; i < mesh->vertexCount; ++i) {
            float distance = Vector2Distance(mouse, GetWorldToScreen(VertexWorld(object, i), camera));
            if (distance < best) { best = distance; picked = i; }
        }
    } else if (edit->selectMode == EDITOR_MESH_SELECT_EDGE) {
        LevlMeshEdge edges[EDITOR_MESH_MAX_EDGES];
        int edgeCount = LevlMeshBuildEdges(mesh, edges, EDITOR_MESH_MAX_EDGES);
        float best = 16.0f;
        for (int i = 0; i < edgeCount; ++i) {
            float distance = DistanceToSegment(mouse, GetWorldToScreen(VertexWorld(object, edges[i].a), camera),
                                               GetWorldToScreen(VertexWorld(object, edges[i].b), camera));
            if (distance < best) { best = distance; picked = i; }
        }
    } else {
        Ray ray = GetScreenToWorldRay(mouse, camera);
        float best = FLT_MAX;
        for (int faceIndex = 0; faceIndex < mesh->faceCount; ++faceIndex) {
            const LevlMeshFace *face = &mesh->faces[faceIndex];
            for (int vertex = 1; vertex + 1 < face->vertexCount; ++vertex) {
                RayCollision hit = GetRayCollisionTriangle(ray, VertexWorld(object, face->vertices[0]),
                                                            VertexWorld(object, face->vertices[vertex]),
                                                            VertexWorld(object, face->vertices[vertex + 1]));
                if (hit.hit && hit.distance < best) { best = hit.distance; picked = faceIndex; }
            }
        }
    }
    if (picked < 0) return false;
    SelectSingle(edit, picked, append);
    return true;
}

void EditorMeshEditSelectAll(EditorMeshEdit *edit, const LevlObject *object)
{
    if (!IsEditingObject(edit, object)) return;
    EditorMeshEditClear(edit);
    if (edit->selectMode == EDITOR_MESH_SELECT_VERTEX)
        for (int i = 0; i < object->data.mesh.vertexCount; ++i) edit->vertices[i] = true;
    else if (edit->selectMode == EDITOR_MESH_SELECT_FACE)
        for (int i = 0; i < object->data.mesh.faceCount; ++i) edit->faces[i] = true;
    else {
        LevlMeshEdge edges[EDITOR_MESH_MAX_EDGES];
        int count = LevlMeshBuildEdges(&object->data.mesh, edges, EDITOR_MESH_MAX_EDGES);
        for (int i = 0; i < count; ++i) edit->edges[i] = true;
    }
}

static void CollectAffectedVertices(const EditorMeshEdit *edit, const LevlObject *object, bool affected[LEVL_MESH_MAX_VERTICES])
{
    memset(affected, 0, LEVL_MESH_MAX_VERTICES * sizeof(affected[0]));
    const LevlMeshData *mesh = &object->data.mesh;
    if (edit->selectMode == EDITOR_MESH_SELECT_VERTEX) {
        for (int i = 0; i < mesh->vertexCount; ++i) affected[i] = edit->vertices[i];
    } else if (edit->selectMode == EDITOR_MESH_SELECT_FACE) {
        for (int faceIndex = 0; faceIndex < mesh->faceCount; ++faceIndex) if (edit->faces[faceIndex]) {
            const LevlMeshFace *face = &mesh->faces[faceIndex];
            for (int vertex = 0; vertex < face->vertexCount; ++vertex) affected[face->vertices[vertex]] = true;
        }
    } else {
        LevlMeshEdge edges[EDITOR_MESH_MAX_EDGES];
        int count = LevlMeshBuildEdges(mesh, edges, EDITOR_MESH_MAX_EDGES);
        for (int i = 0; i < count; ++i) if (edit->edges[i]) { affected[edges[i].a] = true; affected[edges[i].b] = true; }
    }
}

static Vector3 SelectionCenter(const EditorMeshEdit *edit, const LevlObject *object)
{
    bool affected[LEVL_MESH_MAX_VERTICES];
    CollectAffectedVertices(edit, object, affected);
    Vector3 center = { 0 };
    int count = 0;
    for (int i = 0; i < object->data.mesh.vertexCount; ++i) if (affected[i]) { center = Vector3Add(center, object->data.mesh.vertices[i]); count++; }
    return count > 0 ? Vector3Scale(center, 1.0f / (float)count) : (Vector3){ 0 };
}

bool EditorMeshEditGetSelectionCenter(const EditorMeshEdit *edit, const LevlObject *object, Vector3 *center)
{
    if (!center || !IsEditingObject(edit, object)) return false;
    bool affected[LEVL_MESH_MAX_VERTICES];
    CollectAffectedVertices(edit, object, affected);
    int count = 0;
    for (int i = 0; i < object->data.mesh.vertexCount; ++i) if (affected[i]) count++;
    if (count == 0) return false;
    *center = Vector3Add(object->position, SelectionCenter(edit, object));
    return true;
}

void EditorMeshEditMove(EditorMeshEdit *edit, LevlObject *object, Vector3 delta)
{
    if (!IsEditingObject(edit, object)) return;
    bool affected[LEVL_MESH_MAX_VERTICES];
    CollectAffectedVertices(edit, object, affected);
    for (int i = 0; i < object->data.mesh.vertexCount; ++i) if (affected[i]) object->data.mesh.vertices[i] = Vector3Add(object->data.mesh.vertices[i], delta);
}

void EditorMeshEditScale(EditorMeshEdit *edit, LevlObject *object, float factor)
{
    if (!IsEditingObject(edit, object)) return;
    bool affected[LEVL_MESH_MAX_VERTICES];
    CollectAffectedVertices(edit, object, affected);
    Vector3 center = SelectionCenter(edit, object);
    for (int i = 0; i < object->data.mesh.vertexCount; ++i) if (affected[i]) {
        Vector3 offset = Vector3Subtract(object->data.mesh.vertices[i], center);
        object->data.mesh.vertices[i] = Vector3Add(center, Vector3Scale(offset, factor));
    }
}

void EditorMeshEditScaleAxis(EditorMeshEdit *edit, LevlObject *object, int axis, float factor)
{
    if (!IsEditingObject(edit, object) || axis < 0 || axis > 2) return;
    bool affected[LEVL_MESH_MAX_VERTICES];
    CollectAffectedVertices(edit, object, affected);
    Vector3 center = SelectionCenter(edit, object);
    for (int i = 0; i < object->data.mesh.vertexCount; ++i) if (affected[i]) {
        float *coordinate = &object->data.mesh.vertices[i].x;
        float *centerCoordinate = &center.x;
        coordinate[axis] = centerCoordinate[axis] + (coordinate[axis] - centerCoordinate[axis]) * factor;
    }
}

void EditorMeshEditRotate(EditorMeshEdit *edit, LevlObject *object, Vector3 axis, float degrees)
{
    if (!IsEditingObject(edit, object)) return;
    bool affected[LEVL_MESH_MAX_VERTICES];
    CollectAffectedVertices(edit, object, affected);
    Vector3 center = SelectionCenter(edit, object);
    Matrix rotation = MatrixRotate(axis, degrees * DEG2RAD);
    for (int i = 0; i < object->data.mesh.vertexCount; ++i) if (affected[i]) {
        Vector3 offset = Vector3Subtract(object->data.mesh.vertices[i], center);
        object->data.mesh.vertices[i] = Vector3Add(center, Vector3Transform(offset, rotation));
    }
}

bool EditorMeshEditSubdivideFaces(EditorMeshEdit *edit, LevlObject *object)
{
    if (!IsEditingObject(edit, object) || edit->selectMode != EDITOR_MESH_SELECT_FACE) return false;
    LevlMeshData *mesh = &object->data.mesh;
    int selectedFaces = 0;
    int resultingFaces = 0;
    for (int face = 0; face < mesh->faceCount; ++face) {
        if (edit->faces[face]) { selectedFaces++; resultingFaces += mesh->faces[face].vertexCount; }
        else resultingFaces++;
    }
    if (selectedFaces == 0 || mesh->vertexCount + selectedFaces > LEVL_MESH_MAX_VERTICES ||
        resultingFaces > LEVL_MESH_MAX_FACES) return false;

    LevlMeshFace oldFaces[LEVL_MESH_MAX_FACES];
    bool selected[LEVL_MESH_MAX_FACES];
    memcpy(oldFaces, mesh->faces, (size_t)mesh->faceCount * sizeof(oldFaces[0]));
    memcpy(selected, edit->faces, sizeof(selected));
    int oldFaceCount = mesh->faceCount;
    mesh->faceCount = 0;
    EditorMeshEditClear(edit);
    for (int faceIndex = 0; faceIndex < oldFaceCount; ++faceIndex) {
        LevlMeshFace face = oldFaces[faceIndex];
        if (!selected[faceIndex]) {
            mesh->faces[mesh->faceCount++] = face;
            continue;
        }
        Vector3 center = { 0 };
        for (int vertex = 0; vertex < face.vertexCount; ++vertex)
            center = Vector3Add(center, mesh->vertices[face.vertices[vertex]]);
        unsigned short centerIndex = (unsigned short)mesh->vertexCount++;
        mesh->vertices[centerIndex] = Vector3Scale(center, 1.0f / (float)face.vertexCount);
        for (int vertex = 0; vertex < face.vertexCount; ++vertex) {
            int next = (vertex + 1) % face.vertexCount;
            LevlMeshFace *triangle = &mesh->faces[mesh->faceCount];
            *triangle = (LevlMeshFace){ .vertexCount = 3, .color = face.color,
                .vertices = { face.vertices[vertex], face.vertices[next], centerIndex, centerIndex } };
            edit->faces[mesh->faceCount++] = true;
        }
    }
    return true;
}

void EditorMeshEditCycleFaceColor(EditorMeshEdit *edit, LevlObject *object, const Color *palette, int paletteCount)
{
    if (!IsEditingObject(edit, object) || !palette || paletteCount <= 0) return;
    for (int face = 0; face < object->data.mesh.faceCount; ++face) if (edit->faces[face]) {
        int current = -1;
        for (int color = 0; color < paletteCount; ++color) if (ColorIsEqual(object->data.mesh.faces[face].color, palette[color])) { current = color; break; }
        object->data.mesh.faces[face].color = palette[(current + 1) % paletteCount];
    }
}

int EditorMeshEditSelectionCount(const EditorMeshEdit *edit, const LevlObject *object)
{
    if (!IsEditingObject(edit, object)) return 0;
    int count = 0;
    int maximum = edit->selectMode == EDITOR_MESH_SELECT_VERTEX ? object->data.mesh.vertexCount :
                  (edit->selectMode == EDITOR_MESH_SELECT_FACE ? object->data.mesh.faceCount : EDITOR_MESH_MAX_EDGES);
    const bool *selection = edit->selectMode == EDITOR_MESH_SELECT_VERTEX ? edit->vertices :
                            (edit->selectMode == EDITOR_MESH_SELECT_EDGE ? edit->edges : edit->faces);
    for (int i = 0; i < maximum; ++i) if (selection[i]) count++;
    return count;
}

void EditorMeshEditDraw(const EditorMeshEdit *edit, const LevlObject *object)
{
    if (!IsEditingObject(edit, object)) return;
    const LevlMeshData *mesh = &object->data.mesh;
    for (int faceIndex = 0; faceIndex < mesh->faceCount; ++faceIndex) if (edit->faces[faceIndex]) {
        const LevlMeshFace *face = &mesh->faces[faceIndex];
        for (int vertex = 1; vertex + 1 < face->vertexCount; ++vertex)
            DrawTriangle3D(VertexWorld(object, face->vertices[0]), VertexWorld(object, face->vertices[vertex]),
                           VertexWorld(object, face->vertices[vertex + 1]), Fade(YELLOW, 0.38f));
    }
    LevlMeshEdge edges[EDITOR_MESH_MAX_EDGES];
    int edgeCount = LevlMeshBuildEdges(mesh, edges, EDITOR_MESH_MAX_EDGES);
    for (int edge = 0; edge < edgeCount; ++edge) {
        Color color = edit->edges[edge] ? YELLOW : Fade(BLACK, 0.8f);
        DrawLine3D(VertexWorld(object, edges[edge].a), VertexWorld(object, edges[edge].b), color);
    }
    for (int vertex = 0; vertex < mesh->vertexCount; ++vertex)
        DrawSphere(VertexWorld(object, vertex), edit->vertices[vertex] ? 0.12f : 0.065f, edit->vertices[vertex] ? YELLOW : RAYWHITE);
}

const char *EditorMeshSelectModeName(EditorMeshSelectMode mode)
{
    switch (mode) {
        case EDITOR_MESH_SELECT_VERTEX: return "Vertex";
        case EDITOR_MESH_SELECT_EDGE: return "Edge";
        case EDITOR_MESH_SELECT_FACE: return "Face";
        default: return "Unknown";
    }
}
