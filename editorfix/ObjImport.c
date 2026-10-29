/*
 * ObjImport - converts Wavefront OBJ files to LightWave 5 objects (LWOB).
 *
 * The alpha engine imports 3D objects only as LightWave 5 files
 * (CObject3D::LoadLWO_t).  Its loader reads these chunks:
 *
 *   PNTS  big-endian float x, y, z per point, stored in the engine as
 *         (x, y, -z) (LightWave is left-handed, the engine right-handed)
 *   SRFS  surface names, zero-terminated and padded to even length; the
 *         editor uses each name as the texture file of that surface
 *   POLS  per polygon: U2 vertex count (3 or more), U2 point indices,
 *         I2 surface number (1-based; negative "detail" polygons are refused).
 *         Indices are read as signed 16-bit, so at most 32767 points.
 *         The engine reverses the vertex order.
 *   SURF  surface name followed by sub-chunks; only COLR (R, G, B, 0) is used
 *
 * OBJ is right-handed with counter-clockwise front faces.  Points are written
 * as (x, y, -z), which the engine turns back into the original positions.
 * Polygons are written with their vertex order reversed: tested in the editor,
 * an OBJ floor facing up is then visible from above and invisible from below,
 * so the side you see in the editor is the OBJ's front side.
 *
 * LWOB has no texture coordinates; the editor gives imported polygons its
 * default texture mapping, the same as for any LightWave import.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ObjImport.h"

#define MAX_LWO_POINTS        32767   /* the engine reads point indices as signed 16-bit */
#define MAX_POLYGON_VERTICES  64      /* larger faces are triangulated */
#define DEFAULT_SURFACE_NAME  "Default"


/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */
/* ---------------------------------------------------------------------- */

typedef struct { double x, y, z; } Vec3;

/* growable array of fixed-size items */
typedef struct {
  unsigned char *pub;
  int ct, ctMax;
  size_t size;
} Array;

static void ArrayInit(Array *pa, size_t size) { pa->pub = NULL; pa->ct = pa->ctMax = 0; pa->size = size; }
static void ArrayFree(Array *pa) { free(pa->pub); ArrayInit(pa, pa->size); }

static void *ArrayPush(Array *pa)
{
  void *pv;
  if (pa->ct == pa->ctMax) {
    int ctNew = (pa->ctMax < 16) ? 16 : pa->ctMax * 2;
    unsigned char *pubNew = (unsigned char *)realloc(pa->pub, (size_t)ctNew * pa->size);
    if (pubNew == NULL) return NULL;
    pa->pub = pubNew;
    pa->ctMax = ctNew;
  }
  pv = pa->pub + (size_t)pa->ct * pa->size;
  memset(pv, 0, pa->size);
  pa->ct++;
  return pv;
}

#define AT(pa, type, i) (((type *)(pa)->pub)[i])

/* growable byte buffer for the output file */
typedef struct { unsigned char *pub; size_t ct, ctMax; int bFailed; } Buffer;

static void Put(Buffer *pb, const void *pv, size_t ct)
{
  if (pb->bFailed) return;
  if (pb->ct + ct > pb->ctMax) {
    size_t ctNew = (pb->ctMax < 4096) ? 4096 : pb->ctMax * 2;
    unsigned char *pubNew;
    while (ctNew < pb->ct + ct) ctNew *= 2;
    pubNew = (unsigned char *)realloc(pb->pub, ctNew);
    if (pubNew == NULL) { pb->bFailed = 1; return; }
    pb->pub = pubNew;
    pb->ctMax = ctNew;
  }
  memcpy(pb->pub + pb->ct, pv, ct);
  pb->ct += ct;
}

static void PutU2(Buffer *pb, unsigned int u)
{
  unsigned char aub[2];
  aub[0] = (unsigned char)(u >> 8); aub[1] = (unsigned char)u;
  Put(pb, aub, 2);
}

static void PutU4(Buffer *pb, unsigned long ul)
{
  unsigned char aub[4];
  aub[0] = (unsigned char)(ul >> 24); aub[1] = (unsigned char)(ul >> 16);
  aub[2] = (unsigned char)(ul >> 8);  aub[3] = (unsigned char)ul;
  Put(pb, aub, 4);
}

static void PutF4(Buffer *pb, double d)
{
  float f = (float)d;
  unsigned long ul;
  memcpy(&ul, &f, 4);
  PutU4(pb, ul);
}

/* zero-terminated string padded to even length */
static void PutS0(Buffer *pb, const char *str)
{
  size_t ct = strlen(str) + 1;
  Put(pb, str, ct);
  if (ct & 1) Put(pb, "", 1);
}

static void PutChunk(Buffer *pb, const char *strID, const Buffer *pbBody)
{
  Put(pb, strID, 4);
  PutU4(pb, (unsigned long)pbBody->ct);
  Put(pb, pbBody->pub, pbBody->ct);
  if (pbBody->ct & 1) Put(pb, "", 1);
}

