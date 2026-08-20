#include "levl.h"

#include <math.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void CopyString(char *destination, size_t destinationSize, const char *source)
{
    snprintf(destination, destinationSize, "%s", source ? source : "");
}

static void SetMessage(char *message, int messageSize, const char *text)
{
    if (message && messageSize > 0) snprintf(message, (size_t)messageSize, "%s", text);
}

static void SetFaces(Color faceColors[6], Color color)
{
    for (int i = 0; i < 6; ++i) faceColors[i] = color;
}

static void SetMeshFace(LevlMeshFace *face, int count, int a, int b, int c, int d, Color color)
{
    *face = (LevlMeshFace){ .vertexCount = (unsigned char)count, .color = color };
    face->vertices[0] = (unsigned short)a;
    face->vertices[1] = (unsigned short)b;
    face->vertices[2] = (unsigned short)c;
    face->vertices[3] = (unsigned short)d;
}

static void MakeCubeMesh(LevlMeshData *mesh, Vector3 size, Vector3 rotation, const Color faceColors[6])
{
    float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
    const Vector3 localVertices[8] = {
        {-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {-hx, hy, -hz},
        {-hx, -hy, hz}, {hx, -hy, hz}, {hx, hy, hz}, {-hx, hy, hz}
    };
    Matrix matrix = MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD));
    *mesh = (LevlMeshData){ .vertexCount = 8, .faceCount = 6, .collidable = true };
    for (int i = 0; i < 8; ++i) mesh->vertices[i] = Vector3Transform(localVertices[i], matrix);
    SetMeshFace(&mesh->faces[0], 4, 3, 7, 6, 2, faceColors[0]);
    SetMeshFace(&mesh->faces[1], 4, 4, 0, 1, 5, faceColors[1]);
    SetMeshFace(&mesh->faces[2], 4, 0, 3, 2, 1, faceColors[2]);
    SetMeshFace(&mesh->faces[3], 4, 5, 6, 7, 4, faceColors[3]);
    SetMeshFace(&mesh->faces[4], 4, 1, 2, 6, 5, faceColors[4]);
    SetMeshFace(&mesh->faces[5], 4, 4, 7, 3, 0, faceColors[5]);
}

/* .levl treats the outside of a closed mesh as its visible/front side. */
static void EnsureMeshOutwardWinding(LevlMeshData *mesh)
{
    if (!mesh || mesh->vertexCount < 3) return;
    Vector3 meshCenter = { 0 };
    for (int i = 0; i < mesh->vertexCount; ++i) meshCenter = Vector3Add(meshCenter, mesh->vertices[i]);
    meshCenter = Vector3Scale(meshCenter, 1.0f / (float)mesh->vertexCount);
    for (int i = 0; i < mesh->faceCount; ++i) {
        LevlMeshFace *face = &mesh->faces[i];
        if (face->vertexCount < 3) continue;
        Vector3 a = mesh->vertices[face->vertices[0]];
        Vector3 b = mesh->vertices[face->vertices[1]];
        Vector3 c = mesh->vertices[face->vertices[2]];
        Vector3 normal = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        Vector3 faceCenter = { 0 };
        for (int vertex = 0; vertex < face->vertexCount; ++vertex)
            faceCenter = Vector3Add(faceCenter, mesh->vertices[face->vertices[vertex]]);
        faceCenter = Vector3Scale(faceCenter, 1.0f / (float)face->vertexCount);
        if (Vector3DotProduct(normal, Vector3Subtract(faceCenter, meshCenter)) < 0.0f) {
            for (int left = 1, right = face->vertexCount - 1; left < right; ++left, --right) {
                unsigned short swap = face->vertices[left];
                face->vertices[left] = face->vertices[right];
                face->vertices[right] = swap;
            }
        }
    }
}

const char *LevlObjectTypeName(LevlObjectType type)
{
    switch (type) {
        case LEVL_OBJECT_BOX: return "Box";
        case LEVL_OBJECT_MESH: return "Mesh";
        case LEVL_OBJECT_PLATFORM: return "Platform";
        case LEVL_OBJECT_RAMP: return "Ramp";
        case LEVL_OBJECT_MODEL: return "Model";
        case LEVL_OBJECT_MARKER: return "Marker";
        case LEVL_OBJECT_EMPTY: return "Empty";
        default: return "Unknown";
    }
}

LevlObject *LevlDocumentAdd(LevlDocument *document, LevlObjectType type)
{
    if (document->objectCount >= LEVL_MAX_OBJECTS) return NULL;
    LevlObject *object = &document->objects[document->objectCount++];
    *object = (LevlObject){ .id = document->nextId++, .type = type };
    snprintf(object->name, sizeof(object->name), "%s_%d", LevlObjectTypeName(type), object->id);
    CopyString(object->tag, sizeof(object->tag), "untagged");

    if (type == LEVL_OBJECT_BOX) {
        object->data.box.size = (Vector3){ 2.0f, 2.0f, 2.0f };
        object->data.box.subdivisions = 1;
        object->data.box.collidable = true;
        SetFaces(object->data.box.faceColors, (Color){ 91, 188, 151, 255 });
    } else if (type == LEVL_OBJECT_MESH) {
        Color colors[6];
        SetFaces(colors, (Color){ 91, 188, 151, 255 });
        MakeCubeMesh(&object->data.mesh, (Vector3){ 2.0f, 2.0f, 2.0f }, (Vector3){ 0 }, colors);
    } else if (type == LEVL_OBJECT_PLATFORM) {
        object->data.platform.size = (Vector2){ 16.0f, 16.0f };
        object->data.platform.top = 0.0f;
        object->data.platform.color = (Color){ 74, 148, 116, 255 };
    } else if (type == LEVL_OBJECT_RAMP) {
        object->data.ramp.end = (Vector3){ 7.0f, 2.0f, 0.0f };
        object->data.ramp.width = 5.0f;
        object->data.ramp.color = (Color){ 117, 177, 223, 255 };
    } else if (type == LEVL_OBJECT_MODEL) {
        object->data.model.scale = (Vector3){ 1.0f, 1.0f, 1.0f };
        object->data.model.collisionSize = (Vector3){ 1.0f, 1.0f, 1.0f };
        object->data.model.collidable = true;
    } else if (type == LEVL_OBJECT_EMPTY) {
        object->data.empty.scale = (Vector3){ 1.0f, 1.0f, 1.0f };
    }
    document->dirty = true;
    return object;
}

