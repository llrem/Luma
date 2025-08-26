#pragma once

class IRenderer {
public:
    virtual void init(GLFWwindow* window) = 0;
    virtual void setup() = 0;
    virtual void draw()  = 0;
    virtual void exit()  = 0;
};