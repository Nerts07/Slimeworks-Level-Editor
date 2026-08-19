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

static bool WriteMtl(const Model *model, const char *baseName, char *message, int messageSize)
{
    char mtlPath[LEVL_PATH_LENGTH];
    snprintf(mtlPath, sizeof(mtlPath), "assets/%s.mtl", baseName);
    FILE *file = fopen(mtlPath, "w");
    if (!file) { SetMessage(message, messageSize, "Could not write baked material file"); return false; }
    for (int materialIndex = 0; materialIndex < model->materialCount; ++materialIndex) {
        const Material *material = &model->materials[materialIndex];
        Color color = material->maps[MATERIAL_MAP_ALBEDO].color;
        fprintf(file, "newmtl material_%d\nKd %.4f %.4f %.4f\n", materialIndex,
                color.r / 255.0f, color.g / 255.0f, color.b / 255.0f);
        Texture2D texture = material->maps[MATERIAL_MAP_ALBEDO].texture;
        if (texture.id != 0) {
            char textureName[LEVL_NAME_LENGTH + 32], texturePath[LEVL_PATH_LENGTH];
            snprintf(textureName, sizeof(textureName), "%s_%d.png", baseName, materialIndex);
            snprintf(texturePath, sizeof(texturePath), "assets/%s", textureName);
            Image image = LoadImageFromTexture(texture);
            if (!image.data || !ExportImage(image, texturePath)) {
                if (image.data) UnloadImage(image);
                fclose(file);
                SetMessage(message, messageSize, "Could not export a model texture while applying scale");
                return false;
            }
            UnloadImage(image);
            fprintf(file, "map_Kd %s\n", textureName);
        }
        fprintf(file, "\n");
    }
    fclose(file);
    return true;
}

static void WriteFaceVertex(FILE *file, int vertexIndex, int vertexOffset, int texcoordOffset, int normalOffset,
                            bool hasTexcoords, bool hasNormals)
{
    int index = vertexIndex + vertexOffset;
    if (hasTexcoords && hasNormals) fprintf(file, "%d/%d/%d", index, vertexIndex + texcoordOffset, vertexIndex + normalOffset);
    else if (hasTexcoords) fprintf(file, "%d/%d", index, vertexIndex + texcoordOffset);
    else if (hasNormals) fprintf(file, "%d//%d", index, vertexIndex + normalOffset);
    else fprintf(file, "%d", index);
}

bool ModelImportApplyScale(LevlObject *object, const Model *model, char *message, int messageSize)
{
    if (!object || object->type != LEVL_OBJECT_MODEL || !model || !IsModelValid(*model)) {
        SetMessage(message, messageSize, "Apply Scale is available for a loaded model only");
        return false;
    }
    Vector3 scale = object->data.model.scale;
    if (fabsf(scale.x - 1.0f) < 0.0001f && fabsf(scale.y - 1.0f) < 0.0001f && fabsf(scale.z - 1.0f) < 0.0001f) {
        SetMessage(message, messageSize, "Scale is already applied");
        return true;
    }
    if (MakeDirectory("assets") != 0 && !DirectoryExists("assets")) {
        SetMessage(message, messageSize, "Could not create assets directory");
        return false;
    }

    char baseName[LEVL_NAME_LENGTH + 16], objPath[LEVL_PATH_LENGTH];
    snprintf(baseName, sizeof(baseName), "%s_applied", object->name);
    snprintf(objPath, sizeof(objPath), "assets/%s.obj", baseName);
    if (!WriteMtl(model, baseName, message, messageSize)) return false;

    FILE *file = fopen(objPath, "w");
    if (!file) { SetMessage(message, messageSize, "Could not write baked OBJ model"); return false; }
    fprintf(file, "# Scale baked by Slimeworks Level Editor\nmtllib %s.mtl\n", baseName);
    for (int meshIndex = 0; meshIndex < model->meshCount; ++meshIndex) {
        const Mesh *mesh = &model->meshes[meshIndex];
        if (!mesh->vertices) { fclose(file); SetMessage(message, messageSize, "Model mesh has no editable vertex data"); return false; }
        for (int vertex = 0; vertex < mesh->vertexCount; ++vertex) {
            const float *v = &mesh->vertices[vertex * 3];
            fprintf(file, "v %.8f %.8f %.8f\n", v[0] * scale.x, v[1] * scale.y, v[2] * scale.z);
        }
    }
    for (int meshIndex = 0; meshIndex < model->meshCount; ++meshIndex) {
        const Mesh *mesh = &model->meshes[meshIndex];
        if (!mesh->texcoords) continue;
        for (int vertex = 0; vertex < mesh->vertexCount; ++vertex) {
            const float *uv = &mesh->texcoords[vertex * 2];
            fprintf(file, "vt %.8f %.8f\n", uv[0], uv[1]);
        }
    }
    for (int meshIndex = 0; meshIndex < model->meshCount; ++meshIndex) {
        const Mesh *mesh = &model->meshes[meshIndex];
        if (!mesh->normals) continue;
        for (int vertex = 0; vertex < mesh->vertexCount; ++vertex) {
            const float *n = &mesh->normals[vertex * 3];
            Vector3 normal = { n[0] / scale.x, n[1] / scale.y, n[2] / scale.z };
            float length = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (length > 0.00001f) { normal.x /= length; normal.y /= length; normal.z /= length; }
            fprintf(file, "vn %.8f %.8f %.8f\n", normal.x, normal.y, normal.z);
        }
    }

    int vertexOffset = 1, texcoordOffset = 1, normalOffset = 1;
    for (int meshIndex = 0; meshIndex < model->meshCount; ++meshIndex) {
        const Mesh *mesh = &model->meshes[meshIndex];
        int materialIndex = model->meshMaterial ? model->meshMaterial[meshIndex] : 0;
        fprintf(file, "g mesh_%d\nusemtl material_%d\n", meshIndex, materialIndex);
        bool hasTexcoords = mesh->texcoords != NULL, hasNormals = mesh->normals != NULL;
        for (int triangle = 0; triangle < mesh->triangleCount; ++triangle) {
            int indices[3];
            for (int corner = 0; corner < 3; ++corner) indices[corner] = mesh->indices ? mesh->indices[triangle * 3 + corner] : triangle * 3 + corner;
            fprintf(file, "f ");
            for (int corner = 0; corner < 3; ++corner) {
                WriteFaceVertex(file, indices[corner], vertexOffset, texcoordOffset, normalOffset, hasTexcoords, hasNormals);
                fputc(corner == 2 ? '\n' : ' ', file);
            }
        }
        vertexOffset += mesh->vertexCount;
        if (hasTexcoords) texcoordOffset += mesh->vertexCount;
        if (hasNormals) normalOffset += mesh->vertexCount;
    }
    fclose(file);

    snprintf(object->data.model.assetPath, sizeof(object->data.model.assetPath), "%s", objPath);
    object->data.model.collisionSize.x *= scale.x;
    object->data.model.collisionSize.y *= scale.y;
    object->data.model.collisionSize.z *= scale.z;
    object->data.model.scale = (Vector3){ 1.0f, 1.0f, 1.0f };
    SetMessage(message, messageSize, "Scale baked into assets/<model>_applied.obj");
    return true;
}