static void SetError(char *strError, size_t ctError, const char *strFormat, ...)
{
  va_list args;
  if (strError == NULL || ctError == 0) return;
  va_start(args, strFormat);
  _vsnprintf(strError, ctError - 1, strFormat, args);
  va_end(args);
  strError[ctError - 1] = 0;
}

static int FileExists(const char *strFile)
{
  DWORD dw = GetFileAttributesA(strFile);
  return dw != INVALID_FILE_ATTRIBUTES && !(dw & FILE_ATTRIBUTE_DIRECTORY);
}

static char *ReadWholeFile(const char *strFile)
{
  FILE *f = fopen(strFile, "rb");
  long sl;
  char *str;
  if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  sl = ftell(f);
  fseek(f, 0, SEEK_SET);
  str = (sl >= 0) ? (char *)malloc((size_t)sl + 1) : NULL;
  if (str != NULL && fread(str, 1, (size_t)sl, f) != (size_t)sl) { free(str); str = NULL; }
  if (str != NULL) str[sl] = 0;
  fclose(f);
  return str;
}

/* copy of the directory part of strPath, with a trailing backslash (or empty) */
static void DirectoryOf(const char *strPath, char *strDir, size_t ctDir)
{
  const char *pch1 = strrchr(strPath, '\\'), *pch2 = strrchr(strPath, '/');
  const char *pch = (pch2 > pch1) ? pch2 : pch1;
  size_t ct = (pch != NULL) ? (size_t)(pch - strPath + 1) : 0;
  if (ct >= ctDir) ct = ctDir - 1;
  memcpy(strDir, strPath, ct);
  strDir[ct] = 0;
}

/* file name without directory and extension */
static void StemOf(const char *strPath, char *strStem, size_t ctStem)
{
  const char *pch1 = strrchr(strPath, '\\'), *pch2 = strrchr(strPath, '/');
  const char *pchName = (pch2 > pch1) ? pch2 + 1 : (pch1 != NULL ? pch1 + 1 : strPath);
  const char *pchDot = strrchr(pchName, '.');
  size_t ct = (pchDot != NULL) ? (size_t)(pchDot - pchName) : strlen(pchName);
  if (ct >= ctStem) ct = ctStem - 1;
  memcpy(strStem, pchName, ct);
  strStem[ct] = 0;
}

static int EndsWithNoCase(const char *str, const char *strEnd)
{
  size_t ct = strlen(str), ctEnd = strlen(strEnd);
  return ct >= ctEnd && _stricmp(str + ct - ctEnd, strEnd) == 0;
}

static void TrimRight(char *str)
{
  size_t ct = strlen(str);
  while (ct > 0 && isspace((unsigned char)str[ct - 1])) str[--ct] = 0;
}

static char *SkipSpace(char *pch)
{
  while (*pch == ' ' || *pch == '\t') pch++;
  return pch;
}


/* ---------------------------------------------------------------------- */
/* OBJ and MTL parsing                                                     */
/* ---------------------------------------------------------------------- */

typedef struct {
  char   strName[256];
  char   strDiffuseMap[MAX_PATH];
  double r, g, b;
  int    bHasColor;
  int    iSurface;              /* filled when writing */
} Material;

typedef struct {
  int iFirst;                   /* into aiFaceIndices */
  int ct;
  int iMaterial;                /* -1: no material */
} Face;

typedef struct {
  Array aVertices;              /* Vec3 */
  Array aiFaceIndices;          /* int, 0-based OBJ vertex numbers */
  Array aFaces;                 /* Face */
  Array aMaterials;             /* Material */
} ObjData;

static int FindOrAddMaterial(ObjData *pod, const char *strName)
{
  int i;
  Material *pm;
  for (i = 0; i < pod->aMaterials.ct; i++) {
    if (strcmp(AT(&pod->aMaterials, Material, i).strName, strName) == 0) return i;
  }
  pm = (Material *)ArrayPush(&pod->aMaterials);
  if (pm == NULL) return -1;
  lstrcpynA(pm->strName, strName, sizeof(pm->strName));
  pm->iSurface = -1;
  return pod->aMaterials.ct - 1;
}

/* Splits text into lines in place; returns the next line or NULL at the end. */
static char *NextLine(char **ppch)
{
  char *pchLine = *ppch, *pch;
  if (*pchLine == 0) return NULL;
  pch = pchLine;
  while (*pch != 0 && *pch != '\n') pch++;
  if (*pch == '\n') *pch++ = 0;
  *ppch = pch;
  TrimRight(pchLine);
  return pchLine;
}

/* first word of a line (lower case) and a pointer to the rest */
static char *Keyword(char *pchLine, char *strKeyword, size_t ctKeyword)
{
  size_t ct = 0;
  pchLine = SkipSpace(pchLine);
  while (*pchLine != 0 && !isspace((unsigned char)*pchLine)) {
    if (ct + 1 < ctKeyword) strKeyword[ct++] = (char)tolower((unsigned char)*pchLine);
    pchLine++;
  }
  strKeyword[ct] = 0;
  return SkipSpace(pchLine);
}

