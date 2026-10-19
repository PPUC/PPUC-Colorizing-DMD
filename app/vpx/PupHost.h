#pragma once

#include <QImage>

#include <memory>
#include <string>

namespace colorizing::vpx {

// The PuP tester's side of the plugin bus: VPX's PUP plugin (Pin Up Player) hosted as VPX hosts
// it, so that a PuP pack plays as it does on a machine.
//
// While it runs, the editor is to PUP what a machine with a Serum colorization is: a game
// (controller "pinmame::<game>", whose pack PUP looks for in <PUP folder>/pupvideos/<game>)
// and the DMD event source of that game, which sends the trigger ID of each identified frame
// (trigger()). PUP plays the pack's media for them; render() draws its three screens. Textures
// go through the VPX API the host answers. No sound: the editor has no audio output yet.
//
// Everything on the UI thread; process() runs the bus (the caller's timer).
class PupHost {
public:
    enum class Screen {
        Topper,
        Backglass,
        // PUP's DMD screen (VPX's score view).
        Dmd,
    };

    PupHost();
    ~PupHost();
    PupHost(const PupHost&) = delete;
    PupHost& operator=(const PupHost&) = delete;

    // Whether this build hosts plugins and the PUP plugin is in the plugin folder.
    [[nodiscard]] static bool isAvailable();

    // Loads PUP with the PUP folder and announces the game. Returns false with error when PUP
    // does not load; whether a pack was found PUP says in the plugin log.
    bool start(const std::string& pupFolder, const std::string& game, std::string* error);
    void stop();
    [[nodiscard]] bool isRunning() const;

    // Sends a frame's trigger ID, as a machine's colorization does when it identifies the frame.
    void trigger(unsigned int id);
    void process();

    // Draws the screen into the image (its size is the screen's); false when PUP shows nothing
    // there.
    bool render(Screen screen, QImage& image);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace colorizing::vpx