bool LevlObjectConvertBoxToMesh(LevlObject *object)
{
    if (!object || object->type != LEVL_OBJECT_BOX) return false;
    LevlBoxData box = object->data.box;
    LevlMeshData mesh;
    MakeCubeMesh(&mesh, box.size, box.rotation, box.faceColors);
    mesh.collidable = box.collidable;
    object->type = LEVL_OBJECT_MESH;
    object->data.mesh = mesh;
    return true;
}

int LevlMeshBuildEdges(const LevlMeshData *mesh, LevlMeshEdge *edges, int maxEdges)
{
    if (!mesh || !edges || maxEdges <= 0) return 0;
    int count = 0;
    for (int faceIndex = 0; faceIndex < mesh->faceCount; ++faceIndex) {
        const LevlMeshFace *face = &mesh->faces[faceIndex];
        if (face->vertexCount < 2 || face->vertexCount > LEVL_MESH_FACE_MAX_VERTICES) continue;
        for (int i = 0; i < face->vertexCount; ++i) {
            unsigned short a = face->vertices[i];
            unsigned short b = face->vertices[(i + 1) % face->vertexCount];
            if (a > b) { unsigned short swap = a; a = b; b = swap; }
            bool exists = false;
            for (int edge = 0; edge < count; ++edge) if (edges[edge].a == a && edges[edge].b == b) { exists = true; break; }
            if (!exists && count < maxEdges) edges[count++] = (LevlMeshEdge){ a, b };
        }
    }
    return count;
}

BoundingBox LevlMeshGetBounds(const LevlObject *object)
{
    BoundingBox bounds = { 0 };
    if (!object || object->type != LEVL_OBJECT_MESH || object->data.mesh.vertexCount <= 0) return bounds;
    Vector3 first = Vector3Add(object->position, object->data.mesh.vertices[0]);
    bounds.min = first;
    bounds.max = first;
    for (int i = 1; i < object->data.mesh.vertexCount; ++i) {
        Vector3 vertex = Vector3Add(object->position, object->data.mesh.vertices[i]);
        if (vertex.x < bounds.min.x) bounds.min.x = vertex.x;
        if (vertex.y < bounds.min.y) bounds.min.y = vertex.y;
        if (vertex.z < bounds.min.z) bounds.min.z = vertex.z;
        if (vertex.x > bounds.max.x) bounds.max.x = vertex.x;
        if (vertex.y > bounds.max.y) bounds.max.y = vertex.y;
        if (vertex.z > bounds.max.z) bounds.max.z = vertex.z;
    }
    return bounds;
}

static LevlObject *FindObjectById(LevlDocument *document, int id)
{
    if (!document || id <= 0) return NULL;
    for (int i = 0; i < document->objectCount; ++i) if (document->objects[i].id == id) return &document->objects[i];
    return NULL;
}

static const LevlObject *FindObjectByIdConst(const LevlDocument *document, int id)
{
    return FindObjectById((LevlDocument *)document, id);
}

static void LocalTransform(const LevlObject *object, Vector3 *position, Vector3 *rotation, Vector3 *scale)
{
    *position = object->position;
    *rotation = (Vector3){ 0 };
    *scale = (Vector3){ 1.0f, 1.0f, 1.0f };
    if (object->type == LEVL_OBJECT_MODEL) {
        *rotation = object->data.model.rotation;
        *scale = object->data.model.scale;
    } else if (object->type == LEVL_OBJECT_EMPTY) {
        *rotation = object->data.empty.rotation;
        *scale = object->data.empty.scale;
    } else if (object->type == LEVL_OBJECT_BOX) {
        *rotation = object->data.box.rotation;
    }
}

bool LevlObjectGetWorldTransform(const LevlDocument *document, const LevlObject *object,
                                Vector3 *position, Vector3 *rotation, Vector3 *scale)
{
    if (!document || !object || !position || !rotation || !scale) return false;
    LocalTransform(object, position, rotation, scale);
    const LevlObject *parent = FindObjectByIdConst(document, object->parentId);
    if (!parent || parent == object) return true;
    Vector3 parentPosition, parentRotation, parentScale;
    LevlObjectGetWorldTransform(document, parent, &parentPosition, &parentRotation, &parentScale);
    Matrix parentRotationMatrix = MatrixRotateXYZ(Vector3Scale(parentRotation, DEG2RAD));
    Vector3 scaledLocal = Vector3Multiply(*position, parentScale);
    *position = Vector3Add(parentPosition, Vector3Transform(scaledLocal, parentRotationMatrix));
    *rotation = Vector3Add(parentRotation, *rotation);
    *scale = Vector3Multiply(parentScale, *scale);
    return true;
}

