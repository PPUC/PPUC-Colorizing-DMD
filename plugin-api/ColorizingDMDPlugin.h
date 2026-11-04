// license:GPLv3+
//
// ColorizingDMD editor API for plugins, version 3 (version 2 without the pictures and the
// author's windows, version 1 without the editing part either).
//
// The editor hosts plugins on the VPX plugin bus (libvpxhost, the plugin SDK's MsgPlugin.h).
// A plugin in the editor's plugin folder, enabled under Settings -> Plugins, is loaded at the
// editor's start. It asks for this API with a message to the editor's endpoint:
//
//     const ColorizingDMDEditorAPI* editor = nullptr;
//     const unsigned int getApi = api->GetMsgID(CDMDPI_NAMESPACE, CDMDPI_MSG_GET_API);
//     api->BroadcastMsg(endpointId, getApi, &editor);
//     api->ReleaseMsgID(getApi);
//     if (!editor || editor->version < 1) { ... not hosted by the editor ... }
//
// Threading: every function of the table is called on the editor's main thread only (the
// thread the plugin's load function runs on). A plugin thread goes through RunOnMainThread of
// the message API, blocking (delay -1) when it needs the answer.
//
// Lifetime: the table stays valid while the plugin is loaded. Pointers and texts handed to a
// plugin are valid during the call only; the functions copy what they get.
//
// Data: frames are indexed from 0 in the order of the editor's frame list. Pixel buffers are
// row-major, width * height elements. A function filling a buffer returns the number of
// elements the whole data has and fills the buffer only when its capacity holds them all, so
// a first call with capacity 0 asks for the size. Texts are UTF-8 and NUL-terminated.
//
// Compatibility: the message name ends with its version (":1"); the table only grows at its
// end, with `version` raised. A plugin checks `version` before it uses a later entry.

#pragma once

#include "plugins/MsgPlugin.h"

#define CDMDPI_NAMESPACE "ColorizingDMD"

// msgData: const ColorizingDMDEditorAPI** that the editor sets.
#define CDMDPI_MSG_GET_API "GetEditorAPI:1"
// Broadcast by the editor, msgData null: a project was opened or closed, or edited (any
// change of ColorizingDMDProjectInfo.revision).
#define CDMDPI_EVT_ON_PROJECT_CHANGED "OnProjectChanged:1"
// Broadcast by the editor, msgData null: the current or the selected frames changed.
#define CDMDPI_EVT_ON_SELECTION_CHANGED "OnSelectionChanged:1"

#define CDMDPI_NONE 0xffffffffu
// Dynamic sets a frame has (the sets of a dynamic mask are 0 .. 31).
#define CDMDPI_DYNAMIC_SETS 32
// The size of a sprite's pictures and masks on either plane (version 3).
#define CDMDPI_SPRITE_WIDTH 256
#define CDMDPI_SPRITE_HEIGHT 64

// What a ColorizingDMDPicture is the picture of (version 3).
#define CDMDPI_PICTURE_FRAME 0
#define CDMDPI_PICTURE_BACKGROUND 1
#define CDMDPI_PICTURE_SPRITE 2

// The kinds of a ColorizingDMDSettingField (version 3).
#define CDMDPI_FIELD_TEXT 0
#define CDMDPI_FIELD_PASSWORD 1 // text shown as dots
#define CDMDPI_FIELD_CHECKBOX 2 // "1" or "0"
#define CDMDPI_FIELD_CHOICE 3   // one of the field's choices

typedef struct ColorizingDMDProjectInfo
{
   uint32_t hasProject;       // 0: no project is open; nothing else is filled
   char romName[64];
   uint32_t frameCount;
   uint32_t colorCount;       // shades of the original frames: 4, 16 or 64
   uint32_t originalWidth;    // the ROM's frames
   uint32_t originalHeight;
   uint32_t sdWidth;          // the colorized SD plane
   uint32_t sdHeight;
   uint32_t hdWidth;          // the colorized HD plane, 0 without one
   uint32_t hdHeight;
   uint32_t dynamicPlaneIsHd; // dynamic zones are on the HD plane (else SD)
   uint32_t comparisonMaskCount;
   uint32_t spriteCount;
   uint32_t backgroundCount;
   uint32_t paletteCount;
   uint32_t sectionCount;
   uint64_t revision;         // changes with every edit of the project
} ColorizingDMDProjectInfo;

