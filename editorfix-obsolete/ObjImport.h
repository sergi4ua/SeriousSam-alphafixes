/*
 * ObjImport - converts Wavefront OBJ files to LightWave 5 objects (LWOB),
 * the format the Serious Sam alpha engine imports (CObject3D::LoadLWO_t).
 */

#ifndef OBJIMPORT_H
#define OBJIMPORT_H

#include <stddef.h>

typedef struct {
  int ctVertices;       /* points written to the LWO */
  int ctPolygons;       /* polygons written (after triangulating where needed) */
  int ctSurfaces;
  int ctTexturesFound;  /* surfaces whose name is an existing .tex */
  int ctSkippedFaces;   /* degenerate faces left out */
} ObjImportStats;

/*
 * Converts strObjFile to strLwoFile.  strGamePath is the game folder (with or
 * without a trailing backslash); it is used to turn OBJ materials into texture
 * names the editor can use.  Pass NULL to keep material names as they are.
 * Returns nonzero on success; otherwise strError explains what went wrong.
 */
int ObjToLwo(const char *strObjFile, const char *strLwoFile, const char *strGamePath,
             ObjImportStats *pStats, char *strError, size_t ctError);

#endif
