// Modeler fixes (Modeler.exe, build of 13 Jan 2000).
//
// The winmm.dll proxy loads AlphaFixes.dll at Modeler.exe's entry point, the
// same way as in the game and the World Editor (Engine.dll imports winmm.dll).
// AF_Init() installs the engine fixes that apply here, most importantly the
// stream fix (streamfix.cpp): on 64-bit Windows the Modeler crashed as soon as
// it read a file (ACCESS_VIOLATION in Engine.dll at 6001171A while the File >
// Open dialog was up), since its loading runs inside window procedures.
//
// Crash at startup when the released Modeler was used before: the toolbar
// layout under HKCU\Software\CroTeam\Modeler is shared with it, the same
// problem as the World Editor's (barstate.cpp).  CMainFrame::OnCreate loads it
// at 0x004127A0 and CMainFrame saves it at 0x00412803.
//
// Saving.  CModelerDoc::OnSaveDocument (0x00417670) is a stub that only says
// "Saving is disabled in this version!", as in the World Editor.  The engine
// part is there: CEditModel::Save_t writes the .mdl, its .h include file (the
// animation and texture defines) and the .ini (the Modeler's own data: mapping,
// surfaces, patches), the same three files CEditModel::Load_t reads.  The mod
// puts its own OnSaveDocument in the CModelerDoc vtable (0x0042E788, slot 32).
// It mirrors the Modeler's OnOpenDocument (0x004174D0): make the path relative
// to the game folder with CTFileName::RemoveApplicationPath_t(), save the
// document's model (m_emEditModel, at this+0xD0) and clear the modified flag.
// Engine errors are shown with the engine's WarningMessage().  Read-only
// target files (many of the alpha's are) are reported before anything is
// written, since Save_t stops at the first file it can't create.
#include "alphafixes.h"

namespace {

// Modeler.exe (13 Jan 2000, always at 0x00400000)
void **const MOD_DOC_VTABLE = (void **)0x0042E788;      // CModelerDoc vtable
constexpr int MOD_VTBL_SETMODIFIEDFLAG = 25;             // CDocument::SetModifiedFlag(BOOL)
constexpr int MOD_VTBL_ONSAVEDOCUMENT = 32;              // CDocument::OnSaveDocument(LPCTSTR)
void *const MOD_ONSAVEDOCUMENT_STUB = (void *)0x00417670;
constexpr size_t MOD_DOC_EDITMODEL = 0xD0;               // CModelerDoc::m_emEditModel

// the engine's CTFileName: CTString (one engine-allocated char *) plus one long
struct EngFileName { char *str; long lReserved; };

using Method_t = void(__fastcall *)(void *pThis, void *pvEdx);
using FileNameCtor_t = void(__fastcall *)(EngFileName *pfnm, void *pvEdx, const char *str, int iOffset);
using FileNameCopy_t = void(__fastcall *)(EngFileName *pfnm, void *pvEdx, const EngFileName *pfnmOther);
using FileNameDtor_t = void(__fastcall *)(EngFileName *pfnm, void *pvEdx);
using EditModelSave_t = void(__fastcall *)(void *pem, void *pvEdx, EngFileName fnm);   // by value: the callee destroys it
using SetModifiedFlag_t = void(__fastcall *)(void *pDoc, void *pvEdx, BOOL bModified);
using WarningMessage_t = void(__cdecl *)(const char *strFormat, ...);

FileNameCtor_t _pfnFileNameCtor = nullptr;
FileNameCopy_t _pfnFileNameCopy = nullptr;
FileNameDtor_t _pfnFileNameDtor = nullptr;
Method_t _pfnRemoveApplicationPath = nullptr;
EditModelSave_t _pfnEditModelSave = nullptr;
WarningMessage_t _pfnWarningMessage = nullptr;

// Many of the alpha's model files are read-only.  CEditModel::Save_t writes the
// .mdl, then the .h, then the .ini, and stops at the first it can't create, so
// a read-only .h or .ini left a half-saved model.  All three are checked first.
bool ReadOnlyFiles(const char *strPathName, char *strError, size_t ctError)
{
  std::string strBase = strPathName;
  size_t iDot = strBase.find_last_of('.'), iSlash = strBase.find_last_of("\\/");
  if (iDot != std::string::npos && (iSlash == std::string::npos || iDot > iSlash)) strBase.erase(iDot);
  std::string strList;
  for (const char *strExt : { ".mdl", ".h", ".ini" }) {
    DWORD dwAttrib = GetFileAttributesA((strBase + strExt).c_str());
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_READONLY)) {
      std::string strFile = strBase + strExt;
      size_t i = strFile.find_last_of("\\/");
      strList += (strList.empty() ? "" : ", ") + (i == std::string::npos ? strFile : strFile.substr(i + 1));
    }
  }
  if (strList.empty()) return false;
  _snprintf(strError, ctError - 1,
            "Cannot save: read-only file(s) %s.\n\nA model is saved as its .mdl, .h and .ini files. Clear the "
            "read-only flag in the files' Properties to save over them, or use Save As.", strList.c_str());
  strError[ctError - 1] = 0;
  return true;
}

