#pragma once

#include "aven/blocks/blocks.h"
#include "code_editor.h"

#include <imgui.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aven::editor {

// Scratch-style block coding canvas. Blocks snap together into scripts, and the
// EasyScript they turn into is shown live next to them.
class BlockEditor {
public:
    BlockEditor();
    ~BlockEditor();

    bool load(const Json& document);
    Json save() const;
    // Draws the palette, canvas and code preview. Returns true when the blocks changed.
    bool draw(const char* id, ImVec2 size);
    const std::string& code();
    void setCodeError(int line, const std::string& message);

    ImFont* font = nullptr;
    ImFont* codeFont = nullptr;
    bool showCode = true;
    std::function<std::vector<std::string>(blocks::InputType)> assetOptions;
    std::function<void()> onConvertToCode;

private:
    struct Block;
    using BlockPtr = std::unique_ptr<Block>;
    struct Slot {
        Json literal;
        BlockPtr block;
    };
    struct SlotRect {
        std::string name;
        ImVec2 min, max;
    };
    struct Block {
        const blocks::BlockDef* def = nullptr;
        std::map<std::string, Slot> inputs;
        std::vector<BlockPtr> body, elseBody;
        // Layout, recomputed every frame (canvas units).
        ImVec2 pos, size;
        float header = 0, bodyTop = 0, elseTop = 0, bodyHeight = 0, elseHeight = 0;
        std::vector<SlotRect> slots;
    };
    struct Stack {
        ImVec2 pos;
        std::vector<BlockPtr> blocks;
        bool autoPlace = false; // no saved position: placed below the previous stack on first draw
    };
    // Where a block lives, so it can be picked up.
    struct Location {
        std::vector<BlockPtr>* list = nullptr;
        size_t index = 0;
        Block* parent = nullptr;
        std::string input;
        int stack = -1;
    };
    struct Hit {
        Block* block;
        Location where;
        ImVec2 min, max;
    };
    struct SlotHit {
        Block* block;
        std::string input;
        ImVec2 min, max;
    };
    struct AttachPoint {
        std::vector<BlockPtr>* list;
        size_t index;
        ImVec2 point;
        int stack; // top-of-stack attach (the stack moves up)
    };

    std::vector<Stack> stacks_;
    std::vector<std::pair<std::string, Json>> variables_;
    ImVec2 pan_{20, 20};
    float zoom_ = 1.0f;
    std::string category_ = "Events";
    std::vector<Hit> hits_;
    std::vector<SlotHit> slotHits_;
    std::vector<AttachPoint> attach_;

    // Dragging
    bool dragging_ = false;
    std::vector<BlockPtr> dragStack_;
    BlockPtr dragReporter_;
    ImVec2 dragPos_, grabOffset_;
    ImVec2 pressPos_;
    bool pressed_ = false;
    Hit pressedHit_{};
    bool pressedOnSlot_ = false;
    SlotHit pressedSlot_{};

    // Editing an input
    Block* editBlock_ = nullptr;
    std::string editInput_;
    char editBuffer_[256] = {};
    bool openEditPopup_ = false;
    Block* contextBlock_ = nullptr;
    Location contextWhere_;
    bool openContext_ = false;
    char newVarName_[64] = "score";

    std::string code_;
    bool codeDirty_ = true;
    CodeEditor preview_;
    std::vector<Json> undo_, redo_;
    bool changed_ = false;
    ImVec2 canvasOrigin_;
    float paletteWidth_ = 230;

    static BlockPtr makeBlock(const blocks::BlockDef* def);
    static BlockPtr fromJson(const Json& j);
    static Json toJson(const Block& b);
    static BlockPtr clone(const Block& b);
    void snapshot();
    void undo();
    void redo();
    void modified();

    ImVec2 measure(Block& b);
    ImVec2 measureSlot(Block& b, const blocks::Input& in);
    float stackHeight(std::vector<BlockPtr>& list);
    void layout(Block& b, ImVec2 pos);
    void layoutList(std::vector<BlockPtr>& list, ImVec2 pos);
    void collect(std::vector<BlockPtr>& list, int stack, Block* parent);
    void collectSlots(Block& b);

    ImVec2 toScreen(ImVec2 p) const;
    ImVec2 toCanvas(ImVec2 p) const;
    void drawBlock(ImDrawList* dl, Block& b, bool ghost = false);
    void drawList(ImDrawList* dl, std::vector<BlockPtr>& list, bool ghost = false);
    void drawSlot(ImDrawList* dl, Block& b, const blocks::Input& in, const SlotRect& r);
    std::string literalText(const blocks::Input& in, const Json& v) const;
    float textWidth(const std::string& s) const;
    void drawPalette(ImVec2 origin, ImVec2 size);
    void handleCanvasInput(ImVec2 canvasMin, ImVec2 canvasMax, bool canvasHovered);
    void beginDrag(const Hit& hit, ImVec2 mouse);
    void endDrag(ImVec2 mouse, bool overPalette);
    void drawEditPopup();
    void drawContextMenu();
    BlockPtr detach(const Location& where);
    std::vector<std::string> variableNames() const;
};

} // namespace aven::editor