static void LoadMtl(ObjData *pod, const char *strMtlFile)
{
  char *strText = ReadWholeFile(strMtlFile), *pch, *pchLine, *pchRest;
  char strKeyword[32];
  int iCurrent = -1;

  if (strText == NULL) return;   /* a missing material library only loses colors and texture hints */
  pch = strText;
  while ((pchLine = NextLine(&pch)) != NULL) {
    pchRest = Keyword(pchLine, strKeyword, sizeof(strKeyword));
    if (strcmp(strKeyword, "newmtl") == 0) {
      iCurrent = FindOrAddMaterial(pod, pchRest);
    } else if (iCurrent >= 0 && strcmp(strKeyword, "kd") == 0) {
      Material *pm = &AT(&pod->aMaterials, Material, iCurrent);
      if (sscanf(pchRest, "%lf %lf %lf", &pm->r, &pm->g, &pm->b) == 3) pm->bHasColor = 1;
    } else if (iCurrent >= 0 && strcmp(strKeyword, "map_kd") == 0) {
      /* options (-s 1 1 1 ...) may come first; the file name is the last word */
      char *pchName = strrchr(pchRest, ' ');
      char *pchTab = strrchr(pchRest, '\t');
      if (pchTab > pchName) pchName = pchTab;
      pchName = (pchName != NULL) ? pchName + 1 : pchRest;
      lstrcpynA(AT(&pod->aMaterials, Material, iCurrent).strDiffuseMap, pchName, MAX_PATH);
    }
  }
  free(strText);
}

static int ParseObj(ObjData *pod, const char *strObjFile, char *strError, size_t ctError)
{
  char strDir[MAX_PATH], strKeyword[32];
  char *strText, *pch, *pchLine, *pchRest;
  int iLine = 0, iMaterial = -1;

  strText = ReadWholeFile(strObjFile);
  if (strText == NULL) {
    SetError(strError, ctError, "Cannot read '%s'.", strObjFile);
    return 0;
  }
  DirectoryOf(strObjFile, strDir, sizeof(strDir));

  pch = strText;
  while ((pchLine = NextLine(&pch)) != NULL) {
    iLine++;
    pchRest = Keyword(pchLine, strKeyword, sizeof(strKeyword));

    if (strcmp(strKeyword, "v") == 0) {
      Vec3 *pv = (Vec3 *)ArrayPush(&pod->aVertices);
      if (pv == NULL) goto OutOfMemory;
      if (sscanf(pchRest, "%lf %lf %lf", &pv->x, &pv->y, &pv->z) != 3) {
        SetError(strError, ctError, "%s, line %d: a vertex needs three coordinates.", strObjFile, iLine);
        goto Failed;
      }

    } else if (strcmp(strKeyword, "f") == 0) {
      Face *pf = (Face *)ArrayPush(&pod->aFaces);
      if (pf == NULL) goto OutOfMemory;
      pf->iFirst = pod->aiFaceIndices.ct;
      pf->iMaterial = iMaterial;
      while (*pchRest != 0) {
        /* v, v/vt, v//vn or v/vt/vn: only the vertex number is used */
        char *pchEnd;
        long sl = strtol(pchRest, &pchEnd, 10);
        int *pi;
        if (pchEnd == pchRest || sl == 0) {
          SetError(strError, ctError, "%s, line %d: cannot read face '%s'.", strObjFile, iLine, pchLine);
          goto Failed;
        }
        if (sl < 0) sl += pod->aVertices.ct;   /* relative to the vertices read so far */
        else sl -= 1;
        if (sl < 0) {
          SetError(strError, ctError, "%s, line %d: face uses a vertex that doesn't exist.", strObjFile, iLine);
          goto Failed;
        }
        pi = (int *)ArrayPush(&pod->aiFaceIndices);
        if (pi == NULL) goto OutOfMemory;
        *pi = (int)sl;
        pf->ct++;
        while (*pchEnd != 0 && !isspace((unsigned char)*pchEnd)) pchEnd++;
        pchRest = SkipSpace(pchEnd);
      }

    } else if (strcmp(strKeyword, "usemtl") == 0) {
      iMaterial = FindOrAddMaterial(pod, pchRest);
      if (iMaterial < 0) goto OutOfMemory;

    } else if (strcmp(strKeyword, "mtllib") == 0) {
      /* usually one file; names with spaces are tried whole first */
      char strMtl[MAX_PATH * 2];
      _snprintf(strMtl, sizeof(strMtl) - 1, "%s%s", strDir, pchRest);
      strMtl[sizeof(strMtl) - 1] = 0;
      if (FileExists(strMtl)) {
        LoadMtl(pod, strMtl);
      } else {
        char *pchName = pchRest;
        while (*pchName != 0) {
          char *pchEnd = pchName;
          char chSaved;
          while (*pchEnd != 0 && !isspace((unsigned char)*pchEnd)) pchEnd++;
          chSaved = *pchEnd;
          *pchEnd = 0;
          _snprintf(strMtl, sizeof(strMtl) - 1, "%s%s", strDir, pchName);
          strMtl[sizeof(strMtl) - 1] = 0;
          LoadMtl(pod, strMtl);
          *pchEnd = chSaved;
          pchName = SkipSpace(pchEnd);
        }
      }
    }
    /* vt, vn, o, g, s, l, p and anything else are not needed */
  }
  free(strText);
  return 1;

OutOfMemory:
  SetError(strError, ctError, "Not enough memory to read '%s'.", strObjFile);
Failed:
  free(strText);
  return 0;
}