static bool IsDescendant(const LevlDocument *document, const LevlObject *candidate, int ancestorId)
{
    int parentId = candidate ? candidate->parentId : 0;
    for (int depth = 0; depth < LEVL_MAX_OBJECTS && parentId > 0; ++depth) {
        if (parentId == ancestorId) return true;
        const LevlObject *parent = FindObjectByIdConst(document, parentId);
        if (!parent) break;
        parentId = parent->parentId;
    }
    return false;
}

bool LevlObjectSetParent(LevlDocument *document, LevlObject *child, LevlObject *parent)
{
    if (!document || !child || child == parent || (parent && IsDescendant(document, parent, child->id))) return false;
    Vector3 worldPosition, worldRotation, worldScale;
    LevlObjectGetWorldTransform(document, child, &worldPosition, &worldRotation, &worldScale);
    child->parentId = parent ? parent->id : 0;
    if (!parent) {
        child->position = worldPosition;
        if (child->type == LEVL_OBJECT_BOX) child->data.box.rotation = worldRotation;
        if (child->type == LEVL_OBJECT_MODEL) { child->data.model.rotation = worldRotation; child->data.model.scale = worldScale; }
        if (child->type == LEVL_OBJECT_EMPTY) { child->data.empty.rotation = worldRotation; child->data.empty.scale = worldScale; }
    } else {
        Vector3 parentPosition, parentRotation, parentScale;
        LevlObjectGetWorldTransform(document, parent, &parentPosition, &parentRotation, &parentScale);
        Matrix inverseRotation = MatrixInvert(MatrixRotateXYZ(Vector3Scale(parentRotation, DEG2RAD)));
        child->position = Vector3Multiply(Vector3Transform(Vector3Subtract(worldPosition, parentPosition), inverseRotation),
                                          (Vector3){ 1.0f / fmaxf(parentScale.x, 0.0001f), 1.0f / fmaxf(parentScale.y, 0.0001f), 1.0f / fmaxf(parentScale.z, 0.0001f) });
        if (child->type == LEVL_OBJECT_BOX) child->data.box.rotation = Vector3Subtract(worldRotation, parentRotation);
        if (child->type == LEVL_OBJECT_MODEL) {
            child->data.model.rotation = Vector3Subtract(worldRotation, parentRotation);
            child->data.model.scale = Vector3Divide(worldScale, parentScale);
        }
        if (child->type == LEVL_OBJECT_EMPTY) {
            child->data.empty.rotation = Vector3Subtract(worldRotation, parentRotation);
            child->data.empty.scale = Vector3Divide(worldScale, parentScale);
        }
    }
    document->dirty = true;
    return true;
}

void LevlDocumentBakeHierarchy(LevlDocument *document)
{
    if (!document) return;
    for (int i = 0; i < document->objectCount; ++i) {
        LevlObject *object = &document->objects[i];
        if (object->parentId == 0) continue;
        Vector3 position, rotation, scale;
        LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
        object->position = position;
        if (object->type == LEVL_OBJECT_BOX) object->data.box.rotation = rotation;
        if (object->type == LEVL_OBJECT_MODEL) { object->data.model.rotation = rotation; object->data.model.scale = scale; }
        if (object->type == LEVL_OBJECT_EMPTY) { object->data.empty.rotation = rotation; object->data.empty.scale = scale; }
        object->parentId = 0;
    }
}

static bool PointInsideMesh(const LevlDocument *document, const LevlObject *object, Vector3 point)
{
    const LevlMeshData *mesh = &object->data.mesh;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    Matrix transform = MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD)));
    Vector3 local = Vector3Transform(Vector3Subtract(point, position), MatrixInvert(transform));
    BoundingBox bounds = LevlMeshGetBounds(object);
    bounds.min = Vector3Subtract(bounds.min, object->position);
    bounds.max = Vector3Subtract(bounds.max, object->position);
    if (local.x <= bounds.min.x + 0.001f || local.x >= bounds.max.x - 0.001f ||
        local.y <= bounds.min.y + 0.001f || local.y >= bounds.max.y - 0.001f ||
        local.z <= bounds.min.z + 0.001f || local.z >= bounds.max.z - 0.001f) return false;
    Ray ray = { .position = local, .direction = Vector3Normalize((Vector3){ 1.0f, 0.173f, 0.317f }) };
    int intersections = 0;
    for (int faceIndex = 0; faceIndex < mesh->faceCount; ++faceIndex) {
        const LevlMeshFace *face = &mesh->faces[faceIndex];
        for (int vertex = 1; vertex + 1 < face->vertexCount; ++vertex) {
            RayCollision hit = GetRayCollisionTriangle(ray, mesh->vertices[face->vertices[0]],
                                                        mesh->vertices[face->vertices[vertex]],
                                                        mesh->vertices[face->vertices[vertex + 1]]);
            if (hit.hit && hit.distance > 0.0001f) intersections++;
        }
    }
    return (intersections & 1) != 0;
}

