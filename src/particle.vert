#version 460

layout (location = 0) in vec2 position;

void main (){
    gl_Position = vec4(position.x, position.y, 0.0, 1.0);
    gl_PointSize = 5;
}