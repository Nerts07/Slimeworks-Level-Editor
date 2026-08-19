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

const char *LevlObjectTypeName(LevlObjectType type)
{
    switch (type) {
        case LEVL_OBJECT_BOX: return "Box";
        case LEVL_OBJECT_PLATFORM: return "Platform";
        case LEVL_OBJECT_RAMP: return "Ramp";
        case LEVL_OBJECT_MODEL: return "Model";
        case LEVL_OBJECT_MARKER: return "Marker";
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
    }
    document->dirty = true;
    return object;
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
    if (index + 1 < document->objectCount) {
        memmove(&document->objects[index], &document->objects[index + 1],
                (size_t)(document->objectCount - index - 1) * sizeof(document->objects[0]));
    }
    document->objectCount--;
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
        }
    }
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
    char gameDirectory[LEVL_PATH_LENGTH];
    GetDirectory(gameLevelPath, gameDirectory, sizeof(gameDirectory));
    if (MakeDirectory(gameDirectory) != 0 && !DirectoryExists(gameDirectory)) {
        SetMessage(message, messageSize, "Could not create game's levels directory");
        return false;
    }
    if (!WriteDocument(document, gameLevelPath, message, messageSize)) return false;

    char sourceDirectory[LEVL_PATH_LENGTH];
    GetDirectory(document->filePath, sourceDirectory, sizeof(sourceDirectory));
    for (int i = 0; i < document->objectCount; ++i) {
        const LevlObject *object = &document->objects[i];
        if (object->type != LEVL_OBJECT_MODEL) continue;
        const char *asset = object->data.model.assetPath;
        char sourcePath[512], destinationPath[512], destinationAssets[512];
        snprintf(sourcePath, sizeof(sourcePath), "%s/%s", sourceDirectory, asset);
        if (!FileExists(sourcePath)) snprintf(sourcePath, sizeof(sourcePath), "%s", asset);
        snprintf(destinationPath, sizeof(destinationPath), "%s/%s", gameDirectory, asset);
        snprintf(destinationAssets, sizeof(destinationAssets), "%s/assets", gameDirectory);
        if (MakeDirectory(destinationAssets) != 0 && !DirectoryExists(destinationAssets)) {
            SetMessage(message, messageSize, "Could not create game's assets directory");
            return false;
        }
        if (!CopyFileContents(sourcePath, destinationPath)) {
            SetMessage(message, messageSize, "Could not copy a model asset to game");
            return false;
        }
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