/* ---------------------------------------------------------------------- */
/* Faces to engine polygons                                                */
/* ---------------------------------------------------------------------- */

typedef struct {
  int iFirst;                   /* into aiPolygonIndices */
  int ct;
  int iMaterial;
} LwoPolygon;

typedef struct {
  Array aiPolygonIndices;       /* int, OBJ vertex numbers */
  Array aPolygons;              /* LwoPolygon */
  int   ctSkipped;
  int   bOutOfMemory;
} PolygonList;

static Vec3 Sub(Vec3 a, Vec3 b) { Vec3 v; v.x = a.x - b.x; v.y = a.y - b.y; v.z = a.z - b.z; return v; }
static double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static double Length(Vec3 a) { return sqrt(Dot(a, a)); }
static Vec3 Cross(Vec3 a, Vec3 b)
{
  Vec3 v;
  v.x = a.y * b.z - a.z * b.y;
  v.y = a.z * b.x - a.x * b.z;
  v.z = a.x * b.y - a.y * b.x;
  return v;
}

static void EmitPolygon(PolygonList *ppl, const int *aiVertices, const int *aiOrder, int ct, int iMaterial)
{
  LwoPolygon *pp = (LwoPolygon *)ArrayPush(&ppl->aPolygons);
  int i;
  if (pp == NULL) { ppl->bOutOfMemory = 1; return; }
  pp->iFirst = ppl->aiPolygonIndices.ct;
  pp->ct = ct;
  pp->iMaterial = iMaterial;
  for (i = 0; i < ct; i++) {
    int *pi = (int *)ArrayPush(&ppl->aiPolygonIndices);
    if (pi == NULL) { ppl->bOutOfMemory = 1; return; }
    *pi = aiVertices[aiOrder != NULL ? aiOrder[i] : i];
  }
}

/* Newell normal: robust for any simple polygon, its length is twice the area */
static Vec3 PolygonNormal(const Vec3 *av, int ct)
{
  Vec3 n = { 0, 0, 0 };
  int i;
  for (i = 0; i < ct; i++) {
    const Vec3 *a = &av[i], *b = &av[(i + 1) % ct];
    n.x += (a->y - b->y) * (a->z + b->z);
    n.y += (a->z - b->z) * (a->x + b->x);
    n.z += (a->x - b->x) * (a->y + b->y);
  }
  return n;
}

static double Extent(const Vec3 *av, int ct)
{
  Vec3 vMin = av[0], vMax = av[0];
  double d;
  int i;
  for (i = 1; i < ct; i++) {
    if (av[i].x < vMin.x) vMin.x = av[i].x;
    if (av[i].y < vMin.y) vMin.y = av[i].y;
    if (av[i].z < vMin.z) vMin.z = av[i].z;
    if (av[i].x > vMax.x) vMax.x = av[i].x;
    if (av[i].y > vMax.y) vMax.y = av[i].y;
    if (av[i].z > vMax.z) vMax.z = av[i].z;
  }
  d = vMax.x - vMin.x;
  if (vMax.y - vMin.y > d) d = vMax.y - vMin.y;
  if (vMax.z - vMin.z > d) d = vMax.z - vMin.z;
  return d;
}

static int IsPlanarAndConvex(const Vec3 *av, int ct, Vec3 n, double dExtent)
{
  double dTolerance = 1e-4 * dExtent, dLength = Length(n);
  int i;
  if (dLength == 0) return 0;
  n.x /= dLength; n.y /= dLength; n.z /= dLength;
  for (i = 1; i < ct; i++) {
    if (fabs(Dot(n, Sub(av[i], av[0]))) > dTolerance) return 0;
  }
  for (i = 0; i < ct; i++) {
    Vec3 e1 = Sub(av[(i + 1) % ct], av[i]);
    Vec3 e2 = Sub(av[(i + 2) % ct], av[(i + 1) % ct]);
    if (Dot(Cross(e1, e2), n) < -1e-9 * dExtent * dExtent) return 0;
  }
  return 1;
}

