// In-memory world: what the converter reads from a TFE .wld and writes to an
// alpha .wld.  Geometry blocks that both formats store the same way (vertices,
// planes, edges, BSP trees, portal-sector links) are kept as raw bytes.
#pragma once

#include "stream.h"

// entity property types (CEntityProperty::PropertyType, same numbers in both engines)
enum PropertyType {
  EPT_ENUM = 1, EPT_BOOL = 2, EPT_FLOAT = 3, EPT_COLOR = 4, EPT_STRING = 5, EPT_RANGE = 6,
  EPT_ENTITYPTR = 7, EPT_FILENAME = 8, EPT_INDEX = 9, EPT_ANIMATION = 10, EPT_ILLUMINATIONTYPE = 11,
  EPT_FLOATAABBOX3D = 12, EPT_ANGLE = 13, EPT_FLOAT3D = 14, EPT_ANGLE3D = 15, EPT_FLOATplane3D = 16,
  EPT_MODELOBJECT = 17, EPT_PLACEMENT3D = 18, EPT_ANIMOBJECT = 19, EPT_FILENAMENODEP = 20,
  EPT_SOUNDOBJECT = 21, EPT_STRINGTRANS = 22, EPT_FLOATQUAT3D = 23, EPT_FLOATMATRIX3D = 24,
  EPT_FLAGS = 25, EPT_MODELINSTANCE = 26,
};

// entity render types (CEntity::RenderType)
enum RenderType {
  RT_ILLEGAL = 1, RT_NONE = 2, RT_MODEL = 3, RT_BRUSH = 4, RT_EDITORMODEL = 5, RT_VOID = 7,
  RT_FIELDBRUSH = 8, RT_SKAMODEL = 9, RT_SKAEDITORMODEL = 10, RT_TERRAIN = 11,
};

struct Placement { float af[6]; };   // position (x, y, z), angles (heading, pitch, banking)

struct Texture {
  std::string strFile;
  uint8_t aubMapping[24];             // CMappingDefinition
  uint8_t ubScroll, ubBlend, ubFlags, ubDummy;
  uint32_t colColor;
};

struct ShadowLayer {
  uint32_t ulFlags;
  int32_t slSizeInPixels;
  Bytes abMask;                       // (slSizeInPixels + 7) / 8 bytes
  int32_t aiRect[4];                  // min U, min V, size U, size V
};

struct BrushPolygon {
  int32_t iPlane;
  uint32_t colColor, ulFlags;
  Texture atex[3];
  uint8_t aubProperties[8];           // surface, illumination, shadow blend, mirror, gradient, cluster size, pretender distance (2)
  std::vector<int32_t> aiEdges;       // high bit set for reversed edges
  uint8_t aubShadowMap[24];           // LSHM: flags, first mip, offset x/y, width, height
  int32_t iShadowVersion, slPolygonSizeU, slPolygonSizeV;
  std::vector<ShadowLayer> alayers;
  uint32_t colShadow;
};

struct Sector {
  std::string strName;
  uint32_t colColor, colAmbient, ulFlags;
  int32_t ctVertices, ctPlanes, ctEdges;
  Bytes abVertices, abPlanes, abEdges;   // DOUBLE3D, DOUBLEplane3D, two vertex indices
  std::vector<BrushPolygon> apolygons;
  bool bHasBSP = false;
  int32_t iBSPVersion = 0;
  Bytes abBSP;                          // node data (its size is stored before it)
};

struct BrushMip {
  float fMaxDistance;                   // TFE: switch distance in metres
  std::vector<Sector> asectors;
};

struct Brush {
  std::vector<BrushMip> amips;
};

struct LightLayerRef { int32_t iBrush, iMip, iSector, iPolygon, iLayer; };

struct Property {
  uint32_t ulID;                        // (class ID << 8) | index
  PropertyType ept;
  int32_t iValue = 0;                   // BOOL, INDEX-like, ENTITYPTR (entity ID or index)
  float fValue = 0;
  std::string strValue;                 // strings and file names
  Bytes abValue;                        // vectors, angles, boxes, planes, placements
};

struct Entity {
  uint32_t ulID = 0;                    // TFE entity ID (pointers refer to it)
  std::string strClass;                 // Classes\Name.ecl
  Placement pl;
  uint32_t ulRenderType = 0, ulPhysicsFlags = 0, ulCollisionFlags = 0, ulSpawnFlags = 0, ulFlags = 0;
  int32_t iBrush = -1;                  // for brush entities
  bool bHasParent = false;
  uint32_t ulParent = 0;
  Placement plRelative;
  std::vector<Property> aprops;
  bool bHasLight = false;
  std::vector<LightLayerRef> alayers;
  bool bHasHealth = false;
  float fHealth = 0;
  std::string strModel;                 // model file of a model entity (for the report)
};

struct World {
  std::string strName;                  // TFE only
  uint32_t ulSpawnFlags = 0;            // TFE only
  std::string strDescription;
  uint32_t colBackground = 0;
  std::vector<Brush> abrushes;
  bool bHasPortalLinks = false;
  int32_t iPortalLinksVersion = 0;
  Bytes abPortalLinks;
  int32_t iBackgroundViewer = -1;       // TFE: entity ID
  std::string astrBackdrop[3];          // up, front, right images
  float afBackdrop[12];
  std::string strBackdropObject;
  Placement plViewer;
  float fViewerDistance = 0;
  bool bHasThumbnail = false;
  Placement plThumbnail;
  float fThumbnailDistance = 0;
  std::vector<Entity> aentities;
};
