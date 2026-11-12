#ifndef TUBELIVIANO_ENGINE_PROCESS_H
#define TUBELIVIANO_ENGINE_PROCESS_H
#include "engine.h"
#define ENGINE_COMMAND_CAP 32768
/* Private process helpers shared by the one-shot and reusable extractors. */
BOOL engine_command_arg(WCHAR *command, size_t *used, const WCHAR *argument);
WCHAR *engine_child_environment(void);
#endif