/* Ear clipping in the plane of the polygon; triangles keep the face's winding. */
static void Triangulate(PolygonList *ppl, const int *aiVertices, const Vec3 *av, int ct, Vec3 n, int iMaterial)
{
  double *au = (double *)malloc(sizeof(double) * ct * 2);
  int *aiLeft = (int *)malloc(sizeof(int) * ct);
  double *av2, dSign;
  int ctLeft = ct, i, iAxis, aiTriangle[3];

  if (au == NULL || aiLeft == NULL) { free(au); free(aiLeft); ppl->bOutOfMemory = 1; return; }
  av2 = au + ct;

  /* project onto the axis plane the polygon faces most, keeping its winding */
  if (fabs(n.x) >= fabs(n.y) && fabs(n.x) >= fabs(n.z)) { iAxis = 0; dSign = n.x; }
  else if (fabs(n.y) >= fabs(n.z))                       { iAxis = 1; dSign = n.y; }
  else                                                    { iAxis = 2; dSign = n.z; }
  dSign = (dSign >= 0) ? 1.0 : -1.0;
  for (i = 0; i < ct; i++) {
    if (iAxis == 0)      { au[i] = av[i].y; av2[i] = av[i].z; }
    else if (iAxis == 1) { au[i] = av[i].z; av2[i] = av[i].x; }
    else                 { au[i] = av[i].x; av2[i] = av[i].y; }
    aiLeft[i] = i;
  }

  while (ctLeft > 3) {
    int bFound = 0, j;
    for (i = 0; i < ctLeft && !bFound; i++) {
      int iPrev = aiLeft[(i + ctLeft - 1) % ctLeft], iCur = aiLeft[i], iNext = aiLeft[(i + 1) % ctLeft];
      double dCross = (au[iCur] - au[iPrev]) * (av2[iNext] - av2[iPrev]) - (av2[iCur] - av2[iPrev]) * (au[iNext] - au[iPrev]);
      int bInside = 0;
      if (dCross * dSign <= 0) continue;          /* reflex or collinear corner */
      for (j = 0; j < ctLeft && !bInside; j++) {
        int k = aiLeft[j];
        double d1, d2, d3;
        if (k == iPrev || k == iCur || k == iNext) continue;
        d1 = ((au[iCur] - au[iPrev]) * (av2[k] - av2[iPrev]) - (av2[iCur] - av2[iPrev]) * (au[k] - au[iPrev])) * dSign;
        d2 = ((au[iNext] - au[iCur]) * (av2[k] - av2[iCur]) - (av2[iNext] - av2[iCur]) * (au[k] - au[iCur])) * dSign;
        d3 = ((au[iPrev] - au[iNext]) * (av2[k] - av2[iNext]) - (av2[iPrev] - av2[iNext]) * (au[k] - au[iNext])) * dSign;
        if (d1 >= 0 && d2 >= 0 && d3 >= 0) bInside = 1;
      }
      if (bInside) continue;
      aiTriangle[0] = iPrev; aiTriangle[1] = iCur; aiTriangle[2] = iNext;
      EmitPolygon(ppl, aiVertices, aiTriangle, 3, iMaterial);
      memmove(&aiLeft[i], &aiLeft[i + 1], sizeof(int) * (ctLeft - i - 1));
      ctLeft--;
      bFound = 1;
    }
    if (!bFound) break;   /* self-intersecting or otherwise odd: finish with a fan */
  }
  for (i = 1; i + 1 < ctLeft; i++) {
    aiTriangle[0] = aiLeft[0]; aiTriangle[1] = aiLeft[i]; aiTriangle[2] = aiLeft[i + 1];
    EmitPolygon(ppl, aiVertices, aiTriangle, 3, iMaterial);
  }
  free(au);
  free(aiLeft);
}

static void AddFace(PolygonList *ppl, const ObjData *pod, const Face *pf)
{
  const Vec3 *avAll = (const Vec3 *)pod->aVertices.pub;
  int *aiVertices = (int *)malloc(sizeof(int) * pf->ct);
  Vec3 *av = (Vec3 *)malloc(sizeof(Vec3) * pf->ct);
  int ct = 0, i;
  double dExtent;
  Vec3 n;

  if (aiVertices == NULL || av == NULL) { free(aiVertices); free(av); ppl->bOutOfMemory = 1; return; }

  /* drop repeated corners (same vertex or same position as the previous one) */
  for (i = 0; i < pf->ct; i++) {
    int iVertex = AT(&pod->aiFaceIndices, int, pf->iFirst + i);
    Vec3 v = avAll[iVertex];
    if (ct > 0 && (aiVertices[ct - 1] == iVertex || (v.x == av[ct - 1].x && v.y == av[ct - 1].y && v.z == av[ct - 1].z))) continue;
    aiVertices[ct] = iVertex;
    av[ct] = v;
    ct++;
  }
  while (ct > 1 && (aiVertices[ct - 1] == aiVertices[0]
    || (av[ct - 1].x == av[0].x && av[ct - 1].y == av[0].y && av[ct - 1].z == av[0].z))) {
    ct--;
  }

  if (ct < 3) { ppl->ctSkipped++; goto Done; }
  dExtent = Extent(av, ct);
  n = PolygonNormal(av, ct);
  if (dExtent <= 0 || Length(n) <= 1e-9 * dExtent * dExtent) { ppl->ctSkipped++; goto Done; }

  if (ct == 3 || (ct <= MAX_POLYGON_VERTICES && IsPlanarAndConvex(av, ct, n, dExtent))) {
    EmitPolygon(ppl, aiVertices, NULL, ct, pf->iMaterial);
  } else {
    Triangulate(ppl, aiVertices, av, ct, n, pf->iMaterial);
  }
Done:
  free(aiVertices);
  free(av);
}