static bool PointInsideModel(const LevlDocument *document, const LevlObject *object, Vector3 point)
{
    char path[512];
    if (object->data.model.assetPath[0] == '/') snprintf(path, sizeof(path), "%s", object->data.model.assetPath);
    else {
        const char *slash = strrchr(document->filePath, '/');
        if (slash) {
            size_t directoryLength = (size_t)(slash - document->filePath);
            if (directoryLength >= sizeof(path)) directoryLength = sizeof(path) - 1;
            memcpy(path, document->filePath, directoryLength);
            path[directoryLength] = '\0';
            snprintf(path + directoryLength, sizeof(path) - directoryLength, "/%s", object->data.model.assetPath);
        } else snprintf(path, sizeof(path), "%s", object->data.model.assetPath);
    }
    Model model = LoadModel(path);
    if (!IsModelValid(model)) return false;
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    Matrix transform = MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z),
                                      MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD)));
    Ray ray = { .position = point, .direction = Vector3Normalize((Vector3){ 1.0f, 0.173f, 0.317f }) };
    int intersections = 0;
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        const Mesh *mesh = &model.meshes[meshIndex];
        for (int triangle = 0; triangle < mesh->triangleCount; ++triangle) {
            int indices[3] = {
                mesh->indices ? mesh->indices[triangle * 3] : triangle * 3,
                mesh->indices ? mesh->indices[triangle * 3 + 1] : triangle * 3 + 1,
                mesh->indices ? mesh->indices[triangle * 3 + 2] : triangle * 3 + 2
            };
            Vector3 vertices[3];
            for (int corner = 0; corner < 3; ++corner) {
                Vector3 local = { mesh->vertices[indices[corner] * 3], mesh->vertices[indices[corner] * 3 + 1], mesh->vertices[indices[corner] * 3 + 2] };
                vertices[corner] = Vector3Add(position, Vector3Transform(local, transform));
            }
            RayCollision hit = GetRayCollisionTriangle(ray, vertices[0], vertices[1], vertices[2]);
            if (hit.hit && hit.distance > 0.0001f) intersections++;
        }
    }
    UnloadModel(model);
    return (intersections & 1) != 0;
}

bool LevlDocumentPlayerSpawnBlocked(const LevlDocument *document, Vector3 *blockedSpawn, bool inspectModelAssets)
{
    if (!document) return false;
    for (int markerIndex = 0; markerIndex < document->objectCount; ++markerIndex) {
        const LevlObject *marker = &document->objects[markerIndex];
        if (marker->type != LEVL_OBJECT_MARKER || strcmp(marker->tag, "player_spawn") != 0) continue;
        for (int objectIndex = 0; objectIndex < document->objectCount; ++objectIndex) {
            const LevlObject *object = &document->objects[objectIndex];
            if (object == marker) continue;
            Vector3 markerPosition, markerRotation, markerScale;
            LevlObjectGetWorldTransform(document, marker, &markerPosition, &markerRotation, &markerScale);
            (void)markerRotation;
            (void)markerScale;
            if (object->type == LEVL_OBJECT_MESH && object->data.mesh.collidable && PointInsideMesh(document, object, markerPosition)) {
                if (blockedSpawn) *blockedSpawn = markerPosition;
                return true;
            }
            if (object->type == LEVL_OBJECT_BOX && object->data.box.collidable) {
                BoundingBox bounds = LevlObjectGetWorldBounds(document, object);
                if (markerPosition.x > bounds.min.x && markerPosition.x < bounds.max.x &&
                    markerPosition.y > bounds.min.y && markerPosition.y < bounds.max.y &&
                    markerPosition.z > bounds.min.z && markerPosition.z < bounds.max.z) {
                    if (blockedSpawn) *blockedSpawn = markerPosition;
                    return true;
                }
            }
            if (inspectModelAssets && object->type == LEVL_OBJECT_MODEL && object->data.model.collidable) {
                if (PointInsideModel(document, object, markerPosition)) {
                    if (blockedSpawn) *blockedSpawn = markerPosition;
                    return true;
                }
                BoundingBox bounds = LevlObjectGetWorldBounds(document, object);
                if (markerPosition.x > bounds.min.x && markerPosition.x < bounds.max.x &&
                    markerPosition.y > bounds.min.y && markerPosition.y < bounds.max.y &&
                    markerPosition.z > bounds.min.z && markerPosition.z < bounds.max.z) {
                    if (blockedSpawn) *blockedSpawn = markerPosition;
                    return true;
                }
            }
        }
    }
    return false;
}

void LevlDocumentNew(LevlDocument *document)
{
    *document = (LevlDocument){ .nextId = 1 };
    CopyString(document->name, sizeof(document->name), "factory_level");
    CopyString(document->filePath, sizeof(document->filePath), "level.levl");

    LevlObject *floor = LevlDocumentAdd(document, LEVL_OBJECT_PLATFORM);
    CopyString(floor->name, sizeof(floor->name), "factory_floor");
    CopyString(floor->tag, sizeof(floor->tag), "ground");
    floor->data.platform.size = (Vector2){ 80.0f, 80.0f };
    floor->data.platform.color = (Color){ 55, 115, 102, 255 };

    LevlObject *spawn = LevlDocumentAdd(document, LEVL_OBJECT_MARKER);
    CopyString(spawn->name, sizeof(spawn->name), "player_start");
    CopyString(spawn->tag, sizeof(spawn->tag), "player_spawn");
    spawn->position = (Vector3){ 0.0f, 0.0f, 6.0f };

    LevlObject *block = LevlDocumentAdd(document, LEVL_OBJECT_BOX);
    CopyString(block->name, sizeof(block->name), "starter_factory");
    CopyString(block->tag, sizeof(block->tag), "factory");
    block->position = (Vector3){ 0.0f, 1.5f, 0.0f };
    block->data.box.size = (Vector3){ 5.0f, 3.0f, 5.0f };
    block->data.box.bevel = 0.18f;
    block->data.box.faceColors[0] = (Color){ 126, 255, 182, 255 };
    block->data.box.faceColors[1] = (Color){ 26, 91, 72, 255 };
    block->data.box.faceColors[2] = (Color){ 49, 196, 137, 255 };
    block->data.box.faceColors[3] = (Color){ 65, 221, 153, 255 };
    block->data.box.faceColors[4] = (Color){ 36, 163, 114, 255 };
    block->data.box.faceColors[5] = (Color){ 40, 177, 124, 255 };
    document->dirty = false;
}

