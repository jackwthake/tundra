#ifndef SCENE_H
#define SCENE_H


float lerp(float a, float b, float t);
float smoothstep(float t);
float hash2(int x, int y);

float noise2D(float x, float y);
float terrainHeight(float x, float y);

#endif // SCENE_H