#include <iostream>
#include <glfw3.h>
#include <glfw3native.h>

#include "src/common/IRenderer.h"
#include "src/backend/dx12/DxRenderer.h"
#include "src/common/Utils.h"

void mouse_callback(GLFWwindow *window, double posX, double posY);
void scroll_callback(GLFWwindow *window, double offsetX, double offsetY);
void processInput(GLFWwindow *window);

int main() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize glfw");
    }
    glfwWindowHint(GLFW_RESIZABLE, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(1200, 900, "DirectX12", nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create window!");
    }
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    IRenderer *renderer = new DxRenderer();
    try {
        renderer->init(window);
        renderer->setup();
        while (!glfwWindowShouldClose(window)) {
            renderer->draw();
            processInput(window);
            glfwPollEvents();
        }
        renderer->exit();
    } catch (Exception &e) {
        std::cerr << WStringToAnsi(e.ToString()) << std::endl;
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}

void mouse_callback(GLFWwindow *window, double posX, double posY) {
    Camera* camera = reinterpret_cast<Camera*>(glfwGetWindowUserPointer(window));
    camera->processMouseMovement(posX, posY);
}

void scroll_callback(GLFWwindow *window, double offsetX, double offsetY) {
    Camera* camera = reinterpret_cast<Camera*>(glfwGetWindowUserPointer(window));
    camera->processMouseScroll(offsetY);
}

void processInput(GLFWwindow *window) {
    Camera *camera = reinterpret_cast<Camera *>(glfwGetWindowUserPointer(window));

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera->position += camera->front * 0.1f;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera->position -= camera->front * 0.1f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera->position -= camera->right * 0.1f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera->position += camera->right * 0.1f;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        camera->position -= camera->up * 0.1f;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        camera->position += camera->up * 0.1f;
}