typedef struct ColorizingDMDFrameInfo
{
   uint32_t durationMs;
   uint32_t comparisonMask;   // CDMDPI_NONE without one
   uint32_t shapeMode;        // 1: compared by shape (any shade above 0 counts as lit)
   uint32_t background;       // CDMDPI_NONE without one
   uint32_t spriteCount;      // sprites the frame detects
   uint32_t hasDynamicPixels;
   uint32_t hasHd;
   uint32_t triggerId;        // the scene it starts, else its PUP trigger, else its monochrome
                              // trigger (65432, or 65431 with its palette); CDMDPI_NONE without
   uint32_t section;          // the section the frame is in, CDMDPI_NONE before the first
} ColorizingDMDFrameInfo;

typedef void(MSGPIAPI* cdmdpi_action_callback)(void* userData);

// A picture proposed to the author with ReviewPictures (version 3): a new colorized picture of
// a frame, a background or a sprite on one plane, of the size the matching Get...Picture
// gives. write: count values, the picture's pixels to write where not 0; null writes all.
typedef struct ColorizingDMDPicture
{
   uint32_t kind;          // CDMDPI_PICTURE_...
   uint32_t index;         // the frame, background or sprite
   uint32_t hd;            // 1 for the HD plane
   const uint16_t* pixels; // RGB565, row-major
   const uint8_t* write;
   uint32_t count;         // pixels in the picture (and in write)
   const char* label;      // shown in the list; null or "" for the editor's own name
} ColorizingDMDPicture;
// applied: the pictures the author applied, 0 when none was (discarded, or the project changed).
typedef void(MSGPIAPI* cdmdpi_review_callback)(void* userData, uint32_t applied);

// A field of the form EditSettings shows (version 3). The value is the plugin's setting of
// that key (GetSetting); the form shows defaultValue while it is not set.
typedef struct ColorizingDMDSettingField
{
   const char* key;
   const char* label;
   uint32_t kind;            // CDMDPI_FIELD_...
   const char* defaultValue; // null for ""
   // CDMDPI_FIELD_CHOICE: the choices, one per line, each "value" or "value=label".
   const char* choices;
   const char* help;         // a line under the field; null for none
} ColorizingDMDSettingField;
// saved: 1 when the author saved the form (the settings are set by then), 0 when cancelled.
typedef void(MSGPIAPI* cdmdpi_settings_callback)(void* userData, int saved);