bool LevlDocumentRemove(LevlDocument *document, int index)
{
    if (index < 0 || index >= document->objectCount) return false;
    int removedId = document->objects[index].id;
    if (index + 1 < document->objectCount) {
        memmove(&document->objects[index], &document->objects[index + 1],
                (size_t)(document->objectCount - index - 1) * sizeof(document->objects[0]));
    }
    document->objectCount--;
    for (int child = 0; child < document->objectCount; ++child)
        if (document->objects[child].parentId == removedId) document->objects[child].parentId = 0;
    document->dirty = true;
    return true;
}

static bool WriteDocument(const LevlDocument *document, const char *filePath, char *message, int messageSize)
{
    FILE *file = fopen(filePath, "w");
    if (!file) { SetMessage(message, messageSize, "Could not write .levl file"); return false; }

    fprintf(file, "# Created by Slimeworks Level Editor\n");
    fprintf(file, "SLIMEWORKS_LEVL 1\n");
    fprintf(file, "LEVEL %s\n\n", document->name);
    for (int i = 0; i < document->objectCount; ++i) {
        const LevlObject *object = &document->objects[i];
        switch (object->type) {
            case LEVL_OBJECT_PLATFORM: {
                const LevlPlatformData *data = &object->data.platform;
                fprintf(file, "PLATFORM %s %s %.3f %.3f %.3f %.3f %.3f %d %d %d %d\n", object->name, object->tag,
                        object->position.x, object->position.z, data->size.x, data->size.y, data->top,
                        data->color.r, data->color.g, data->color.b, data->color.a);
            } break;
            case LEVL_OBJECT_RAMP: {
                const LevlRampData *data = &object->data.ramp;
                fprintf(file, "RAMP %s %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f %d %d %d %d\n", object->name, object->tag,
                        object->position.x, object->position.y, object->position.z,
                        data->end.x, data->end.y, data->end.z, data->width,
                        data->color.r, data->color.g, data->color.b, data->color.a);
            } break;
            case LEVL_OBJECT_BOX: {
                const LevlBoxData *data = &object->data.box;
                fprintf(file, "BOX %s %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %d %d\n", object->name, object->tag,
                        object->position.x, object->position.y, object->position.z,
                        data->size.x, data->size.y, data->size.z,
                        data->rotation.x, data->rotation.y, data->rotation.z,
                        data->bevel, data->subdivisions, data->collidable ? 1 : 0);
                fprintf(file, "FACES");
                for (int face = 0; face < 6; ++face) {
                    Color color = data->faceColors[face];
                    fprintf(file, " %d %d %d %d", color.r, color.g, color.b, color.a);
                }
                fprintf(file, "\n");
            } break;
            case LEVL_OBJECT_MESH: {
                const LevlMeshData *data = &object->data.mesh;
                fprintf(file, "MESH %s %s %.3f %.3f %.3f %d %d %d\n", object->name, object->tag,
                        object->position.x, object->position.y, object->position.z, data->collidable ? 1 : 0,
                        data->vertexCount, data->faceCount);
                for (int vertex = 0; vertex < data->vertexCount; ++vertex) {
                    Vector3 value = data->vertices[vertex];
                    fprintf(file, "VERTEX %.3f %.3f %.3f\n", value.x, value.y, value.z);
                }
                for (int face = 0; face < data->faceCount; ++face) {
                    const LevlMeshFace *value = &data->faces[face];
                    fprintf(file, "FACE %d %d %d %d %d %d %d %d %d\n", value->vertexCount,
                            value->vertices[0], value->vertices[1], value->vertices[2], value->vertices[3],
                            value->color.r, value->color.g, value->color.b, value->color.a);
                }
                fprintf(file, "END_MESH\n");
            } break;
            case LEVL_OBJECT_MODEL: {
                const LevlModelData *data = &object->data.model;
                fprintf(file, "MODEL %s %s %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %d %.3f %.3f %.3f\n",
                        object->name, object->tag, data->assetPath,
                        object->position.x, object->position.y, object->position.z,
                        data->rotation.x, data->rotation.y, data->rotation.z,
                        data->scale.x, data->scale.y, data->scale.z, data->collidable ? 1 : 0,
                        data->collisionSize.x, data->collisionSize.y, data->collisionSize.z);
            } break;
            case LEVL_OBJECT_MARKER:
                fprintf(file, "MARKER %s %s %.3f %.3f %.3f\n", object->name, object->tag,
                        object->position.x, object->position.y, object->position.z);
                break;
            case LEVL_OBJECT_EMPTY:
                fprintf(file, "EMPTY %s %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", object->name, object->tag,
                        object->position.x, object->position.y, object->position.z,
                        object->data.empty.rotation.x, object->data.empty.rotation.y, object->data.empty.rotation.z,
                        object->data.empty.scale.x, object->data.empty.scale.y, object->data.empty.scale.z);
                break;
        }
    }
    for (int i = 0; i < document->objectCount; ++i) if (document->objects[i].parentId > 0)
        fprintf(file, "PARENT %d %d\n", document->objects[i].id, document->objects[i].parentId);
    fprintf(file, "END\n");
    fclose(file);
    SetMessage(message, messageSize, "Saved .levl");
    return true;
}

bool LevlDocumentSave(LevlDocument *document, const char *filePath, char *message, int messageSize)
{
    if (!WriteDocument(document, filePath, message, messageSize)) return false;
    CopyString(document->filePath, sizeof(document->filePath), filePath);
    document->dirty = false;
    return true;
}

static void StripLine(char *line)
{
    line[strcspn(line, "\r\n")] = '\0';
}

