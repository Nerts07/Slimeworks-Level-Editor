#ifndef SLIMEWORKS_EDITOR_MODEL_IMPORT_H
#define SLIMEWORKS_EDITOR_MODEL_IMPORT_H

#include "../level/levl.h"

/* Copies a model into assets/. FBX is converted to GLB through Assimp when installed. */
bool ModelImportAdd(LevlDocument *document, const char *sourcePath, char *message, int messageSize);
/* Bakes the selected static model's transform scale into a new OBJ while preserving UV coordinates. */
bool ModelImportApplyScale(LevlObject *object, const Model *model, char *message, int messageSize);

#endif
