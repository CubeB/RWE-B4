#pragma once

#include <rwe/observable/Subject.h>
#include <rwe/ui/UiComponent.h>
#include <vector>

namespace rwe
{
    class UiStagedButton : public UiComponent
    {
    public:
        struct StageInfo
        {
            std::shared_ptr<Sprite> sprite;
            std::string label;

            StageInfo(const std::shared_ptr<Sprite>& sprite, const std::string& label) : sprite(sprite), label(label)
            {
            }
        };
        enum class TextAlign
        {
            Hidden,
            Left,
            Center,
            BottomCenter,
        };

        enum class BehaviorMode
        {
            Button,
            Toggle,
            Radio,
            Cycle,
            Staged,
        };

    private:
        std::vector<StageInfo> stages;
        std::shared_ptr<Sprite> pressedSprite;
        std::shared_ptr<SpriteSeries> labelFont;

        TextAlign textAlign{TextAlign::Hidden};

        std::optional<int> quickKey;

        /** True if the button is currently pressed down. */
        bool pressed{false};

        /**
         * True if the button is "armed".
         * The button is armed if the mouse cursor was pressed down inside of it
         * and has not yet been released.
         */
        bool armed{false};

        /** True if the button is a toggle and is toggled on. */
        bool toggledOn{false};

        /**
         * The greyed-out frame from the button's own GAF -- every shipped
         * button carries one after its pressed frame. Null when the artwork
         * has no such frame, in which case a disabled button keeps its
         * normal face and merely stops responding.
         */
        std::shared_ptr<Sprite> disabledSprite;

        /** A disabled button draws its greyed frame and ignores all input. */
        bool enabled{true};

        BehaviorMode behaviorMode{BehaviorMode::Button};

        unsigned int currentStage{0};

        Subject<ButtonClickEvent> clickSubject;

        /**
         * True as the pointer arrives, false as it leaves.
         *
         * Separate from the pressed state the two handlers already keep,
         * because a caller can want to know the pointer is over a control
         * without wanting it to look pressed -- the skirmish screen's help
         * line, which describes the option under the pointer.
         */
        Subject<bool> hoverSubject;

    public:
        UiStagedButton(
            int posX,
            int posY,
            unsigned int sizeX,
            unsigned int sizeY,
            std::vector<StageInfo> stages,
            std::shared_ptr<Sprite> pressedSprite,
            std::shared_ptr<SpriteSeries> labelFont);

        void render(UiRenderService& graphics) const override;

        void mouseDown(MouseButtonEvent event) override;

        void mouseUp(MouseButtonEvent event) override;

        void mouseEnter() override;

        void mouseLeave() override;

        void unfocus() override;

        void keyDown(KeyEvent event) override;

        Observable<ButtonClickEvent>& onClick();

        Observable<bool>& onHover();

        void setStage(unsigned int newStage);

        bool autoChangeStage{true};

        void setTextAlign(TextAlign align);

        void setLabel(const std::string& label);

        void setNormalSprite(const std::shared_ptr<Sprite>& sprite);

        void setPressedSprite(const std::shared_ptr<Sprite>& sprite);

        void setToggledOn(bool _toggledOn);

        void setBehaviorMode(BehaviorMode mode);

        void setQuickKey(int quickKey);

        void setDisabledSprite(const std::shared_ptr<Sprite>& sprite);

        void setEnabled(bool enabled);

        bool isEnabled() const;

    private:
        void activateButton(const ButtonClickEvent& event);

        void nextStage();
    };
}
