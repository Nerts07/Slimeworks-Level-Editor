#include "model_import.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void SetMessage(char *message, int messageSize, const char *text)
{
    if (message && messageSize > 0) snprintf(message, (size_t)messageSize, "%s", text);
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

static bool ShellQuote(char *output, size_t outputSize, const char *input)
{
    size_t used = 0;
    if (outputSize < 3) return false;
    output[used++] = '\'';
    for (const char *cursor = input; *cursor; ++cursor) {
        const char *replacement = *cursor == '\'' ? "'\\''" : NULL;
        if (replacement) {
            size_t length = strlen(replacement);
            if (used + length + 2 > outputSize) return false;
            memcpy(output + used, replacement, length);
            used += length;
        } else {
            if (used + 2 > outputSize) return false;
            output[used++] = *cursor;
        }
    }
    output[used++] = '\'';
    output[used] = '\0';
    return true;
}

static void MakeIdentifier(char *destination, size_t destinationSize, const char *source)
{
    size_t written = 0;
    for (const char *cursor = source; *cursor && written + 1 < destinationSize; ++cursor) {
        unsigned char c = (unsigned char)*cursor;
        destination[written++] = (isalnum(c) || c == '_') ? (char)c : '_';
    }
    if (written == 0 && destinationSize > 1) destination[written++] = 'm';
    destination[written] = '\0';
}

static bool IsDirectlySupportedExtension(const char *extension)
{
    return strcasecmp(extension, ".obj") == 0 || strcasecmp(extension, ".glb") == 0 ||
           strcasecmp(extension, ".gltf") == 0 || strcasecmp(extension, ".iqm") == 0 ||
           strcasecmp(extension, ".vox") == 0;
}

bool ModelImportAdd(LevlDocument *document, const char *sourcePath, char *message, int messageSize)
{
    if (!FileExists(sourcePath)) { SetMessage(message, messageSize, "Model file was not found"); return false; }
    const char *extension = GetFileExtension(sourcePath);
    bool isFbx = strcasecmp(extension, ".fbx") == 0;
    if (!isFbx && !IsDirectlySupportedExtension(extension)) {
        SetMessage(message, messageSize, "Use OBJ, GLB, GLTF, IQM, VOX, or FBX");
        return false;
    }
    if (MakeDirectory("assets") != 0 && !DirectoryExists("assets")) {
        SetMessage(message, messageSize, "Could not create assets directory");
        return false;
    }

    const char *baseName = GetFileNameWithoutExt(sourcePath);
    char identifier[LEVL_NAME_LENGTH];
    MakeIdentifier(identifier, sizeof(identifier), baseName);
    char assetPath[LEVL_PATH_LENGTH];
    snprintf(assetPath, sizeof(assetPath), "assets/%s%s", identifier, isFbx ? ".glb" : extension);

    if (isFbx) {
        if (system("command -v assimp >/dev/null 2>&1") != 0) {
            SetMessage(message, messageSize, "FBX needs Assimp: brew install assimp");
            return false;
        }
        char quotedSource[768], quotedTarget[768], command[1600];
        if (!ShellQuote(quotedSource, sizeof(quotedSource), sourcePath) ||
            !ShellQuote(quotedTarget, sizeof(quotedTarget), assetPath)) {
            SetMessage(message, messageSize, "Model path is too long");
            return false;
        }
        snprintf(command, sizeof(command), "assimp export %s %s", quotedSource, quotedTarget);
        if (system(command) != 0 || !FileExists(assetPath)) {
            SetMessage(message, messageSize, "Assimp could not convert the FBX file");
            return false;
        }
    } else if (strcmp(sourcePath, assetPath) != 0 && !CopyFileContents(sourcePath, assetPath)) {
        SetMessage(message, messageSize, "Could not copy model into assets");
        return false;
    }

    Model preview = LoadModel(assetPath);
    if (!IsModelValid(preview)) {
        SetMessage(message, messageSize, "raylib could not load this model");
        return false;
    }
    BoundingBox bounds = GetModelBoundingBox(preview);
    Vector3 collisionSize = { bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z };
    UnloadModel(preview);

    LevlObject *object = LevlDocumentAdd(document, LEVL_OBJECT_MODEL);
    if (!object) { SetMessage(message, messageSize, "Level object limit reached"); return false; }
    snprintf(object->name, sizeof(object->name), "%s_%d", identifier, object->id);
    snprintf(object->tag, sizeof(object->tag), "prop");
    snprintf(object->data.model.assetPath, sizeof(object->data.model.assetPath), "%s", assetPath);
    object->data.model.collisionSize = (Vector3){ fmaxf(collisionSize.x, 0.05f), fmaxf(collisionSize.y, 0.05f), fmaxf(collisionSize.z, 0.05f) };
    object->position.y = object->data.model.collisionSize.y * 0.5f;
    SetMessage(message, messageSize, isFbx ? "FBX converted to GLB and added" : "Model copied into assets and added");
    return true;
}