/* ---------------------------------------------------------------------- */
/* Materials to texture names                                              */
/* ---------------------------------------------------------------------- */

typedef struct {
  char  strGamePath[MAX_PATH];  /* with trailing backslash */
  Array aTextures;              /* char[MAX_PATH], relative to the game folder */
  int   bIndexed;
} TextureIndex;

static void IndexDirectory(TextureIndex *pti, const char *strRelativeDir)
{
  char strPattern[MAX_PATH * 2];
  WIN32_FIND_DATAA fd;
  HANDLE hFind;

  _snprintf(strPattern, sizeof(strPattern) - 1, "%s%s*", pti->strGamePath, strRelativeDir);
  strPattern[sizeof(strPattern) - 1] = 0;
  hFind = FindFirstFileA(strPattern, &fd);
  if (hFind == INVALID_HANDLE_VALUE) return;
  do {
    char strRelative[MAX_PATH];
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
    if (strlen(strRelativeDir) + strlen(fd.cFileName) + 2 >= MAX_PATH) continue;
    sprintf(strRelative, "%s%s", strRelativeDir, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      strcat(strRelative, "\\");
      IndexDirectory(pti, strRelative);
    } else if (EndsWithNoCase(fd.cFileName, ".tex")) {
      char *str = (char *)ArrayPush(&pti->aTextures);
      if (str != NULL) strcpy(str, strRelative);
    }
  } while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
}

static const char *FindTextureByStem(TextureIndex *pti, const char *strStem)
{
  char strWanted[MAX_PATH];
  int i;
  if (strStem[0] == 0 || strlen(strStem) + 5 >= MAX_PATH) return NULL;
  if (!pti->bIndexed) {
    IndexDirectory(pti, "Textures\\");
    pti->bIndexed = 1;
  }
  sprintf(strWanted, "%s.tex", strStem);
  for (i = 0; i < pti->aTextures.ct; i++) {
    const char *str = &AT(&pti->aTextures, char, (size_t)i * MAX_PATH);
    const char *pchName = strrchr(str, '\\');
    pchName = (pchName != NULL) ? pchName + 1 : str;
    if (_stricmp(pchName, strWanted) == 0) return str;
  }
  return NULL;
}

/* Picks the surface name for a material: an existing .tex if one can be
   found, else the material name (the editor then uses its default texture). */
static int ResolveSurfaceName(TextureIndex *pti, const Material *pm, char *strOut, size_t ctOut)
{
  char strName[MAX_PATH], strTry[MAX_PATH * 2], strStem[MAX_PATH];
  const char *strFound;
  char *pch;

  lstrcpynA(strName, pm->strName, sizeof(strName));
  for (pch = strName; *pch != 0; pch++) if (*pch == '/') *pch = '\\';
  lstrcpynA(strOut, strName, (int)ctOut);
  if (pti == NULL) return 0;

  /* material named after a game texture: "Textures\...\Name.tex" or without .tex */
  _snprintf(strTry, sizeof(strTry) - 1, "%s%s", pti->strGamePath, strName);
  strTry[sizeof(strTry) - 1] = 0;
  if (EndsWithNoCase(strName, ".tex") && FileExists(strTry)) return 1;
  strncat(strTry, ".tex", sizeof(strTry) - strlen(strTry) - 1);
  if (FileExists(strTry)) {
    _snprintf(strOut, ctOut - 1, "%s.tex", strName);
    strOut[ctOut - 1] = 0;
    return 1;
  }

  /* a game texture with the same name as the diffuse map image or the material */
  StemOf(pm->strDiffuseMap, strStem, sizeof(strStem));
  strFound = FindTextureByStem(pti, strStem);
  if (strFound == NULL) {
    StemOf(strName, strStem, sizeof(strStem));
    strFound = FindTextureByStem(pti, strStem);
  }
  if (strFound != NULL) {
    lstrcpynA(strOut, strFound, (int)ctOut);
    return 1;
  }
  return 0;
}


/* ---------------------------------------------------------------------- */
/* Conversion                                                              */
/* ---------------------------------------------------------------------- */

