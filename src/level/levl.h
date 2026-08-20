#ifndef SLIMEWORKS_EDITOR_LEVL_H
#define SLIMEWORKS_EDITOR_LEVL_H

#include <raylib.h>
#include <stdbool.h>

#define LEVL_MAX_OBJECTS 256
#define LEVL_NAME_LENGTH 32
#define LEVL_TAG_LENGTH 32
#define LEVL_PATH_LENGTH 256
#define LEVL_MESH_MAX_VERTICES 128
#define LEVL_MESH_MAX_FACES 128
#define LEVL_MESH_FACE_MAX_VERTICES 4

typedef enum LevlObjectType {
    LEVL_OBJECT_BOX,
    LEVL_OBJECT_MESH,
    LEVL_OBJECT_PLATFORM,
    LEVL_OBJECT_RAMP,
    LEVL_OBJECT_MODEL,
    LEVL_OBJECT_MARKER,
    LEVL_OBJECT_EMPTY
} LevlObjectType;

typedef struct LevlBoxData {
    Vector3 size;
    Vector3 rotation;
    Color faceColors[6];
    float bevel;
    int subdivisions;
    bool collidable;
} LevlBoxData;

typedef struct LevlMeshFace {
    unsigned char vertexCount;
    unsigned short vertices[LEVL_MESH_FACE_MAX_VERTICES];
    Color color;
} LevlMeshFace;

typedef struct LevlMeshData {
    Vector3 vertices[LEVL_MESH_MAX_VERTICES];
    int vertexCount;
    LevlMeshFace faces[LEVL_MESH_MAX_FACES];
    int faceCount;
    bool collidable;
} LevlMeshData;

typedef struct LevlMeshEdge {
    unsigned short a;
    unsigned short b;
} LevlMeshEdge;

typedef struct LevlPlatformData {
    Vector2 size;
    float top;
    Color color;
} LevlPlatformData;

typedef struct LevlRampData {
    Vector3 end;
    float width;
    Color color;
} LevlRampData;

typedef struct LevlModelData {
    char assetPath[LEVL_PATH_LENGTH];
    Vector3 rotation;
    Vector3 scale;
    Vector3 collisionSize;
    bool collidable;
} LevlModelData;

typedef struct LevlEmptyData {
    Vector3 rotation;
    Vector3 scale;
} LevlEmptyData;

typedef struct LevlObject {
    int id;
    LevlObjectType type;
    int parentId;
    char name[LEVL_NAME_LENGTH];
    char tag[LEVL_TAG_LENGTH];
    Vector3 position;
    union {
        LevlBoxData box;
        LevlMeshData mesh;
        LevlPlatformData platform;
        LevlRampData ramp;
        LevlModelData model;
        LevlEmptyData empty;
    } data;
} LevlObject;

typedef struct LevlDocument {
    char name[LEVL_NAME_LENGTH];
    char filePath[LEVL_PATH_LENGTH];
    LevlObject objects[LEVL_MAX_OBJECTS];
    int objectCount;
    int nextId;
    bool dirty;
} LevlDocument;

void LevlDocumentNew(LevlDocument *document);
LevlObject *LevlDocumentAdd(LevlDocument *document, LevlObjectType type);
bool LevlDocumentRemove(LevlDocument *document, int index);
bool LevlDocumentSave(LevlDocument *document, const char *filePath, char *message, int messageSize);
bool LevlDocumentLoad(LevlDocument *document, const char *filePath, char *message, int messageSize);
bool LevlDocumentExportToGame(const LevlDocument *document, const char *gameLevelPath, char *message, int messageSize);
BoundingBox LevlObjectGetBounds(const LevlObject *object);
const char *LevlObjectTypeName(LevlObjectType type);
bool LevlObjectConvertBoxToMesh(LevlObject *object);
int LevlMeshBuildEdges(const LevlMeshData *mesh, LevlMeshEdge *edges, int maxEdges);
BoundingBox LevlMeshGetBounds(const LevlObject *object);
bool LevlDocumentPlayerSpawnBlocked(const LevlDocument *document, Vector3 *blockedSpawn, bool inspectModelAssets);
bool LevlObjectGetWorldTransform(const LevlDocument *document, const LevlObject *object,
                                 Vector3 *position, Vector3 *rotation, Vector3 *scale);
bool LevlObjectSetParent(LevlDocument *document, LevlObject *child, LevlObject *parent);
void LevlDocumentBakeHierarchy(LevlDocument *document);

#endif