static bool ReadFaces(const char *line, Color faceColors[6])
{
    char copy[512];
    CopyString(copy, sizeof(copy), line);
    char *token = strtok(copy, " \t");
    if (!token || strcmp(token, "FACES") != 0) return false;
    for (int face = 0; face < 6; ++face) {
        int values[4];
        for (int component = 0; component < 4; ++component) {
            token = strtok(NULL, " \t");
            if (!token) return false;
            values[component] = atoi(token);
            if (values[component] < 0 || values[component] > 255) return false;
        }
        faceColors[face] = (Color){ (unsigned char)values[0], (unsigned char)values[1],
                                   (unsigned char)values[2], (unsigned char)values[3] };
    }
    return true;
}

bool LevlDocumentLoad(LevlDocument *document, const char *filePath, char *message, int messageSize)
{
    FILE *file = fopen(filePath, "r");
    if (!file) { SetMessage(message, messageSize, "Could not open .levl file"); return false; }
    LevlDocument parsed = { .nextId = 1 };
    char line[1024];
    bool headerSeen = false;
    LevlObject *lastBox = NULL;

    while (fgets(line, sizeof(line), file)) {
        StripLine(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        if (!headerSeen) {
            int version = 0;
            if (sscanf(line, "SLIMEWORKS_LEVL %d", &version) != 1 || version != 1) goto parse_error;
            headerSeen = true;
            continue;
        }
        if (strncmp(line, "LEVEL ", 6) == 0) { CopyString(parsed.name, sizeof(parsed.name), line + 6); continue; }
        if (strcmp(line, "END") == 0) break;
        if (strncmp(line, "PARENT ", 7) == 0) {
            int childId, parentId;
            if (sscanf(line, "PARENT %d %d", &childId, &parentId) != 2 || !FindObjectById(&parsed, childId)) goto parse_error;
            LevlObject *child = FindObjectById(&parsed, childId);
            if (parentId != 0 && !FindObjectById(&parsed, parentId)) goto parse_error;
            child->parentId = parentId;
            continue;
        }

        LevlObject *object = NULL;
        if (strncmp(line, "PLATFORM ", 9) == 0) {
            object = LevlDocumentAdd(&parsed, LEVL_OBJECT_PLATFORM);
            LevlPlatformData *data = &object->data.platform;
            int r, g, b, a;
            if (sscanf(line, "PLATFORM %31s %31s %f %f %f %f %f %d %d %d %d", object->name, object->tag,
                       &object->position.x, &object->position.z, &data->size.x, &data->size.y, &data->top,
                       &r, &g, &b, &a) != 11) goto parse_error;
            object->position.y = 0.0f;
            data->color = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
        } else if (strncmp(line, "RAMP ", 5) == 0) {
            object = LevlDocumentAdd(&parsed, LEVL_OBJECT_RAMP);
            LevlRampData *data = &object->data.ramp;
            int r, g, b, a;
            if (sscanf(line, "RAMP %31s %31s %f %f %f %f %f %f %f %d %d %d %d", object->name, object->tag,
                       &object->position.x, &object->position.y, &object->position.z,
                       &data->end.x, &data->end.y, &data->end.z, &data->width,
                       &r, &g, &b, &a) != 13) goto parse_error;
            data->color = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
        } else if (strncmp(line, "BOX ", 4) == 0) {
            object = LevlDocumentAdd(&parsed, LEVL_OBJECT_BOX);
            LevlBoxData *data = &object->data.box;
            int collidable;
            if (sscanf(line, "BOX %31s %31s %f %f %f %f %f %f %f %f %f %f %d %d", object->name, object->tag,
                       &object->position.x, &object->position.y, &object->position.z,
                       &data->size.x, &data->size.y, &data->size.z,
                       &data->rotation.x, &data->rotation.y, &data->rotation.z,
                       &data->bevel, &data->subdivisions, &collidable) != 14) goto parse_error;
            data->collidable = collidable != 0;
            if (data->subdivisions < 1) data->subdivisions = 1;
            if (data->subdivisions > 16) data->subdivisions = 16;
            lastBox = object;
            continue;
        } else if (strncmp(line, "MESH ", 5) == 0) {
            object = LevlDocumentAdd(&parsed, LEVL_OBJECT_MESH);
            LevlMeshData *data = &object->data.mesh;
            int collidable, vertexCount, faceCount;
            if (sscanf(line, "MESH %31s %31s %f %f %f %d %d %d", object->name, object->tag,
                       &object->position.x, &object->position.y, &object->position.z, &collidable, &vertexCount, &faceCount) != 8 ||
                vertexCount < 3 || vertexCount > LEVL_MESH_MAX_VERTICES || faceCount < 1 || faceCount > LEVL_MESH_MAX_FACES) goto parse_error;
            *data = (LevlMeshData){ .vertexCount = vertexCount, .faceCount = faceCount, .collidable = collidable != 0 };
            for (int vertex = 0; vertex < vertexCount; ++vertex) {
                if (!fgets(line, sizeof(line), file)) goto parse_error;
                StripLine(line);
                if (sscanf(line, "VERTEX %f %f %f", &data->vertices[vertex].x, &data->vertices[vertex].y, &data->vertices[vertex].z) != 3) goto parse_error;
            }
            for (int face = 0; face < faceCount; ++face) {
                int count, a, b, c, d, r, g, blue, alpha;
                if (!fgets(line, sizeof(line), file)) goto parse_error;
                StripLine(line);
                if (sscanf(line, "FACE %d %d %d %d %d %d %d %d %d", &count, &a, &b, &c, &d, &r, &g, &blue, &alpha) != 9 ||
                    count < 3 || count > LEVL_MESH_FACE_MAX_VERTICES || a < 0 || b < 0 || c < 0 || d < 0 ||
                    a >= vertexCount || b >= vertexCount || c >= vertexCount || d >= vertexCount ||
                    r < 0 || r > 255 || g < 0 || g > 255 || blue < 0 || blue > 255 || alpha < 0 || alpha > 255) goto parse_error;
                SetMeshFace(&data->faces[face], count, a, b, c, d, (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)blue, (unsigned char)alpha });
            }
            if (!fgets(line, sizeof(line), file)) goto parse_error;
            StripLine(line);
            if (strcmp(line, "END_MESH") != 0) goto parse_error;
            EnsureMeshOutwardWinding(data);
            lastBox = NULL;
            continue;
        } else if (strncmp(line, "FACES ", 6) == 0) {
            if (!lastBox || !ReadFaces(line, lastBox->data.box.faceColors)) goto parse_error;
            continue;
        } else if (strncmp(line, "MODEL ", 6) == 0) {
            object = LevlDocumentAdd(&parsed, LEVL_OBJECT_MODEL);
            LevlModelData *data = &object->data.model;
            int collidable;
            if (sscanf(line, "MODEL %31s %31s %255s %f %f %f %f %f %f %f %f %f %d %f %f %f",
                       object->name, object->tag, data->assetPath,
                       &object->position.x, &object->position.y, &object->position.z,
                       &data->rotation.x, &data->rotation.y, &data->rotation.z,
                       &data->scale.x, &data->scale.y, &data->scale.z, &collidable,
                       &data->collisionSize.x, &data->collisionSize.y, &data->collisionSize.z) != 16) goto parse_error;
            data->collidable = collidable != 0;
        } else if (strncmp(line, "MARKER ", 7) == 0) {
            object = LevlDocumentAdd(&parsed, LEVL_OBJECT_MARKER);
            if (sscanf(line, "MARKER %31s %31s %f %f %f", object->name, object->tag,
                       &object->position.x, &object->position.y, &object->position.z) != 5) goto parse_error;
        } else if (strncmp(line, "EMPTY ", 6) == 0) {
            object = LevlDocumentAdd(&parsed, LEVL_OBJECT_EMPTY);
            if (sscanf(line, "EMPTY %31s %31s %f %f %f %f %f %f %f %f %f", object->name, object->tag,
                       &object->position.x, &object->position.y, &object->position.z,
                       &object->data.empty.rotation.x, &object->data.empty.rotation.y, &object->data.empty.rotation.z,
                       &object->data.empty.scale.x, &object->data.empty.scale.y, &object->data.empty.scale.z) != 11) goto parse_error;
        } else goto parse_error;
        lastBox = NULL;
    }

    fclose(file);
    if (!headerSeen) { SetMessage(message, messageSize, "Missing SLIMEWORKS_LEVL header"); return false; }
    parsed.dirty = false;
    CopyString(parsed.filePath, sizeof(parsed.filePath), filePath);
    *document = parsed;
    SetMessage(message, messageSize, "Loaded .levl");
    return true;

parse_error:
    fclose(file);
    SetMessage(message, messageSize, "Invalid or unsupported .levl file");
    return false;
}

