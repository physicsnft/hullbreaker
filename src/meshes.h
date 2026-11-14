// Procedural unit meshes. Every mesh fits inside the cube [-0.5, 0.5]^3 so a part's
// scale is also its bounding box, which the damage model uses for hit tests.
#pragma once
#include "math.h"
#include <vector>

enum MeshId {
  M_BOX = 0,
  M_CYL,     // axis along Z
  M_CONE,    // tip at -Z (forward), base at +Z
  M_SPHERE,
  M_WEDGE,   // full height at +Z, zero height at -Z, flat bottom
  M_TAPER,   // fuselage section easing down to 40% at -Z
  M_PRISM,   // hexagonal prism along Z
  M_WING_R,  // swept, tapered wing segment with an airfoil section; root at -X
  M_WING_L,  // mirror image; root at +X
  M_CRYSTAL, // hexagonal bipyramid along Y
  M_RBOX,    // box with rounded edges
  M_SQPRISM, // fuselage section: rounded-square bar along Z
  M_NOSE,    // ogive nose, point at -Z
  M_FIN,     // upright swept fin with an airfoil section; root at -Y
  M_NACELLE, // engine pod
  M_NOZZLE,  // bell nozzle, throat at -Z
  M_AST0, M_AST1, M_AST2, M_AST3, M_AST4, M_AST5,
  M_ASTLO0, M_ASTLO1, M_ASTLO2, M_ASTLO3, M_ASTLO4, M_ASTLO5,
  MESH_COUNT
};
const int AST_VARIANTS = 6;

enum Material {
  MAT_HULL = 0,
  MAT_ROCK = 1,
  MAT_GLASS = 2,
  MAT_EMISSIVE = 3,
  MAT_ICE = 4,
  MAT_FLOOR = 5,
  MAT_SHIELD = 6, // additive pass
  MAT_FLAME = 7,  // additive pass
  MAT_FIELD = 8,  // additive pass: force field across the hangar door
  MAT_CONTAINER = 9,
  MAT_STRUCT = 10 // station walls, girders, machinery
};

struct Vertex {
  float px, py, pz, nx, ny, nz;
};
struct MeshData {
  std::vector<Vertex> verts;
  std::vector<uint32_t> idx;
};

extern float g_meshRadius[MESH_COUNT]; // mean radius of the unit mesh (used for asteroid collision spheres)
void buildMeshes(std::vector<MeshData>& out);