bool SaveModel(void *pDoc, const char *strPathName, char *strError, size_t ctError)
{
  if (ReadOnlyFiles(strPathName, strError, ctError)) return false;
  EngFileName fnm = {};
  volatile bool bConstructed = false;
  volatile bool bSaved = false;
  __try {
    _pfnFileNameCtor(&fnm, nullptr, strPathName, 0);
    bConstructed = true;
    _pfnRemoveApplicationPath(&fnm, nullptr);   // models must be saved inside the game folder
    EngFileName fnmArg;
    _pfnFileNameCopy(&fnmArg, nullptr, &fnm);
    _pfnEditModelSave((char *)pDoc + MOD_DOC_EDITMODEL, nullptr, fnmArg);
    bSaved = true;
  } __except (CatchEngineError(GetExceptionInformation(), strError, ctError, "Cannot save the model: unknown error.")) {
    bSaved = false;
  }
  if (bSaved) Log("Saved %s", fnm.str != nullptr ? fnm.str : strPathName);
  if (bConstructed) _pfnFileNameDtor(&fnm, nullptr);
  return bSaved;
}

// BOOL CModelerDoc::OnSaveDocument(LPCTSTR lpszPathName)
BOOL __fastcall ModelerDoc_OnSaveDocument(void *pDoc, void *pvEdx, const char *strPathName)
{
  char strError[1024];
  (void)pvEdx;
  if (!SaveModel(pDoc, strPathName, strError, sizeof(strError))) {
    Log("Saving %s failed: %s", strPathName, strError);
    _pfnWarningMessage("%s", strError);
    return FALSE;
  }
  void **ppvVTable = *(void ***)pDoc;
  ((SetModifiedFlag_t)ppvVTable[MOD_VTBL_SETMODIFIEDFLAG])(pDoc, nullptr, FALSE);
  return TRUE;
}

bool InstallSaveFix()
{
  if (MOD_DOC_VTABLE[MOD_VTBL_ONSAVEDOCUMENT] != MOD_ONSAVEDOCUMENT_STUB) return false;
  _pfnFileNameCtor = (FileNameCtor_t)EngineProc("??0CTFileName@@QAE@PBDH@Z");
  _pfnFileNameCopy = (FileNameCopy_t)EngineProc("??0CTFileName@@QAE@ABV0@@Z");
  _pfnFileNameDtor = (FileNameDtor_t)EngineProc("??1CTFileName@@QAE@XZ");
  _pfnRemoveApplicationPath = (Method_t)EngineProc("?RemoveApplicationPath_t@CTFileName@@QAEXXZ");
  _pfnEditModelSave = (EditModelSave_t)EngineProc("?Save_t@CEditModel@@QAEXVCTFileName@@@Z");
  _pfnWarningMessage = (WarningMessage_t)EngineProc("?WarningMessage@@YAXPBDZZ");
  if (!_pfnFileNameCtor || !_pfnFileNameCopy || !_pfnFileNameDtor || !_pfnRemoveApplicationPath || !_pfnEditModelSave
   || !_pfnWarningMessage) {
    return false;
  }
  return SetVTableSlot(&MOD_DOC_VTABLE[MOD_VTBL_ONSAVEDOCUMENT], (void *)ModelerDoc_OnSaveDocument);
}

constexpr uintptr_t MOD_LOADBAR_PUSH = 0x004127A0;   // push offset "General"
constexpr uintptr_t MOD_LOADBAR_CALL = 0x004127A7;   // call CFrameWnd::LoadBarState
constexpr uintptr_t MOD_SAVEBAR_PUSH = 0x00412803;   // push offset "General"

const BYTE _aubLoadPushOld[5] = { 0x68, 0x68, 0x9D, 0x43, 0x00 };
const BYTE _aubLoadCallOld[5] = { 0xE8, 0x24, 0x22, 0x01, 0x00 };
const BYTE _aubSavePushOld[5] = { 0x68, 0x70, 0x9D, 0x43, 0x00 };

}  // namespace

void InstallModelerFixes()
{
  bool bBarState = InstallBarStateFix(MOD_LOADBAR_PUSH, MOD_LOADBAR_CALL, MOD_SAVEBAR_PUSH,
                                      _aubLoadPushOld, _aubLoadCallOld, _aubSavePushOld);
  Log("Toolbar layout: %s", bBarState ? "installed" : "code differs, not installed");
  Log("Saving: %s", InstallSaveFix() ? "installed" : "code differs, not installed");
}
