#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Mouse and keyboard: pan, zoom, click-to-inspect and the key bindings.
// The window's user pointer must point at the App.
void installInput(GLFWwindow* win);