typedef struct ColorizingDMDEditorAPI
{
   int version; // 3

   // ---- The project ----------------------------------------------------------------------
   // Returns 0 when no project is open.
   int(MSGPIAPI* GetProjectInfo)(ColorizingDMDProjectInfo* info);
   // Returns 0 for a frame that does not exist.
   int(MSGPIAPI* GetFrameInfo)(uint32_t frame, ColorizingDMDFrameInfo* info);
   // The original frame's shades (0 .. colorCount - 1).
   uint32_t(MSGPIAPI* GetOriginalFrame)(uint32_t frame, uint8_t* pixels, uint32_t capacity,
                                        uint32_t* width, uint32_t* height);
   // The colorized frame as the editor shows it, RGB565; hd 1 for the HD plane.
   uint32_t(MSGPIAPI* GetColorizedFrame)(uint32_t frame, int hd, uint16_t* pixels,
                                         uint32_t capacity, uint32_t* width, uint32_t* height);
   // A comparison mask, original size: 1 where the comparison ignores the pixel.
   uint32_t(MSGPIAPI* GetComparisonMask)(uint32_t mask, uint8_t* pixels, uint32_t capacity,
                                         uint32_t* width, uint32_t* height);
   // The frame's dynamic zones on the dynamic plane: the set of each pixel, 255 for none.
   uint32_t(MSGPIAPI* GetDynamicMask)(uint32_t frame, uint8_t* pixels, uint32_t capacity,
                                      uint32_t* width, uint32_t* height);
   // The frame's dynamic sets on the dynamic plane: colors[set * colorCount + shade] in
   // RGB565, for all CDMDPI_DYNAMIC_SETS sets.
   uint32_t(MSGPIAPI* GetDynamicColors)(uint32_t frame, uint16_t* colors, uint32_t capacity);
   // A section's name (into name, at most capacity bytes with the NUL) and first frame.
   // Returns 0 for a section that does not exist.
   int(MSGPIAPI* GetSection)(uint32_t section, char* name, uint32_t capacity,
                             uint32_t* firstFrame);

   // ---- What the author looks at ---------------------------------------------------------
   // The current frame; CDMDPI_NONE without one.
   uint32_t(MSGPIAPI* GetCurrentFrame)();
   // The selected frames, ascending (the current one when no other is selected).
   uint32_t(MSGPIAPI* GetSelectedFrames)(uint32_t* frames, uint32_t capacity);
   // Shows frames in the editor: makes `current` the current frame and selects `frames`
   // (count 0: the current one only). Returns 0 when a frame does not exist.
   int(MSGPIAPI* ShowFrames)(uint32_t current, const uint32_t* frames, uint32_t count);

   // ---- The plugin's place in the editor -------------------------------------------------
   // Adds an entry to the editor's Plugins menu; the callback runs on the main thread. The
   // entries go when the plugin unloads (or with RemoveMenuEntries).
   void(MSGPIAPI* AddMenuEntry)(uint32_t endpointId, const char* text,
                                cdmdpi_action_callback callback, void* userData);
   void(MSGPIAPI* RemoveMenuEntries)(uint32_t endpointId);
   // A message to the author in a window whose text can be selected and copied.
   void(MSGPIAPI* ShowMessage)(uint32_t endpointId, const char* title, const char* text);
   // A line in the editor's status bar for a few seconds.
   void(MSGPIAPI* ShowStatus)(uint32_t endpointId, const char* text);
   // The plugin's own settings, kept by the editor. GetSetting returns the value's size in
   // bytes with the NUL (0 when not set) and fills value when it fits.
   uint32_t(MSGPIAPI* GetSetting)(uint32_t endpointId, const char* key, char* value,
                                  uint32_t capacity);
   void(MSGPIAPI* SetSetting)(uint32_t endpointId, const char* key, const char* value);

   // ---- Editing (version 2) --------------------------------------------------------------
   // Changes go into an edit batch. Between BeginEdit and EndEdit every change applies at once
   // (later reads see it); EndEdit records them as one step, which the author takes back with
   // one Undo, under the name given (shown as "Undo: <name>"). One batch at a time, for the
   // plugin that began it. A batch ends in the main-thread call that began it: the editor
   // records a batch still open the next time it runs the plugin bus.
   //
   // Returns 0 without a project or while a batch is open.
   int(MSGPIAPI* BeginEdit)(uint32_t endpointId, const char* name);
   // apply 1: records the batch (nothing when no change changed anything); 0: takes its changes
   // back. Returns 0 when the plugin has no batch open.
   int(MSGPIAPI* EndEdit)(uint32_t endpointId, int apply);
   // The changes. Each returns 1 when it was made (also when it changed nothing) and 0 when the
   // plugin has no batch open or the change is refused (a frame or record that does not
   // exist, data of the wrong size or with values out of range); a refused change changes
   // nothing.
   //
   // The frame's colorized pixels on every plane it has get the fill color (RGB565), or with
   // fill CDMDPI_NONE the look of a frame fresh from a dump (each original shade in its plain
   // color, see GetPlainColors), except where keep is not 0. keep: the original's size
   // (count = width * height), row-major; an HD pixel follows the original pixel under it.
   int(MSGPIAPI* ResetFramePixels)(uint32_t endpointId, uint32_t frame, const uint8_t* keep,
                                   uint32_t count, uint32_t fill);
   // The frame's dynamic zones on the dynamic plane, as GetDynamicMask gives them: the set of
   // each pixel (0 .. CDMDPI_DYNAMIC_SETS - 1), 255 for none.
   int(MSGPIAPI* SetDynamicZones)(uint32_t endpointId, uint32_t frame, const uint8_t* sets,
                                  uint32_t count);
   // The frame's background; CDMDPI_NONE for none.
   int(MSGPIAPI* SetFrameBackground)(uint32_t endpointId, uint32_t frame, uint32_t background);
   // Deletes a background; the frames that have it lose it, and the later backgrounds move
   // down by one (delete from the last to the first to delete several).
   int(MSGPIAPI* DeleteBackground)(uint32_t endpointId, uint32_t background);
   // Removes the frame's color rotations.
   int(MSGPIAPI* ClearColorRotations)(uint32_t endpointId, uint32_t frame);
   // The plain color (RGB565) of each original shade, colors[shade], colorCount of them.
   uint32_t(MSGPIAPI* GetPlainColors)(uint16_t* colors, uint32_t capacity);

   // ---- Pictures (version 3) -------------------------------------------------------------
   // The frame's background mask on the plane: 1 where the background shows through, else 0;
   // all 0 for a frame without background data.
   uint32_t(MSGPIAPI* GetBackgroundMask)(uint32_t frame, int hd, uint8_t* pixels,
                                         uint32_t capacity, uint32_t* width, uint32_t* height);
   // A background's picture, RGB565; 0 when it has none on the plane.
   uint32_t(MSGPIAPI* GetBackgroundPicture)(uint32_t background, int hd, uint16_t* pixels,
                                            uint32_t capacity, uint32_t* width,
                                            uint32_t* height);
   // The sprites the frame looks for, ascending.
   uint32_t(MSGPIAPI* GetFrameSprites)(uint32_t frame, uint32_t* sprites, uint32_t capacity);
   // A sprite's colors, RGB565, CDMDPI_SPRITE_WIDTH x CDMDPI_SPRITE_HEIGHT; 0 when it has none
   // on the plane. In a project with both planes the SD sprite fills the upper left quarter
   // and the HD sprite is it doubled: SD pixel (x, y) lies under HD pixels (2x, 2y) to
   // (2x + 1, 2y + 1). In a project that renders HD only, the HD sprite is one to one.
   uint32_t(MSGPIAPI* GetSpritePicture)(uint32_t sprite, int hd, uint16_t* pixels,
                                        uint32_t capacity, uint32_t* width, uint32_t* height);
   // A sprite's shape on the plane, the sprite's size: 1 where the sprite covers, else 0.
   uint32_t(MSGPIAPI* GetSpriteMask)(uint32_t sprite, int hd, uint8_t* pixels,
                                     uint32_t capacity, uint32_t* width, uint32_t* height);
   // The background and the sprite the author works on (the background and sprite panels);
   // CDMDPI_NONE without one.
   uint32_t(MSGPIAPI* GetCurrentBackground)();
   uint32_t(MSGPIAPI* GetCurrentSprite)();

   // Changes of an edit batch, as those of version 2: the picture (RGB565, the size the
   // matching Get...Picture gives, count pixels) is written where write is not 0 (write null:
   // everywhere). A frame, background or sprite without HD gets it first, as Create HD makes
   // it; a frame with a background mask gets it doubled. The frame's background mask, dynamic
   // zones and the sprite's shape stay as they are.
   int(MSGPIAPI* SetFramePicture)(uint32_t endpointId, uint32_t frame, int hd,
                                  const uint16_t* pixels, const uint8_t* write, uint32_t count);
   int(MSGPIAPI* SetBackgroundPicture)(uint32_t endpointId, uint32_t background, int hd,
                                       const uint16_t* pixels, const uint8_t* write,
                                       uint32_t count);
   int(MSGPIAPI* SetSpritePicture)(uint32_t endpointId, uint32_t sprite, int hd,
                                   const uint16_t* pixels, const uint8_t* write,
                                   uint32_t count);

   // ---- The author's windows (version 3) -------------------------------------------------
   // Both open a window and return at once (1), or return 0 when it cannot open: no project
   // (ReviewPictures), no or invalid entries, or the plugin has such a window open. The
   // callback runs on the main thread once the author closes the window, unless the plugin is
   // unloaded first (the window then closes without it).
   //
   // Proposes new pictures: the window shows each beside the picture it replaces, and the
   // author picks which to apply. The editor applies them as one edit batch under undoName
   // (one Undo). The pictures are checked and copied at the call. If the project changes
   // while the window is open, nothing can be applied any more.
   int(MSGPIAPI* ReviewPictures)(uint32_t endpointId, const char* title, const char* undoName,
                                 const ColorizingDMDPicture* pictures, uint32_t count,
                                 cdmdpi_review_callback callback, void* userData);
   // A form for the plugin's settings: the fields with their values, kept with SetSetting
   // when the author saves.
   int(MSGPIAPI* EditSettings)(uint32_t endpointId, const char* title,
                               const ColorizingDMDSettingField* fields, uint32_t count,
                               cdmdpi_settings_callback callback, void* userData);
} ColorizingDMDEditorAPI;
