#ifndef CAMERA_H
#define CAMERA_H

#include <vector>
#include <glfw3.h>
#include <glfw3native.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

class Camera {
public:
    glm::vec3 position;
    glm::vec3 front, right, up;

    float pitch, yaw;
    float speed;
    float fov, aspect;

    bool firstMouse = true;
    double prePosX;
    double prePosY;

    Camera() = default;

    Camera(glm::vec3 position, float pitch, float yaw, float aspect, float fov, float speed) 
    : position(position), pitch(pitch), yaw(yaw), aspect(aspect), fov(fov), speed(speed) { 
        updateCamera();
    }

    glm::mat4 getViewMatrix() { 
        return glm::lookAt(position, position + front, up); 
    }

    void processMouseMovement(double posX, double posY) {
        if (firstMouse) {
            prePosX = posX;
            prePosY = posY;
            firstMouse = false;
        }
        double offsetX = (posX - prePosX) * speed;
        double offsetY = (prePosY - posY) * speed;

        prePosX = posX;
        prePosY = posY;

        yaw += offsetX;
        pitch += offsetY;

        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateCamera();
    }

    void processMouseScroll(double offset) {
        fov -= offset;

        if (fov < 1.0f) fov = 1.0f;
        if (fov > 45.0f) fov = 45.0f;
    }

private:
    void updateCamera() {
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

        front = glm::normalize(front);
        right = glm::normalize(glm::cross(front, glm::vec3(0.0, 1.0, 0.0)));
        up    = glm::normalize(glm::cross(right, front));
    }
};
#endif