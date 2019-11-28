/*
 * VIM - Vi IMproved    by Bram Moolenaar
 *
 * *****************************************************************
 * *****************************************************************
 * *****************************************************************
 * *****************************************************************
 * *              This is not a part of the official               *
 * *              version of Vim found on www.vim.org              *
 * *****************************************************************
 * *****************************************************************
 * *****************************************************************
 * *****************************************************************
 *
 * MUI support by Ola S�der. AmigaOS4 port by KAS1E.
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

#include "vim.h"
#include "version.h"

#include <libraries/asl.h>
#include <mui/TheBar_mcc.h>
#include <graphics/rpattr.h>
#include <proto/muimaster.h>
#include <proto/icon.h>
#include <proto/iffparse.h>
#ifndef __amigaos4__
#include <cybergraphx/cybergraphics.h>
#endif
#include <devices/rawkeycodes.h>
#include <clib/alib_protos.h>
#include <clib/debug_protos.h>
#include <libraries/gadtools.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/keymap.h>

#ifdef __amigaos4__

#include <dos/obsolete.h>

typedef unsigned long IPTR;

struct Library *MUIMasterBase = NULL;
struct MUIMasterIFace *IMUIMaster = NULL;

struct Library *CyberGfxBase = NULL;
struct CyberGfxIFace *ICyberGfx = NULL;

struct Library *KeymapBase = NULL;
struct KeymapIFace *IKeymap = NULL;

#define RPTAG_FgColor RPTAG_APenColor
#define RPTAG_BgColor RPTAG_BPenColor
#define RPTAG_PenMode TAG_IGNORE
#define KPrintF DebugPrintF

Object * VARARGS68K DoSuperNew(struct IClass *cl, Object *obj, ...);
Object * VARARGS68K DoSuperNew(struct IClass *cl, Object *obj, ...)
{
    Object *rc;
    va_list args;

    va_startlinear(args, obj);
    rc = (Object *) DoSuperMethod(cl, obj, OM_NEW, va_getlinearva(args, ULONG), NULL);
    va_end(args);

    return rc;
}

#endif

//------------------------------------------------------------------------------
// Macros - Debug and log
//------------------------------------------------------------------------------
#define PUT(E,S) KPrintF((CONST_STRPTR)"%s:\t%s\t(%s)\n",E,S,__func__)
#define ERR(S) PUT("ERR",S)
#if LLVL == 2 || LLVL == 1
# define WARN(S) PUT("WARN",S)
# if LLVL == 2
#  define INFO(S) PUT("INFO",S)
# else
#  define INFO(S)
# endif
#else
# define WARN(S)
# define INFO(S)
#endif
#define HERE \
do {static int c;KPrintF("%s[%ld]:%ld\n",__func__,__LINE__,++c);}while(0)

//------------------------------------------------------------------------------
// Macros - MUI
//------------------------------------------------------------------------------
#ifdef __MORPHOS__
# define DISPATCH_GATE(C) &C ## Gate
# define DISPATCH_ARGS void
# define DISPATCH_HEAD \
  Class *cls = (Class *) REG_A0; \
  Object *obj = (Object *) REG_A2; \
  Msg msg = (Msg) REG_A1
# define CLASS_DEF(C) \
  static IPTR C ## Dispatch (void); \
  static struct MUI_CustomClass * C ## Class; \
  const struct EmulLibEntry C ## Gate = \
  { TRAP_LIB, 0, (void (*) (void)) C ## Dispatch }; \
  struct C ## Data
#else
    #ifndef __amigaos4__
    # define DoSuperNew(C,O,...) DoSuperNewTags(C,O,NULL,__VA_ARGS__)
    #endif
# define DISPATCH_HEAD
# define DISPATCH_ARGS Class *cls, Object *obj, Msg msg
# define DISPATCH_GATE(C) C ## Dispatch
# define CLASS_DEF(C) \
  struct MUI_CustomClass * C ## Class; \
  struct C ## Data
#endif
#define DISPATCH(C) static IPTR C ## Dispatch (DISPATCH_ARGS)
#define CLASS_DATA(C) C ## Data
#define TAGBASE_sTx (TAG_USER | 27<<16)
#ifdef __GNUC__
# define MUIDSP static inline __attribute__((always_inline))
#else
# define MUIDSP static inline
#endif

//------------------------------------------------------------------------------
// Global variables - Console, menu and toolbar
//------------------------------------------------------------------------------
static Object *Con, *Mnu, *Tlb;

//------------------------------------------------------------------------------
// VimCon - MUI custom class handling everything that the console normally
//          takes care of when running Vim in text mode. It requires CGFX but
//          you probably wouldn't like to run gvim without RTG anyway.
//------------------------------------------------------------------------------
CLASS_DEF(VimCon)
{
    int cursor[3];
    int state, blink;
    int xdelta, ydelta, space;
    int xd1, yd1, xd2, yd2;
    struct BitMap *bm;
    struct RastPort rp;
    struct MUI_EventHandlerNode event;
    struct MUI_InputHandlerNode ticker;
    struct MUI_InputHandlerNode timeout;
};

//------------------------------------------------------------------------------
// VimCon public methods, parameters and constants
//------------------------------------------------------------------------------
#define MUIM_VimCon_Callback         (TAGBASE_sTx + 101)
#define MUIM_VimCon_GetState         (TAGBASE_sTx + 102)
#define MUIM_VimCon_Flush            (TAGBASE_sTx + 103)
#define MUIM_VimCon_DrawString       (TAGBASE_sTx + 104)
#define MUIM_VimCon_SetFgColor       (TAGBASE_sTx + 105)
#define MUIM_VimCon_SetBgColor       (TAGBASE_sTx + 106)
#define MUIM_VimCon_FillBlock        (TAGBASE_sTx + 107)
#define MUIM_VimCon_DeleteLines      (TAGBASE_sTx + 108)
#define MUIM_VimCon_DrawPartCursor   (TAGBASE_sTx + 109)
#define MUIM_VimCon_DrawHollowCursor (TAGBASE_sTx + 110)
#define MUIM_VimCon_SetTimeout       (TAGBASE_sTx + 111)
#define MUIM_VimCon_Timeout          (TAGBASE_sTx + 112)
#define MUIM_VimCon_Beep             (TAGBASE_sTx + 113)
#define MUIM_VimCon_Ticker           (TAGBASE_sTx + 114)
#define MUIM_VimCon_SetBlinking      (TAGBASE_sTx + 115)
#define MUIM_VimCon_StartBlink       (TAGBASE_sTx + 116)
#define MUIM_VimCon_StopBlink        (TAGBASE_sTx + 117)
#define MUIM_VimCon_Browse           (TAGBASE_sTx + 118)
#define MUIM_VimCon_SetTitle         (TAGBASE_sTx + 119)
#define MUIM_VimCon_IsBlinking       (TAGBASE_sTx + 120)
#define MUIM_VimCon_GetScreenDim     (TAGBASE_sTx + 121)
#define MUIM_VimCon_InvertRect       (TAGBASE_sTx + 122)
#define MUIM_VimCon_AppMessage       (TAGBASE_sTx + 123)
#define MUIM_VimCon_Copy             (TAGBASE_sTx + 124)
#define MUIM_VimCon_Paste            (TAGBASE_sTx + 125)
#define MUIM_VimCon_AboutMUI         (TAGBASE_sTx + 126)
#define MUIM_VimCon_MUISettings      (TAGBASE_sTx + 127)
#define MUIV_VimCon_State_Idle       (1 << 0)
#define MUIV_VimCon_State_Yield      (1 << 1)
#define MUIV_VimCon_State_Timeout    (1 << 2)
#define MUIV_VimCon_State_Unknown    (0)

struct MUIP_VimCon_SetFgColor
{
    ULONG MethodID;
    ULONG Color;
};

struct MUIP_VimCon_SetBgColor
{
    ULONG MethodID;
    ULONG Color;
};

struct MUIP_VimCon_Browse
{
    ULONG MethodID;
    ULONG Title;
    ULONG Drawer;
};

struct MUIP_VimCon_SetTitle
{
    ULONG MethodID;
    ULONG Title;
};

struct MUIP_VimCon_GetScreenDim
{
    ULONG MethodID;
    ULONG WidthPtr;
    ULONG HeightPtr;
};

struct MUIP_VimCon_SetTimeout
{
    ULONG MethodID;
    ULONG Timeout;
};

struct MUIP_VimCon_Callback
{
    ULONG MethodID;
    IPTR VimMenuPtr;
};

struct MUIP_VimCon_SetBlinking
{
    ULONG MethodID;
    ULONG Wait;
    ULONG On;
    ULONG Off;
};

struct MUIP_VimCon_DrawHollowCursor
{
    ULONG MethodID;
    ULONG Row;
    ULONG Col;
    ULONG Color;
};

struct MUIP_VimCon_DrawString
{
    ULONG MethodID;
    ULONG Row;
    ULONG Col;
    IPTR  Str;
    ULONG Len;
    ULONG Flags;
};

struct MUIP_VimCon_FillBlock
{
    ULONG MethodID;
    ULONG Row1;
    ULONG Col1;
    ULONG Row2;
    ULONG Col2;
    ULONG Color;
};

struct MUIP_VimCon_InvertRect
{
    ULONG MethodID;
    ULONG Row;
    ULONG Col;
    ULONG Rows;
    ULONG Cols;
};

struct MUIP_VimCon_DrawPartCursor
{
    ULONG MethodID;
    ULONG Row;
    ULONG Col;
    ULONG Width;
    ULONG Height;
    ULONG Color;
};

struct MUIP_VimCon_DeleteLines
{
    ULONG MethodID;
    ULONG Row;
    ULONG Lines;
    ULONG RegLeft;
    ULONG RegRight;
    ULONG RegBottom;
    ULONG Color;
};

struct MUIP_VimCon_AppMessage
{
    ULONG MethodID;
    ULONG Message;
};

struct MUIP_VimCon_Copy
{
    ULONG MethodID;
    ULONG Clipboard;
};

struct MUIP_VimCon_Paste
{
    ULONG MethodID;
    ULONG Clipboard;
};

//------------------------------------------------------------------------------
// VimConAppMessage - AppMessage notification handler
// Input:             Message
// Return:            0
//------------------------------------------------------------------------------
MUIDSP ULONG VimConAppMessage(Class *cls, Object *obj,
                              struct MUIP_VimCon_AppMessage *msg)
{
    // We assume that all arguments are valid files that we
    // have the permission to read from, the number of file
    // names equals the number of arguments from Workbench
    struct AppMessage *m = (struct AppMessage *) msg->Message;
    char_u **fnames = calloc(m->am_NumArgs, sizeof(char_u *));
    BPTR owd = CurrentDir(m->am_ArgList->wa_Lock);

    if(fnames)
    {
        BPTR f;
        int arg = 0;
        int nfiles = 0;

        // Traverse whatever we get in and save the names
        // of everything that we have permission to read
        while(arg < m->am_NumArgs)
        {
            // If we can get a read lock, Vim might be able to use this.
            CurrentDir(m->am_ArgList[arg].wa_Lock);
            f = Lock((STRPTR) m->am_ArgList[arg++].wa_Name, ACCESS_READ);

            if(f)
            {
                // Find out what the lock is refering to.
                char_u *fn = calloc(PATH_MAX, sizeof(char_u));

                if(fn)
                {
                    struct FileInfoBlock *fib = (struct FileInfoBlock *)
                           AllocDosObject(DOS_FIB, NULL);

                    if(fib)
                    {
                        // If it's a file, save it in the list.
                        if(Examine(f, fib) && fib->fib_DirEntryType < 0)
                        {
                            NameFromLock(f, (STRPTR) fn, PATH_MAX);
                            fnames[nfiles++] = fn;
                        }

                        // Otherwise, skip it.
                        FreeDosObject(DOS_FIB, fib);
                    }

                    // Done.
                    UnLock(f);
                }
                else
                {
                    // Free and bail.
                    while(nfiles--)
                    {
                        free(fnames[nfiles]);
                    }

                    UnLock(f);
                    ERR("Out of memory");
                    break;
                }
            }
            else
            {
                // Something else. Pretend this
                // didn't happen.
                WARN("Could not acquire lock");
            }
        }

        // Don't do anything if all we get is garbage
        if(nfiles > 0)
        {
            // Transpose and cap mouse coordinates
            int x = m->am_MouseX - _mleft(obj);
            int y = m->am_MouseY - _mtop(obj);

            x = x > 0 ? x : 0;
            y = y > 0 ? y : 0;
            x = x >= _mwidth(obj) ? _mwidth(obj) - 1 : x;
            y = y >= _mheight(obj) ? _mheight(obj) - 1 : y;

            // There was something among the arguments that we
            // could not acquire a read lock for. Shrink list
            // of files before handing it over to Vim
            if(nfiles < m->am_NumArgs)
            {
                char_u **shrunk = calloc(nfiles, sizeof(char_u *));

                if(shrunk)
                {
                    // Copy old contents to new list.
                    for(arg = 0; arg < nfiles; ++arg)
                    {
                        shrunk[arg] = fnames[arg];
                    }
                }

                // Replace old list with new list.
                free(fnames);
                fnames = shrunk;
            }

            // We might be empty handed here, the
            // shrunk allocation could have failed.
            if(fnames)
            {
                // In some cases Vim will try to interact with the
                // user when handling the file drop. Activate the
                // window to save one annoying mouse click in that
                // case
                set(_win(obj), MUIA_Window_Activate, TRUE);
                gui_handle_drop(x, y, 0, fnames, nfiles);
            }
            else
            {
                // Shrinkage failed.
                ERR("Out of memory");
            }
        }
        else
        {
            // Since we can't pass anything over to
            // Vim we need to do this ourselves
            free(fnames);
        }
    }
    else
    {
        ERR("Out of memory");
    }

    // Go back to where we started and
    // show whatever (if anything) was
    // read from disk on screen
    CurrentDir(owd);
    MUI_Redraw(obj, MADF_DRAWUPDATE);
    return 0;
}

//------------------------------------------------------------------------------
// VimConStopBlink - Disable cursor blinking
// Input:            -
// Return:           TRUE on state change, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConStopBlink(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // If enabled, remove input handler and reset status
    if(my->blink)
    {
        my->blink = 0;
        DoMethod(_app(obj), MUIM_Application_RemInputHandler, &my->ticker);
        return TRUE;
    }

    // Nothing to do
    return FALSE;
}

//------------------------------------------------------------------------------
// VimConIsBlinking - Get cursor state
// Input:             -
// Return:            TRUE if cursor is blinking, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConIsBlinking(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Indexing any of the delays?
    return my->blink ? TRUE : FALSE;
}

//------------------------------------------------------------------------------
// VimConAboutMUI - Show MUI about window
// Input:         -
// Return:        0
//------------------------------------------------------------------------------
MUIDSP IPTR VimConAboutMUI(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Needed to not mess up the message loop
    my->state |= MUIV_VimCon_State_Yield;
    DoMethod(_app(obj), MUIM_Application_AboutMUI, _win(obj));
    return 0;
}

//------------------------------------------------------------------------------
// VimConMUISettings - Open MUI settings
// Input:            -
// Return:           0
//------------------------------------------------------------------------------
MUIDSP IPTR VimConMUISettings(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Needed to not mess up the message loop
    my->state |= MUIV_VimCon_State_Yield;
    DoMethod(_app(obj), MUIM_Application_OpenConfigWindow, _win(obj));
    return 0;
}

//------------------------------------------------------------------------------
// VimConStartBlink - Enable cursor blinking
// Input:             -
// Return:            TRUE on state change, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConStartBlink(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // If not enabled and none of the delays (wait, on, off) are 0
    // add input handler and increase status / delay index
    if(!my->blink && my->cursor[0] && my->cursor[1] && my->cursor[2])
    {
        my->ticker.ihn_Millis = my->cursor[my->blink++];
        DoMethod(_app(obj), MUIM_Application_AddInputHandler, &my->ticker);
        return TRUE;
    }

    // Nothing to do
    return FALSE;
}

//------------------------------------------------------------------------------
// VimConSetBlinking - Set wait, on and off values
// Input:              Wait - initial delay in ms until blinking starts
//                     On - number of ms when the cursor is shown
//                     Off - number of ms when the cursor is hidden
// Return:             TRUE
//------------------------------------------------------------------------------
MUIDSP IPTR VimConSetBlinking(Class *cls, Object *obj,
                              struct MUIP_VimCon_SetBlinking *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Accept anything. Filter later.
    my->cursor[0] = (int) msg->Wait;
    my->cursor[1] = (int) msg->Off;
    my->cursor[2] = (int) msg->On;
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConBrowse - Open file requester
// Input:         Title - Title of the file requester to be created
// Return:        Absolute path of selected file / NULL if cancelled
//------------------------------------------------------------------------------
MUIDSP IPTR VimConBrowse(Class *cls, Object *obj,
                         struct MUIP_VimCon_Browse *msg)
{
    STRPTR res = NULL;
    struct FileRequester *req;

    // Create file requester
    req = MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, msg->Title,
                                  ASLFR_InitialDrawer, msg->Drawer, TAG_DONE);
    if(!req)
    {
        ERR("Failed creating file requester");
        return (IPTR) NULL;
    }

    // Go to sleep and show requester
    set(_app(obj), MUIA_Application_Sleep, TRUE);

    if(MUI_AslRequestTags(req,TAG_DONE) && req->fr_File)
    {
        // 2 extra bytes for term 0 + AddPart() separator
        size_t s = STRLEN(req->fr_Drawer) + STRLEN(req->fr_File) + 2;

        // Vim will take care of freeing this memory
        res = calloc(s, sizeof(unsigned char));

        if(res)
        {
            // Prepare the result
            STRCPY(res, req->fr_Drawer);
            AddPart(res, req->fr_File, s);
        }
        else
        {
            ERR("Out of memory");
        }
    }

    // Free memory and wake up!
    set(_app(obj), MUIA_Application_Sleep, FALSE);
    MUI_FreeAslRequest(req);
    return (IPTR) res;
}

//------------------------------------------------------------------------------
// VimConGetScreenDim - Get screen dimensions, or rather the dimensions of the
//                      off screen buffer used to render text. This might not
//                      be the same as the screen dimensions, but as far as
//                      Vim is concerned, it is.
// Input:               WidthPtr - ULONG * to contain (output) width
//                      HeightPtr - ULONG * to contain (output) height
// Return:              Buffer area
//------------------------------------------------------------------------------
MUIDSP IPTR VimConGetScreenDim(Class  *cls, Object *obj,
                               struct MUIP_VimCon_GetScreenDim *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);
    ULONG *w = (ULONG *) msg->WidthPtr;
    ULONG *h = (ULONG *) msg->HeightPtr;

    if(!my->bm)
    {
        ERR("No off screen buffer");
        return 0;
    }

    *w = GetBitMapAttr(my->bm, BMA_WIDTH);
    *h = GetBitMapAttr(my->bm, BMA_HEIGHT);
    return (*w) * (*h);
}

//------------------------------------------------------------------------------
// VimConSetTitle - Set window title
// Input:           Title - Window title
// Return:          TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConSetTitle(Class *cls, Object *obj,
                           struct MUIP_VimCon_SetTitle *msg)
{
    if(msg->Title)
    {
        set(_win(obj), MUIA_Window_Title, msg->Title);
        return TRUE;
    }

    WARN("Invalid title string");
    return FALSE;
}


//------------------------------------------------------------------------------
// VimConTicker - Event handler managing cursor state
// Input:         -
// Return:        TRUE if state changed, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConTicker(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Only on (2) or off (1) here.
    if(my->blink < 1 || my->blink > 2)
    {
        ERR("Invalid state");
        return FALSE;
    }

    // Remove current timer
    DoMethod(_app(obj), MUIM_Application_RemInputHandler, &my->ticker);

    // Hide or show cursor depending on index
    if(my->blink == 1)
    {
        // Hide cursor
        gui_undraw_cursor();
    }
    else
    {
        // Show cursor
        gui_update_cursor(TRUE, FALSE);
    }

    // Install new timer. Disallow values lower than 100ms (perf. reasons)
    my->ticker.ihn_Millis = my->cursor[my->blink] >= 100 ?
                            my->cursor[my->blink] : 100;

    DoMethod(_app(obj), MUIM_Application_AddInputHandler, &my->ticker);

    // Next index 1,2,1,2,1,2,1..
    my->blink = ~my->blink & 3;

    // Make the results visible
    MUI_Redraw(obj, MADF_DRAWUPDATE);
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConTimeout - Timeout handler
// Input:          -
// Return:         TRUE
//------------------------------------------------------------------------------
MUIDSP IPTR VimConTimeout(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Take note and keep on going
    my->state |= MUIV_VimCon_State_Timeout;
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConSetTimeout - Set time (in ms) until next timeout
// Input:             Timeout - Number of ms until the next timeout or 0.
//                              A value of 0 will disable timeouts.
// Return:            TRUE if state changed, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConSetTimeout(Class *cls, Object *obj,
                             struct MUIP_VimCon_SetTimeout *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Only act if old timeout != new timout
    if(my->timeout.ihn_Millis != msg->Timeout)
    {
        // If old timeout exists, remove it
        if(my->timeout.ihn_Millis)
        {
            DoMethod(_app(obj), MUIM_Application_RemInputHandler, &my->timeout);
            my->timeout.ihn_Millis = 0;
        }

        // If new timeout > 0, install new handler
        if(msg->Timeout)
        {
            my->timeout.ihn_Millis = (UWORD) msg->Timeout;
            DoMethod(_app(obj), MUIM_Application_AddInputHandler, &my->timeout);
        }

        return TRUE;
    }

    return FALSE;
}

//------------------------------------------------------------------------------
// VimConDirty - Tag rectangular part of raster port as dirty
// Input:        x1 - Left
//               y1 - Top
//               x2 - Right
//               y2 - Bottom
// Return:       -
//------------------------------------------------------------------------------
MUIDSP void VimConDirty(Class *cls, Object *obj, int x1, int y1, int x2, int y2)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Grow dirty region if it doesn't cover the new one.
    my->xd1 = x1 < my->xd1 ? x1 : my->xd1;
    my->xd2 = x2 > my->xd2 ? x2 : my->xd2;
    my->yd1 = y1 < my->yd1 ? y1 : my->yd1;
    my->yd2 = y2 > my->yd2 ? y2 : my->yd2;
}

//------------------------------------------------------------------------------
// VimConClean - Consider raster port clean
// Input:        -
// Return:       -
//------------------------------------------------------------------------------
MUIDSP void VimConClean(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Start with a 'negative' size
    my->xd1 = my->yd1 = INT_MAX;
    my->xd2 = my->yd2 = INT_MIN;
}

//------------------------------------------------------------------------------
// VimConDrawHollowCursor - Draw outline cursor
// Input:                   Row - Y
//                          Column - X
//                          Color - RGB color of cursor
// Return:                  TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConDrawHollowCursor(Class *cls, Object *obj,
                                   struct MUIP_VimCon_DrawHollowCursor *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);
    int x1 = msg->Col * my->xdelta;
    int y1 = msg->Row * my->ydelta;
    int x2 = x1 + my->xdelta - my->space;
    int y2 = y1 + my->ydelta;

    if(x2 > x1 && y2 > y1)
    {
        // Stick to CGFX
        FillPixelArray(&my->rp, x1, y1, my->xdelta, 1, msg->Color);
        FillPixelArray(&my->rp, x1, y1, 1, my->ydelta, msg->Color);
        FillPixelArray(&my->rp, x1, y2, my->xdelta, 1, msg->Color);
        FillPixelArray(&my->rp, x2, y1, 1, my->ydelta, msg->Color);

        // There's no hollow dirt
        VimConDirty(cls, obj, x1, y1, x2, y2);
        return TRUE;
    }

    WARN("Invalid geometry");
    return FALSE;
}

//------------------------------------------------------------------------------
// VimConDrawPartCursor - Draw part of cursor
// Input:                 Row - Y
//                        Column - X
//                        Width - Width in pixels
//                        Height - Height in pixels
//                        Color - RGB color
// Return:                TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConDrawPartCursor(Class *cls, Object *obj,
                                 struct MUIP_VimCon_DrawPartCursor *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);
    int x = msg->Col * my->xdelta;
    int xs = msg->Width;
    int y = msg->Row * my->ydelta + my->ydelta - msg->Height;
    int ys = msg->Height - my->space;

    if(xs > 0 && ys > 0)
    {
        FillPixelArray(&my->rp, x, y, xs, ys, msg->Color);
        VimConDirty(cls, obj, x, y, x + xs, y + ys);
        return TRUE;
    }

    WARN("Invalid cursor size");
    return FALSE;
}

//------------------------------------------------------------------------------
// VimConInvertRect - Invert colors in rectangular character block
// Input:             Row - Top character row
//                    Col - Left character column
//                    Rows - Number of rows
//                    Cols - Number of columns
// Return:            TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConInvertRect(Class *cls, Object *obj,
                             struct MUIP_VimCon_InvertRect *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);
    int x = msg->Col * my->xdelta;
    int xs = my->xdelta * msg->Cols;
    int y = msg->Row * my->ydelta;
    int ys = my->ydelta * msg->Rows;

    if( ys > 0 )
    {
        InvertPixelArray(&my->rp, x, y, xs, ys);
        VimConDirty(cls, obj, x, y, x + xs, y + ys);
        return TRUE;
    }

    WARN("Invalid block size");
    return FALSE;
}

//------------------------------------------------------------------------------
// VimConFillBlock - Fill character block with color
// Input:            Row1 - Top character row
//                   Col1 - Left character column
//                   Row2 - Bottom character row
//                   Col2 - Right character column
//                   Color - RGB color
// Return:           TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConFillBlock(Class *cls, Object *obj,
                            struct MUIP_VimCon_FillBlock *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);
    int x = msg->Col1 * my->xdelta;
    int xs = my->xdelta * (msg->Col2 + 1) - x;
    int y = msg->Row1 * my->ydelta;
    int ys = my->ydelta * (msg->Row2 + 1) - y;

    if(xs > 0 && ys > 0)
    {
        // We might be dealing with incomplete characters
        xs = xs + x > _mwidth(obj) ? _mwidth(obj) - x : xs;
        ys = ys + y > _mheight(obj) ? _mheight(obj) - y : ys;
        FillPixelArray(&my->rp, x, y, xs, ys, msg->Color);
        VimConDirty(cls, obj, x, y, x + xs, y + ys);
        return TRUE;
    }

    WARN("Invalid block size");
    return FALSE;
}

//------------------------------------------------------------------------------
// VimConDeleteLines - Delete line(s) of text / insert empty line(s) within
//                     rectangular text region
// Input:              Row - (1st) row to delete / where to insert empty line(s)
//                     Lines - Number of lines to delete / insert
//                     RegLeft - Left side of text region
//                     RegRight - Right side of text region
//                     RegBottom - Bottom of text region
//                     Color - RGB color used to fill empty lines
// Return:             TRUE if state changed, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConDeleteLines(Class *cls, Object *obj,
                              struct MUIP_VimCon_DeleteLines *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);
    int n = (int) msg->Lines;

    if(n)
    {
        int yctop, ycsiz, ydst, ysrc;
        int xsrcdst = my->xdelta * msg->RegLeft;
        int xsize = my->xdelta * (msg->RegRight + 1) - xsrcdst;
        int ysize = my->ydelta * (msg->RegBottom + 1) - msg->Row * my->ydelta;

        if(n > 0)
        {
            // Deletion
            ysize -= n * my->ydelta;
            ydst = msg->Row * my->ydelta;
            ysrc = ydst + n * my->ydelta;
            yctop = ydst + ysize;
            ycsiz = n * my->ydelta;
            VimConDirty(cls, obj, xsrcdst, ydst,  xsrcdst + xsize, yctop + ycsiz);
        }
        else
        {
            // Insertion
            ysize += n * my->ydelta;
            ysrc = msg->Row * my->ydelta;
            ydst = ysrc - n * my->ydelta;
            yctop = ysrc;
            ycsiz = - (n * my->ydelta);
            VimConDirty(cls, obj, xsrcdst, yctop,  xsrcdst + xsize, ydst + ysize);
        }

        // Blit and fill the abandoned area with Color
        MovePixelArray(xsrcdst, ysrc, &my->rp, xsrcdst, ydst , xsize, ysize);
        FillPixelArray(&my->rp, xsrcdst, yctop, xsize, ycsiz, msg->Color);
        return TRUE;
    }

    WARN("No lines to delete / insert");
    return FALSE;
}

//------------------------------------------------------------------------------
// VimConSetFgColor - Set foreground color
// Input:             Color - RGB color
// Return:            TRUE
//------------------------------------------------------------------------------
MUIDSP IPTR VimConSetFgColor(Class *cls, Object *obj,
                             struct MUIP_VimCon_SetFgColor *msg)
{
    static struct TagItem tags[] =
    {
        { .ti_Tag = RPTAG_FgColor  },
        { .ti_Tag = TAG_END        }
    };

    tags[0].ti_Data = msg->Color|0xFF000000;

    struct VimConData *my = INST_DATA(cls,obj);
    SetRPAttrsA(&my->rp, tags);
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConSetBgColor - Set background color
// Input:             Color - RGB color
// Return:            TRUE
//------------------------------------------------------------------------------
MUIDSP IPTR VimConSetBgColor(Class *cls, Object *obj,
                             struct MUIP_VimCon_SetBgColor *msg)
{
    static struct TagItem tags[] =
    {
        { .ti_Tag = RPTAG_BgColor  },
        { .ti_Tag = TAG_END        }
    };

    struct VimConData *my = INST_DATA(cls,obj);
    #ifdef __amigaos4__
    tags[0].ti_Data = msg->Color|0xFF000000;
    #else
    tags[0].ti_Data = msg->Color;
    #endif
    SetRPAttrsA(&my->rp, tags);
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConDrawString - Render string of text
// Input:             Row - Character row
//                    Col - Character column
//                    Str - String
//                    Len - Number of characters to render
//                    Flags - DRAW_UNDERL | DRAW_BOLD | DRAW_TRANSP
// Return:            TRUE if anything was rendered, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConDrawString(Class *cls, Object *obj,
                             struct MUIP_VimCon_DrawString *msg)
{
    // AROS is lacking SoftStyle.
    #ifndef RPTAG_SoftStyle
    # define RPTAG_SoftStyle TAG_IGNORE
    #endif

    struct VimConData *my = INST_DATA(cls,obj);

    // Do we have anything to print?
    if(msg->Len)
    {
        static struct TagItem tags[] =
        {
            { .ti_Tag = RPTAG_DrMd,
              .ti_Data = JAM2 },
            { .ti_Tag = RPTAG_SoftStyle,
              .ti_Data = FS_NORMAL },
            { .ti_Tag = TAG_END }
        };

        int y = msg->Row * my->ydelta,
            x = msg->Col * my->xdelta;

        static ULONG flags;

        // Tag area as dirty and move into position.
        VimConDirty(cls, obj, x, y, x + msg->Len * my->xdelta, y + my->ydelta);
        Move(&my->rp, x, y + my->rp.TxBaseline);

        if(flags != msg->Flags)
        {
            // Translate Vim flags to Amiga flags.
            tags[0].ti_Data = (msg->Flags & DRAW_TRANSP) ? JAM1 : JAM2;
            tags[1].ti_Data = (msg->Flags & (DRAW_UNDERL | DRAW_BOLD)) ?
                              ((msg->Flags & DRAW_UNDERL ? FSF_UNDERLINED : 0) |
                              (msg->Flags & DRAW_BOLD ? FSF_BOLD : 0)) : FS_NORMAL;

            // Set rastport attributes.
            SetRPAttrsA(&my->rp, tags);

            // Render into off screen buffer.
            Text(&my->rp, (CONST_STRPTR) msg->Str, msg->Len);

            // Reset draw mode to JAM2.
            if(tags[0].ti_Data != JAM2)
            {
                tags[0].ti_Data = JAM2;
                SetRPAttrsA(&my->rp, tags);
                msg->Flags ^= DRAW_TRANSP;
            }

            // Store until next invocation.
            flags = msg->Flags;
        }
        else
        {
            // Render text
            Text(&my->rp, (CONST_STRPTR) msg->Str, msg->Len);
        }
    }

    return TRUE;
}

//------------------------------------------------------------------------------
// VimConNew - Overloading OM_NEW
// Input:      See BOOPSI docs
// Return:     See BOOPSI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimConNew(Class *cls, Object *obj, struct opSet *msg)
{
    obj = (Object *) DoSuperNew(cls, obj, MUIA_Frame, MUIV_Frame_Text,
                                MUIA_InputMode, MUIV_InputMode_None,
                                MUIA_FillArea, FALSE, MUIA_Font,
                                MUIV_Font_Fixed, TAG_MORE, msg->ops_AttrList);

    if(!obj)
    {
        ERR("Unknown error");
        return (IPTR) NULL;
    }

    struct Screen *s = LockPubScreen(NULL);

    if(!s)
    {
        ERR("Could not lock public screen");
        CoerceMethod(cls, obj, OM_DISPOSE);
        return (IPTR) NULL;
    }

    struct VimConData *my = INST_DATA(cls,obj);

    my->bm = (struct BitMap *) AllocBitMap
             (GetBitMapAttr(s->RastPort.BitMap, BMA_WIDTH),
              GetBitMapAttr(s->RastPort.BitMap, BMA_HEIGHT),
              GetBitMapAttr(s->RastPort.BitMap, BMA_DEPTH),
              BMF_CLEAR | BMF_DISPLAYABLE | BMF_MINPLANES,
#ifndef __MORPHOS__
              s->RastPort.BitMap);
#else
              NULL);
#endif

    UnlockPubScreen(NULL, s);

    if(!my->bm)
    {
        ERR("Failed allocating bitmap memory");
        CoerceMethod(cls, obj, OM_DISPOSE);
        return (IPTR) NULL;
    }

    // Initial RP settings
    InitRastPort(&my->rp);
    my->rp.BitMap = my->bm;
    SetRPAttrs(&my->rp, RPTAG_DrMd, JAM2, RPTAG_PenMode, FALSE, TAG_DONE);

    // Static settings
    my->blink = 0;
    my->cursor[0] = 700;
    my->cursor[1] = 250;
    my->cursor[2] = 400;
    my->xdelta = my->ydelta = 1;
    my->xd1 = my->yd1 = INT_MAX;
    my->xd2 = my->yd2 = INT_MIN;
    my->timeout.ihn_Object = obj;
    my->timeout.ihn_Millis = 0;
    my->timeout.ihn_Flags = MUIIHNF_TIMER;
    my->timeout.ihn_Method = MUIM_VimCon_Timeout;
    my->ticker.ihn_Object = obj;
    my->ticker.ihn_Flags = MUIIHNF_TIMER;
    my->ticker.ihn_Method = MUIM_VimCon_Ticker;
    my->event.ehn_Priority = 1;
    my->event.ehn_Flags = 0;
    my->event.ehn_Object = obj;
    my->event.ehn_Class = NULL;
    my->event.ehn_Events = IDCMP_RAWKEY |
                           #ifdef __amigaos4__
                           IDCMP_EXTENDEDMOUSE |
                           #endif
                           IDCMP_MOUSEBUTTONS;
    return (IPTR) obj;
}

//------------------------------------------------------------------------------
// VimConSetup - Overloading MUIM_Setup
// Input:        See MUI docs
// Return:       See MUI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimConSetup(Class *cls, Object *obj, struct MUI_RenderInfo *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Setup parent class
    if(!DoSuperMethodA (cls, obj, (Msg) msg) )
    {
        ERR("Setup failed");
        return FALSE;
    }

    // Font might have changed
    SetFont(&my->rp, _font(obj));

    // Make room for smearing, no overlap allowed
    my->rp.TxSpacing = my->rp.Font->tf_BoldSmear;

    // If we aren't using a bitmap font we need to
    // do more work to determine the right amount
    // of spacing necessary to avoid overlap
    if(!(my->rp.Font->tf_Flags & FPF_DESIGNED))
    {
        int xs;
        struct TextExtent te;

        TextExtent(&my->rp, (STRPTR) "VI", 2, &te);
        xs = te.te_Extent.MaxX - te.te_Extent.MinX;
        xs -= my->rp.TxWidth * 2;
        my->rp.TxSpacing += xs;
    }

    my->xdelta = my->rp.TxWidth + my->rp.TxSpacing;
    my->ydelta = my->rp.TxHeight;
    my->space = my->rp.TxSpacing;
    gui.char_width = my->xdelta;
    gui.char_height = my->ydelta;

    // Install the main event handler
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, &my->event);

    // Install timeout timer if previously present
    if(my->timeout.ihn_Millis)
    {
        DoMethod(_app(obj), MUIM_Application_AddInputHandler, &my->timeout);
    }

    // Install blink handler if previously present
    if(my->blink)
    {
        DoMethod(_app(obj), MUIM_Application_AddInputHandler, &my->ticker);
    }

    // Yield CPU when init is done.
    my->state |= MUIV_VimCon_State_Yield;

    // Let Vim know about changes in size (if any)
    gui_resize_shell(_mwidth(obj), _mheight(obj));
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConDispose - Overloading OM_DISPOSE
// Input:          See BOOPSI docs
// Return:         See BOOPSI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimConDispose(Class *cls, Object *obj, Msg msg)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Free bitmap when no one's using it
    if(my->bm)
    {
        WaitBlit();
        FreeBitMap(my->bm);
        my->bm = NULL;
    }

    return DoSuperMethodA(cls, obj, msg);
}

//------------------------------------------------------------------------------
// VimConCleanup - Overloading MUIM_Cleanup
// Input:          See MUI docs
// Return:         See MUI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimConCleanup(Class *cls, Object *obj, Msg msg)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Remove timeout timer if present
    if(my->timeout.ihn_Millis)
    {
        DoMethod(_app(obj), MUIM_Application_RemInputHandler, &my->timeout);
    }

    // Remove cursor blink timer if present
    if(my->blink)
    {
        DoMethod(_app(obj), MUIM_Application_RemInputHandler, &my->ticker);
    }

    // Input handler for keys and mouse is always present
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, &my->event);

    // Let the superclass do its part
    return (IPTR) DoSuperMethodA(cls, obj, (Msg) msg);
}

//------------------------------------------------------------------------------
// VimConAddEvent - Add event subscription to event handler
// Input:           IDCMP event to add
// Return:          -
//------------------------------------------------------------------------------
static void VimConAddEvent(Class *cls, Object *obj, ULONG event)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Don't add the event twice.
    if(event != (my->event.ehn_Events & event))
    {
        // We can't modify event handlers on the fly,
        // we need to remove them and add them again.
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, &my->event);
        my->event.ehn_Events |= event;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, &my->event);
    }
    else
    {
        WARN("Event exists");
    }
}

//------------------------------------------------------------------------------
// VimConRemEvent - Remove event subscription from event handler
// Input:           IDCMP event to remove
// Return:          -
//------------------------------------------------------------------------------
static void VimConRemEvent(Class *cls, Object *obj, ULONG event)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Don't remove what's not there.
    if(my->event.ehn_Events & event)
    {
        // We can't modify event handlers on the fly,
        // we need to remove them and add them again.
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, &my->event);
        my->event.ehn_Events &= ~event;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, &my->event);
    }
    else
    {
        WARN("No such event");
    }
}

//------------------------------------------------------------------------------
// VimConMouseScrollEvent - Determine in which direction(s) we need to scroll.
// Input:                   IDCMP MOUSEMOVE event messaage
// Return:                  Vim mouse scroll event
//------------------------------------------------------------------------------
MUIDSP int VimConMouseScrollEvent(Class *cls, Object *obj,
                                  struct MUIP_HandleEvent *msg)
{
    int x = msg->imsg->MouseX;
    int y = msg->imsg->MouseY;
    int event = (y - _mtop(obj) < 0 ? MOUSE_4 : 0) |
                (y - _mtop(obj) >= _mheight(obj) ? MOUSE_5 : 0) |
                (x - _mleft(obj) < 0 ? MOUSE_7 : 0) |
                (x - _mleft(obj) >= _mwidth(obj) ? MOUSE_6 : 0);
    return event;
}

//------------------------------------------------------------------------------
// VimConMouseMove - Handle mouse dragging
// Input:            IDCMP MOUSEMOVE or INTUITICKS
// Return:           TRUE
//------------------------------------------------------------------------------
MUIDSP int VimConMouseMove(Class *cls, Object *obj,
                           struct MUIP_HandleEvent *msg)
{
    static int x = 0, y = 0, tick = FALSE;
    int out = msg->imsg->MouseX <= _mleft(obj) ||
              msg->imsg->MouseX >= _mright(obj) ||
              msg->imsg->MouseY <= _mtop(obj) ||
              msg->imsg->MouseY >= _mbottom(obj);

    // Replace MOUSEMOVE with INTUITICKS when we're outside
    if(out)
    {
        int event = VimConMouseScrollEvent(cls, obj, msg);
        gui_send_mouse_event(event, x, y, FALSE, 0);

        // But only do it once
        if(!tick)
        {
            VimConRemEvent(cls, obj, IDCMP_MOUSEMOVE);
            VimConAddEvent(cls, obj, IDCMP_INTUITICKS);
            tick = TRUE;
        }
    }
    // Replace INTUITICKS with MOUSEMOVE when we're inside
    else
    {
        x = msg->imsg->MouseX - _mleft(obj);
        y = msg->imsg->MouseY - _mtop(obj);

        // But only do it once
        if(tick)
        {
            VimConRemEvent(cls, obj, IDCMP_INTUITICKS);
            VimConAddEvent(cls, obj, IDCMP_MOUSEMOVE);
            tick = FALSE;
        }
    }

    gui_send_mouse_event(MOUSE_DRAG, x, y, FALSE, 0);
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConMouseHandleButton - Handle mouse click and release
// Input:                    IDCMP MOUSEBUTTON event messaage
// Return:                   TRUE if handled, FALSE otherwise.
//------------------------------------------------------------------------------
MUIDSP int VimConMouseHandleButton(Class *cls, Object *obj,
                                   struct MUIP_HandleEvent *msg)
{
    static int drag;
    int out = msg->imsg->MouseX <= _mleft(obj) ||
              msg->imsg->MouseX >= _mright(obj) ||
              msg->imsg->MouseY <= _mtop(obj) ||
              msg->imsg->MouseY >= _mbottom(obj);

    // Left button within region -> start dragging
    if(msg->imsg->Code == SELECTDOWN && !out)
    {
        drag = TRUE;
        VimConAddEvent(cls, obj, IDCMP_MOUSEMOVE);
        gui_send_mouse_event(MOUSE_LEFT, msg->imsg->MouseX - _mleft(obj),
                             msg->imsg->MouseY - _mtop(obj), FALSE, 0);
        return TRUE;
    }

    // Left button released -> stop dragging if we're doing so.
    if(drag && msg->imsg->Code == SELECTUP)
    {
        drag = FALSE;
        VimConRemEvent(cls, obj, IDCMP_MOUSEMOVE|IDCMP_INTUITICKS);
        gui_send_mouse_event(MOUSE_RELEASE, msg->imsg->MouseX - _mleft(obj),
                             msg->imsg->MouseY - _mtop(obj), FALSE, 0);
        return TRUE;
    }

    return FALSE;
}

//------------------------------------------------------------------------------
// VimConHandleRaw - Handle raw key event
// Input:            IDCMP RAW event message
// Return:           TRUE
//------------------------------------------------------------------------------
MUIDSP int VimConHandleRaw(Class *cls, Object *obj,
                           struct MUIP_HandleEvent *msg)
{
    static TEXT b[4];
    static char_u s[6];
    static struct InputEvent ie = { .ie_Class = IECLASS_RAWKEY };

    WORD w;
    int l = 0, c, m;

    ie.ie_Code = msg->imsg->Code;
    ie.ie_Qualifier = msg->imsg->Qualifier;

    // Are we dealing with a vanilla key?
    w = MapRawKey(&ie, (STRPTR) b, 4, 0);
    if(w == 1)
    {
        // If yes, we're done
        add_to_input_buf(b, w);
        return TRUE;
    }

    // No, something else....
    switch(msg->imsg->Code)
    {
    case RAWKEY_UP:
        c = TO_SPECIAL('k', 'u');
        break;

    case RAWKEY_DOWN:
        c = TO_SPECIAL('k', 'd');
        break;

    case RAWKEY_LEFT:
        c = TO_SPECIAL('k', 'l');
        break;

    case RAWKEY_RIGHT:
        c = TO_SPECIAL('k', 'r');
        break;

    case RAWKEY_F1:
        c = TO_SPECIAL('k', '1');
        break;

    case RAWKEY_F2:
        c = TO_SPECIAL('k', '2');
        break;

    case RAWKEY_F3:
        c = TO_SPECIAL('k', '3');
        break;

    case RAWKEY_F4:
        c = TO_SPECIAL('k', '4');
        break;

    case RAWKEY_F5:
        c = TO_SPECIAL('k', '5');
        break;

    case RAWKEY_F6:
        c = TO_SPECIAL('k', '6');
        break;

    case RAWKEY_F7:
        c = TO_SPECIAL('k', '7');
        break;

    case RAWKEY_F8:
        c = TO_SPECIAL('k', '8');
        break;

    case RAWKEY_F9:
        c = TO_SPECIAL('k', '9');
        break;

    case RAWKEY_F10:
        c = TO_SPECIAL('k', ';');
        break;

    case RAWKEY_F11:
        c = TO_SPECIAL('F', '1');
        break;

    case RAWKEY_F12:
        c = TO_SPECIAL('F', '2');
        break;

    case RAWKEY_HELP:
        c = TO_SPECIAL('%', '1');
        break;

    case RAWKEY_INSERT:
        c = TO_SPECIAL('k', 'I');
        break;

    case RAWKEY_HOME:
        c = TO_SPECIAL('k', 'h');
        break;

    case RAWKEY_END:
        c = TO_SPECIAL('@', '7');
        break;

    case RAWKEY_PAGEUP:
        c = TO_SPECIAL('k', 'P');
        break;

    case RAWKEY_PAGEDOWN:
        c = TO_SPECIAL('k', 'N');
        break;

    case RAWKEY_NM_WHEEL_DOWN:
        gui_send_mouse_event(MOUSE_5, msg->imsg->MouseX, msg->imsg->MouseY,
                             FALSE, 0);
        c = 0;
        break;

    case RAWKEY_NM_WHEEL_UP:
        gui_send_mouse_event(MOUSE_4, msg->imsg->MouseX, msg->imsg->MouseY,
                             FALSE, 0);
        c = 0;
        break;

    case RAWKEY_NM_WHEEL_LEFT:
        gui_send_mouse_event(MOUSE_7, msg->imsg->MouseX, msg->imsg->MouseY,
                             FALSE, 0);
        c = 0;
        break;

    case RAWKEY_NM_WHEEL_RIGHT:
        gui_send_mouse_event(MOUSE_6, msg->imsg->MouseX, msg->imsg->MouseY,
                             FALSE, 0);
        c = 0;
        break;

    default:
        c = 0;
    }

    if(c)
    {
        m = (msg->imsg->Qualifier & IEQUALIFIER_CONTROL) ? MOD_MASK_CTRL : 0;
        m |= (msg->imsg->Qualifier &
             (IEQUALIFIER_LALT|IEQUALIFIER_RALT)) ? MOD_MASK_ALT : 0;
        m |= (msg->imsg->Qualifier &
             (IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)) ? MOD_MASK_SHIFT : 0;
        m |= (msg->imsg->Qualifier &
             (IEQUALIFIER_LCOMMAND|IEQUALIFIER_RCOMMAND)) ? MOD_MASK_META : 0;
        c = simplify_key(c, &m);

        if(m)
        {
            s[l++] = CSI;
            s[l++] = KS_MODIFIER;
            s[l++] = (char_u) m;
        }

        if(IS_SPECIAL(c) )
        {
            s[l++] = CSI;
            s[l++] = K_SECOND(c);
            s[l++] = K_THIRD(c);
        }
        else
        {
            s[l++] = (char_u) c;
        }
    }

    add_to_input_buf(s, l);
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConHandleEvent - Top-level IDCMP event handler
// Input:              IDCMP event
// Return:             See MUI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimConHandleEvent(Class *cls, Object *obj,
                              struct MUIP_HandleEvent *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);

    switch(msg->imsg->Class)
    {
    case IDCMP_MOUSEMOVE:
    case IDCMP_INTUITICKS:
        VimConMouseMove(cls, obj, msg);
        break;

#ifdef __amigaos4__
    case IDCMP_EXTENDEDMOUSE:
        if(msg->imsg->Code == IMSGCODE_INTUIWHEELDATA)
        {
            struct IntuiWheelData *iwd = (struct IntuiWheelData *)msg->imsg->IAddress;
            if(iwd->WheelY<0)
                msg->imsg->Code=RAWKEY_NM_WHEEL_UP;
            else if(iwd->WheelY>0)
                msg->imsg->Code=RAWKEY_NM_WHEEL_DOWN;
            else if(iwd->WheelX<0)
                msg->imsg->Code=RAWKEY_NM_WHEEL_LEFT;
            else if(iwd->WheelX>0)
                msg->imsg->Code=RAWKEY_NM_WHEEL_RIGHT;

            VimConHandleRaw(cls, obj, msg);
        }
        break;
#endif

    case IDCMP_RAWKEY:
        VimConHandleRaw(cls, obj, msg);
        break;

    case IDCMP_MOUSEBUTTONS:
        // Fall through unless we eat the button.
        if(VimConMouseHandleButton(cls, obj, msg))
        {
            break;
        }

    default:
        // Leave the rest to our parent class
        return DoSuperMethodA(cls, obj, (Msg) msg);
    }

    my->state |= MUIV_VimCon_State_Yield;
    return MUI_EventHandlerRC_Eat;
}

//------------------------------------------------------------------------------
// VimConMinMax - Overloading MUIM_AskMinMax
// Input:         See MUI docs
// Return:        See MUI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimConMinMax(Class *cls, Object *obj, struct MUIP_AskMinMax *msg)
{
    IPTR r = (IPTR) DoSuperMethodA(cls, obj, (Msg) msg);
    struct VimConData *my = INST_DATA(cls,obj);
    if(!my->bm)
    {
        ERR("No off screen buffer");
        return 0;
    }

    // Maybe something less ad hoc?
    msg->MinMaxInfo->MaxWidth = GetBitMapAttr(my->bm, BMA_WIDTH);
    msg->MinMaxInfo->MaxHeight = GetBitMapAttr(my->bm, BMA_HEIGHT);
    msg->MinMaxInfo->MinWidth += msg->MinMaxInfo->MaxWidth >> 3L;
    msg->MinMaxInfo->MinHeight += msg->MinMaxInfo->MaxHeight >> 3L;

    msg->MinMaxInfo->DefWidth += (msg->MinMaxInfo->MaxWidth >> 1L) +
                                  msg->MinMaxInfo->MinWidth;
    msg->MinMaxInfo->DefHeight += (msg->MinMaxInfo->MaxHeight >> 1L) +
                                   msg->MinMaxInfo->MinHeight;
    return r;
}

//------------------------------------------------------------------------------
// VimConDraw - Overloading MUIM_Draw
// Input:       See MUI docs
// Return:      See MUI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimConDraw(Class *cls, Object *obj, struct MUIP_Draw *msg)
{
    IPTR r = (IPTR) DoSuperMethodA(cls, obj, (Msg) msg);
    struct VimConData *my = INST_DATA(cls,obj);
    LONG xs = 0, ys = 0, xd = 0,
         yd = 0, w = 0, h = 0;

    if(msg->flags & MADF_DRAWUPDATE)
    {
        // Anything dirty?
        if(my->xd1 < INT_MAX)
        {
            // Blit dirt
            xs = my->xd1;
            ys = my->yd1;
            xd = _mleft(obj) + my->xd1;
            yd = _mtop(obj) + my->yd1;
            w = my->xd2 - my->xd1;
            h = my->yd2 - my->yd1;
            w = w > _mwidth(obj) ? _mwidth(obj) : w;
            h = h > _mheight(obj) ? _mheight(obj) : h;

            // Forget the dirt.
            VimConClean(cls, obj);
        }
    }
    else if(msg->flags & MADF_DRAWOBJECT)
    {
        static int lw, lh;

        // Blit everything
        xs = ys = 0;
        xd = _mleft(obj);
        yd = _mtop(obj);
        w = _mwidth(obj);
        h = _mheight(obj);

        // Clear sub character trash if we're
        // growing.
        if(lw < w || lh < h)
        {
            if(lw < w)
            {
                lw = w % my->xdelta;
                FillPixelArray(&my->rp, w - lw, 0, lw, h, gui.back_pixel);
            }

            lw = w;

            if(lh < h)
            {
                lh = h % my->ydelta;
                FillPixelArray(&my->rp, 0, h - lh, w, lh, gui.back_pixel);
            }

            lh = h;
        }
        else
        {
            if(lw != w || lh != h)
            {
                lw = w;
                lh = h;
                w = h = 0;
            }
        }
    }

    // Something to do?
    if(w > 0 && h > 0)
    {
        ClipBlit(&my->rp, xs, ys, _rp(obj), xd, yd, w, h, 0xc0);
    }

    return r;
}

//------------------------------------------------------------------------------
// VimConCallback - Vim menu / button type callback method
// Input:           VimMenuPtr - Vim menuitem pointer
// Return:          TRUE on successful invocation of callback, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimConCallback(Class *cls, Object *obj,
                           struct MUIP_VimCon_Callback *msg)
{
    struct VimConData *my = INST_DATA(cls,obj);
    vimmenu_T *mp = (vimmenu_T *) msg->VimMenuPtr;

    if(mp && mp->cb)
    {
        // Treat menu / buttons like keyboard input
        my->state |= MUIV_VimCon_State_Yield;
        mp->cb(mp);
        return TRUE;
    }

    WARN("Invalid callback");
    return FALSE;
}

//------------------------------------------------------------------------------
// VimConGetState - Why did we wake up?
// Input:           -
// Return:          MUIV_VimCon_GetState_(Idle|Input|Timeout|Unknown)
//------------------------------------------------------------------------------
MUIDSP IPTR VimConGetState(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);

    // Can't idle and X at the same time
    if(my->state == MUIV_VimCon_State_Idle)
    {
        // A signal of some sort?
        return MUIV_VimCon_State_Idle;
    }

    // Yields take precendence over timeouts
    if(my->state & MUIV_VimCon_State_Yield)
    {
        my->state = MUIV_VimCon_State_Idle;
        return MUIV_VimCon_State_Yield;
    }

    // Timeout takes precedence over nothing
    if(my->state & MUIV_VimCon_State_Timeout)
    {
        my->state = MUIV_VimCon_State_Idle;
        return MUIV_VimCon_State_Timeout;
    }

    // Real trouble
    ERR("Unknown state");
    return MUIV_VimCon_State_Unknown;
}

//------------------------------------------------------------------------------
// VimConShow - Overloading MUIM_Show
// Input:       See MUI docs
// Return:      See MUI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimConShow(Class *cls, Object *obj, Msg msg)
{
    IPTR r = (IPTR) DoSuperMethodA(cls, obj, msg);
    gui_resize_shell(_mwidth(obj), _mheight(obj));
    return r;
}

//------------------------------------------------------------------------------
// VimConBeep - Visual bell
// Input:       -
// Return:      TRUE
//------------------------------------------------------------------------------
MUIDSP IPTR VimConBeep(Class *cls, Object *obj)
{
    struct VimConData *my = INST_DATA(cls,obj);
    InvertPixelArray(&my->rp, 0, 0, _mwidth(obj), _mheight(obj));
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    Delay(1);
    InvertPixelArray(&my->rp, 0, 0, _mwidth(obj), _mheight(obj));
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    Delay(1);
    return TRUE;
}

//------------------------------------------------------------------------------
// VimConCopy - Copy data from Vim clipboard to clipboard.device
// Input:       Clipboard
// Return:      0
//------------------------------------------------------------------------------
MUIDSP ULONG VimConCopy(Class *cls, Object *obj, struct MUIP_VimCon_Copy *msg)
{
    int type;
    long_u size;
    char_u *data;
    Clipboard_T *cbd = (Clipboard_T *) msg;

    if(!cbd->owned)
    {
        WARN("Clipboard not 'owned'");
        return 0;
    }

    clip_get_selection(cbd);
    cbd->owned = FALSE;
    type = clip_convert_selection(&data, &size, cbd);

    if(type == -1)
    {
        // Do nothing if conversion fails
        WARN("Could not convert selection");
    }
    else
    {
        struct IFFHandle *iffh = AllocIFF();

        if(iffh)
        {
            iffh->iff_Stream = (ULONG) OpenClipboard(PRIMARY_CLIP);

            if(iffh->iff_Stream)
            {
                LONG ftxt = MAKE_ID('F','T','X','T'),
                     chrs = MAKE_ID('C','H','R','S');

                InitIFFasClip(iffh);
                if(!OpenIFF(iffh, IFFF_WRITE))
                {
                    if(!PushChunk(iffh, ftxt, ID_FORM, IFFSIZE_UNKNOWN))
                    {
                        if(!PushChunk(iffh, 0, chrs, IFFSIZE_UNKNOWN))
                        {
                            WriteChunkBytes(iffh, data, size);
                            PopChunk(iffh);
                        }

                        PopChunk(iffh);
                    }

                    CloseIFF(iffh);
                }
                else
                {
                    WARN("IFF error");
                }

                CloseClipboard((struct ClipboardHandle *) iffh->iff_Stream);
            }
            else
            {
                WARN("Could not open clipboard");
            }

            FreeIFF(iffh);
        }
        else
        {
            ERR("Out of memory");
        }
    }

    vim_free(data);
    return 0;
}

//------------------------------------------------------------------------------
// VimConPaste - Paste data from clipboard.device
// Input:        Clipboard
// Return:       0
//------------------------------------------------------------------------------
MUIDSP ULONG VimConPaste(Class *cls, Object *obj,
                         struct MUIP_VimCon_Paste *msg)
{
    Clipboard_T *cbd = (Clipboard_T *) msg;
    struct IFFHandle *iffh = AllocIFF();

    if(iffh)
    {
        iffh->iff_Stream = (ULONG) OpenClipboard(PRIMARY_CLIP);
        if(iffh->iff_Stream)
        {
            LONG ftxt = MAKE_ID('F','T','X','T'),
                 chrs = MAKE_ID('C','H','R','S'),
                 cset = MAKE_ID('C','S','E','T');

            InitIFFasClip(iffh);

            // Open and set stop points
            if(!OpenIFF(iffh,IFFF_READ) && !StopChunk(iffh, ftxt, chrs) &&
               !StopChunk(iffh, ftxt, cset))
            {
                LONG stat;
                for(stat = IFFERR_EOC; stat == IFFERR_EOC; )
                {
                    for(stat = ParseIFF(iffh, IFFPARSE_SCAN); !stat;
                        stat = ParseIFF(iffh, IFFPARSE_SCAN))
                    {
                        struct ContextNode *c = CurrentChunk(iffh);

                        if(c && c->cn_Type == ftxt)
                        {
                            if(c->cn_ID == chrs)
                            {
                                // Start with a 1k buffer
                                LONG read;
                                LONG size = 1 << 10;
                                char_u *data = calloc(size, sizeof(char_u));

                                if(!data)
                                {
                                    ERR("Out of memory");
                                    break;
                                }

                                read = ReadChunkBytes(iffh, data, size);

                                // If 1k isn't enough ROL size until it is
                                while(read == size)
                                {
                                    char_u *next = calloc(size << 1, sizeof(char_u));
                                    if(!next)
                                    {
                                        ERR("Out of memory");
                                        stat = IFFERR_NOMEM;
                                        break;
                                    }
                                    // Read some more and do some stitching
                                    read += ReadChunkBytes(iffh, next + size, size);
                                    memcpy(next, data, size);
                                    free(data);
                                    data = next;
                                    size <<= 1;
                                }
                                // Yank unless we ran out of memory
                                if(!stat)
                                {
                                    clip_yank_selection(MCHAR, data, read, cbd);
                                }

                                free(data);
                            }

                            // Ignore code sets for now
                            if(c->cn_ID == cset)
                            {
                                WARN("ID_CSET not supported");
                            }
                        }
                    }
                }

                CloseIFF(iffh);
            }
            else
            {
                // Unknown IFF problem
                WARN("IFF error");
            }

            CloseClipboard((struct ClipboardHandle *) iffh->iff_Stream);
        }
        else
        {
            WARN("Could not open clipboard");
        }

        FreeIFF(iffh);
        return 0;
    }

    // Only the OOM break:s can make us end up here
    ERR("Out of memory");
    return 0;
}


//------------------------------------------------------------------------------
// VimConDispatch - MUI custom class dispatcher
// Input:           See dispatched method
// Return:          See dispatched method
//------------------------------------------------------------------------------
DISPATCH(VimCon)
{
    DISPATCH_HEAD;

    // Dispatch according to MethodID
    switch(msg->MethodID)
    {
    case OM_NEW:
        return VimConNew(cls, obj,
               (struct opSet *) msg);

    case OM_DISPOSE:
        return VimConDispose(cls, obj, msg);

    case MUIM_VimCon_AppMessage:
        return VimConAppMessage(cls, obj,
               (struct MUIP_VimCon_AppMessage *) msg);

    case MUIM_Setup:
        return VimConSetup(cls, obj,
               (struct MUI_RenderInfo *) msg);

    case MUIM_Cleanup:
        return VimConCleanup(cls, obj, msg);

    case MUIM_HandleEvent:
        return VimConHandleEvent(cls, obj,
               (struct MUIP_HandleEvent *) msg);

    case MUIM_AskMinMax:
        return VimConMinMax(cls, obj,
               (struct MUIP_AskMinMax *) msg);

    case MUIM_Draw:
        return VimConDraw(cls, obj,
               (struct MUIP_Draw *) msg);

    case MUIM_Show:
        return VimConShow(cls, obj, msg);

    case MUIM_VimCon_Copy:
        return VimConCopy(cls, obj,
               (struct MUIP_VimCon_Copy *) msg);

    case MUIM_VimCon_Paste:
        return VimConPaste(cls, obj,
               (struct MUIP_VimCon_Paste *) msg);

    case MUIM_VimCon_Callback:
        return VimConCallback(cls, obj,
               (struct MUIP_VimCon_Callback *) msg);

    case MUIM_VimCon_DrawString:
        return VimConDrawString(cls, obj,
               (struct MUIP_VimCon_DrawString *) msg);

    case MUIM_VimCon_SetFgColor:
        return VimConSetFgColor(cls, obj,
               (struct MUIP_VimCon_SetFgColor *) msg);

    case MUIM_VimCon_SetBgColor:
        return VimConSetBgColor(cls, obj,
               (struct MUIP_VimCon_SetBgColor *) msg);

    case MUIM_VimCon_FillBlock:
        return VimConFillBlock(cls, obj,
               (struct MUIP_VimCon_FillBlock *) msg);

    case MUIM_VimCon_InvertRect:
        return VimConInvertRect(cls, obj,
               (struct MUIP_VimCon_InvertRect *) msg);

    case MUIM_VimCon_DeleteLines:
        return VimConDeleteLines(cls, obj,
               (struct MUIP_VimCon_DeleteLines *) msg);

    case MUIM_VimCon_DrawPartCursor:
        return VimConDrawPartCursor(cls, obj,
               (struct MUIP_VimCon_DrawPartCursor *) msg);

    case MUIM_VimCon_DrawHollowCursor:
        return VimConDrawHollowCursor(cls, obj,
               (struct MUIP_VimCon_DrawHollowCursor *) msg);

    case MUIM_VimCon_SetTimeout:
        return VimConSetTimeout(cls, obj,
               (struct MUIP_VimCon_SetTimeout *) msg);

    case MUIM_VimCon_SetBlinking:
        return VimConSetBlinking(cls, obj,
               (struct MUIP_VimCon_SetBlinking *) msg);

    case MUIM_VimCon_Browse:
        return VimConBrowse(cls, obj,
               (struct MUIP_VimCon_Browse *) msg);

    case MUIM_VimCon_SetTitle:
        return VimConSetTitle(cls, obj,
               (struct MUIP_VimCon_SetTitle *) msg);

    case MUIM_VimCon_GetScreenDim:
        return VimConGetScreenDim(cls, obj,
               (struct MUIP_VimCon_GetScreenDim *) msg);

    case MUIM_VimCon_GetState:
        return VimConGetState(cls, obj);

    case MUIM_VimCon_Timeout:
        return VimConTimeout(cls, obj);

    case MUIM_VimCon_Ticker:
        return VimConTicker(cls, obj);

    case MUIM_VimCon_Beep:
        return VimConBeep(cls, obj);

    case MUIM_VimCon_StartBlink:
        return VimConStartBlink(cls, obj);

    case MUIM_VimCon_StopBlink:
        return VimConStopBlink(cls, obj);

    case MUIM_VimCon_IsBlinking:
        return VimConIsBlinking(cls, obj);

    case MUIM_VimCon_AboutMUI:
        return VimConAboutMUI(cls, obj);

    case MUIM_VimCon_MUISettings:
        return VimConMUISettings(cls, obj);

    case MUIM_VimCon_Flush:
        MUI_Redraw(obj, MADF_DRAWUPDATE);
        return 0;
    }

    // Unknown method, promote to parent.
    return DoSuperMethodA(cls, obj, msg);
}

//------------------------------------------------------------------------------
// VimToolbar - MUI custom class handling the toolbar. Currently this class
//              is rather primitve, pretty much everything is hardcoded and
//              slow, but it does the job for now. We currently ignore the
//              user settings to achieve something which looks like the bar
//              on other platforms, refer to gui_mch_init() for details.
//------------------------------------------------------------------------------
CLASS_DEF(VimToolbar)
{
    struct MUIS_TheBar_Button *btn;
};

//------------------------------------------------------------------------------
// VimToolbar public methods and parameters
//------------------------------------------------------------------------------
#define MUIM_VimToolbar_AddButton     (TAGBASE_sTx + 202)
#define MUIM_VimToolbar_DisableButton (TAGBASE_sTx + 203)

struct MUIP_VimToolbar_AddButton
{
    ULONG MethodID;
    ULONG ID;
    IPTR Label;
    IPTR Help;
};

struct MUIP_VimToolbar_DisableButton
{
    ULONG MethodID;
    ULONG ID;
    ULONG Grey;
};

//------------------------------------------------------------------------------
// VimToolbarAddButton - Add button to toolbar
// Input:                ID - Vim menu item pointer
//                       Label - Button text (not shown)
//                       Help - Help text shown when hovering over button
// Return:               TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimToolbarAddButton(Class *cls, Object *obj,
                                struct MUIP_VimToolbar_AddButton *msg)
{
    struct VimToolbarData *my = INST_DATA(cls,obj);
    struct MUIS_TheBar_Button *b = my->btn;

    // Traverse our static toolbar and set up
    // a notification if we find a match.
    while(b->img != MUIV_TheBar_End)
    {
        const char *n = (const char *) msg->Label, *h = b->help;

        if(h && !strcmp(n, h))
        {
            DoMethod(obj, MUIM_TheBar_Notify, b->ID,
                      MUIA_Pressed, FALSE, Con, 2,
                      MUIM_VimCon_Callback, msg->ID);

            // This is a bit of a hack; save the Vim
            // menu item pointer as the parent class
            // of the button. Used to translate from
            // menu item to MUI button ID.
            b->_class = (struct IClass *) msg->ID;
            return TRUE;
        }
        else
        {
            b++;
        }
    }

    // We should consider not finding
    // a match an error. Otherwise we
    // will end up with dead buttons.
    ERR("Could not create button");
    return FALSE;
}

//------------------------------------------------------------------------------
// VimToolbarDisableButton - Disable button
// Input:                    ID - Vim menu item pointer
//                           Grey - TRUE to disable item, FALSE to enable
// Return:                   TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimToolbarDisableButton(Class *cls, Object *obj,
                                    struct MUIP_VimToolbar_DisableButton *msg)
{
    struct VimToolbarData *my = INST_DATA(cls,obj);
    struct MUIS_TheBar_Button *b = my->btn;

    // Traverse our static toolbar and look
    // for a Vim menu item match. If we find
    // one, we can translate this into a MUI
    // ID and use the proper TheBar method.
    while(b->img != MUIV_TheBar_End)
    {
        if(msg->ID == (ULONG) b->_class)
        {
            // We found the MUI ID. Use the proper TheBar
            // method to disable / enable the button.
            DoMethod(obj, MUIM_TheBar_SetAttr, b->ID, MUIV_TheBar_Attr_Disabled,
                     msg->Grey);
            return TRUE;
        }
        else
        {
            b++;
        }
    }

    // This is expected since we ignore
    // some of the menu items that Vim
    // is throwing at us. We might want
    // to know anyway.
    WARN("Button not found");
    return FALSE;
}

//------------------------------------------------------------------------------
// VimToolbarNew - Overloading OM_NEW
// Input:          See BOOPSI docs
// Return:         See BOOPSI docs
//------------------------------------------------------------------------------
MUIDSP IPTR VimToolbarNew(Class *cls, Object *obj, struct opSet *msg)
{
    struct VimToolbarData *my;
    static struct MUIS_TheBar_Button b[] =
    {
        { .ID = 1, .img = 0, .help = "Open"        },
        { .ID = 2, .img = 1, .help = "Save"        },
        { .ID = 3, .img = 2, .help = "SaveAll"     },
        { .img = MUIV_TheBar_BarSpacer             },
        { .ID = 5, .img = 3, .help = "Undo"        },
        { .ID = 6, .img = 4, .help = "Redo"        },
        { .img = MUIV_TheBar_BarSpacer             },
        { .ID = 8, .img = 5, .help = "Cut"         },
        { .ID = 9, .img = 6, .help = "Copy"        },
        { .ID = 10, .img = 7, .help = "Paste"      },
        { .img = MUIV_TheBar_BarSpacer             },
        { .ID = 12, .img = 8, .help = "Replace"    },
        { .ID = 13, .img = 9,  .help = "FindNext"  },
        { .ID = 14, .img = 10, .help = "FindPrev"  },
        { .img = MUIV_TheBar_BarSpacer             },
        { .ID = 16, .img = 11, .help = "LoadSesn"  },
        { .ID = 17, .img = 12, .help = "SaveSesn"  },
        { .ID = 18, .img = 13, .help = "RunScript" },
        { .img = MUIV_TheBar_BarSpacer             },
        { .ID = 20, .img = 14, .help = "Make"      },
        { .ID = 21, .img = 15, .help = "RunCtags"  },
        { .ID = 22, .img = 16, .help = "TagJump"   },
        { .img = MUIV_TheBar_BarSpacer             },
        { .ID = 24, .img = 17, .help = "Help"      },
        { .ID = 25, .img = 18, .help = "FindHelp"  },
        { .img = MUIV_TheBar_End                   },
    };

    BPTR icons = Lock("VIM:icons", ACCESS_READ);

    if(!icons)
    {
#ifdef __amigaos4__
        struct TagItem tags[] = { ESA_Position, REQPOS_CENTERSCREEN, TAG_DONE };
#endif
        struct EasyStruct req =
        {
            .es_StructSize   = sizeof(struct EasyStruct),
            .es_Flags        = 0, // ESF_SCREEN | ESF_TAGGED | ESF_EVENSIZE;
            .es_Title        = (UBYTE *)"Error",
            .es_TextFormat   = (UBYTE *)"Invalid VIM assign",
            .es_GadgetFormat = (UBYTE *)"OK",
#ifdef __amigaos4__
            .es_Screen       = ((struct IntuitionBase *)IntuitionBase)->ActiveScreen,
            .es_TagList      = tags
#endif
        };

        EasyRequest(NULL, &req, NULL, NULL);
        ERR("Invalid VIM: assign");
    }

    UnLock(icons);

    obj = (Object *) DoSuperNew(cls, obj, MUIA_Group_Horiz, TRUE,
                                MUIA_TheBar_Buttons, b,
                                MUIA_TheBar_IgnoreAppearance, TRUE,
                                MUIA_TheBar_Borderless, TRUE,
                                MUIA_TheBar_ViewMode, MUIV_TheBar_ViewMode_Gfx,
                                MUIA_TheBar_PicsDrawer, "VIM:icons",
                                MUIA_TheBar_Strip, "tb_strip.png",
                                MUIA_TheBar_DisStrip, "tb_dis_strip.png",
                                MUIA_TheBar_SelStrip, "tb_sel_strip.png",
                                MUIA_TheBar_StripCols, 19,
                                MUIA_TheBar_StripRows, 1,
                                MUIA_TheBar_StripHSpace, 0,
                                MUIA_TheBar_StripVSpace, 0,
                                TAG_MORE, msg->ops_AttrList);

    my = INST_DATA(cls,obj);
    my->btn = b;
    return (IPTR) obj;
}

//------------------------------------------------------------------------------
// VimToolbarDispatch - MUI custom class dispatcher
// Input:               See dispatched method
// Return:              See dispatched method
//------------------------------------------------------------------------------
DISPATCH(VimToolbar)
{
    DISPATCH_HEAD;

    // Dispatch according to MethodID
    switch(msg->MethodID)
    {
    case OM_NEW:
        return VimToolbarNew(cls, obj, (struct opSet *) msg);

    case MUIM_VimToolbar_AddButton:
        return VimToolbarAddButton(cls, obj,
            (struct MUIP_VimToolbar_AddButton *) msg);

    case MUIM_VimToolbar_DisableButton:
        return VimToolbarDisableButton(cls, obj,
            (struct MUIP_VimToolbar_DisableButton *) msg);
    }

    // Unknown method, the parent class might be able to care of it.
    return DoSuperMethodA(cls, obj, msg);
}

//------------------------------------------------------------------------------
// VimMenu - MUI custom class handling the menu. Currently it's not possible
//           to hide the menu. This must be fixed in order to support all
//           gui options in Vim. This class does not handle the fact that Vim
//           treats menu items and buttons in the same way, there's glue for
//           that in gui_mch_add_menu and gui_mch_add_menu_item.
//------------------------------------------------------------------------------
CLASS_DEF(VimMenu)
{
    int state;
};

//------------------------------------------------------------------------------
// VimMenu public methods and parameters
//------------------------------------------------------------------------------
#define MUIM_VimMenu_AddSpacer          (TAGBASE_sTx + 201)
#define MUIM_VimMenu_AddMenu            (TAGBASE_sTx + 202)
#define MUIM_VimMenu_AddMenuItem        (TAGBASE_sTx + 203)
#define MUIM_VimMenu_RemoveMenu         (TAGBASE_sTx + 204)
#define MUIM_VimMenu_Grey               (TAGBASE_sTx + 205)
#define MUIV_VimMenu_AddMenu_AlwaysLast (TAGBASE_sTx + 206)

struct MUIP_VimMenu_AddSpacer
{
    ULONG MethodID;
    ULONG ParentID;
};

struct MUIP_VimMenu_AddMenu
{
    ULONG MethodID;
    ULONG ParentID;
    ULONG ID;
    IPTR Label;
};

struct MUIP_VimMenu_AddMenuItem
{
    ULONG MethodID;
    ULONG ParentID;
    ULONG ID;
    IPTR Label;
};

struct MUIP_VimMenu_RemoveMenu
{
    ULONG MethodID;
    ULONG ID;
};

struct MUIP_VimMenu_Grey
{
    ULONG MethodID;
    ULONG ID;
    ULONG Grey;
};

//------------------------------------------------------------------------------
// VimMenuGrey - Enable/disable menu item
// Input:        ID - Menu item ID
//               Grey - TRUE to disable item, FALSE to enable
// Return:       TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimMenuGrey(Class *cls, Object *obj, struct MUIP_VimMenu_Grey *msg)
{
    Object *m;
    vimmenu_T *menu;

    // ID:s must be valid vim menu pointers
    if(!msg || !msg->ID)
    {
        WARN("Invalid menu");
        return FALSE;
    }
    else
    {
        menu = (vimmenu_T *) msg->ID;
    }

    // Ignore popup menus and separators
    if(menu_is_popup(menu->name) || menu_is_separator(menu->name) ||
       (menu->parent && menu_is_popup(menu->parent->name) ) )
    {
        return FALSE;
    }

    // Some of our menu items are in fact toolbar buttons
    if(menu_is_toolbar(menu->name) ||
       (menu->parent && menu_is_toolbar(menu->parent->name) ) )
    {
        if(Tlb)
        {
            DoMethod(Tlb, MUIM_VimToolbar_DisableButton, menu, msg->Grey);
        }

        return TRUE;
    }

    // Vim menu pointers are used as MUI user data / ID:s
    m = (Object *) DoMethod(obj, MUIM_FindUData, msg->ID);

    if(!m)
    {
        WARN("Menu not found");
        return FALSE;
    }

    SetAttrs(m, MUIA_Menuitem_Enabled, msg->Grey ? FALSE : TRUE, TAG_END);
    return TRUE;
}

//------------------------------------------------------------------------------
// VimMenuRemoveMenu - Remove menu / menu item
// Input:              ID - Menu item ID
// Return:             TRUE on success, FALSE otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimMenuRemoveMenu(Class *cls, Object *obj,
                              struct MUIP_VimMenu_RemoveMenu *msg)
{
    Object *m;

    // ID:s must be valid vim menu pointers
    if(!msg || !msg->ID)
    {
        WARN("Invalid menu");
        return FALSE;
    }

    // Vim menu pointers are used as MUI user data / ID:s
    m = (Object *) DoMethod(obj, MUIM_FindUData, msg->ID);

    if(!m)
    {
        WARN("Menu not found");
        return FALSE;
    }

    // Are we leaking m?
    DoMethod(obj, MUIM_Family_Remove, m);
    return TRUE;
}

//------------------------------------------------------------------------------
// VimMenuAddSpacer - Add menu spacer
// Input:             ParentID - ID of parent menu
// Return:            The created spacer on success, NULL otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimMenuAddSpacer(Class *cls, Object *obj,
                             struct MUIP_VimMenu_AddSpacer *msg)
{
    Object *m, *i;

    // All spacers have parents (they can't be top level menus)
    m = (Object *) DoMethod(obj, MUIM_FindUData, msg->ParentID);

    if(!m)
    {
        WARN("Parent not found");
        return (IPTR) NULL;
    }

    // No MUI user data needed, spacers have no callback
    i = MUI_NewObject(MUIC_Menuitem, MUIA_Menuitem_Title, NM_BARLABEL, TAG_END);

    if(!i)
    {
        ERR("Could not create spacer");
        return (IPTR) NULL;
    }

    // Add spacer to parent menu
    DoMethod(m, MUIM_Family_AddTail, i);
    return (IPTR) i;
}

//------------------------------------------------------------------------------
// VimMenuAddMenu - Add menu to menu strip
// Input:           ParentID - ID of parent. Either a menu strip or a menu
//                  ID - ID of menu to add
//                  Label - Text label of menu
// Return:          The created menu on success, NULL otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimMenuAddMenu(Class *cls, Object *obj,
                           struct MUIP_VimMenu_AddMenu *msg)
{
    Object *m, *i, *l;

    if(msg->ParentID)
    {
        // Sub menus have their own parents
        m = (Object *) DoMethod(obj, MUIM_FindUData, msg->ParentID);
    }
    else
    {
        // Top level menus belong to us
        m = obj;
    }

    // We should be able to find the parent
    if(!m)
    {
        WARN("Parent not found");
        return (IPTR) NULL;
    }

    // Vim menu type pointers used as MUI user data
    i = MUI_NewObject(MUIC_Menu, MUIA_Menu_Title, msg->Label, MUIA_UserData,
                      msg->ID, TAG_END);

    if(!i)
    {
        ERR("Could not create menu");
        return (IPTR) NULL;
    }

    // Add menu to menu strip
    DoMethod(m, MUIM_Family_AddTail, i);

    // Make sure that the AlwaysLast menu really is last
    l = (Object *) DoMethod(obj, MUIM_FindUData,
                            MUIV_VimMenu_AddMenu_AlwaysLast);

    // But it doesn't really need to exist
    if(l)
    {
        DoMethod(obj, MUIM_Family_Remove, l);
        DoMethod(obj, MUIM_Family_AddTail, l);
    }

    return (IPTR) i;
}

//------------------------------------------------------------------------------
// VimMenuAddMenuItem - Add menu item to menu
// Input:               ParentID - ID of menu. Always a menu, never a strip
//                      ID - ID of menu item to add
//                      Label - Text label of item
// Return:              The created item on success, NULL otherwise
//------------------------------------------------------------------------------
MUIDSP IPTR VimMenuAddMenuItem(Class *cls, Object *obj,
                               struct MUIP_VimMenu_AddMenuItem *msg)
{
    Object *m, *i;
    // Menu items must have a parent menu
    m = (Object *) DoMethod(obj, MUIM_FindUData, msg->ParentID);

    if(!m)
    {
        WARN("Parent not found");
        return (IPTR) NULL;
    }

    // Vim menu type pointers used as MUI user data
    i = MUI_NewObject(MUIC_Menuitem, MUIA_Menuitem_Title, msg->Label,
                      MUIA_UserData, msg->ID, TAG_END);

    if(!i)
    {
        ERR("Could not create item");
        return (IPTR) NULL;
    }

    // Add item to menu and set callback
    DoMethod(m, MUIM_Family_AddTail, i);
    DoMethod(i, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime, Con, 2,
             MUIM_VimCon_Callback, msg->ID);

    return (IPTR) i;
}

//------------------------------------------------------------------------------
// VimMenuDispatch - MUI custom class dispatcher
// Input:            See dispatched method
// Return:           See dispatched method
//------------------------------------------------------------------------------
DISPATCH(VimMenu)
{
    DISPATCH_HEAD;

    // Dispatch according to MethodID
    switch(msg->MethodID)
    {
    case MUIM_VimMenu_AddSpacer:
        return VimMenuAddSpacer(cls, obj,
            (struct MUIP_VimMenu_AddSpacer *) msg);

    case MUIM_VimMenu_AddMenu:
        return VimMenuAddMenu(cls, obj, (struct MUIP_VimMenu_AddMenu *) msg);

    case MUIM_VimMenu_AddMenuItem:
        return VimMenuAddMenuItem(cls, obj,
            (struct MUIP_VimMenu_AddMenuItem *) msg);

    case MUIM_VimMenu_RemoveMenu:
        return VimMenuRemoveMenu(cls, obj,
            (struct MUIP_VimMenu_RemoveMenu *) msg);

    case MUIM_VimMenu_Grey:
        return VimMenuGrey(cls, obj, (struct MUIP_VimMenu_Grey *) msg);
    }

    // Unknown method, the parent class might be able to care of it.
    return DoSuperMethodA(cls, obj, msg);
}

//------------------------------------------------------------------------------
// Vim interface - The functions below, all prefixed with (gui|clip)_mch, are
//                 the interface to Vim. Most of them contain almost no code,
//                 they merely act as a proxy between the rest of Vim and
//                 the MUI classes above. Some of them contain some glue code
//                 that I for some reason did not want in any of the classes
//                 above. Some of them do more, but those are the exceptions.
//                 Quite a few of them are currently empty, some of them will
//                 be implemented in the future and some of them don't make
//                 sense in combination with MUI and will therefore remain
//                 empty until the end of time.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// gui_mch_set_foreground -
//------------------------------------------------------------------------------
void gui_mch_set_foreground()
{
    DoMethod(_win(Con), MUIM_Window_ToFront);
}

//------------------------------------------------------------------------------
// gui_mch_get_font - Not supported (let MUI handle this)
//------------------------------------------------------------------------------
GuiFont gui_mch_get_font(char_u *vim_font_name, int report_error)
{
    (void) vim_font_name;
    (void) report_error;

    INFO("Not supported");
    return (GuiFont) NULL;
}

//------------------------------------------------------------------------------
// gui_mch_get_fontname - Not supported (let MUI handle this)
//------------------------------------------------------------------------------
char_u *gui_mch_get_fontname(GuiFont font, char_u *name)
{
    (void) font;
    (void) name;

    INFO("Not supported");
    return NULL;
}

//------------------------------------------------------------------------------
// gui_mch_free_font - Not supported (let MUI handle this)
//------------------------------------------------------------------------------
void gui_mch_free_font(GuiFont font)
{
    (void) font;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_get_winpos
//------------------------------------------------------------------------------
int gui_mch_get_winpos(int *x, int *y)
{
    if(GetAttr(MUIA_Window_TopEdge, _win(Con), (IPTR *) x) &&
       GetAttr(MUIA_Window_LeftEdge, _win(Con), (IPTR *) y))
    {
        return OK;
    }

    return FAIL;
}

//------------------------------------------------------------------------------
// gui_mch_set_winpos - Not supported
//------------------------------------------------------------------------------
void gui_mch_set_winpos(int x, int y)
{
    (void) x;
    (void) y;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_enable_scrollbar - Not supported
//------------------------------------------------------------------------------
void gui_mch_enable_scrollbar(scrollbar_T *sb, int flag)
{
    (void) sb;
    (void) flag;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_create_scrollbar - Not supported
//------------------------------------------------------------------------------
void gui_mch_create_scrollbar(scrollbar_T *sb, int orient)
{
    (void) sb;
    (void) orient;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_set_scrollbar_thumb - Not supported
//------------------------------------------------------------------------------
void gui_mch_set_scrollbar_thumb(scrollbar_T *sb, int val, int size, int max)
{
    (void) sb;
    (void) val;
    (void) size;
    (void) max;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_set_scrollbar_pos - Not supported
//------------------------------------------------------------------------------
void gui_mch_set_scrollbar_pos(scrollbar_T *sb, int x, int y, int w, int h)
{
    (void) sb;
    (void) x;
    (void) y;
    (void) w;
    (void) h;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_get_rgb
//------------------------------------------------------------------------------
guicolor_T gui_mch_get_rgb(guicolor_T pixel)
{
    return pixel;
}

//------------------------------------------------------------------------------
// gui_mch_get_color
//------------------------------------------------------------------------------
guicolor_T gui_mch_get_color(char_u *name)
{
    static struct { char *n; guicolor_T c; } t[] =
    {
        {"Black",             0x00000000},
        {"DarkGray",          0x00A9A9A9},
        {"DarkGrey",          0x00A9A9A9},
        {"Gray",              0x00C0C0C0},
        {"Grey",              0x00C0C0C0},
        {"LightGray",         0x00D3D3D3},
        {"LightGrey",         0x00D3D3D3},
        {"Gray10",            0x001A1A1A},
        {"Grey10",            0x001A1A1A},
        {"Gray20",            0x00333333},
        {"Grey20",            0x00333333},
        {"Gray30",            0x004D4D4D},
        {"Grey30",            0x004D4D4D},
        {"Gray40",            0x00666666},
        {"Grey40",            0x00666666},
        {"Gray50",            0x007F7F7F},
        {"Grey50",            0x007F7F7F},
        {"Gray60",            0x00999999},
        {"Grey60",            0x00999999},
        {"Gray70",            0x00B3B3B3},
        {"Grey70",            0x00B3B3B3},
        {"Gray80",            0x00CCCCCC},
        {"Grey80",            0x00CCCCCC},
        {"Gray90",            0x00E5E5E5},
        {"Grey90",            0x00E5E5E5},
        {"White",             0x00FFFFFF},
        {"DarkRed",           0x00800000},
        {"Red",               0x00FF0000},
        {"LightRed",          0x00FFA0A0},
        {"DarkBlue",          0x00000080},
        {"Blue",              0x000000FF},
        {"LightBlue",         0x00ADD8E6},
        {"DarkGreen",         0x00008000},
        {"Green",             0x0000FF00},
        {"LightGreen",        0x0090EE90},
        {"DarkCyan",          0x00008080},
        {"Cyan",              0x0000FFFF},
        {"LightCyan",         0x00E0FFFF},
        {"DarkMagenta",       0x00800080},
        {"Magenta",           0x00FF00FF},
        {"LightMagenta",      0x00FFA0FF},
        {"Brown",             0x00804040},
        {"Yellow",            0x00FFFF00},
        {"LightYellow",       0x00FFFFE0},
        {"DarkYellow",        0x00BBBB00},
        {"SeaGreen",          0x002E8B57},
        {"Orange",            0x00FFA500},
        {"Purple",            0x00A020F0},
        {"SlateBlue",         0x006A5ACD},
        {"Violet",            0x00EE82EE},
        {"olivedrab",         0x006B8E23},
        {"coral",             0x00FF7F50},
        {"gold",              0x00FFD700},
        {"red2",              0x00EE0000},
        {"green3",            0x0000CD00},
        {"cyan4",             0x00008B8B},
        {"magenta3",          0x00CD00CD},
        {"deeppink",          0x00FF1493},
        {"khaki",             0x00F0E68C},
        {"slategrey",         0x00708090},
        {"tan",               0x00D2B48C},
        {"goldenrod",         0x00DAA520},
        {"springgreen",       0x0000FF7F},
        {"peru",              0x00CD853F},
        {"wheat",             0x00F5DEB3},
        {"yellowgreen",       0x009ACB32},
        {"indianred",         0x00CD5C5C},
        {"salmon",            0x00FA8072},
        {"SkyBlue",           0x0087CEEB},
        {"palegreen",         0x0098FB98},
        {"darkkhaki",         0x00BDB76B},
        {"navajowhite",       0x00FFDEAD},
        {"orangered",         0x00FF4500},
        {"yellow2",           0x00EEEE00},
        {"grey5",             0x000D0D0D},
        {"grey95",            0x00F2F2F2},
        {"Orchid",            0x00DA70D6},
        {"Pink",              0x00FFC0CB},
        {"PeachPuff",         0x00FFDAB9},
        {"Gold2",             0x00EEC900},
        {"Red3",              0x00CD0000},
        {"Gray45",            0x00737373},
        {"DeepPink3",         0x00CD1076},
        {"Pink2",             0x00EEA9B8},
        {"steelblue",         0x004682B4},
        {"DarkOrange",        0x00FF8C00},
        {"grey15",            0x00262626},
        {"RoyalBlue",         0x004169e1},
        {"CornflowerBlue",    0x006495ED},
    };
    guicolor_T r = INVALCOLOR;
    size_t i = sizeof(t) / sizeof(t[0]);

    if(name[0] == '#' && strlen((char *) name) == 7)
    {
        r = strtol((char *) name + 1, NULL, 16);
    }
    else
    {
        while(i--)
        {
            if(STRICMP(name, t[i].n) == 0)
            {
                r = t[i].c;
                break;
            }
        }
    }

    return r;
}

//------------------------------------------------------------------------------
// gui_mch_getmouse - Not supported
//------------------------------------------------------------------------------
void gui_mch_getmouse(int *x, int *y)
{
    INFO("Not supported");
    *x = 0;
    *y = 0;
}

//------------------------------------------------------------------------------
// gui_mch_setmouse - Not supported
//------------------------------------------------------------------------------
void gui_mch_setmouse(int x, int y)
{
    (void) x;
    (void) y;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_update - Not supported (not needed)
//------------------------------------------------------------------------------
void gui_mch_update(void)
{
    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_wait_for_chars - Main loop, here control is passed back and forth
//                          between Vim and our MUI classes
//
// GUI input routine called by gui_wait_for_chars().  Waits for a character
// from the keyboard.
//
//  wtime == -1       Wait forever.
//  wtime == 0        This should never happen.
//  wtime > 0         Wait wtime milliseconds for a character.
//
// Returns OK if a character was found to be available within the given time,
// or FAIL otherwise.
//------------------------------------------------------------------------------
int gui_mch_wait_for_chars(int wtime)
{
    DoMethod(Con, MUIM_VimCon_Flush);

    // Don't enable timeouts for now, it might cause
    // problems in the MUI message loop. Passing the
    // control over to Vim at any time is not safe.
#ifdef FEAT_TIMEOUT
    #warning Timeout support will cause MUI message loop problems
    DoMethod(Con, MUIM_VimCon_SetTimeout, wtime > 0 ? wtime : 0);
#else
    // Timeout immediately.
    if(wtime > 0)
    {
        return FAIL;
    }
#endif

    while(!vim_is_input_buf_full())
    {
        static ULONG sig;

        // Pass control over to MUI.
        if(DoMethod(_app(Con), MUIM_Application_NewInput, &sig) != (ULONG)
                    MUIV_Application_ReturnID_Quit)
        {
            // Get current input state.
            int state = DoMethod(Con, MUIM_VimCon_GetState);

            // Wait for something to happen if we're idle.
            if(state == MUIV_VimCon_State_Idle)
            {
                // For some reason MUI returns 0 when jumping
                // to the same screen that we're currently on.
                // If so, just pass control over to Vim.
                if(sig)
                {
                    sig = Wait(sig | SIGBREAKF_CTRL_C);

                    if(sig & SIGBREAKF_CTRL_C)
                    {
                        getout_preserve_modified(0);
                    }
                }
            }
            else
            {
                // Something happened. Either input, a voluntary
                // yield or a timeout.
                if(state != MUIV_VimCon_State_Yield &&
                   state != MUIV_VimCon_State_Timeout)
                {
                    ERR("Unknown state");
                    getout_preserve_modified(0);
                }

                // No input == a timeout has occurred
                return state == MUIV_VimCon_State_Yield ? OK : FAIL;
            }
        }
        else
        {
            // Quit.
            gui_shell_closed();
        }
    }

    // We have probably filled the buffer with mouse events
    return FAIL;
}

//------------------------------------------------------------------------------
// gui_mch_set_fg_color
//------------------------------------------------------------------------------
void gui_mch_set_fg_color(guicolor_T fg)
{
    DoMethod(Con, MUIM_VimCon_SetFgColor, fg);
}

//----------------------------------n--------------------------------------------
// gui_mch_set_bg_color
//------------------------------------------------------------------------------
void gui_mch_set_bg_color(guicolor_T bg)
{
    DoMethod(Con, MUIM_VimCon_SetBgColor, bg);
}

//------------------------------------------------------------------------------
// gui_mch_set_sp_color
//------------------------------------------------------------------------------
void gui_mch_set_sp_color(guicolor_T sp)
{
    DoMethod(Con, MUIM_VimCon_SetFgColor, sp);
}

//------------------------------------------------------------------------------
// gui_mch_draw_string
//------------------------------------------------------------------------------
void gui_mch_draw_string(int row, int col, char_u *s, int len, int flags)
{
    DoMethod(Con, MUIM_VimCon_DrawString, row, col, s, len, flags);
}

//------------------------------------------------------------------------------
// gui_mch_enable_menu - Not supported
//------------------------------------------------------------------------------
void gui_mch_enable_menu(int flag)
{
    (void) flag;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_toggle_tearoff - Not supported
//------------------------------------------------------------------------------
void gui_mch_toggle_tearoffs(int enable)
{
    (void) enable;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_flush - Not supported (not needed)
//------------------------------------------------------------------------------
void gui_mch_flush(void)
{
}

//------------------------------------------------------------------------------
// gui_mch_beep
//------------------------------------------------------------------------------
void gui_mch_beep(void)
{
    DoMethod(Con, MUIM_VimCon_Beep);
}

//------------------------------------------------------------------------------
// gui_mch_set_shellsize - Not supported
//------------------------------------------------------------------------------
void gui_mch_set_shellsize(int width, int height, int min_width, int min_height,
                           int base_width, int base_height, int direction)
{
    (void) width;
    (void) height;
    (void) min_width;
    (void) min_height;
    (void) base_width;
    (void) base_height;
    (void) direction;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_clear_block
//------------------------------------------------------------------------------
void gui_mch_clear_block(int row1, int col1, int row2, int col2)
{
    DoMethod(Con, MUIM_VimCon_FillBlock, row1, col1, row2, col2,
             gui.back_pixel);
}

//------------------------------------------------------------------------------
// gui_mch_delete_lines
//------------------------------------------------------------------------------
void gui_mch_delete_lines(int row, int num_lines)
{
    DoMethod(Con, MUIM_VimCon_DeleteLines, row, num_lines,
             gui.scroll_region_left, gui.scroll_region_right,
             gui.scroll_region_bot, gui.back_pixel);
}

//------------------------------------------------------------------------------
// gui_mch_insert_lines
//------------------------------------------------------------------------------
void gui_mch_insert_lines(int row, int num_lines)
{
    DoMethod(Con, MUIM_VimCon_DeleteLines, row, -num_lines,
             gui.scroll_region_left, gui.scroll_region_right,
             gui.scroll_region_bot, gui.back_pixel);
}

//------------------------------------------------------------------------------
// gui_mch_set_font - Not supported (let MUI handle this)
//------------------------------------------------------------------------------
void gui_mch_set_font(GuiFont font)
{
    (void) font;
}

//------------------------------------------------------------------------------
// gui_mch_clear_all
//------------------------------------------------------------------------------
void gui_mch_clear_all()
{
    DoMethod(Con, MUIM_VimCon_FillBlock, 0, 0, gui.num_rows, gui.num_cols,
             gui.back_pixel);
}

//------------------------------------------------------------------------------
// gui_mch_flash
//------------------------------------------------------------------------------
void gui_mch_flash(int msec)
{
    (void) msec;

    DoMethod(Con, MUIM_VimCon_Beep);
}

//------------------------------------------------------------------------------
// gui_mch_set_menu_pos - Not supported (let MUI handle this)
//------------------------------------------------------------------------------
void gui_mch_set_menu_pos(int x, int y, int w, int h)
{
    (void) x;
    (void) y;
    (void) w;
    (void) h;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_init - Initialise the GUI
//------------------------------------------------------------------------------
int gui_mch_init(void)
{
    char vs[64];
    Object *App, *Win, *Set = NULL, *Abo = NULL;

#ifdef __amigaos4__
    if(!(MUIMasterBase = OpenLibrary("muimaster.library", 19)))
    {
        fprintf(stderr, "Failed to open muimaster.library.\n");
        return FALSE;
    }

    IMUIMaster = (struct MUIMasterIFace *)
                 GetInterface(MUIMasterBase, "main", 1, NULL);

    if(!(CyberGfxBase=OpenLibrary("cybergraphics.library",40)))
    {
        fprintf(stderr, "Failed to open cybergraphics.library.\n");
        return FALSE;
    }

    ICyberGfx= (struct CyberGfxIFace*) GetInterface(CyberGfxBase,"main",1,NULL);

    if(!(KeymapBase =OpenLibrary("keymap.library", 50)))
    {
        fprintf(stderr, "Failed to open keymap.library.\n");
        return FALSE;
    }

    IKeymap = (struct KeymapIFace*) GetInterface(KeymapBase, "main", 1, NULL);
#endif

    static const CONST_STRPTR classes[] = { "TheBar.mcc", NULL};
    VimToolbarClass = VimConClass = VimMenuClass = NULL;

    // Create custom classes
    VimToolbarClass = MUI_CreateCustomClass(NULL, (ClassID) MUIC_TheBar, NULL,
                                            sizeof(struct CLASS_DATA(VimToolbar)),
                                            (APTR) DISPATCH_GATE(VimToolbar));
    if(!VimToolbarClass)
    {
#ifdef __amigaos4__
        struct TagItem tags[] = { ESA_Position, REQPOS_CENTERSCREEN, TAG_DONE };
#endif
        struct EasyStruct req =
        {
            .es_StructSize   = sizeof(struct EasyStruct),
            .es_Flags        = 0, // ESF_SCREEN | ESF_TAGGED | ESF_EVENSIZE;
            .es_Title        = (UBYTE *)"Error",
            .es_TextFormat   = (UBYTE *)"MCC_TheBar required",
            .es_GadgetFormat = (UBYTE *)"OK",
#ifdef __amigaos4__
            .es_Screen       = ((struct IntuitionBase *)IntuitionBase)->ActiveScreen,
            .es_TagList      = tags
#endif
        };

        EasyRequest(NULL, &req, NULL, NULL);
        ERR("MCC_TheBar required");
        return FAIL;
    }

    VimConClass = MUI_CreateCustomClass(NULL, (ClassID) MUIC_Area, NULL,
                                        sizeof(struct CLASS_DATA(VimCon)),
                                        (APTR) DISPATCH_GATE(VimCon));
    VimMenuClass = MUI_CreateCustomClass(NULL, (ClassID) MUIC_Menustrip, NULL,
                                         sizeof(struct CLASS_DATA(VimMenu)),
                                         (APTR) DISPATCH_GATE(VimMenu));

    if(!VimConClass || !VimMenuClass)
    {
        ERR("Failed creating MUI custom class");
        return FAIL;
    }

    // Generate full version string
    snprintf(vs, sizeof(vs), " Vim %d.%d.%d",
              VIM_VERSION_MAJOR, VIM_VERSION_MINOR, highest_patch());

    // Set up the class hierachy
    App = MUI_NewObject(MUIC_Application,
        MUIA_Application_UsedClasses, classes,
        MUIA_Application_Menustrip, Mnu =
            NewObject(VimMenuClass->mcc_Class, NULL,
            MUIA_Menustrip_Enabled, TRUE,
            MUIA_Family_Child, MUI_NewObject(MUIC_Menu,
                MUIA_Menu_Title, "MUI",
                    MUIA_UserData, MUIV_VimMenu_AddMenu_AlwaysLast,
                    MUIA_Family_Child, Set = MUI_NewObject(MUIC_Menuitem,
                        MUIA_Menuitem_Title, "MUI Settings...",
                    TAG_END),
                    MUIA_Family_Child, MUI_NewObject(MUIC_Menuitem,
                        MUIA_Menuitem_Title, NM_BARLABEL,
                    TAG_END),
                    MUIA_Family_Child, Abo = MUI_NewObject(MUIC_Menuitem,
                        MUIA_Menuitem_Title, "About MUI...",
                    TAG_END),
                TAG_END),
        TAG_END),
        MUIA_Application_Base, "Vim",
        MUIA_Application_Description, "The ubiquitous editor",
        MUIA_Application_Title, "Vim",
        MUIA_Application_Version, vs,
        MUIA_Application_DiskObject,
            GetDiskObject((STRPTR) "VIM:icons/Vim_LodsaColors"),
        MUIA_Application_Window, Win =
            MUI_NewObject(MUIC_Window,
            MUIA_Window_Title, (IPTR) "Vim",
            MUIA_Window_ID, MAKE_ID('W','D','L','A'),
            MUIA_Window_AppWindow, TRUE,
            MUIA_Window_DisableKeys, 0xffffffff,
            MUIA_Window_RootObject,
            MUI_NewObject(MUIC_Group,
                MUIA_Group_Child, Tlb =
                    NewObject(VimToolbarClass->mcc_Class, NULL,
                    TAG_END),
                MUIA_Group_Child, Con =
                    NewObject(VimConClass->mcc_Class, NULL,
                    TAG_END),
                TAG_END),
            TAG_END),
        TAG_END);

    if(!App)
    {
        ERR("Failed creating MUI application");
        return FAIL;
    }

    // Open the window to finish setup, cheat (see gui_mch_open).
    set(Win, MUIA_Window_Open, TRUE);

    // We want keyboard input by default
    set(Win, MUIA_Window_DefaultObject, Con);

    // Exit application upon close request (trap this later on)
    DoMethod(Win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, App, 2,
             MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    // Set up drag and drop notifications
    DoMethod(Win, MUIM_Notify, MUIA_AppMessage, MUIV_EveryTime, Con, 2,
             MUIM_VimCon_AppMessage, MUIV_TriggerValue);

    // MUI specific menu parts
    if(Abo && Set)
    {
        DoMethod(Abo, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime, Con,
                 1, MUIM_VimCon_AboutMUI);
        DoMethod(Set, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime, Con,
                 1, MUIM_VimCon_MUISettings);
    }

    // From where do we get these defaults?
    gui.def_norm_pixel = gui.norm_pixel = gui_mch_get_color((char_u *) "Grey");
    gui.def_back_pixel = gui.back_pixel = gui_mch_get_color((char_u *) "Black");

    // MUI takes care of this
    gui.menu_height = 0;
    gui.scrollbar_height = 0;
    gui.scrollbar_width = 0;
    gui.border_offset = 0;
    gui.border_width = 0;
    return OK;
}

//------------------------------------------------------------------------------
// gui_mch_prepare - Handle WB invocation.
//------------------------------------------------------------------------------
void gui_mch_prepare(int *argc, char **argv)
{
    // Invoked from WB?
    if(!*argc)
    {
        // Count number arguments, argv is
        // populated in get_cmd_argsA(..).
        while(*argv)
        {
            argv++;
            (*argc)++;
        }

        // Always start GUI from WB.
        gui.starting = TRUE;
    }
}

//------------------------------------------------------------------------------
//gui_mch_init_check - Not used (not needed)
//------------------------------------------------------------------------------
int gui_mch_init_check(void)
{
    return OK;
}

//------------------------------------------------------------------------------
// gui_mch_open
//------------------------------------------------------------------------------
int gui_mch_open(void)
{
    // The window is already open (see gui_mch_init)
    DoMethod(Con, MUIM_Show);
    return OK;
}

//------------------------------------------------------------------------------
// gui_mch_init_font - Not supported (let MUI handle this)
//------------------------------------------------------------------------------
int gui_mch_init_font(char_u *vim_font_name, int fontset)
{
    (void) vim_font_name;
    (void) fontset;
    return OK;
}

//------------------------------------------------------------------------------
// gui_mch_exit
//------------------------------------------------------------------------------
void gui_mch_exit(int rc)
{
    (void) rc;

    if(Con)
    {
        // Save icon pointer
        ULONG icon = 0;
        get(_app(Con), MUIA_Application_DiskObject, &icon);

        // Close window and destroy app
        set(_win(Con), MUIA_Window_Open, FALSE);
        MUI_DisposeObject(_app(Con));

        // Free icon resources, MUI won't do this
        if(icon)
        {
            FreeDiskObject((struct DiskObject *) icon);
        }
    }

    // Destroy custom classes. Must check for NULL.
    if(VimMenuClass)
    {
        MUI_DeleteCustomClass(VimMenuClass);
    }

    if(VimConClass)
    {
        MUI_DeleteCustomClass(VimConClass);
    }

    if(VimToolbarClass)
    {
        MUI_DeleteCustomClass(VimToolbarClass);
    }
#ifdef __amigaos4__
    if(IMUIMaster)
    {
        DropInterface((struct Interface *)IMUIMaster);
    }

    if(MUIMasterBase)
    {
        CloseLibrary((struct Library *)MUIMasterBase);
    }

    if(ICyberGfx)
    {
        DropInterface((struct Interface *)ICyberGfx);
    }

    if(CyberGfxBase)
    {
        CloseLibrary((struct Library *)CyberGfxBase);
    }

    if(IKeymap)
    {
        DropInterface((struct Interface*)IKeymap);
    }

    if(KeymapBase)
    {
        CloseLibrary((struct Library *)KeymapBase);
    }
#endif

}

//------------------------------------------------------------------------------
// gui_mch_draw_hollow_cursor
//------------------------------------------------------------------------------
void gui_mch_draw_hollow_cursor(guicolor_T color)
{
    (void) color;

    DoMethod(Con, MUIM_VimCon_DrawHollowCursor, gui.row, gui.col,
             gui.norm_pixel);
}

//------------------------------------------------------------------------------
// gui_mch_draw_part_cursor
//------------------------------------------------------------------------------
void gui_mch_draw_part_cursor(int w, int h, guicolor_T color)
{
    (void) color;

    DoMethod(Con, MUIM_VimCon_DrawPartCursor, gui.row, gui.col, w, h,
             gui.norm_pixel);
}

//------------------------------------------------------------------------------
// gui_mch_set_text_area_pos - Not supported (let MUI handle this)
//------------------------------------------------------------------------------
void gui_mch_set_text_area_pos(int x, int y, int w, int h)
{
    (void) x;
    (void) y;
    (void) w;
    (void) h;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_get_screen_dimensions
//------------------------------------------------------------------------------
void gui_mch_get_screen_dimensions(int *screen_w, int *screen_h)
{
    DoMethod(Con, MUIM_VimCon_GetScreenDim, screen_w, screen_h);
}

//------------------------------------------------------------------------------
// gui_mch_add_menu
//------------------------------------------------------------------------------
void gui_mch_add_menu(vimmenu_T *menu, int index)
{
    (void) index;

    if(Mnu && !menu_is_popup(menu->name) && !menu_is_toolbar(menu->name))
    {
        DoMethod(Mnu, MUIM_VimMenu_AddMenu, menu->parent, menu, menu->dname);
    }
}

//------------------------------------------------------------------------------
// gui_mch_add_menu_item - Since Vim treats menu items and toolbar buttons
//                         in the same way we need some code for demuxing
//                         and discarding.
//------------------------------------------------------------------------------
void gui_mch_add_menu_item(vimmenu_T *menu, int index)
{
    (void) index;
    vimmenu_T *p = menu->parent;

    // Menu items must have parents
    if(!p)
    {
        WARN("No parent");
        return;
    }

    // Ignore popups for now
    if(menu_is_popup(p->name) )
    {
        INFO("Ignoring pop-up");
        return;
    }

    // Menu items can be proper menu items or toolbar buttons
    if(Tlb && menu_is_toolbar(p->name) && !menu_is_separator(menu->name))
    {
        DoMethod(Tlb, MUIM_VimToolbar_AddButton, menu, menu->dname,
                 menu->dname);
    }
    else
    {
        if(Mnu)
        {
            // Spacer or menu item?
            if(menu_is_separator(menu->name) )
            {
                DoMethod(Mnu, MUIM_VimMenu_AddSpacer, menu->parent);
            }
            else
            {
//------------------------------------------------------------------------------
#warning ! after scrollbar functionality is implemented, remove that hack !
//------------------------------------------------------------------------------
                if(!strstr(menu->dname, "Scrollbar"))
                {
                    DoMethod(Mnu, MUIM_VimMenu_AddMenuItem, menu->parent, menu,
                             menu->dname);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// gui_mch_show_toolbar
//------------------------------------------------------------------------------
void gui_mch_show_toolbar(int showit)
{
    if(Tlb)
    {
        DoMethod(Tlb, MUIM_Set, MUIA_ShowMe, showit);
    }
}

//------------------------------------------------------------------------------
// gui_mch_destroy_menu
//------------------------------------------------------------------------------
void gui_mch_destroy_menu(vimmenu_T *menu)
{
    if(Mnu)
    {
        DoMethod(Mnu, MUIM_VimMenu_RemoveMenu, menu);
    }
}

//------------------------------------------------------------------------------
// gui_mch_menu_grey
//------------------------------------------------------------------------------
void gui_mch_menu_grey(vimmenu_T *menu, int grey)
{
    if(Mnu)
    {
        DoMethod(Mnu, MUIM_VimMenu_Grey, menu, grey);
    }
}

//------------------------------------------------------------------------------
// gui_mch_menu_hidden - Not supported
//------------------------------------------------------------------------------
void gui_mch_menu_hidden(vimmenu_T *menu, int hidden)
{
    (void) menu;
    (void) hidden;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_draw_menubar - Not supported (let MUI handle this)
//------------------------------------------------------------------------------
void gui_mch_draw_menubar(void)
{
    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_show_popupmenu - Not supported
//------------------------------------------------------------------------------
void gui_mch_show_popupmenu(vimmenu_T *menu)
{
    (void) menu;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_mousehide - Not supported
//------------------------------------------------------------------------------
void gui_mch_mousehide(int hide)
{
    (void) hide;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_adjust_charheight - Not supported
//------------------------------------------------------------------------------
int gui_mch_adjust_charheight(void)
{
    INFO("Not supported");
    return OK;
}

//------------------------------------------------------------------------------
// gui_mch_new_colors - Not supported (not needed)
//------------------------------------------------------------------------------
void gui_mch_new_colors(void)
{
    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_haskey - Not supported
//------------------------------------------------------------------------------
int gui_mch_haskey(char_u *name)
{
    (void) name;

    INFO("Not supported");
    return OK;
}

//------------------------------------------------------------------------------
// gui_mch_iconify - Not supported
//------------------------------------------------------------------------------
void gui_mch_iconify(void)
{
    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_invert_rectangle
//------------------------------------------------------------------------------
void gui_mch_invert_rectangle(int row, int col, int nr, int nc)
{
    DoMethod(Con, MUIM_VimCon_InvertRect, row, col, nr, nc);
}

//------------------------------------------------------------------------------
// clip_mch_own_selection - Not supported
//------------------------------------------------------------------------------
int clip_mch_own_selection(Clipboard_T *cbd)
{
    (void) cbd;

    INFO("Not supported");
    return OK;
}

//------------------------------------------------------------------------------
// clip_mch_lose_selection - Not supported
//------------------------------------------------------------------------------
void clip_mch_lose_selection(Clipboard_T *cbd)
{
    (void) cbd;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// clip_mch_request_selection
//------------------------------------------------------------------------------
void clip_mch_request_selection(Clipboard_T *cbd)
{
    (void) cbd;

    DoMethod(Con, MUIM_VimCon_Paste, cbd);
}

//------------------------------------------------------------------------------
// clip_mch_set_selection
//------------------------------------------------------------------------------
void clip_mch_set_selection(Clipboard_T *cbd)
{
    (void) cbd;

    DoMethod(Con, MUIM_VimCon_Copy, cbd);
}

//------------------------------------------------------------------------------
// gui_mch_destroy_scrollbar - Not supported
//------------------------------------------------------------------------------
void gui_mch_destroy_scrollbar(scrollbar_T *sb)
{
    (void) sb;

    INFO("Not supported");
}

//------------------------------------------------------------------------------
// gui_mch_browse - Put up a file requester.
// Returns the selected name in allocated memory, or NULL for Cancel.
// saving           select file to write
// title            title for the window
// dflt             default name
// ext              not used (extension added)
// initdir          initial directory, NULL for current dir
// filter           not used (file name filter)
//------------------------------------------------------------------------------
char_u *gui_mch_browse(int saving, char_u *title, char_u *dflt, char_u *ext,
                       char_u *initdir, char_u *filter )
{
    (void) saving;
    (void) filter;
    (void) dflt;
    (void) ext;

    return (char_u *) DoMethod(Con, MUIM_VimCon_Browse, title, initdir);
}

//------------------------------------------------------------------------------
// gui_mch_set_blinking
//------------------------------------------------------------------------------
void gui_mch_set_blinking(long wait, long on, long off)
{
    DoMethod(Con, MUIM_VimCon_SetBlinking, wait, on, off);
}

//------------------------------------------------------------------------------
// gui_mch_start_blink
//------------------------------------------------------------------------------
void gui_mch_start_blink(void)
{
    DoMethod(Con, MUIM_VimCon_StartBlink);
}

//------------------------------------------------------------------------------
// gui_mch_stop_blink
//------------------------------------------------------------------------------
void gui_mch_stop_blink(int FIXME)
{
    //.See gui_w32.c
    (void) FIXME;

    DoMethod(Con, MUIM_VimCon_StopBlink);
}

//------------------------------------------------------------------------------
// gui_mch_is_blinking
//------------------------------------------------------------------------------
int gui_mch_is_blinking(void)
{
    return DoMethod(Con, MUIM_VimCon_IsBlinking);
}

//------------------------------------------------------------------------------
// gui_mch_is_blink_off
//------------------------------------------------------------------------------
int gui_mch_is_blink_off(void)
{
    return !DoMethod(Con, MUIM_VimCon_IsBlinking);
}

//------------------------------------------------------------------------------
// gui_mch_settitle
//------------------------------------------------------------------------------
void gui_mch_settitle(char_u *title, char_u *icon)
{
    (void) icon;

    DoMethod(Con, MUIM_VimCon_SetTitle, title);
}

