#include <math.h>

// Simple 1D interpolation
float lerp(float a, float b, float t) {
  return a + t * (b - a);
}

// Smooth step function for better interpolation
float smoothstep(float t) {
  return t * t * (3.0f - 2.0f * t);
}

// Hash function to get pseudo-random gradients
float hash2(int x, int y) {
  int n = x + y * 57;
  n = (n << 13) ^ n;
  return (1.0f - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

// Simple 2D Perlin-like noise (using a hash for gradients)
float noise2D(float x, float y) {
  // Grid coordinates
  int xi = (int)x;
  int yi = (int)y;
  
  // Fractional parts
  float xf = x - (float)xi;
  float yf = y - (float)yi;
  
  // Hash function to get pseudo-random gradients at grid points
  // (This is simplified - you'd want better hashing in practice)
  float a = hash2(xi, yi);
  float b = hash2(xi + 1, yi);
  float c = hash2(xi, yi + 1);
  float d = hash2(xi + 1, yi + 1);
  
  // Interpolate
  float i1 = lerp(a, b, smoothstep(xf));
  float i2 = lerp(c, d, smoothstep(xf));
  return lerp(i1, i2, smoothstep(yf));
}

// Fractal Brownian Motion (fBm) for generating terrain
static float fbm(float x, float y, int octaves) {
  float value = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float maxValue = 0.0f;  // For normalizing
  
  for (int i = 0; i < octaves; i++) {
    value += noise2D(x * frequency, y * frequency) * amplitude;
    maxValue += amplitude;
    
    amplitude *= 0.5f;  // Each octave has half the amplitude
    frequency *= 2.0f;  // Each octave has double the frequency
  }
  
  return value / maxValue;  // Normalize to [-1, 1]
}

// Ridge noise function to create sharp features
static float ridgeNoise(float x, float y) {
  float n = fbm(x, y, 4);
  return 1.0f - fabsf(n);  // Creates ridges by inverting absolute value
}

// Terrain height function combining fBm and ridge noise
float terrainHeight(float x, float y) {
  // Base terrain with rolling hills
  float base = fbm(x * 0.01f, y * 0.01f, 6) * 5.0f;

  // Add ridges for cliffs
  float ridges = ridgeNoise(x * 0.005f, y * 0.005f) * 3.0f;

  // Combine them (don't square ridges - too extreme)
  return base + ridges;
}