#include "vpx/EditorApi.h"

#ifdef COLORIZINGDMD_HAS_VPX_PLUGINS

#include <algorithm>
#include <cstring>
#include <exception>
#include <set>

#include "ColorizingDMDPlugin.h"
#include "document/ProjectDocument.h"
#include "document/Triggers.h"
#include "edit/EditBatch.h"
#include "edit/Sections.h"
#include "edit/StructureCommand.h"
#include "vpx/EditorApiHost.h"

namespace colorizing::vpx {

namespace {

using document::ProjectDocument;
using render::Resolution;

EditorBackend* g_backend = nullptr;
const MsgPluginAPI* g_api = nullptr;
uint32_t g_hostEndpoint = 0;
unsigned int g_getApiId = 0;
unsigned int g_projectChangedId = 0;
unsigned int g_selectionChangedId = 0;

// The edit batch a plugin has open (BeginEdit .. EndEdit).
struct OpenBatch {
    uint32_t endpointId = 0;
    document::ProjectDocument* document = nullptr;
    edit::EditBatch batch;
};
std::optional<OpenBatch> g_batch;

const ProjectDocument* openDocument()
{
    return g_backend ? g_backend->document() : nullptr;
}

const ProjectDocument* documentWithFrame(uint32_t frame)
{
    const ProjectDocument* document = openDocument();
    return document && frame < document->frameCount() ? document : nullptr;
}

std::string pluginIdOf(uint32_t endpointId)
{
    if (!g_api) {
        return {};
    }
    MsgEndpointInfo info{};
    g_api->GetEndpointInfo(endpointId, &info);
    return info.id ? std::string(info.id) : std::string();
}

uint32_t toApiId(uint32_t value, uint32_t none)
{
    return value == none ? CDMDPI_NONE : value;
}

// Copies all of the source into the buffer when it fits; returns its size either way.
template <typename Source, typename Target>
uint32_t copyAll(const Source& source, std::size_t size, Target* buffer, uint32_t capacity)
{
    if (buffer && capacity >= size) {
        for (std::size_t index = 0; index < size; ++index) {
            buffer[index] = static_cast<Target>(source[index]);
        }
    }
    return static_cast<uint32_t>(size);
}

void setSize(render::Size size, uint32_t* width, uint32_t* height)
{
    if (width) {
        *width = static_cast<uint32_t>(size.width);
    }
    if (height) {
        *height = static_cast<uint32_t>(size.height);
    }
}

int MSGPIAPI getProjectInfo(ColorizingDMDProjectInfo* info)
{
    if (!info) {
        return 0;
    }
    *info = ColorizingDMDProjectInfo{};
    const ProjectDocument* document = openDocument();
    if (!document) {
        return 0;
    }
    info->hasProject = 1;
    std::strncpy(info->romName, document->romName().c_str(), sizeof(info->romName) - 1);
    info->frameCount = static_cast<uint32_t>(document->frameCount());
    info->colorCount = document->colorCount();
    const render::ProjectGeometry& geometry = document->geometry();
    setSize(geometry.romFrameSize(), &info->originalWidth, &info->originalHeight);
    setSize(geometry.planeSize(Resolution::Sd), &info->sdWidth, &info->sdHeight);
    if (geometry.isEditable(Resolution::Hd)) {
        setSize(geometry.planeSize(Resolution::Hd), &info->hdWidth, &info->hdHeight);
    }
    info->dynamicPlaneIsHd = document->dynamicPlane() == Resolution::Hd ? 1 : 0;
    info->comparisonMaskCount = static_cast<uint32_t>(document->comparisonMaskCount());
    info->spriteCount = static_cast<uint32_t>(document->spriteCount());
    info->backgroundCount = static_cast<uint32_t>(document->backgroundCount());
    info->paletteCount = static_cast<uint32_t>(document->paletteCount());
    info->sectionCount = static_cast<uint32_t>(edit::sections(*document).size());
    info->revision = document->revision();
    return 1;
}

int MSGPIAPI getFrameInfo(uint32_t frame, ColorizingDMDFrameInfo* info)
{
    const ProjectDocument* document = documentWithFrame(frame);
    if (!document || !info) {
        return 0;
    }
    *info = ColorizingDMDFrameInfo{};
    info->durationMs = document->frameDurationMs(frame);
    info->comparisonMask = toApiId(document->comparisonMaskId(frame),
                                   document::kNoComparisonMaskId);
    info->shapeMode = document->shapeComparisonMode(frame) ? 1 : 0;
    // backgroundId() throws for a frame without background data.
    const std::optional<document::FrameBackgroundData>& background =
        document->backgroundData(frame);
    info->background = background ? toApiId(background->backgroundId, document::kNoBackgroundId)
                                  : CDMDPI_NONE;
    std::set<uint16_t> sprites;
    for (const document::FrameSpriteReference& reference :
         document->frameSpriteReferences(frame)) {
        if (reference.spriteId != document::kNoSpriteId) {
            sprites.insert(reference.spriteId);
        }
    }
    info->spriteCount = static_cast<uint32_t>(sprites.size());
    info->hasDynamicPixels = document->hasDynamicPixels(frame) ? 1 : 0;
    info->hasHd = document->hasHdFrame(frame) ? 1 : 0;
    info->triggerId = toApiId(document::combinedTriggerId(document->pupTriggerId(frame),
                                                          document->sceneTriggerId(frame),
                                                          document->monochromeTriggerId(frame)),
                              document::kNoTriggerId);
    const std::optional<std::size_t> section = edit::sectionOfFrame(*document, frame);
    info->section = section ? static_cast<uint32_t>(*section) : CDMDPI_NONE;
    return 1;
}

uint32_t MSGPIAPI getOriginalFrame(uint32_t frame, uint8_t* pixels, uint32_t capacity,
                                   uint32_t* width, uint32_t* height)
{
    const ProjectDocument* document = documentWithFrame(frame);
    if (!document) {
        return 0;
    }
    const document::IndexedFrame& original = document->originalFrame(frame);
    setSize(original.size, width, height);
    return copyAll(original.pixels, original.pixelCount(), pixels, capacity);
}

uint32_t MSGPIAPI getColorizedFrame(uint32_t frame, int hd, uint16_t* pixels, uint32_t capacity,
                                    uint32_t* width, uint32_t* height)
{
    const ProjectDocument* document = documentWithFrame(frame);
    const Resolution plane = hd ? Resolution::Hd : Resolution::Sd;
    if (!document || !document->geometry().isEditable(plane)) {
        return 0;
    }
    try {
        const document::Rgb565Frame& colorized = document->colorizedFrame(frame, plane);
        setSize(colorized.size, width, height);
        return copyAll(colorized.pixels, colorized.pixelCount(), pixels, capacity);
    } catch (const std::exception&) {
        return 0;
    }
}

uint32_t MSGPIAPI getComparisonMask(uint32_t mask, uint8_t* pixels, uint32_t capacity,
                                    uint32_t* width, uint32_t* height)
{
    const ProjectDocument* document = openDocument();
    if (!document || mask >= document->comparisonMaskCount()) {
        return 0;
    }
    const document::IndexedFrame& frame = document->comparisonMask(mask);
    setSize(frame.size, width, height);
    return copyAll(frame.pixels, frame.pixelCount(), pixels, capacity);
}

uint32_t MSGPIAPI getDynamicMask(uint32_t frame, uint8_t* pixels, uint32_t capacity,
                                 uint32_t* width, uint32_t* height)
{
    const ProjectDocument* document = documentWithFrame(frame);
    if (!document) {
        return 0;
    }
    const Resolution plane = document->dynamicPlane();
    const render::Size size = document->geometry().planeSize(plane);
    setSize(size, width, height);
    const std::size_t count =
        static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
    const std::optional<document::FrameDynamicData>& data = document->dynamicData(frame);
    const document::IndexedFrame* mask =
        data ? (plane == Resolution::Hd ? &data->maskHd : &data->maskSd) : nullptr;
    if (mask && mask->pixelCount() == count) {
        return copyAll(mask->pixels, count, pixels, capacity);
    }
    // No dynamic zones: every pixel is outside them.
    if (pixels && capacity >= count) {
        std::fill(pixels, pixels + count, uint8_t{255});
    }
    return static_cast<uint32_t>(count);
}

uint32_t MSGPIAPI getDynamicColors(uint32_t frame, uint16_t* colors, uint32_t capacity)
{
    const ProjectDocument* document = documentWithFrame(frame);
    if (!document) {
        return 0;
    }
    const std::size_t shades = document->colorCount();
    const std::size_t count = CDMDPI_DYNAMIC_SETS * shades;
    if (!colors || capacity < count) {
        return static_cast<uint32_t>(count);
    }
    std::fill(colors, colors + count, uint16_t{0});
    if (!document->dynamicData(frame)) {
        return static_cast<uint32_t>(count);
    }
    const Resolution plane = document->dynamicPlane();
    for (std::size_t set = 0; set < CDMDPI_DYNAMIC_SETS; ++set) {
        for (std::size_t shade = 0; shade < shades; ++shade) {
            try {
                colors[set * shades + shade] = document->dynamicColor(frame, plane, set, shade);
            } catch (const std::exception&) {
                // A frame without colors on the plane keeps 0.
            }
        }
    }
    return static_cast<uint32_t>(count);
}

int MSGPIAPI getSection(uint32_t section, char* name, uint32_t capacity, uint32_t* firstFrame)
{
    const ProjectDocument* document = openDocument();
    if (!document) {
        return 0;
    }
    const std::vector<edit::Section> sections = edit::sections(*document);
    if (section >= sections.size()) {
        return 0;
    }
    if (name && capacity > 0) {
        std::strncpy(name, sections[section].name.c_str(), capacity - 1);
        name[capacity - 1] = '\0';
    }
    if (firstFrame) {
        *firstFrame = static_cast<uint32_t>(sections[section].firstFrame);
    }
    return 1;
}

uint32_t MSGPIAPI getCurrentFrame()
{
    const std::optional<std::size_t> frame =
        openDocument() ? g_backend->currentFrame() : std::nullopt;
    return frame ? static_cast<uint32_t>(*frame) : CDMDPI_NONE;
}

uint32_t MSGPIAPI getSelectedFrames(uint32_t* frames, uint32_t capacity)
{
    if (!openDocument()) {
        return 0;
    }
    const std::vector<std::size_t> selected = g_backend->selectedFrames();
    return copyAll(selected, selected.size(), frames, capacity);
}

int MSGPIAPI showFrames(uint32_t current, const uint32_t* frames, uint32_t count)
{
    const ProjectDocument* document = documentWithFrame(current);
    if (!document || (count > 0 && !frames)) {
        return 0;
    }
    std::vector<std::size_t> selected;
    for (uint32_t index = 0; index < count; ++index) {
        if (frames[index] >= document->frameCount()) {
            return 0;
        }
        selected.push_back(frames[index]);
    }
    g_backend->showFrames(current, selected);
    return 1;
}

void MSGPIAPI addMenuEntry(uint32_t endpointId, const char* text,
                           cdmdpi_action_callback callback, void* userData)
{
    if (!g_backend || !text || !callback) {
        return;
    }
    g_backend->addMenuEntry(pluginIdOf(endpointId), text,
                            [callback, userData]() { callback(userData); });
}

void MSGPIAPI removeMenuEntries(uint32_t endpointId)
{
    if (g_backend) {
        g_backend->removeMenuEntries(pluginIdOf(endpointId));
    }
}

void MSGPIAPI showMessage(uint32_t endpointId, const char* title, const char* text)
{
    if (g_backend) {
        g_backend->showMessage(pluginIdOf(endpointId), title ? title : "", text ? text : "");
    }
}

void MSGPIAPI showStatus(uint32_t endpointId, const char* text)
{
    if (g_backend) {
        g_backend->showStatus(pluginIdOf(endpointId), text ? text : "");
    }
}

uint32_t MSGPIAPI getSetting(uint32_t endpointId, const char* key, char* value, uint32_t capacity)
{
    if (!g_backend || !key) {
        return 0;
    }
    const std::string stored = g_backend->setting(pluginIdOf(endpointId), key);
    if (stored.empty()) {
        return 0;
    }
    const uint32_t size = static_cast<uint32_t>(stored.size() + 1);
    if (value && capacity >= size) {
        std::memcpy(value, stored.c_str(), size);
    }
    return size;
}

void MSGPIAPI setSetting(uint32_t endpointId, const char* key, const char* value)
{
    if (g_backend && key) {
        g_backend->setSetting(pluginIdOf(endpointId), key, value ? value : "");
    }
}

void finishBatch(bool apply)
{
    if (!g_batch) {
        return;
    }
    OpenBatch open = std::move(*g_batch);
    g_batch.reset();
    if (!g_backend || g_backend->editableDocument() != open.document) {
        // The project it was made on is gone.
        return;
    }
    if (!apply) {
        open.batch.cancel(*open.document);
        return;
    }
    if (std::unique_ptr<edit::EditBatchCommand> command = open.batch.finish()) {
        g_backend->recordEdit(std::move(command));
    }
}

int MSGPIAPI beginEdit(uint32_t endpointId, const char* name)
{
    document::ProjectDocument* document = g_backend ? g_backend->editableDocument() : nullptr;
    if (!document || g_batch) {
        return 0;
    }
    g_batch = OpenBatch{endpointId, document, edit::EditBatch(name && *name ? name : "Plugin edit")};
    return 1;
}

int MSGPIAPI endEdit(uint32_t endpointId, int apply)
{
    if (!g_batch || g_batch->endpointId != endpointId) {
        return 0;
    }
    finishBatch(apply != 0);
    return 1;
}

// Runs one change of the plugin's open batch: 0 when it has none or the change is refused.
template <typename Change>
int change(uint32_t endpointId, Change&& makeChange)
{
    if (!g_batch || g_batch->endpointId != endpointId || !g_backend ||
        g_backend->editableDocument() != g_batch->document) {
        return 0;
    }
    try {
        g_batch->batch.add(makeChange(*g_batch->document));
        return 1;
    } catch (const std::exception&) {
        // The change left the document as it was.
        return 0;
    }
}

int MSGPIAPI resetFramePixels(uint32_t endpointId, uint32_t frame, const uint8_t* keep,
                              uint32_t count, uint32_t fill)
{
    if (!keep || (fill != CDMDPI_NONE && fill > 0xffff)) {
        return 0;
    }
    std::vector<uint8_t> mask(keep, keep + count);
    const std::optional<uint16_t> color =
        fill == CDMDPI_NONE ? std::nullopt : std::optional<uint16_t>(static_cast<uint16_t>(fill));
    return change(endpointId, [&](document::ProjectDocument& document) {
        return edit::resetPixels(document, frame, mask, color);
    });
}

int MSGPIAPI setDynamicZones(uint32_t endpointId, uint32_t frame, const uint8_t* sets,
                             uint32_t count)
{
    if (!sets) {
        return 0;
    }
    std::vector<uint8_t> zones(sets, sets + count);
    return change(endpointId, [&](document::ProjectDocument& document) {
        return edit::setDynamicZones(document, frame, zones);
    });
}

int MSGPIAPI setFrameBackground(uint32_t endpointId, uint32_t frame, uint32_t background)
{
    if (background != CDMDPI_NONE && background >= document::kNoBackgroundId) {
        return 0;
    }
    const uint16_t id = background == CDMDPI_NONE ? document::kNoBackgroundId
                                                  : static_cast<uint16_t>(background);
    return change(endpointId, [&](document::ProjectDocument& document) {
        return edit::setFrameBackground(document, frame, id);
    });
}

int MSGPIAPI deleteBackground(uint32_t endpointId, uint32_t background)
{
    return change(endpointId, [&](document::ProjectDocument& document) {
        return std::unique_ptr<edit::EditCommand>(edit::deleteBackground(document, background));
    });
}

int MSGPIAPI clearColorRotations(uint32_t endpointId, uint32_t frame)
{
    return change(endpointId, [&](document::ProjectDocument& document) {
        return edit::clearColorRotations(document, frame);
    });
}

uint32_t MSGPIAPI getPlainColors(uint16_t* colors, uint32_t capacity)
{
    const ProjectDocument* document = openDocument();
    if (!document) {
        return 0;
    }
    const std::vector<uint16_t> plain = document::plainOriginalColors(document->colorCount());
    return copyAll(plain, plain.size(), colors, capacity);
}

const ColorizingDMDEditorAPI kApi{
    2,
    &getProjectInfo,
    &getFrameInfo,
    &getOriginalFrame,
    &getColorizedFrame,
    &getComparisonMask,
    &getDynamicMask,
    &getDynamicColors,
    &getSection,
    &getCurrentFrame,
    &getSelectedFrames,
    &showFrames,
    &addMenuEntry,
    &removeMenuEntries,
    &showMessage,
    &showStatus,
    &getSetting,
    &setSetting,
    &beginEdit,
    &endEdit,
    &resetFramePixels,
    &setDynamicZones,
    &setFrameBackground,
    &deleteBackground,
    &clearColorRotations,
    &getPlainColors,
};

void MSGPIAPI onGetApi(const unsigned int, void*, void* msgData)
{
    if (msgData) {
        *static_cast<const ColorizingDMDEditorAPI**>(msgData) = &kApi;
    }
}

void broadcast(unsigned int msgId)
{
    if (g_api && msgId) {
        g_api->BroadcastMsg(g_hostEndpoint, msgId, nullptr);
    }
}

}  // namespace

void setEditorBackend(EditorBackend* backend)
{
    g_backend = backend;
    g_batch.reset();
}

void notifyProjectChanged()
{
    broadcast(g_projectChangedId);
}

void notifySelectionChanged()
{
    broadcast(g_selectionChangedId);
}

const void* editorApiTable()
{
    return &kApi;
}

void finishOpenEditBatch()
{
    finishBatch(true);
}

void installEditorApi(const MsgPluginAPI* api, uint32_t hostEndpoint)
{
    g_api = api;
    g_hostEndpoint = hostEndpoint;
    g_getApiId = api->GetMsgID(CDMDPI_NAMESPACE, CDMDPI_MSG_GET_API);
    g_projectChangedId = api->GetMsgID(CDMDPI_NAMESPACE, CDMDPI_EVT_ON_PROJECT_CHANGED);
    g_selectionChangedId = api->GetMsgID(CDMDPI_NAMESPACE, CDMDPI_EVT_ON_SELECTION_CHANGED);
    api->SubscribeMsg(hostEndpoint, g_getApiId, &onGetApi, nullptr);
}

void uninstallEditorApi()
{
    if (!g_api) {
        return;
    }
    g_api->UnsubscribeMsg(g_getApiId, &onGetApi, nullptr);
    g_api->ReleaseMsgID(g_getApiId);
    g_api->ReleaseMsgID(g_projectChangedId);
    g_api->ReleaseMsgID(g_selectionChangedId);
    g_api = nullptr;
    g_getApiId = g_projectChangedId = g_selectionChangedId = 0;
}

}  // namespace colorizing::vpx

#else  // no plugin hosting in this build

namespace colorizing::vpx {

void setEditorBackend(EditorBackend*) {}
void finishOpenEditBatch() {}
void notifyProjectChanged() {}
void notifySelectionChanged() {}
const void* editorApiTable()
{
    return nullptr;
}

}  // namespace colorizing::vpx

#endif