static void GetDirectory(const char *path, char *directory, size_t directorySize)
{
    const char *slash = strrchr(path, '/');
    if (!slash) { CopyString(directory, directorySize, "."); return; }
    size_t length = (size_t)(slash - path);
    if (length >= directorySize) length = directorySize - 1;
    memcpy(directory, path, length);
    directory[length] = '\0';
}

static bool CopyFileContents(const char *sourcePath, const char *destinationPath)
{
    FILE *source = fopen(sourcePath, "rb");
    if (!source) return false;
    FILE *destination = fopen(destinationPath, "wb");
    if (!destination) { fclose(source); return false; }
    unsigned char buffer[16384];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, read, destination) != read) { fclose(source); fclose(destination); return false; }
    }
    fclose(source);
    fclose(destination);
    return true;
}

bool LevlDocumentExportToGame(const LevlDocument *document, const char *gameLevelPath, char *message, int messageSize)
{
    Vector3 blockedSpawn;
    if (LevlDocumentPlayerSpawnBlocked(document, &blockedSpawn, true)) {
        SetMessage(message, messageSize, "Export blocked: player_spawn is inside a collision");
        return false;
    }
    char gameDirectory[LEVL_PATH_LENGTH];
    GetDirectory(gameLevelPath, gameDirectory, sizeof(gameDirectory));
    if (MakeDirectory(gameDirectory) != 0 && !DirectoryExists(gameDirectory)) {
        SetMessage(message, messageSize, "Could not create game's levels directory");
        return false;
    }
    LevlDocument exportDocument = *document;
    LevlDocumentBakeHierarchy(&exportDocument);
    if (!WriteDocument(&exportDocument, gameLevelPath, message, messageSize)) return false;

    char sourceDirectory[LEVL_PATH_LENGTH], sourceAssets[512], destinationAssets[512];
    GetDirectory(document->filePath, sourceDirectory, sizeof(sourceDirectory));
    snprintf(sourceAssets, sizeof(sourceAssets), "%s/assets", sourceDirectory);
    if (!DirectoryExists(sourceAssets)) snprintf(sourceAssets, sizeof(sourceAssets), "assets");
    snprintf(destinationAssets, sizeof(destinationAssets), "%s/assets", gameDirectory);
    if (DirectoryExists(sourceAssets)) {
        if (MakeDirectory(destinationAssets) != 0 && !DirectoryExists(destinationAssets)) {
            SetMessage(message, messageSize, "Could not create game's assets directory");
            return false;
        }
        FilePathList assets = LoadDirectoryFiles(sourceAssets);
        for (unsigned int i = 0; i < assets.count; ++i) {
            if (!FileExists(assets.paths[i])) continue;
            char destinationPath[512];
            snprintf(destinationPath, sizeof(destinationPath), "%s/%s", destinationAssets, GetFileName(assets.paths[i]));
            if (!CopyFileContents(assets.paths[i], destinationPath)) {
                UnloadDirectoryFiles(assets);
                SetMessage(message, messageSize, "Could not copy an asset to game");
                return false;
            }
        }
        UnloadDirectoryFiles(assets);
    }
    SetMessage(message, messageSize, "Exported to Slimeworks Factory/levels/factory.levl");
    return true;
}