int ObjToLwo(const char *strObjFile, const char *strLwoFile, const char *strGamePath,
             ObjImportStats *pStats, char *strError, size_t ctError)
{
  ObjData od;
  PolygonList pl;
  TextureIndex ti, *pti = NULL;
  Array aiRemap, astrSurfaces, aSurfaceColors;
  Buffer bPoints = { 0 }, bSurfaceNames = { 0 }, bPolygons = { 0 }, bSurfaceDefs = { 0 }, bBody = { 0 }, bFile = { 0 };
  int i, j, ctPoints = 0, iDefaultSurface = -1, ctTexturesFound = 0, bOK = 0;
  FILE *f;

  ArrayInit(&od.aVertices, sizeof(Vec3));
  ArrayInit(&od.aiFaceIndices, sizeof(int));
  ArrayInit(&od.aFaces, sizeof(Face));
  ArrayInit(&od.aMaterials, sizeof(Material));
  ArrayInit(&pl.aiPolygonIndices, sizeof(int));
  ArrayInit(&pl.aPolygons, sizeof(LwoPolygon));
  pl.ctSkipped = 0;
  pl.bOutOfMemory = 0;
  ArrayInit(&ti.aTextures, MAX_PATH);
  ArrayInit(&aiRemap, sizeof(int));
  ArrayInit(&astrSurfaces, MAX_PATH);
  ArrayInit(&aSurfaceColors, sizeof(unsigned long));
  if (pStats != NULL) memset(pStats, 0, sizeof(*pStats));

  if (strGamePath != NULL && strGamePath[0] != 0) {
    size_t ct = strlen(strGamePath);
    _snprintf(ti.strGamePath, sizeof(ti.strGamePath) - 1, "%s%s", strGamePath,
      (strGamePath[ct - 1] == '\\' || strGamePath[ct - 1] == '/') ? "" : "\\");
    ti.strGamePath[sizeof(ti.strGamePath) - 1] = 0;
    ti.bIndexed = 0;
    pti = &ti;
  }

  if (!ParseObj(&od, strObjFile, strError, ctError)) goto Done;

  /* faces must only use vertices that exist (positive indices may point forward) */
  for (i = 0; i < od.aiFaceIndices.ct; i++) {
    if (AT(&od.aiFaceIndices, int, i) >= od.aVertices.ct) {
      SetError(strError, ctError, "'%s' has a face using vertex %d, but only %d vertices exist.",
        strObjFile, AT(&od.aiFaceIndices, int, i) + 1, od.aVertices.ct);
      goto Done;
    }
  }
  for (i = 0; i < od.aFaces.ct; i++) AddFace(&pl, &od, &AT(&od.aFaces, Face, i));
  if (pl.bOutOfMemory) { SetError(strError, ctError, "Not enough memory to convert '%s'.", strObjFile); goto Done; }
  if (pl.aPolygons.ct == 0) {
    SetError(strError, ctError, "'%s' has no faces that can be imported.", strObjFile);
    goto Done;
  }

  /* keep only the vertices that polygons use */
  for (i = 0; i < od.aVertices.ct; i++) {
    int *pi = (int *)ArrayPush(&aiRemap);
    if (pi == NULL) goto OutOfMemory;
    *pi = -1;
  }
  for (i = 0; i < pl.aiPolygonIndices.ct; i++) {
    int iVertex = AT(&pl.aiPolygonIndices, int, i);
    if (AT(&aiRemap, int, iVertex) < 0) {
      AT(&aiRemap, int, iVertex) = ctPoints++;
      {
        Vec3 v = AT(&od.aVertices, Vec3, iVertex);
        PutF4(&bPoints, v.x);
        PutF4(&bPoints, v.y);
        PutF4(&bPoints, -v.z);       /* right-handed OBJ -> left-handed LightWave */
      }
    }
  }
  if (ctPoints > MAX_LWO_POINTS) {
    SetError(strError, ctError, "'%s' has %d vertices. The engine can import at most %d per object; "
      "split the model into smaller parts.", strObjFile, ctPoints, MAX_LWO_POINTS);
    goto Done;
  }

  /* one surface per distinct texture name */
  for (i = 0; i <= od.aMaterials.ct; i++) {
    char strSurface[MAX_PATH];
    Material mDefault;
    Material *pm;
    int bFound, bUsed = 0, iSurface;
    unsigned long ulColor;

    if (i < od.aMaterials.ct) {
      pm = &AT(&od.aMaterials, Material, i);
    } else {
      memset(&mDefault, 0, sizeof(mDefault));
      strcpy(mDefault.strName, DEFAULT_SURFACE_NAME);
      pm = &mDefault;
    }
    for (j = 0; j < pl.aPolygons.ct && !bUsed; j++) {
      int iMaterial = AT(&pl.aPolygons, LwoPolygon, j).iMaterial;
      bUsed = (i < od.aMaterials.ct) ? (iMaterial == i) : (iMaterial < 0);
    }
    if (!bUsed) continue;

    bFound = ResolveSurfaceName(pti, pm, strSurface, sizeof(strSurface));
    for (iSurface = 0; iSurface < astrSurfaces.ct; iSurface++) {
      if (_stricmp(&AT(&astrSurfaces, char, (size_t)iSurface * MAX_PATH), strSurface) == 0) break;
    }
    if (iSurface == astrSurfaces.ct) {
      char *str = (char *)ArrayPush(&astrSurfaces);
      unsigned long *pulColor = (unsigned long *)ArrayPush(&aSurfaceColors);
      double r = pm->bHasColor ? pm->r : 1.0, g = pm->bHasColor ? pm->g : 1.0, b = pm->bHasColor ? pm->b : 1.0;
      if (str == NULL || pulColor == NULL) goto OutOfMemory;
      strcpy(str, strSurface);
      if (r < 0) r = 0; if (r > 1) r = 1;
      if (g < 0) g = 0; if (g > 1) g = 1;
      if (b < 0) b = 0; if (b > 1) b = 1;
      ulColor = ((unsigned long)(r * 255 + 0.5) << 24) | ((unsigned long)(g * 255 + 0.5) << 16)
              | ((unsigned long)(b * 255 + 0.5) << 8);
      *pulColor = ulColor;
      if (bFound) ctTexturesFound++;
    }
    if (i < od.aMaterials.ct) pm->iSurface = iSurface;
    else iDefaultSurface = iSurface;
  }

  /* chunks */
  for (i = 0; i < astrSurfaces.ct; i++) PutS0(&bSurfaceNames, &AT(&astrSurfaces, char, (size_t)i * MAX_PATH));
  for (i = 0; i < pl.aPolygons.ct; i++) {
    LwoPolygon *pp = &AT(&pl.aPolygons, LwoPolygon, i);
    int iSurface = (pp->iMaterial >= 0) ? AT(&od.aMaterials, Material, pp->iMaterial).iSurface : iDefaultSurface;
    PutU2(&bPolygons, (unsigned int)pp->ct);
    /* reversed: see the note on winding at the top of this file */
    for (j = pp->ct - 1; j >= 0; j--) {
      PutU2(&bPolygons, (unsigned int)AT(&aiRemap, int, AT(&pl.aiPolygonIndices, int, pp->iFirst + j)));
    }
    PutU2(&bPolygons, (unsigned int)(iSurface + 1));
  }

  PutChunk(&bBody, "PNTS", &bPoints);
  PutChunk(&bBody, "SRFS", &bSurfaceNames);
  PutChunk(&bBody, "POLS", &bPolygons);
  for (i = 0; i < astrSurfaces.ct; i++) {
    unsigned long ulColor = AT(&aSurfaceColors, unsigned long, i);
    unsigned char aubColor[4];
    Buffer bSurface = { 0 };
    PutS0(&bSurface, &AT(&astrSurfaces, char, (size_t)i * MAX_PATH));
    Put(&bSurface, "COLR", 4);
    PutU2(&bSurface, 4);
    aubColor[0] = (unsigned char)(ulColor >> 24);
    aubColor[1] = (unsigned char)(ulColor >> 16);
    aubColor[2] = (unsigned char)(ulColor >> 8);
    aubColor[3] = 0;
    Put(&bSurface, aubColor, 4);
    PutChunk(&bSurfaceDefs, "SURF", &bSurface);
    if (bSurface.bFailed) bSurfaceDefs.bFailed = 1;
    free(bSurface.pub);
  }
  Put(&bBody, bSurfaceDefs.pub, bSurfaceDefs.ct);

  Put(&bFile, "FORM", 4);
  PutU4(&bFile, (unsigned long)(4 + bBody.ct));
  Put(&bFile, "LWOB", 4);
  Put(&bFile, bBody.pub, bBody.ct);
  if (bPoints.bFailed || bSurfaceNames.bFailed || bPolygons.bFailed || bSurfaceDefs.bFailed
   || bBody.bFailed || bFile.bFailed) {
    goto OutOfMemory;
  }

  f = fopen(strLwoFile, "wb");
  if (f == NULL || fwrite(bFile.pub, 1, bFile.ct, f) != bFile.ct) {
    if (f != NULL) fclose(f);
    SetError(strError, ctError, "Cannot write '%s'.", strLwoFile);
    goto Done;
  }
  if (fclose(f) != 0) {
    SetError(strError, ctError, "Cannot write '%s'.", strLwoFile);
    goto Done;
  }

  if (pStats != NULL) {
    pStats->ctVertices = ctPoints;
    pStats->ctPolygons = pl.aPolygons.ct;
    pStats->ctSurfaces = astrSurfaces.ct;
    pStats->ctTexturesFound = ctTexturesFound;
    pStats->ctSkippedFaces = pl.ctSkipped;
  }
  bOK = 1;
  goto Done;

OutOfMemory:
  SetError(strError, ctError, "Not enough memory to convert '%s'.", strObjFile);
Done:
  ArrayFree(&od.aVertices);
  ArrayFree(&od.aiFaceIndices);
  ArrayFree(&od.aFaces);
  ArrayFree(&od.aMaterials);
  ArrayFree(&pl.aiPolygonIndices);
  ArrayFree(&pl.aPolygons);
  ArrayFree(&ti.aTextures);
  ArrayFree(&aiRemap);
  ArrayFree(&astrSurfaces);
  ArrayFree(&aSurfaceColors);
  free(bPoints.pub); free(bSurfaceNames.pub); free(bPolygons.pub);
  free(bSurfaceDefs.pub); free(bBody.pub); free(bFile.pub);
  return bOK;
}
