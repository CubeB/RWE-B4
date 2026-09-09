#pragma once

#include <rwe/AudioService.h>
#include <rwe/MainMenuModel.h>
#include <rwe/PathMapping.h>
#include <rwe/TextureService.h>
#include <rwe/io/gui/gui.h>
#include <rwe/ui/UiLabel.h>
#include <rwe/ui/UiListBox.h>
#include <rwe/ui/UiPanel.h>
#include <rwe/ui/UiScrollBar.h>
#include <rwe/ui/UiStagedButton.h>
#include <string>
#include <vector>

namespace rwe
{
    class UiFactory
    {
    private:
        struct ButtonSprites
        {
            std::vector<std::shared_ptr<Sprite>> normal;
            std::shared_ptr<Sprite> pressed;
            std::shared_ptr<Sprite> disabled;
        };

    private:
        TextureService* textureService;
        AudioService* audioService;
        TdfBlock* soundLookup;
        AbstractVirtualFileSystem* vfs;
        const PathMapping* const pathMapping;

        int screenWidth;
        int screenHeight;

    public:
        UiFactory(TextureService* textureService, AudioService* audioService, TdfBlock* soundLookup, AbstractVirtualFileSystem* vfs, const PathMapping* const pathMapping, int screenWidth, int screenHeight);

        std::unique_ptr<UiPanel> panelFromGuiFile(const std::string& name, const std::string& background, const std::vector<GuiEntry>& entries);

        std::unique_ptr<UiPanel> panelFromGuiFile(const std::string& name);

        std::unique_ptr<UiPanel> panelFromGuiFile(const std::string& name, const std::vector<GuiEntry>& entries);

        std::unique_ptr<UiPanel> createPanel(int x, int y, int width, int height, const std::string& name);
        std::unique_ptr<UiPanel> createPanel(int x, int y, int width, int height, const std::string& name, const std::optional<std::string>& background);

        std::unique_ptr<UiStagedButton> createButton(int x, int y, int width, int height, const std::string& guiName, const std::string& name, const std::string& label);

        std::unique_ptr<UiStagedButton> createBasicButton(int x, int y, int width, int height, const std::string& guiName, const std::string& name, const std::string& label);

        std::unique_ptr<UiStagedButton> createStagedButton(int x, int y, int width, int height, const std::string& guiName, const std::string& name, const std::vector<std::string>& labels, unsigned int stages);

        /**
         * Swaps a button declared in a GUI file for one with a different
         * number of stages, at exactly the geometry the data gave it.
         *
         * The GUI files are read-only game data and there is no override
         * directory, so a control RWE has and the original does not cannot be
         * declared in data. `artName` is the name the button art is looked up
         * under and is deliberately separate from `name`: a gadget's own
         * entry carries only as many faces as the original needed, so asking
         * under a name the data does not carry falls through to the generic
         * stagebuttnN face for the count actually wanted. The replacement is
         * named `name` regardless, because that is what click dispatch
         * matches on.
         *
         * Does nothing if the panel has no such button.
         */
        void replaceStagedButton(UiPanel& panel, const std::string& guiName, const std::string& name, const std::string& artName, const std::vector<std::string>& labels, unsigned int stage);

        /**
         * Adds a staged button the GUI data does not contain, one row below
         * anchorName, taking the row step from the gap between anchorName and
         * aboveAnchorName so no coordinate has to be written down. Does
         * nothing unless the panel has both, which is what keeps it off the
         * pages it does not belong on. For settings TA never had a gadget
         * for; see GameScene::addBuildingHaloButton.
         */
        void addStagedButtonBelow(UiPanel& panel, const std::string& guiName, const std::string& artName, const std::string& name, const std::string& anchorName, const std::string& aboveAnchorName, const std::vector<std::string>& labels, unsigned int stage);

        /**
         * A label the gui data does not declare, in the same font every label
         * read out of a gui file gets.
         *
         * YESORNO.GUI is the reason it exists. The dialog is three gadgets --
         * the panel and the two buttons -- and carries no gadget for the
         * question it is asking, because the original writes that text into
         * the panel itself at runtime (0x4605c0). RWE's panels have no text,
         * so the question needs somewhere to live.
         */
        std::unique_ptr<UiLabel> createLabel(int x, int y, int width, int height, const std::string& text, UiLabel::Alignment alignment);

    private:
        std::unique_ptr<UiComponent> componentFromGuiEntry(const std::string& guiName, const GuiEntry& entry);

        std::unique_ptr<UiStagedButton> buttonFromGuiEntry(const std::string& guiName, const GuiEntry& entry);

        std::unique_ptr<UiStagedButton> stagedButtonFromGuiEntry(const std::string& guiName, const GuiEntry& entry);

        std::unique_ptr<UiLabel> labelFromGuiEntry(const std::string& guiName, const GuiEntry& entry);

        std::unique_ptr<UiScrollBar> scrollBarFromGuiEntry(const std::string& guiName, const GuiEntry& entry);

        std::unique_ptr<UiListBox> listBoxFromGuiEntry(const std::string& guiName, const GuiEntry& entry);

        std::shared_ptr<SpriteSeries> getDefaultButtonGraphics(const std::string& guiName, int width, int height);

        std::optional<AudioService::SoundHandle> getButtonSound(const std::string& buttonName);

        std::optional<AudioService::SoundHandle> deduceButtonSound(const std::string& guiName, const GuiEntry& entry);

        std::optional<AudioService::SoundHandle> deduceButtonSound(const std::string& guiName, const std::string& name, int width, int height);

        std::shared_ptr<SpriteSeries> getDefaultStagedButtonGraphics(const std::string& guiName, unsigned int stages);

        std::unique_ptr<UiComponent> surfaceFromGuiEntry(const std::string& guiName, const GuiEntry& entry);

        ButtonSprites getButtonGraphics(const std::string& guiName, const std::string& name, int width, int height);

        ButtonSprites getBasicButtonGraphics(const std::string& guiName, const std::string& name, int width, int height);

        ButtonSprites getStagedButtonGraphics(const std::string& guiName, const std::string& name, unsigned int stages);
    };
}
