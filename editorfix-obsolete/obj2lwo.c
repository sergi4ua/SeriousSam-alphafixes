/*
 * obj2lwo - command-line front end for ObjImport.c: converts a Wavefront OBJ
 * file to a LightWave 5 object the Serious Sam alpha editor can import.
 *
 * Usage:  obj2lwo input.obj [output.lwo] [game folder]
 *
 * With a game folder, OBJ materials are matched to the game's textures the
 * same way the editor's OBJ import does it.
 */

#include <stdio.h>
#include <string.h>

#include "ObjImport.h"

int main(int argc, char *argv[])
{
  char strOutput[1024], strError[1024];
  ObjImportStats stats;
  const char *pchDot;

  if (argc < 2) {
    printf("Usage: obj2lwo input.obj [output.lwo] [game folder]\n");
    return 1;
  }
  if (argc > 2) {
    strncpy(strOutput, argv[2], sizeof(strOutput) - 1);
  } else {
    size_t ct;
    strncpy(strOutput, argv[1], sizeof(strOutput) - 5);
    strOutput[sizeof(strOutput) - 5] = 0;
    pchDot = strrchr(strOutput, '.');
    ct = (pchDot != NULL && strchr(pchDot, '\\') == NULL && strchr(pchDot, '/') == NULL)
       ? (size_t)(pchDot - strOutput) : strlen(strOutput);
    strcpy(strOutput + ct, ".lwo");
  }
  strOutput[sizeof(strOutput) - 1] = 0;

  if (!ObjToLwo(argv[1], strOutput, (argc > 3) ? argv[3] : NULL, &stats, strError, sizeof(strError))) {
    fprintf(stderr, "%s\n", strError);
    return 1;
  }
  printf("Wrote %s: %d vertices, %d polygons, %d surfaces (%d matched to textures), %d degenerate faces skipped.\n",
    strOutput, stats.ctVertices, stats.ctPolygons, stats.ctSurfaces, stats.ctTexturesFound, stats.ctSkippedFaces);
  return 0;
}
