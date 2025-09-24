#include "scene.h"

#include <math.h>
#include <stdlib.h>

#include <shader-works/maths.h>

float map_range(float value, float old_min, float old_max, float new_min, float new_max) {
  return new_min + (value - old_min) * (new_max - new_min) /
  (old_max - old_min);
}

// Simple 1D interpolation
float lerp(float a, float b, float t) {
  return a + t * (b - a);
}

// Smooth step function for better interpolation
float smoothstep(float t) {
  return t * t * (3.0f - 2.0f * t);
}

// Hash function to get pseudo-random gradients with seed
float hash2(int x, int y, int seed) {
  int n = x + y * 57 + seed * 2654435761;
  n = (n << 13) ^ n;
  return (1.0f - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

// Simple 2D Perlin-like noise (using a hash for gradients) with seed
float noise2D(float x, float y, int seed) {
  // Grid coordinates
  int xi = (int)x;
  int yi = (int)y;
  
  // Fractional parts
  float xf = x - (float)xi;
  float yf = y - (float)yi;
  
  // Hash function to get pseudo-random gradients at grid points
  // (This is simplified - you'd want better hashing in practice)
  float a = hash2(xi, yi, seed);
  float b = hash2(xi + 1, yi, seed);
  float c = hash2(xi, yi + 1, seed);
  float d = hash2(xi + 1, yi + 1, seed);
  
  // Interpolate
  float i1 = lerp(a, b, smoothstep(xf));
  float i2 = lerp(c, d, smoothstep(xf));
  return lerp(i1, i2, smoothstep(yf));
}

// Fractal Brownian Motion (fBm) for generating terrain with seed
static float fbm(float x, float y, int octaves, int seed) {
  float value = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float maxValue = 0.0f;  // For normalizing
  
  for (int i = 0; i < octaves; i++) {
    value += noise2D(x * frequency, y * frequency, seed + i) * amplitude;
    maxValue += amplitude;
    
    amplitude *= 0.5f;  // Each octave has half the amplitude
    frequency *= 2.0f;  // Each octave has double the frequency
  }
  
  return value / maxValue;  // Normalize to [-1, 1]
}

// Ridge noise function to create sharp features with seed
static float ridgeNoise(float x, float y, int seed) {
  float n = fbm(x, y, 4, seed);
  return 1.0f - fabsf(n);  // Creates ridges by inverting absolute value
}

// Terrain height function combining fBm and ridge noise with seed
float terrainHeight(float x, float y, int seed) {
  // Base terrain with rolling hills
  float base = fbm(x * 0.01f, y * 0.01f, 6, WORLD_SEED + seed) * 5.0f;
  
  // Add ridges for cliffs
  float ridges = ridgeNoise(x * 0.005f, y * 0.005f, WORLD_SEED) * 3.0f;
  
  // Combine them (don't square ridges - too extreme)
  return base + ridges;
}

// Generate a procedural tree generation
int generate_tree(model_t *model, float base_radius, float base_angle, float3 base_position, float branch_chance, usize max_branches, usize level, usize num_levels) {
  if (!model) return -1;
  if (level == num_levels) return 0; // max levels
  
  if (level == 0) {
    base_position.y -= 1.0f;
  } else {
    base_position.y -= 0.2f;
  }
  
  float trunk_height = (level == 0) ? 8.0f : (2.0f + ridgeNoise(base_position.x + level, base_position.y + level, WORLD_SEED) * 0.5f);
  
  float taper_factor = 0.9f;  // Minimal tapering for thick branches
  if (level == num_levels - 1) {
    taper_factor = 0.0f; // Final branches taper to a point
  }
  float top_radius = base_radius * taper_factor;
  
  // Calculate the growth direction for this cylinder segment
  // For level 0 (main trunk), use slight variation from vertical
  // For branches, use the base_angle as the growth direction
  float growth_angle;
  if (level == 0) {
    growth_angle = base_angle + ((hash2((int)base_position.x, (int)base_position.z, WORLD_SEED)) * 0.2f);
  } else {
    growth_angle = base_angle; // Use the angle passed from parent for branching direction
  }
  
  float3 top_center;
  if (level == 0) {
    // Main trunk: mostly vertical with slight lean
    float trunk_lean = 0.1f;
    top_center = make_float3(
      base_position.x + sinf(growth_angle) * trunk_height * trunk_lean,
      base_position.y + trunk_height,
      base_position.z + cosf(growth_angle) * trunk_height * trunk_lean
    );
  } else {
    // Branches: spread outward and curve upward gradually
    float spread_factor = 0.8f + (float)level * 0.1f;
    float upward_factor = 0.3f + (float)level * 0.15f;
    
    top_center = make_float3(
      base_position.x + sinf(growth_angle) * trunk_height * spread_factor,
      base_position.y + trunk_height * upward_factor,
      base_position.z + cosf(growth_angle) * trunk_height * spread_factor
    );
  }
  
  // Generate this cylinder segment with consistent face alignment
  // Use base_angle for both top and bottom to keep faces aligned with parent
  float bottom_angle_offset = base_angle;
  float top_angle_offset = base_angle; // Keep same angle for perfect face alignment
  // Generate cylinder with caps for middle segments, no caps for final pointed segments
  int ret = generate_tree_cylinder(model, base_radius, top_radius, trunk_height, base_position, top_center, 8, bottom_angle_offset, top_angle_offset);
  
  // For middle segments (not final level), ensure they have visible caps by adding small cap cylinders if needed
  if (level < num_levels - 1 && top_radius > 0.01f) {
    // The existing cylinder function already handles caps when radii > 0, so middle segments get caps automatically
  }
  
  // Now from the top of this cylinder, create continuation cylinders (trunk continuation + branches)
  if (level < num_levels - 1 && branch_chance > 0.2f) {
    
    // Determine how many cylinders continue from this point - more branches for better distribution
    usize continuations = (level == 0) ? (4 + (usize)(hash2((int)base_position.x, (int)base_position.z, WORLD_SEED) * 1.0f + 0.5f)) : (2 + (level % 2));
    if (continuations > max_branches) continuations = max_branches;
    
    for (usize i = 0; i < continuations; i++) {
      // Should we generate this continuation?
      float continuation_roll = hash2((int)(top_center.x * 13 + i * 17), (int)(top_center.z * 19 + level * 23), WORLD_SEED);
      if (continuation_roll > branch_chance) continue;
      
      // Continuation radius: slight reduction for branches
      float continuation_radius = top_radius * (0.85f + hash2((int)(i * 29), (int)(level * 31), WORLD_SEED) * 0.1f);
      
      // Calculate the direction for this continuation
      float continuation_growth_angle;
      if (i == 0) {
        // Main continuation: similar direction with slight variation
        continuation_growth_angle = growth_angle + (hash2((int)(top_center.x * 7), (int)(top_center.z * 11), WORLD_SEED) - 0.5f) * 0.3f;
      } else {
        // Branch continuations: evenly distribute around the cylinder with some variation
        float base_radial_offset = ((float)(i - 1) / (float)(continuations - 1)) * 2.0f * PI;
        // Add some randomness but keep it smaller for better distribution
        float random_variation = (hash2((int)(top_center.x * 11 + i), (int)(top_center.z * 13 + level), WORLD_SEED) - 0.5f) * 0.3f;
        continuation_growth_angle = growth_angle + base_radial_offset + random_variation;
      }
      
      // overlap for better connections
      float3 shifted_start = make_float3(
        top_center.x,
        top_center.y + continuation_radius * 0.2f, // Aggressive overlap for seamless look
        top_center.z
      );
      
      // Pass the continuation_growth_angle for both growth direction and face alignment
      ret += generate_tree(model, continuation_radius, continuation_growth_angle, shifted_start,
      branch_chance * 0.8f, max_branches, level + 1, num_levels);
    }
  }

  return ret;
}
  
// this should append each segment to the model's vertex_data and face_normals
int generate_tree_cylinder(model_t* model, float bottom_radius, float top_radius, float height, float3 bottom_center, float3 top_center, usize segments, float bottom_angle_offset, float top_angle_offset) {
  if (!model || segments < 3 || bottom_radius < 0 || top_radius < 0 || height <= 0) return -1;
  
  // Calculate vertices and faces
  usize side_vertices = segments * 6;         // 2 triangles per segment = 6 vertices
  usize bottom_cap_vertices = (segments - 2) * 3;
  usize top_cap_vertices = (segments - 2) * 3;
  usize new_vertices = side_vertices + bottom_cap_vertices + top_cap_vertices;
  usize new_faces = segments * 2 + (segments - 2) + (segments - 2);
  
  usize old_vertices = model->num_vertices;
  usize old_faces = model->num_faces;
  
  // Reallocate arrays for append
  vertex_data_t* tmp_v = realloc(model->vertex_data, (old_vertices + new_vertices) * sizeof(vertex_data_t));
  float3* tmp_f = realloc(model->face_normals, (old_faces + new_faces) * sizeof(float3));
  
  if (!tmp_v || !tmp_f) {
    free(tmp_v);
    free(tmp_f);
    return -1;
  }
  
  model->vertex_data = tmp_v;
  model->face_normals = tmp_f;
  
  float3 axis = float3_normalize(float3_sub(top_center, bottom_center));
  
  usize vertex_idx = old_vertices;
  usize face_idx = old_faces;
  
  // -------- Side faces --------
  for (usize i = 0; i < segments; i++) {
    // Bottom angles
    float angle1b = (2.0f * PI * i) / segments + bottom_angle_offset;
    float angle2b = (2.0f * PI * (i + 1)) / segments + bottom_angle_offset;
    
    float cos1b = cosf(angle1b), sin1b = sinf(angle1b);
    float cos2b = cosf(angle2b), sin2b = sinf(angle2b);
    
    // Top angles
    float angle1t = (2.0f * PI * i) / segments + top_angle_offset;
    float angle2t = (2.0f * PI * (i + 1)) / segments + top_angle_offset;
    
    float cos1t = cosf(angle1t), sin1t = sinf(angle1t);
    float cos2t = cosf(angle2t), sin2t = sinf(angle2t);
    
    // Tangent basis
    float3 right = float3_normalize(float3_cross(axis, make_float3(0, 1, 0)));
    if (float3_magnitude(right) < 0.1f)
    right = float3_normalize(float3_cross(axis, make_float3(1, 0, 0)));
    float3 forward = float3_normalize(float3_cross(axis, right));
    
    // Bottom vertices
    float3 bottom1 = float3_add(bottom_center,
      float3_add(float3_scale(right, cos1b * bottom_radius),
      float3_scale(forward, sin1b * bottom_radius)));
      float3 bottom2 = float3_add(bottom_center,
      float3_add(float3_scale(right, cos2b * bottom_radius),
      float3_scale(forward, sin2b * bottom_radius)));
        
    // Top vertices
    float3 top1 = float3_add(top_center,
                  float3_add(float3_scale(right, cos1t * top_radius),
                  float3_scale(forward, sin1t * top_radius)));
    float3 top2 = float3_add(top_center,
                  float3_add(float3_scale(right, cos2t * top_radius),
                  float3_scale(forward, sin2t * top_radius)));
      
    // Side normal
    float3 edge1 = float3_sub(top1, bottom1);
    float3 edge2 = float3_sub(bottom2, bottom1);
    float3 normal = float3_normalize(float3_cross(edge1, edge2));
    
    // First triangle
    model->vertex_data[vertex_idx++] = (vertex_data_t){ bottom1, make_float2((float)i / segments, 0.0f), normal };
    model->vertex_data[vertex_idx++] = (vertex_data_t){ bottom2, make_float2((float)(i + 1) / segments, 0.0f), normal };
    model->vertex_data[vertex_idx++] = (vertex_data_t){ top1,    make_float2((float)i / segments, 1.0f), normal };
    
    // Second triangle
    model->vertex_data[vertex_idx++] = (vertex_data_t){ bottom2, make_float2((float)(i + 1) / segments, 0.0f), normal };
    model->vertex_data[vertex_idx++] = (vertex_data_t){ top2,    make_float2((float)(i + 1) / segments, 1.0f), normal };
    model->vertex_data[vertex_idx++] = (vertex_data_t){ top1,    make_float2((float)i / segments, 1.0f), normal };
    
    model->face_normals[face_idx++] = normal;
    model->face_normals[face_idx++] = normal;
  }
  
  // -------- Bottom cap --------
  if (bottom_radius > 0) {
    float3 bottom_normal = float3_scale(axis, -1.0f);
    for (usize i = 1; i < segments - 1; i++) {
      float angle0 = 0 + bottom_angle_offset;
      float angle1 = (2.0f * PI * i) / segments + bottom_angle_offset;
      float angle2 = (2.0f * PI * (i + 1)) / segments + bottom_angle_offset;
      
      float cos0 = cosf(angle0), sin0 = sinf(angle0);
      float cos1 = cosf(angle1), sin1 = sinf(angle1);
      float cos2 = cosf(angle2), sin2 = sinf(angle2);
      
      float3 right = float3_normalize(float3_cross(axis, make_float3(0, 1, 0)));
      if (float3_magnitude(right) < 0.1f)
      right = float3_normalize(float3_cross(axis, make_float3(1, 0, 0)));
      float3 forward = float3_normalize(float3_cross(axis, right));
      
      float3 v0 = float3_add(bottom_center,
                  float3_add(float3_scale(right, cos0 * bottom_radius),
                  float3_scale(forward, sin0 * bottom_radius)));
      float3 v1 = float3_add(bottom_center,
                  float3_add(float3_scale(right, cos1 * bottom_radius),
                  float3_scale(forward, sin1 * bottom_radius)));
      float3 v2 = float3_add(bottom_center,
          float3_add(float3_scale(right, cos2 * bottom_radius),
          float3_scale(forward, sin2 * bottom_radius)));
          
          model->vertex_data[vertex_idx++] = (vertex_data_t){ v0, make_float2(0.5f + cos0 * 0.5f, 0.5f + sin0 * 0.5f), bottom_normal };
          model->vertex_data[vertex_idx++] = (vertex_data_t){ v1, make_float2(0.5f + cos1 * 0.5f, 0.5f + sin1 * 0.5f), bottom_normal };
          model->vertex_data[vertex_idx++] = (vertex_data_t){ v2, make_float2(0.5f + cos2 * 0.5f, 0.5f + sin2 * 0.5f), bottom_normal };
          
          model->face_normals[face_idx++] = bottom_normal;
    }
  }
      
  // -------- Top cap --------
  if (top_radius > 0) {
    float3 top_normal = axis;
    for (usize i = 1; i < segments - 1; i++) {
      float angle0 = 0 + top_angle_offset;
      float angle1 = (2.0f * PI * i) / segments + top_angle_offset;
      float angle2 = (2.0f * PI * (i + 1)) / segments + top_angle_offset;
      
      float cos0 = cosf(angle0), sin0 = sinf(angle0);
      float cos1 = cosf(angle1), sin1 = sinf(angle1);
      float cos2 = cosf(angle2), sin2 = sinf(angle2);
      
      float3 right = float3_normalize(float3_cross(axis, make_float3(0, 1, 0)));
      if (float3_magnitude(right) < 0.1f)
      right = float3_normalize(float3_cross(axis, make_float3(1, 0, 0)));
      float3 forward = float3_normalize(float3_cross(axis, right));
      
      float3 v0 = float3_add(top_center,
                  float3_add(float3_scale(right, cos0 * top_radius),
                  float3_scale(forward, sin0 * top_radius)));
      float3 v1 = float3_add(top_center,
                  float3_add(float3_scale(right, cos1 * top_radius),
                  float3_scale(forward, sin1 * top_radius)));
      float3 v2 = float3_add(top_center,
                  float3_add(float3_scale(right, cos2 * top_radius),
                  float3_scale(forward, sin2 * top_radius)));
          
      // Reverse winding
      model->vertex_data[vertex_idx++] = (vertex_data_t){ v0, make_float2(0.5f + cos0 * 0.5f, 0.5f + sin0 * 0.5f), top_normal };
      model->vertex_data[vertex_idx++] = (vertex_data_t){ v2, make_float2(0.5f + cos2 * 0.5f, 0.5f + sin2 * 0.5f), top_normal };
      model->vertex_data[vertex_idx++] = (vertex_data_t){ v1, make_float2(0.5f + cos1 * 0.5f, 0.5f + sin1 * 0.5f), top_normal };
      
      model->face_normals[face_idx++] = top_normal;
    }
  }
      
  model->num_vertices = old_vertices + new_vertices;
  model->num_faces = old_faces + new_faces;
  
  return 0;
}