BoundingBox LevlObjectGetBounds(const LevlObject *object)
{
    Vector3 min = object->position, max = object->position;
    if (object->type == LEVL_OBJECT_BOX) {
        Vector3 half = Vector3Scale(object->data.box.size, 0.5f);
        min = Vector3Subtract(object->position, half); max = Vector3Add(object->position, half);
    } else if (object->type == LEVL_OBJECT_MESH) {
        return LevlMeshGetBounds(object);
    } else if (object->type == LEVL_OBJECT_PLATFORM) {
        Vector2 half = Vector2Scale(object->data.platform.size, 0.5f);
        min = (Vector3){ object->position.x - half.x, object->data.platform.top - 2.0f, object->position.z - half.y };
        max = (Vector3){ object->position.x + half.x, object->data.platform.top, object->position.z + half.y };
    } else if (object->type == LEVL_OBJECT_RAMP) {
        Vector3 a = object->position, b = object->data.ramp.end;
        float halfWidth = object->data.ramp.width * 0.5f;
        min = (Vector3){ fminf(a.x, b.x) - halfWidth, fminf(a.y, b.y) - 0.3f, fminf(a.z, b.z) - halfWidth };
        max = (Vector3){ fmaxf(a.x, b.x) + halfWidth, fmaxf(a.y, b.y) + 0.3f, fmaxf(a.z, b.z) + halfWidth };
    } else if (object->type == LEVL_OBJECT_MODEL) {
        Vector3 half = Vector3Scale((Vector3){ object->data.model.collisionSize.x * object->data.model.scale.x,
                                                object->data.model.collisionSize.y * object->data.model.scale.y,
                                                object->data.model.collisionSize.z * object->data.model.scale.z }, 0.5f);
        min = Vector3Subtract(object->position, half); max = Vector3Add(object->position, half);
    } else if (object->type == LEVL_OBJECT_MARKER) {
        min = Vector3Subtract(object->position, (Vector3){ 0.35f, 0.35f, 0.35f });
        max = Vector3Add(object->position, (Vector3){ 0.35f, 0.35f, 0.35f });
    }
    return (BoundingBox){ min, max };
}

BoundingBox LevlObjectGetWorldBounds(const LevlDocument *document, const LevlObject *object)
{
    if (!document || !object) return (BoundingBox){ 0 };
    Vector3 localMin = { -0.25f, -0.25f, -0.25f }, localMax = { 0.25f, 0.25f, 0.25f };
    if (object->type == LEVL_OBJECT_BOX) {
        Vector3 half = Vector3Scale(object->data.box.size, 0.5f);
        localMin = Vector3Negate(half); localMax = half;
    } else if (object->type == LEVL_OBJECT_MESH) {
        BoundingBox bounds = LevlMeshGetBounds(object);
        localMin = Vector3Subtract(bounds.min, object->position);
        localMax = Vector3Subtract(bounds.max, object->position);
    } else if (object->type == LEVL_OBJECT_PLATFORM) {
        Vector2 half = Vector2Scale(object->data.platform.size, 0.5f);
        localMin = (Vector3){ -half.x, -2.0f - object->position.y, -half.y };
        localMax = (Vector3){ half.x, object->data.platform.top - object->position.y, half.y };
    } else if (object->type == LEVL_OBJECT_RAMP) {
        Vector3 end = Vector3Subtract(object->data.ramp.end, object->position);
        float halfWidth = object->data.ramp.width * 0.5f;
        localMin = (Vector3){ fminf(0.0f, end.x) - halfWidth, fminf(0.0f, end.y) - 0.3f, fminf(0.0f, end.z) - halfWidth };
        localMax = (Vector3){ fmaxf(0.0f, end.x) + halfWidth, fmaxf(0.0f, end.y) + 0.3f, fmaxf(0.0f, end.z) + halfWidth };
    } else if (object->type == LEVL_OBJECT_MODEL) {
        Vector3 half = Vector3Scale(object->data.model.collisionSize, 0.5f);
        localMin = Vector3Negate(half); localMax = half;
    } else if (object->type == LEVL_OBJECT_MARKER) {
        localMin = (Vector3){ -0.35f, -0.35f, -0.35f }; localMax = (Vector3){ 0.35f, 1.35f, 0.35f };
    }
    Vector3 position, rotation, scale;
    LevlObjectGetWorldTransform(document, object, &position, &rotation, &scale);
    BoundingBox world = { position, position };
    Matrix rotationMatrix = MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD));
    for (int corner = 0; corner < 8; ++corner) {
        Vector3 local = {
            (corner & 1) ? localMax.x : localMin.x,
            (corner & 2) ? localMax.y : localMin.y,
            (corner & 4) ? localMax.z : localMin.z
        };
        Vector3 point = Vector3Add(position, Vector3Transform(Vector3Multiply(local, scale), rotationMatrix));
        world.min = Vector3Min(world.min, point);
        world.max = Vector3Max(world.max, point);
    }
    return world;
}
