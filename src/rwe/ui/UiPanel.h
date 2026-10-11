#pragma once

#include <optional>
#include <algorithm>
#include <array>
#include <rwe/observable/Subject.h>
#include <rwe/ui/UiComponent.h>

namespace rwe
{
    class UiPanel : public UiComponent
    {
    private:
        std::optional<std::shared_ptr<Sprite>> background;
        std::vector<std::unique_ptr<UiComponent>> children;
        std::optional<UiComponent*> focusedChild{std::nullopt};
        Subject<GroupMessage> groupMessagesSubject;

        bool drawSolidPlate{false};

        /**
         * The gui file's `crdefault=` and `escdefault=`: the buttons Enter
         * and Escape press (the header gadget's fields, which the original
         * reads into its panel at load). Held always, honoured only after
         * setDefaultKeysEnabled -- the in-game panels declare them too and
         * have always ignored them, and switching that on for all of them at
         * once would change what a key does under the battlefield.
         */
        std::string crDefault;
        std::string escDefault;
        bool defaultKeysEnabled{false};

        /**
         * The 64x64 tile the plate is filled with, when there is one.
         *
         * A dialog whose gui says panel=NULL and ships no bitmap of its own
         * -- YESORNO and EXITMENU are the two -- is not meant to be a flat
         * rectangle. The original fills it with `BackTile` out of
         * commongui.gaf, the same tile the front end sits on, which is where
         * the texture in those boxes comes from.
         */
        std::optional<std::shared_ptr<Sprite>> plateTile;

        /**
         * The nine frames of a multi-frame frame sequence -- `BackTile` is one
         * -- laid out as a 3x3 patch: corners, edges and the middle, in rows
         * of three (frames 0 1 2 / 3 4 5 / 6 7 8). Where it is set it is used
         * in place of plateTile and the flat fill and outline are not drawn:
         * the frames carry their own bevel, which is why tiling frame 0 alone
         * gave a dialog a grid of bevelled squares.
         */
        std::optional<std::array<std::shared_ptr<Sprite>, 9>> plateFrames;

    public:
        UiPanel(int posX, int posY, unsigned int sizeX, unsigned int sizeY);
        UiPanel(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::shared_ptr<Sprite> background);
        UiPanel(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::optional<std::shared_ptr<Sprite>> background);

        UiPanel(const UiPanel&) = delete;
        UiPanel& operator=(const UiPanel&) = delete;

        UiPanel(UiPanel&& panel) noexcept;

        UiPanel& operator=(UiPanel&& panel) noexcept;

        /** Hands back this panel's subscriptions while groupMessagesSubject is still alive; see UiComponent::releaseSubscriptions. */
        ~UiPanel() override { releaseSubscriptions(); }

        void render(UiRenderService& graphics) const override;

        void mouseDown(MouseButtonEvent event) override;

        void mouseUp(MouseButtonEvent event) override;

        void keyDown(KeyEvent event) override;

        void keyUp(KeyEvent event) override;

        void textInput(const std::string& text) override;

        bool wantsTextInput() const override;

        void mouseMove(MouseMoveEvent event) override;

        void mouseWheel(MouseWheelEvent event) override;

        void update(float dt) override;

        void uiMessage(const GroupMessage& message) override;

        void focus() override;

        void unfocus() override;

        void appendChild(std::unique_ptr<UiComponent>&& c);

        /** Draw a solid plate behind the children; for dialogs whose gui declares no art of its own. */
        void setDrawSolidPlate(bool draw) { drawSolidPlate = draw; }

        void setPlateTile(std::shared_ptr<Sprite> tile) { plateTile = std::move(tile); }

        /** Draws the plate as a 3x3 patch of these nine frames; see plateFrames. */
        void setPlateFrames(std::array<std::shared_ptr<Sprite>, 9> frames) { plateFrames = std::move(frames); }

        /** Names the buttons Enter and Escape press; empty for none. */
        void setDefaultButtons(const std::string& cr, const std::string& esc)
        {
            crDefault = cr;
            escDefault = esc;
        }

        const std::string& getCrDefault() const { return crDefault; }
        const std::string& getEscDefault() const { return escDefault; }

        /** Whether Enter and Escape press the default buttons. Off until a scene asks for it. */
        void setDefaultKeysEnabled(bool enabled) { defaultKeysEnabled = enabled; }

        void removeChildrenWithPrefix(const std::string& prefix);

        /**
         * Removes children by exact name. The order panel needs this rather
         * than the prefix form because ARMMOVE is a prefix of ARMMOVEORD, and
         * taking the move button away would otherwise take the standing move
         * order button with it.
         */
        void removeChildrenNamed(const std::string& name);

        std::vector<std::unique_ptr<UiComponent>>& getChildren();

        void setFocus(std::size_t controlIndex);

        void clearFocus();

        void setFocusByName(const std::string& name);

        Observable<GroupMessage>& groupMessages();

        const Observable<GroupMessage>& groupMessages() const;

        template <typename T>
        std::optional<std::reference_wrapper<const T>> find(const std::string& name) const
        {
            static_assert(std::is_base_of_v<UiComponent, T>);
            auto it = std::find_if(children.begin(), children.end(), [&name](const auto& x) {
                return dynamic_cast<const T*>(x.get()) != nullptr && x->getName() == name;
            });
            if (it == children.end())
            {
                return std::nullopt;
            }
            return dynamic_cast<const T&>(**it);
        }

        template <typename T>
        std::optional<std::reference_wrapper<T>> find(const std::string& name)
        {
            auto component = static_cast<const UiPanel*>(this)->find<T>(name);
            if (!component)
            {
                return std::nullopt;
            }

            return const_cast<T&>(component->get());
        }

        template <typename T>
        std::optional<std::reference_wrapper<const T>> findAtPosition(int x, int y) const
        {
            if (!contains(x, y))
            {
                return std::nullopt;
            }

            x -= posX;
            y -= posY;

            static_assert(std::is_base_of_v<UiComponent, T>);
            auto it = std::find_if(children.begin(), children.end(), [&](const auto& c) {
                return dynamic_cast<const T*>(c.get()) != nullptr && c->contains(x, y);
            });
            if (it == children.end())
            {
                return std::nullopt;
            }
            return dynamic_cast<const T&>(**it);
        }

        template <typename T>
        void forAll(const std::function<void(T&)>& f)
        {
            static_assert(std::is_base_of_v<UiComponent, T>);
            for (auto& c : children)
            {
                if (auto cc = dynamic_cast<T*>(c.get()); cc != nullptr)
                {
                    f(*cc);
                }
            }
        }
    };
}
