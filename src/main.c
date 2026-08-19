#include "editor/editor.h"

int main(void)
{
    Editor editor;
    EditorInit(&editor);
    EditorRun(&editor);
    EditorShutdown(&editor);
    return 0;
}
